/* ESP32 Lite fixed-size packet/frame pool.
 *
 * FreeRTOS queues should carry small descriptors, not 1.5--2 KB packet
 * buffers. lwIP linkoutput and UDP RX copy into this bounded pool and enqueue
 * only a pointer to the ZT owner task. The owner task releases the buffer after
 * the matching ZT_Node_processXXX call returns.
 */
#ifndef ZT_ESP32FRAMEPOOL_HPP
#define ZT_ESP32FRAMEPOOL_HPP

#include <stdint.h>
#include <string.h>

#ifndef ZT_ESP32_MAX_WIRE_PACKET_SIZE
#define ZT_ESP32_MAX_WIRE_PACKET_SIZE 2048
#endif
#ifndef ZT_ESP32_FRAME_POOL_BUFFERS
#define ZT_ESP32_FRAME_POOL_BUFFERS 8
#endif

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

namespace ZeroTier {

class ESP32FramePool {
  public:
	struct Buffer {
		bool inUse;
		uint16_t length;
		uint16_t capacity;
		unsigned char data[ZT_ESP32_MAX_WIRE_PACKET_SIZE];
	};

	static ESP32FramePool& instance();

	Buffer* acquireCopy(const void* data, unsigned int length, uint32_t timeoutMs);
	Buffer* acquireEmpty(unsigned int length, uint32_t timeoutMs);
	void release(Buffer* buffer);
	unsigned int freeCount() const;
	unsigned int capacity() const;

  private:
	ESP32FramePool();
	ESP32FramePool(const ESP32FramePool&);
	ESP32FramePool& operator=(const ESP32FramePool&);

	bool _lock(uint32_t timeoutMs) const;
	void _unlock() const;

	mutable bool _fallbackLock;
#ifdef ESP_PLATFORM
	SemaphoreHandle_t _mutex;
#endif
	Buffer _buffers[ZT_ESP32_FRAME_POOL_BUFFERS];
};

} // namespace ZeroTier

#endif
