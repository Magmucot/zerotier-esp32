#include "ESP32FramePool.hpp"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "freertos/task.h"
#endif

namespace ZeroTier {

ESP32FramePool& ESP32FramePool::instance()
{
	static ESP32FramePool p;
	return p;
}

ESP32FramePool::ESP32FramePool()
	: _fallbackLock(false)
#ifdef ESP_PLATFORM
	, _mutex((SemaphoreHandle_t)0)
#endif
{
	for (unsigned int i = 0; i < ZT_ESP32_FRAME_POOL_BUFFERS; ++i) {
		_buffers[i].inUse = false;
		_buffers[i].length = 0;
		_buffers[i].capacity = ZT_ESP32_MAX_WIRE_PACKET_SIZE;
	}
#ifdef ESP_PLATFORM
	_mutex = xSemaphoreCreateMutex();
#endif
}

bool ESP32FramePool::_lock(uint32_t timeoutMs) const
{
#ifdef ESP_PLATFORM
	if (! _mutex) {
		return false;
	}
	return (xSemaphoreTake(_mutex, timeoutMs ? pdMS_TO_TICKS(timeoutMs) : 0) == pdTRUE);
#else
	(void)timeoutMs;
	while (_fallbackLock) {}
	const_cast<ESP32FramePool*>(this)->_fallbackLock = true;
	return true;
#endif
}

void ESP32FramePool::_unlock() const
{
#ifdef ESP_PLATFORM
	if (_mutex) {
		xSemaphoreGive(_mutex);
	}
#else
	const_cast<ESP32FramePool*>(this)->_fallbackLock = false;
#endif
}

ESP32FramePool::Buffer* ESP32FramePool::acquireEmpty(unsigned int length, uint32_t timeoutMs)
{
	if (length > ZT_ESP32_MAX_WIRE_PACKET_SIZE) {
		return (Buffer*)0;
	}
	const uint32_t stepMs = 5;
	uint32_t waited = 0;
	for (;;) {
		if (_lock(0)) {
			for (unsigned int i = 0; i < ZT_ESP32_FRAME_POOL_BUFFERS; ++i) {
				if (! _buffers[i].inUse) {
					_buffers[i].inUse = true;
					_buffers[i].length = (uint16_t)length;
					_unlock();
					return &_buffers[i];
				}
			}
			_unlock();
		}
		if ((timeoutMs == 0) || (waited >= timeoutMs)) {
			return (Buffer*)0;
		}
#ifdef ESP_PLATFORM
		vTaskDelay(pdMS_TO_TICKS(stepMs));
#endif
		waited += stepMs;
	}
}

ESP32FramePool::Buffer* ESP32FramePool::acquireCopy(const void* data, unsigned int length, uint32_t timeoutMs)
{
	if ((length > 0) && (! data)) {
		return (Buffer*)0;
	}
	Buffer* b = acquireEmpty(length, timeoutMs);
	if (! b) {
		return (Buffer*)0;
	}
	if (length > 0) {
		memcpy(b->data, data, length);
	}
	return b;
}

void ESP32FramePool::release(Buffer* buffer)
{
	if (! buffer) {
		return;
	}
	/* Release must not silently fail, otherwise a transient mutex collision can
	 * permanently leak one of the small fixed pool entries. */
	if (_lock(1000)) {
		buffer->length = 0;
		buffer->inUse = false;
		_unlock();
	}
	else {
		fprintf(stderr, "[ZT_ESP32_POOL] release failed reason=lock_timeout\n");
	}
}

unsigned int ESP32FramePool::freeCount() const
{
	unsigned int n = 0;
	if (_lock(0)) {
		for (unsigned int i = 0; i < ZT_ESP32_FRAME_POOL_BUFFERS; ++i) {
			if (! _buffers[i].inUse) {
				++n;
			}
		}
		_unlock();
	}
	return n;
}

unsigned int ESP32FramePool::capacity() const
{
	return ZT_ESP32_FRAME_POOL_BUFFERS;
}

} // namespace ZeroTier
