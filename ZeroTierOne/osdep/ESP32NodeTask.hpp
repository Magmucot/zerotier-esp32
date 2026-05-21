/* ESP32 owner task for ZeroTier C API serialization.
 *
 * All ZT_Node_processXXX calls and ZeroTier network-modifying calls must be
 * serialized through this owner task. lwIP linkoutput and UDP RX tasks enqueue
 * events here instead of calling ZeroTier directly.
 */
#ifndef ZT_ESP32NODETASK_HPP
#define ZT_ESP32NODETASK_HPP

#include "ESP32NetworkState.hpp"
#include "ESP32FramePool.hpp"
#include "../include/ZeroTierOne.h"

#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#ifndef ZT_ESP32_OWNER_QUEUE_SIZE
#define ZT_ESP32_OWNER_QUEUE_SIZE 16
#endif
#ifndef ZT_ESP32_MAX_WIRE_PACKET_SIZE
#define ZT_ESP32_MAX_WIRE_PACKET_SIZE 2048
#endif
#ifndef ZT_ESP32_MAX_VIRTUAL_FRAME_SIZE
#define ZT_ESP32_MAX_VIRTUAL_FRAME_SIZE 1600
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#else
typedef unsigned int UBaseType_t;
typedef int BaseType_t;
#endif

namespace ZeroTier {

class ESP32NodeTask {
  public:
	enum EventType {
		EVENT_NONE = 0,
		EVENT_WIRE_PACKET = 1,
		EVENT_VIRTUAL_FRAME = 2,
		EVENT_CONFIG_UPDATE = 3,
		EVENT_BACKGROUND = 4,
		EVENT_JOIN_NETWORK = 5,
		EVENT_STOP = 99
	};

	struct Event {
		EventType type;
		int64_t localSocket;
		struct sockaddr_storage remoteAddress;
		ESP32FramePool::Buffer* buffer;
		uint64_t nwid;
		uint64_t sourceMac;
		uint64_t destMac;
		unsigned int etherType;
		unsigned int vlanId;
	};

	struct PendingConfig {
		bool valid;
		uint64_t nwid;
		enum ZT_VirtualNetworkConfigOperation operation;
		ZT_VirtualNetworkConfig config;
	};

	ESP32NodeTask(ZT_Node* node, void* tptr, ESP32NetworkState* networkState);
	~ESP32NodeTask();

	bool start(const char* taskName, uint32_t stackWords, UBaseType_t priority, BaseType_t coreId);
	bool stop(uint32_t timeoutMs);
	void runLoop();
	bool pumpOnce(uint32_t timeoutMs);

	bool enqueueWirePacket(int64_t localSocket, const struct sockaddr_storage* remoteAddress, const void* data, unsigned int length, uint32_t timeoutMs);
	/* Enqueue an already acquired frame-pool buffer. On success ownership moves
	 * to the owner task. On failure the caller still owns and must release it.
	 */
	bool enqueueWirePacketBuffer(int64_t localSocket, const struct sockaddr_storage* remoteAddress, ESP32FramePool::Buffer* buffer, uint32_t timeoutMs);
	bool enqueueVirtualFrame(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, const void* frameData, unsigned int frameLength, uint32_t timeoutMs);
	/* Enqueue an already acquired frame-pool buffer. On success ownership moves
	 * to the owner task. On failure the caller still owns and must release it.
	 */
	bool enqueueVirtualFrameBuffer(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, ESP32FramePool::Buffer* buffer, uint32_t timeoutMs);
	/* Capture a NetworkConfig callback payload without doing route/TAP work inside the
	 * ZeroTier callback. This copies one pending config and enqueues a tiny
	 * EVENT_CONFIG_UPDATE token; the heavy apply runs after processXXX returns.
	 */
	bool captureConfigUpdate(uint64_t nwid, enum ZT_VirtualNetworkConfigOperation op, const ZT_VirtualNetworkConfig* config, uint32_t timeoutMs);
	bool enqueueBackground(uint32_t timeoutMs);
	bool enqueueJoin(uint64_t nwid, uint32_t timeoutMs);

	void setNode(ZT_Node* node);
	void setNetworkState(ESP32NetworkState* networkState);
	bool isRunning() const;
	static ESP32NodeTask* defaultTask();
	static void setDefaultTask(ESP32NodeTask* task);

  private:
	ESP32NodeTask(const ESP32NodeTask&);
	ESP32NodeTask& operator=(const ESP32NodeTask&);
	bool _enqueue(const Event& event, uint32_t timeoutMs);
	void _handleEvent(const Event& event);
	void _applyPendingConfig();
	void _releaseEventBuffer(const Event& event);
#ifdef ESP_PLATFORM
	static void _taskEntry(void* arg);
	QueueHandle_t _queue;
	TaskHandle_t _task;
	SemaphoreHandle_t _stopped;
#endif
	ZT_Node* _node;
	void* _tptr;
	ESP32NetworkState* _networkState;
	PendingConfig _pendingConfig;
	volatile bool _running;
	volatile int64_t _nextBackgroundTaskDeadline;
};

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" int zt_esp32_enqueue_virtual_frame_from_lwip(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, const void* frameData, unsigned int frameLength);
#endif

#endif
