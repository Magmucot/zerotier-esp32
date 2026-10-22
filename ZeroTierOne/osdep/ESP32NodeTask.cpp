#include "ESP32NodeTask.hpp"

#include "../node/Utils.hpp"

#include <stdio.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_timer.h"
#include "freertos/portmacro.h"
#endif

namespace ZeroTier {

static int64_t _esp32_now_ms()
{
#ifdef ESP_PLATFORM
	return (int64_t)(esp_timer_get_time() / 1000LL);
#else
	return 0;
#endif
}

static ESP32NodeTask* _defaultTask = (ESP32NodeTask*)0;
#ifdef ESP_PLATFORM
static portMUX_TYPE _ztPendingConfigMux = portMUX_INITIALIZER_UNLOCKED;
#define ZT_PENDING_LOCK() portENTER_CRITICAL(&_ztPendingConfigMux)
#define ZT_PENDING_UNLOCK() portEXIT_CRITICAL(&_ztPendingConfigMux)
#else
#define ZT_PENDING_LOCK() do { } while (0)
#define ZT_PENDING_UNLOCK() do { } while (0)
#endif

ESP32NodeTask::ESP32NodeTask(ZT_Node* node, void* tptr, ESP32NetworkState* networkState)
#ifdef ESP_PLATFORM
	: _queue((QueueHandle_t)0), _task((TaskHandle_t)0), _stopped((SemaphoreHandle_t)0)
	, _node(node), _tptr(tptr), _networkState(networkState), _running(false), _nextBackgroundTaskDeadline(0)
#else
	: _node(node), _tptr(tptr), _networkState(networkState), _running(false), _nextBackgroundTaskDeadline(0)
#endif
{
	memset(&_pendingConfig, 0, sizeof(_pendingConfig));
#ifdef ESP_PLATFORM
	_queue = xQueueCreate(ZT_ESP32_OWNER_QUEUE_SIZE, sizeof(Event));
	_stopped = xSemaphoreCreateBinary();
#endif
}

ESP32NodeTask::~ESP32NodeTask()
{
	const bool stopped = stop(1000);
#ifdef ESP_PLATFORM
	if (stopped) {
		if (_queue) {
			vQueueDelete(_queue);
			_queue = (QueueHandle_t)0;
		}
		if (_stopped) {
			vSemaphoreDelete(_stopped);
			_stopped = (SemaphoreHandle_t)0;
		}
	}
	if (_defaultTask == this) {
		_defaultTask = (ESP32NodeTask*)0;
	}
#endif
}

void ESP32NodeTask::setNode(ZT_Node* node)
{
	_node = node;
}

void ESP32NodeTask::setNetworkState(ESP32NetworkState* networkState)
{
	_networkState = networkState;
}

bool ESP32NodeTask::isRunning() const
{
	return _running;
}

ESP32NodeTask* ESP32NodeTask::defaultTask()
{
	return _defaultTask;
}

void ESP32NodeTask::setDefaultTask(ESP32NodeTask* task)
{
	_defaultTask = task;
}

bool ESP32NodeTask::start(const char* taskName, uint32_t stackWords, UBaseType_t priority, BaseType_t coreId)
{
#ifdef ESP_PLATFORM
	if (! _queue) {
		return false;
	}
	if (_task) {
		return true;
	}
	_running = true;
	if (_stopped) {
		xSemaphoreTake(_stopped, 0);
	}
	BaseType_t rc;
#ifdef CONFIG_FREERTOS_UNICORE
	(void)coreId;
	rc = xTaskCreate(&ESP32NodeTask::_taskEntry, taskName ? taskName : "zt_owner", stackWords, this, priority, &_task);
#else
	if (coreId >= 0) {
		rc = xTaskCreatePinnedToCore(&ESP32NodeTask::_taskEntry, taskName ? taskName : "zt_owner", stackWords, this, priority, &_task, coreId);
	}
	else {
		rc = xTaskCreate(&ESP32NodeTask::_taskEntry, taskName ? taskName : "zt_owner", stackWords, this, priority, &_task);
	}
#endif
	if (rc != pdPASS) {
		_running = false;
		_task = (TaskHandle_t)0;
		return false;
	}
	return true;
#else
	(void)taskName;
	(void)stackWords;
	(void)priority;
	(void)coreId;
	return false;
#endif
}

bool ESP32NodeTask::stop(uint32_t timeoutMs)
{
#ifdef ESP_PLATFORM
	if (! _task) {
		_running = false;
		return true;
	}
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_STOP;
	(void)_enqueue(e, timeoutMs ? timeoutMs : 1);
	_running = false;
	if (_stopped) {
		if (xSemaphoreTake(_stopped, pdMS_TO_TICKS(timeoutMs ? timeoutMs : 1000)) != pdTRUE) {
			/* Do not forcibly delete an owner task that may be inside ZeroTier code.
			 * Caller must not delete ZT_Node until this returns true. */
			return false;
		}
	}
	_task = (TaskHandle_t)0;
	return true;
#else
	(void)timeoutMs;
	_running = false;
	return true;
#endif
}

bool ESP32NodeTask::_enqueue(const Event& event, uint32_t timeoutMs)
{
#ifdef ESP_PLATFORM
	if (! _queue) {
		return false;
	}
	if ((! _running) && (event.type != EVENT_STOP)) {
		return false;
	}
	const TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
	return (xQueueSend(_queue, &event, ticks) == pdTRUE);
#else
	(void)event;
	(void)timeoutMs;
	return false;
#endif
}

bool ESP32NodeTask::enqueueWirePacket(int64_t localSocket, const struct sockaddr_storage* remoteAddress, const void* data, unsigned int length, uint32_t timeoutMs)
{
	ESP32FramePool::Buffer* b = ESP32FramePool::instance().acquireCopy(data, length, timeoutMs);
	if (! b) {
		return false;
	}
	if (! enqueueWirePacketBuffer(localSocket, remoteAddress, b, timeoutMs)) {
		ESP32FramePool::instance().release(b);
		return false;
	}
	return true;
}

bool ESP32NodeTask::enqueueWirePacketBuffer(int64_t localSocket, const struct sockaddr_storage* remoteAddress, ESP32FramePool::Buffer* b, uint32_t timeoutMs)
{
	if (! b) {
		return false;
	}
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_WIRE_PACKET;
	e.localSocket = localSocket;
	e.buffer = b;
	if (remoteAddress) {
		e.remoteAddress = *remoteAddress;
	}
	return _enqueue(e, timeoutMs);
}

bool ESP32NodeTask::enqueueVirtualFrame(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, const void* frameData, unsigned int frameLength, uint32_t timeoutMs)
{
	if (frameLength > ZT_ESP32_MAX_VIRTUAL_FRAME_SIZE) {
		return false;
	}
	ESP32FramePool::Buffer* b = ESP32FramePool::instance().acquireCopy(frameData, frameLength, timeoutMs);
	if (! b) {
		return false;
	}
	if (! enqueueVirtualFrameBuffer(nwid, sourceMac, destMac, etherType, b, timeoutMs)) {
		ESP32FramePool::instance().release(b);
		return false;
	}
	return true;
}

bool ESP32NodeTask::enqueueVirtualFrameBuffer(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, ESP32FramePool::Buffer* b, uint32_t timeoutMs)
{
	if (! b) {
		return false;
	}
	if (b->length > ZT_ESP32_MAX_VIRTUAL_FRAME_SIZE) {
		return false;
	}
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_VIRTUAL_FRAME;
	e.nwid = nwid;
	e.sourceMac = sourceMac;
	e.destMac = destMac;
	e.etherType = etherType;
	e.vlanId = 0;
	e.buffer = b;
	return _enqueue(e, timeoutMs);
}

bool ESP32NodeTask::captureConfigUpdate(uint64_t nwid, enum ZT_VirtualNetworkConfigOperation op, const ZT_VirtualNetworkConfig* config, uint32_t timeoutMs)
{
	ZT_PENDING_LOCK();
	_pendingConfig.valid = true;
	_pendingConfig.nwid = nwid;
	_pendingConfig.operation = op;
	if (config) {
		_pendingConfig.config = *config;
	}
	else {
		memset(&_pendingConfig.config, 0, sizeof(_pendingConfig.config));
		_pendingConfig.config.nwid = nwid;
	}
	ZT_PENDING_UNLOCK();
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_CONFIG_UPDATE;
	/* If this callback is invoked from inside a ZT_Node_processXXX call on the
	 * owner task, _applyPendingConfig() will run immediately after processXXX
	 * returns even if the tiny notification token cannot be queued. Returning
	 * success avoids incorrectly putting the network into PORT_ERROR due only to
	 * a full owner queue. */
	(void)_enqueue(e, timeoutMs);
	return true;
}

bool ESP32NodeTask::enqueueBackground(uint32_t timeoutMs)
{
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_BACKGROUND;
	return _enqueue(e, timeoutMs);
}

bool ESP32NodeTask::enqueueJoin(uint64_t nwid, uint32_t timeoutMs)
{
	Event e;
	memset(&e, 0, sizeof(e));
	e.type = EVENT_JOIN_NETWORK;
	e.nwid = nwid;
	return _enqueue(e, timeoutMs);
}

bool ESP32NodeTask::pumpOnce(uint32_t timeoutMs)
{
#ifdef ESP_PLATFORM
	Event e;
	const TickType_t ticks = (timeoutMs == 0) ? 0 : pdMS_TO_TICKS(timeoutMs);
	if (xQueueReceive(_queue, &e, ticks) == pdTRUE) {
		_handleEvent(e);
		return true;
	}
	return false;
#else
	(void)timeoutMs;
	return false;
#endif
}

void ESP32NodeTask::runLoop()
{
	while (_running) {
		pumpOnce(100);
		if (_running && _node) {
			ZT_Node_processBackgroundTasks(_node, _tptr, _esp32_now_ms(), &_nextBackgroundTaskDeadline);
			_applyPendingConfig();
		}
	}
	/* Drain/drop any queued buffers so shutdown cannot leak pool entries. */
#ifdef ESP_PLATFORM
	if (_queue) {
		Event e;
		while (xQueueReceive(_queue, &e, 0) == pdTRUE) {
			_releaseEventBuffer(e);
		}
	}
#endif
}

void ESP32NodeTask::_releaseEventBuffer(const Event& e)
{
	if (e.buffer) {
		ESP32FramePool::instance().release(e.buffer);
	}
}

void ESP32NodeTask::_handleEvent(const Event& e)
{
	if (e.type == EVENT_STOP) {
		_running = false;
		_releaseEventBuffer(e);
		return;
	}
	if (! _node) {
		_releaseEventBuffer(e);
		return;
	}
	switch (e.type) {
		case EVENT_WIRE_PACKET:
			if (e.buffer) {
				ZT_Node_processWirePacket(_node, _tptr, _esp32_now_ms(), e.localSocket, &e.remoteAddress, e.buffer->data, e.buffer->length, &_nextBackgroundTaskDeadline);
				ESP32FramePool::instance().release(e.buffer);
			}
			_applyPendingConfig();
			break;
		case EVENT_VIRTUAL_FRAME:
			if (e.buffer) {
				ZT_Node_processVirtualNetworkFrame(_node, _tptr, _esp32_now_ms(), e.nwid, e.sourceMac, e.destMac, e.etherType, e.vlanId, e.buffer->data, e.buffer->length, &_nextBackgroundTaskDeadline);
				ESP32FramePool::instance().release(e.buffer);
			}
			_applyPendingConfig();
			break;
		case EVENT_CONFIG_UPDATE:
			_applyPendingConfig();
			break;
		case EVENT_BACKGROUND:
			ZT_Node_processBackgroundTasks(_node, _tptr, _esp32_now_ms(), &_nextBackgroundTaskDeadline);
			_applyPendingConfig();
			break;
		case EVENT_JOIN_NETWORK:
			ZT_Node_join(_node, e.nwid, (void*)0, _tptr);
			_applyPendingConfig();
			break;
		default:
			_releaseEventBuffer(e);
			break;
	}
}

void ESP32NodeTask::_applyPendingConfig()
{
	if (! _networkState) {
		return;
	}
	PendingConfig pc;
	memset(&pc, 0, sizeof(pc));
	ZT_PENDING_LOCK();
	if (_pendingConfig.valid) {
		pc = _pendingConfig;
		_pendingConfig.valid = false;
	}
	ZT_PENDING_UNLOCK();
	if (! pc.valid) {
		return;
	}
	_networkState->applyConfigFromOwnerTask(_node, _tptr, pc.operation, &pc.config);
}

#ifdef ESP_PLATFORM
void ESP32NodeTask::_taskEntry(void* arg)
{
	ESP32NodeTask* self = (ESP32NodeTask*)arg;
	if (self) {
		self->runLoop();
		if (self->_stopped) {
			xSemaphoreGive(self->_stopped);
		}
	}
	vTaskDelete(NULL);
}
#endif

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" int zt_esp32_enqueue_virtual_frame_from_lwip(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, const void* frameData, unsigned int frameLength)
{
	ZeroTier::ESP32NodeTask* task = ZeroTier::ESP32NodeTask::defaultTask();
	if (! task) {
		return -1;
	}
	return task->enqueueVirtualFrame(nwid, sourceMac, destMac, etherType, frameData, frameLength, 0) ? 0 : -1;
}
#endif
