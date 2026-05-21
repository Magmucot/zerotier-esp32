/* ESP32 Lite top-level ZeroTier port glue.
 *
 * This file intentionally stays outside OneService. It wires the C Node API to
 * ESP-IDF/lwIP/NVS while preserving the same high-level ownership model:
 * one owner task serializes all ZT_Node_processXXX calls and platform callbacks
 * only enqueue/capture work.
 */
#ifndef ZT_ESP32LITEPORT_HPP
#define ZT_ESP32LITEPORT_HPP

#include "ESP32NetworkState.hpp"
#include "ESP32NodeTask.hpp"
#include "../include/ZeroTierOne.h"

#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif

#ifndef ZT_ESP32_UDP_PORT
#define ZT_ESP32_UDP_PORT 9993
#endif
#ifndef ZT_ESP32_WIRE_RX_STACK_WORDS
#define ZT_ESP32_WIRE_RX_STACK_WORDS 8192
#endif
#ifndef ZT_ESP32_OWNER_STACK_WORDS
#define ZT_ESP32_OWNER_STACK_WORDS 32768
#endif
#ifndef ZT_ESP32_NVS_COMPARE_BUFFER_SIZE
#define ZT_ESP32_NVS_COMPARE_BUFFER_SIZE 1024
#endif

namespace ZeroTier {

class ESP32LitePort {
  public:
	struct Config {
		uint64_t nwid;
		uint16_t udpPort;
		const char* homePath;
		const char* nvsNamespace;
		uint32_t ownerTaskStackWords;
		uint32_t wireRxTaskStackWords;
		UBaseType_t ownerTaskPriority;
		UBaseType_t wireRxTaskPriority;
		BaseType_t ownerTaskCoreId;
		BaseType_t wireRxTaskCoreId;
		int enableEncryptedHello;
		int lowBandwidthMode;
	};

	ESP32LitePort();
	~ESP32LitePort();

	static void defaultConfig(Config& config, uint64_t nwid);

	int start(const Config& config);
	int join(uint64_t nwid);
	bool verifyProvisionedIdentity() const;
	void stop();
	ZT_Node* node() const;
	ESP32NetworkState* networkState();
	ESP32NodeTask* ownerTask();
	int udpSocket() const;
	const Config& config() const;

	/* C callback entry points. */
	static int wirePacketSendCallback(ZT_Node*, void* userPtr, void*, int64_t localSocket, const struct sockaddr_storage* remoteAddress, const void* packetData, unsigned int packetLength, unsigned int ttl);
	static void virtualNetworkFrameCallback(ZT_Node*, void* userPtr, void*, uint64_t nwid, void**, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, unsigned int vlanId, const void* frameData, unsigned int frameLength);
	static int virtualNetworkConfigCallback(ZT_Node*, void* userPtr, void*, uint64_t nwid, void**, enum ZT_VirtualNetworkConfigOperation operation, const ZT_VirtualNetworkConfig* config);
	static void eventCallback(ZT_Node*, void* userPtr, void*, enum ZT_Event event, const void* metaData);
	static void statePutCallback(ZT_Node*, void* userPtr, void*, enum ZT_StateObjectType type, const uint64_t id[2], const void* data, int len);
	static int stateGetCallback(ZT_Node*, void* userPtr, void*, enum ZT_StateObjectType type, const uint64_t id[2], void* data, unsigned int len);
	static int pathCheckCallback(ZT_Node*, void* userPtr, void*, uint64_t ztaddr, int64_t localSocket, const struct sockaddr_storage* remoteAddress);

  private:
	ESP32LitePort(const ESP32LitePort&);
	ESP32LitePort& operator=(const ESP32LitePort&);

	int _openUdpSocket();
	int _statePut(enum ZT_StateObjectType type, const uint64_t id[2], const void* data, int len);
	int _stateGet(enum ZT_StateObjectType type, const uint64_t id[2], void* data, unsigned int len);
	static void _stateKey(enum ZT_StateObjectType type, const uint64_t id[2], char* out, unsigned int outLen);
	static int64_t _nowMs();
#ifdef ESP_PLATFORM
	static void _wireRxTaskEntry(void* arg);
	void _wireRxLoop();
#endif

	Config _config;
	ZT_Node* _node;
	int _udpSocket;
	ESP32NetworkState _networkState;
	ESP32NodeTask _ownerTask;
	bool _started;
	volatile bool _stopping;
#ifdef ESP_PLATFORM
	TaskHandle_t _wireRxTask;
	SemaphoreHandle_t _wireRxStopped;
#endif
};

} // namespace ZeroTier

#endif
