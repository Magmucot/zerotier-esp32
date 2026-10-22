#include "ESP32LitePort.hpp"
#include "ESP32NetLink.hpp"
#include "ESP32FramePool.hpp"
#include "ESP32LwIPGlue.hpp"

#include "../node/MAC.hpp"
#include "../node/Utils.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#else
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#endif

namespace ZeroTier {

#ifdef ESP_PLATFORM
static const char* ZT_ESP32_PORT_TAG = "ZT_ESP32_PORT";
#endif

static void _esp32_tap_frame_handler(void* arg, void* tptr, uint64_t nwid, const MAC& source, const MAC& dest, unsigned int etherType, unsigned int vlanId, const void* data, unsigned int len)
{
	(void)tptr;
	ESP32LitePort* port = (ESP32LitePort*)arg;
	if (! port || ! port->ownerTask()) {
		return;
	}
	port->ownerTask()->enqueueVirtualFrame(nwid, source.toInt(), dest.toInt(), etherType, data, len, 0);
}

ESP32LitePort::ESP32LitePort()
	: _node((ZT_Node*)0), _udpSocket(-1), _networkState("/", &_esp32_tap_frame_handler, this), _ownerTask((ZT_Node*)0, this, &_networkState), _started(false), _stopping(false)
#ifdef ESP_PLATFORM
	, _wireRxTask((TaskHandle_t)0), _wireRxStopped((SemaphoreHandle_t)0)
#endif
{
	defaultConfig(_config, 0);
#ifdef ESP_PLATFORM
	_wireRxStopped = xSemaphoreCreateBinary();
#endif
}

ESP32LitePort::~ESP32LitePort()
{
	stop();
#ifdef ESP_PLATFORM
	if (_wireRxStopped) {
		vSemaphoreDelete(_wireRxStopped);
		_wireRxStopped = (SemaphoreHandle_t)0;
	}
#endif
}

void ESP32LitePort::defaultConfig(Config& config, uint64_t nwid)
{
	memset(&config, 0, sizeof(config));
	config.nwid = nwid;
	config.udpPort = ZT_ESP32_UDP_PORT;
	config.homePath = "/";
	config.nvsNamespace = "zerotier";
	config.ownerTaskStackWords = ZT_ESP32_OWNER_STACK_WORDS;
	config.wireRxTaskStackWords = ZT_ESP32_WIRE_RX_STACK_WORDS;
	config.ownerTaskPriority = 5;
	config.wireRxTaskPriority = 5;
	config.ownerTaskCoreId = -1;
	config.wireRxTaskCoreId = -1;
	config.enableEncryptedHello = 0;
	config.lowBandwidthMode = 0;
}

int ESP32LitePort::start(const Config& config)
{
	if (_started) {
		return 0;
	}
	_config = config;
	_stopping = false;
#ifdef ESP_PLATFORM
	if (_wireRxStopped) { xSemaphoreTake(_wireRxStopped, 0); }
#endif
#ifdef ESP_PLATFORM
	if (! ESP32LwIPGlue::captureTcpipThread(1000)) {
		fprintf(stderr, "[ZT_ESP32_PORT] warning=tcpip_thread_capture_failed; continuing but lwip sync calls cannot detect same-thread use\n");
	}
#endif

	_udpSocket = _openUdpSocket();
	if (_udpSocket < 0) {
		fprintf(stderr, "[ZT_ESP32_PORT] start failed reason=udp_socket\n");
		return -1;
	}

	ZT_Node_Callbacks cb;
	memset(&cb, 0, sizeof(cb));
	cb.version = 0;
	cb.statePutFunction = &ESP32LitePort::statePutCallback;
	cb.stateGetFunction = &ESP32LitePort::stateGetCallback;
	cb.wirePacketSendFunction = &ESP32LitePort::wirePacketSendCallback;
	cb.virtualNetworkFrameFunction = &ESP32LitePort::virtualNetworkFrameCallback;
	cb.virtualNetworkConfigFunction = &ESP32LitePort::virtualNetworkConfigCallback;
	cb.eventCallback = &ESP32LitePort::eventCallback;
	cb.pathCheckFunction = &ESP32LitePort::pathCheckCallback;
	cb.pathLookupFunction = (ZT_PathLookupFunction)0;

	ZT_Node_Config nc;
	memset(&nc, 0, sizeof(nc));
	nc.enableEncryptedHello = _config.enableEncryptedHello;
	nc.lowBandwidthMode = _config.lowBandwidthMode;

	const enum ZT_ResultCode rc = ZT_Node_new(&_node, &nc, this, this, &cb, _nowMs());
	fprintf(stderr, "[ZT_ESP32_PORT] ZT_Node_new rc=%d node=%p\n", (int)rc, _node);
	if (rc != ZT_RESULT_OK) {
		if (_udpSocket >= 0) {
#ifdef ESP_PLATFORM
			closesocket(_udpSocket);
#else
			close(_udpSocket);
#endif
			_udpSocket = -1;
		}
		return (int)rc;
	}

	_ownerTask.setNode(_node);
	_ownerTask.setNetworkState(&_networkState);
	ESP32NodeTask::setDefaultTask(&_ownerTask);
	if (! _ownerTask.start("zt_owner", _config.ownerTaskStackWords, _config.ownerTaskPriority, _config.ownerTaskCoreId)) {
		fprintf(stderr, "[ZT_ESP32_PORT] start failed reason=owner_task\n");
		if (_node) {
			ZT_Node_delete(_node);
			_node = (ZT_Node*)0;
		}
		if (_udpSocket >= 0) {
#ifdef ESP_PLATFORM
			closesocket(_udpSocket);
#else
			close(_udpSocket);
#endif
			_udpSocket = -1;
		}
		return -2;
	}

#ifdef ESP_PLATFORM
	BaseType_t trc;
#ifdef CONFIG_FREERTOS_UNICORE
	(void)_config.wireRxTaskCoreId;
	trc = xTaskCreate(&ESP32LitePort::_wireRxTaskEntry, "zt_wire_rx", _config.wireRxTaskStackWords, this, _config.wireRxTaskPriority, &_wireRxTask);
#else
	if (_config.wireRxTaskCoreId >= 0) {
		trc = xTaskCreatePinnedToCore(&ESP32LitePort::_wireRxTaskEntry, "zt_wire_rx", _config.wireRxTaskStackWords, this, _config.wireRxTaskPriority, &_wireRxTask, _config.wireRxTaskCoreId);
	}
	else {
		trc = xTaskCreate(&ESP32LitePort::_wireRxTaskEntry, "zt_wire_rx", _config.wireRxTaskStackWords, this, _config.wireRxTaskPriority, &_wireRxTask);
	}
#endif
	if (trc != pdPASS) {
		fprintf(stderr, "[ZT_ESP32_PORT] start failed reason=wire_rx_task\n");
		_ownerTask.stop(1000);
		ESP32NodeTask::setDefaultTask((ESP32NodeTask*)0);
		if (_node) {
			ZT_Node_delete(_node);
			_node = (ZT_Node*)0;
		}
		if (_udpSocket >= 0) {
			closesocket(_udpSocket);
			_udpSocket = -1;
		}
		return -3;
	}
#endif
	_started = true;
	if (_config.nwid) {
		return join(_config.nwid);
	}
	return 0;
}

int ESP32LitePort::join(uint64_t nwid)
{
	if (! _ownerTask.enqueueJoin(nwid, 1000)) {
		fprintf(stderr, "[ZT_ESP32_PORT] join enqueue failed nwid=%llx\n", (unsigned long long)nwid);
		return -1;
	}
	fprintf(stderr, "[ZT_ESP32_PORT] join enqueued nwid=%llx\n", (unsigned long long)nwid);
	return 0;
}

bool ESP32LitePort::verifyProvisionedIdentity() const
{
	ESP32LitePort* self = const_cast<ESP32LitePort*>(this);
	unsigned char tmp[512];
	const int secLen = self->_stateGet(ZT_STATE_OBJECT_IDENTITY_SECRET, (const uint64_t*)0, tmp, sizeof(tmp));
	const int pubLen = self->_stateGet(ZT_STATE_OBJECT_IDENTITY_PUBLIC, (const uint64_t*)0, tmp, sizeof(tmp));
	fprintf(stderr, "[ZT_ESP32_PORT] identity_check id_sec=%d id_pub=%d result=%s\n", secLen, pubLen, ((secLen > 0) && (pubLen > 0)) ? "ok" : "missing");
	return ((secLen > 0) && (pubLen > 0));
}

void ESP32LitePort::stop()
{
	_stopping = true;
	/* Stop underlay RX first so it cannot enqueue new wire packets. Closing the
	 * socket is used to unblock recvfrom() before deleting the Node. */
	if (_udpSocket >= 0) {
#ifdef ESP_PLATFORM
		shutdown(_udpSocket, 0);
		closesocket(_udpSocket);
#else
		close(_udpSocket);
#endif
		_udpSocket = -1;
	}
#ifdef ESP_PLATFORM
	if (_wireRxTask) {
		if (_wireRxStopped) {
			if (xSemaphoreTake(_wireRxStopped, pdMS_TO_TICKS(1000)) != pdTRUE) {
				vTaskDelete(_wireRxTask);
			}
		}
		else {
			vTaskDelete(_wireRxTask);
		}
		_wireRxTask = (TaskHandle_t)0;
	}
#endif
	/* The owner task must stop before ZT_Node_delete(), since it is the only
	 * task allowed to call ZT_Node_processXXX. */
	const bool ownerStopped = _ownerTask.stop(1500);
	if (! ownerStopped) {
		fprintf(stderr, "[ZT_ESP32_PORT] stop incomplete reason=owner_task_timeout node_not_deleted\n");
		return;
	}
	ESP32NodeTask::setDefaultTask((ESP32NodeTask*)0);
	_ownerTask.setNode((ZT_Node*)0);
	if (_node) {
		ZT_Node_delete(_node);
		_node = (ZT_Node*)0;
	}
	_started = false;
}

ZT_Node* ESP32LitePort::node() const { return _node; }
ESP32NetworkState* ESP32LitePort::networkState() { return &_networkState; }
ESP32NodeTask* ESP32LitePort::ownerTask() { return &_ownerTask; }
int ESP32LitePort::udpSocket() const { return _udpSocket; }
const ESP32LitePort::Config& ESP32LitePort::config() const { return _config; }

int ESP32LitePort::_openUdpSocket()
{
	int s = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0) {
		return -1;
	}
	int yes = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	struct sockaddr_in bindAddr;
	memset(&bindAddr, 0, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	bindAddr.sin_port = htons(_config.udpPort ? _config.udpPort : ZT_ESP32_UDP_PORT);
	if (bind(s, (const struct sockaddr*)&bindAddr, sizeof(bindAddr)) != 0) {
#ifdef ESP_PLATFORM
		closesocket(s);
#else
		close(s);
#endif
		return -1;
	}
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_NONBLOCK)
	const int flags = fcntl(s, F_GETFL, 0);
	if (flags >= 0) {
		(void)fcntl(s, F_SETFL, flags | O_NONBLOCK);
	}
#endif
	fprintf(stderr, "[ZT_ESP32_PORT] UDP bound port=%u sock=%d nonblocking=best_effort\n", (unsigned int)(_config.udpPort ? _config.udpPort : ZT_ESP32_UDP_PORT), s);
	return s;
}

int64_t ESP32LitePort::_nowMs()
{
#ifdef ESP_PLATFORM
	return (int64_t)(esp_timer_get_time() / 1000LL);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((int64_t)ts.tv_sec * 1000LL) + ((int64_t)ts.tv_nsec / 1000000LL);
#endif
}

int ESP32LitePort::wirePacketSendCallback(ZT_Node*, void* userPtr, void*, int64_t localSocket, const struct sockaddr_storage* remoteAddress, const void* packetData, unsigned int packetLength, unsigned int ttl)
{
	ESP32LitePort* self = (ESP32LitePort*)userPtr;
	if ((! self) || (! remoteAddress) || (! packetData) || (self->_udpSocket < 0)) {
		return -1;
	}
	(void)localSocket;
	if (ttl) {
		if (remoteAddress->ss_family == AF_INET) {
			setsockopt(self->_udpSocket, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
		}
#ifdef IPV6_UNICAST_HOPS
		else if (remoteAddress->ss_family == AF_INET6) {
			setsockopt(self->_udpSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &ttl, sizeof(ttl));
		}
#endif
	}
	const socklen_t slen = (remoteAddress->ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	const int rc = (int)sendto(self->_udpSocket, packetData, packetLength, 0, (const struct sockaddr*)remoteAddress, slen);
	if (ttl) {
		int defaultTtl = 64;
		if (remoteAddress->ss_family == AF_INET) {
			setsockopt(self->_udpSocket, IPPROTO_IP, IP_TTL, &defaultTtl, sizeof(defaultTtl));
		}
#ifdef IPV6_UNICAST_HOPS
		else if (remoteAddress->ss_family == AF_INET6) {
			setsockopt(self->_udpSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, &defaultTtl, sizeof(defaultTtl));
		}
#endif
	}
	if (rc == (int)packetLength) {
		return 0;
	}
	if ((rc < 0) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
		return -1;
	}
	return -1;
}

void ESP32LitePort::virtualNetworkFrameCallback(ZT_Node*, void* userPtr, void*, uint64_t nwid, void**, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, unsigned int vlanId, const void* frameData, unsigned int frameLength)
{
	(void)nwid;
	(void)vlanId;
	ESP32LitePort* self = (ESP32LitePort*)userPtr;
	if ((! self) || (! self->_networkState.tap())) {
		return;
	}
	self->_networkState.tap()->put(MAC(sourceMac), MAC(destMac), etherType, frameData, frameLength);
}

int ESP32LitePort::virtualNetworkConfigCallback(ZT_Node*, void* userPtr, void*, uint64_t nwid, void**, enum ZT_VirtualNetworkConfigOperation operation, const ZT_VirtualNetworkConfig* config)
{
	ESP32LitePort* self = (ESP32LitePort*)userPtr;
	if (! self) {
		return -1;
	}
	return self->_ownerTask.captureConfigUpdate(nwid, operation, config, 0) ? 0 : -1;
}

void ESP32LitePort::eventCallback(ZT_Node*, void* userPtr, void*, enum ZT_Event event, const void* metaData)
{
	(void)userPtr;
	(void)metaData;
	fprintf(stderr, "[ZT_ESP32_PORT] event=%d\n", (int)event);
}

void ESP32LitePort::statePutCallback(ZT_Node*, void* userPtr, void*, enum ZT_StateObjectType type, const uint64_t id[2], const void* data, int len)
{
	ESP32LitePort* self = (ESP32LitePort*)userPtr;
	if (self) {
		self->_statePut(type, id, data, len);
	}
}

int ESP32LitePort::stateGetCallback(ZT_Node*, void* userPtr, void*, enum ZT_StateObjectType type, const uint64_t id[2], void* data, unsigned int len)
{
	ESP32LitePort* self = (ESP32LitePort*)userPtr;
	return self ? self->_stateGet(type, id, data, len) : -1;
}

int ESP32LitePort::pathCheckCallback(ZT_Node*, void* userPtr, void*, uint64_t ztaddr, int64_t localSocket, const struct sockaddr_storage* remoteAddress)
{
	(void)userPtr;
	(void)ztaddr;
	(void)localSocket;
#ifdef ESP_PLATFORM
	if (remoteAddress && (remoteAddress->ss_family == AF_INET)) {
		const struct sockaddr_in* sin = (const struct sockaddr_in*)remoteAddress;
		ESP32LitePort* self = (ESP32LitePort*)userPtr;
		ESP32NetLink::LookupResult lr = ESP32NetLink::getInstance().lookupIpv4(sin->sin_addr.s_addr);
		if ((lr.matched && lr.supported) || (self && self->_networkState.managedIpv4Contains(sin->sin_addr.s_addr))) {
			fprintf(stderr, "[ZT_ESP32_PORT] path_check reject reason=zt_over_zt\n");
			return 0;
		}
	}
#else
	(void)remoteAddress;
#endif
	return 1;
}

static uint64_t _zt_esp32_fnv1a64(uint64_t a, uint64_t b, unsigned int type)
{
	uint64_t h = 1469598103934665603ULL;
	for (unsigned int i = 0; i < 8; ++i) { h = (h ^ (uint8_t)(a >> (i * 8))) * 1099511628211ULL; }
	for (unsigned int i = 0; i < 8; ++i) { h = (h ^ (uint8_t)(b >> (i * 8))) * 1099511628211ULL; }
	h = (h ^ (uint8_t)type) * 1099511628211ULL;
	return h;
}

void ESP32LitePort::_stateKey(enum ZT_StateObjectType type, const uint64_t id[2], char* out, unsigned int outLen)
{
	if ((! out) || (outLen == 0)) {
		return;
	}
	out[0] = 0;
	/* ESP-IDF NVS keys are limited to 15 visible characters. Keep the legacy
	 * identity/planet keys compatible with the existing ESP32-QEMU NVS image,
	 * and use compact hashed names for indexed objects. */
	switch (type) {
		case ZT_STATE_OBJECT_IDENTITY_PUBLIC: Utils::scopy(out, outLen, "id_pub"); return;
		case ZT_STATE_OBJECT_IDENTITY_SECRET: Utils::scopy(out, outLen, "id_sec"); return;
		case ZT_STATE_OBJECT_PLANET: Utils::scopy(out, outLen, "planet"); return;
		default: break;
	}

	const uint64_t a = id ? id[0] : 0;
	const uint64_t b = id ? id[1] : 0;
	const uint64_t h = _zt_esp32_fnv1a64(a, b, (unsigned int)type);
	char prefix = 'o';
	switch (type) {
		case ZT_STATE_OBJECT_MOON: prefix = 'm'; break;
		case ZT_STATE_OBJECT_PEER: prefix = 'p'; break;
		case ZT_STATE_OBJECT_NETWORK_CONFIG: prefix = 'n'; break;
		default: prefix = 'o'; break;
	}
	/* 1 prefix + 13 hex digits = 14 chars, safely below NVS's 15-char key limit. */
	snprintf(out, outLen, "%c%013llx", prefix, (unsigned long long)(h & 0x1ffffffffffffULL));
}

int ESP32LitePort::_statePut(enum ZT_StateObjectType type, const uint64_t id[2], const void* data, int len)
{
#ifdef ESP_PLATFORM
	char key[48];
	_stateKey(type, id, key, sizeof(key));
	nvs_handle_t h;
	if (nvs_open(_config.nvsNamespace ? _config.nvsNamespace : "zerotier", NVS_READWRITE, &h) != ESP_OK) {
		return -1;
	}
	esp_err_t err;
	if (len < 0) {
		err = nvs_erase_key(h, key);
	}
	else {
		/* Wear-conscious policy: avoid committing identical values without heap
		 * allocation. Compare small/medium values only; larger values are rare and
		 * written directly to avoid a large stack or heap scratch buffer. */
		size_t oldLen = 0;
		esp_err_t oldErr = nvs_get_blob(h, key, NULL, &oldLen);
		if ((oldErr == ESP_OK) && (oldLen == (size_t)len) && (len >= 0) && (len <= ZT_ESP32_NVS_COMPARE_BUFFER_SIZE)) {
			unsigned char old[ZT_ESP32_NVS_COMPARE_BUFFER_SIZE];
			size_t tmpLen = (size_t)len;
			if ((nvs_get_blob(h, key, old, &tmpLen) == ESP_OK) && (tmpLen == (size_t)len) && (memcmp(old, data, (size_t)len) == 0)) {
				nvs_close(h);
				return 0;
			}
		}
		err = nvs_set_blob(h, key, data, (size_t)len);
	}
	if (err == ESP_OK) {
		err = nvs_commit(h);
	}
	nvs_close(h);
	return (err == ESP_OK || (len < 0 && err == ESP_ERR_NVS_NOT_FOUND)) ? 0 : -1;
#else
	(void)type; (void)id; (void)data; (void)len;
	return -1;
#endif
}

int ESP32LitePort::_stateGet(enum ZT_StateObjectType type, const uint64_t id[2], void* data, unsigned int len)
{
#ifdef ESP_PLATFORM
	char key[48];
	_stateKey(type, id, key, sizeof(key));
	nvs_handle_t h;
	if (nvs_open(_config.nvsNamespace ? _config.nvsNamespace : "zerotier", NVS_READONLY, &h) != ESP_OK) {
		return -1;
	}
	size_t required = len;
	esp_err_t err = nvs_get_blob(h, key, data, &required);
	nvs_close(h);
	return (err == ESP_OK) ? (int)required : -1;
#else
	(void)type; (void)id; (void)data; (void)len;
	return -1;
#endif
}

#ifdef ESP_PLATFORM
void ESP32LitePort::_wireRxTaskEntry(void* arg)
{
	ESP32LitePort* self = (ESP32LitePort*)arg;
	if (self) {
		self->_wireRxLoop();
		if (self->_wireRxStopped) {
			xSemaphoreGive(self->_wireRxStopped);
		}
	}
	vTaskDelete(NULL);
}

void ESP32LitePort::_wireRxLoop()
{
	while (! _stopping) {
		ESP32FramePool::Buffer* b = ESP32FramePool::instance().acquireEmpty(ZT_ESP32_MAX_WIRE_PACKET_SIZE, 50);
		if (! b) {
			ESP_LOGW(ZT_ESP32_PORT_TAG, "wire RX drop reason=frame_pool_empty");
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		struct sockaddr_storage from;
		socklen_t flen = sizeof(from);
		const int n = recvfrom(_udpSocket, b->data, b->capacity, 0, (struct sockaddr*)&from, &flen);
		if (n <= 0) {
			ESP32FramePool::instance().release(b);
			if (_stopping) {
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		b->length = (uint16_t)n;
		if (! _ownerTask.enqueueWirePacketBuffer(_udpSocket, &from, b, 0)) {
			ESP32FramePool::instance().release(b);
			ESP_LOGW(ZT_ESP32_PORT_TAG, "wire RX drop queue full len=%d", n);
		}
	}
}
#endif

} // namespace ZeroTier
