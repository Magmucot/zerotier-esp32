/* Bounded ESP32 egress-frame trace for ZeroTier Lite.
 * This is intentionally diagnostic-only and disabled by default. It prints
 * facts about real frames leaving lwIP toward ZeroTier before they are enqueued
 * to the ZT owner task.
 */
#ifndef ZT_ESP32EGRESSTRACE_HPP
#define ZT_ESP32EGRESSTRACE_HPP

#include <stdint.h>
#include "../node/MAC.hpp"

namespace ZeroTier {

class ESP32EgressTrace {
  public:
	static void enable(uint64_t nwid, uint32_t targetIpv4Nbo, uint32_t markerId, uint32_t maxFrames);
	static void disable();
	static bool enabled();
	static void traceFrame(uint64_t nwid, const MAC& src, const MAC& dst, unsigned int etherType, const void* payload, unsigned int payloadLen);

  private:
	static bool _ipv4Matches(uint32_t ipNbo);
	static void _printIpv4(uint32_t nbo);
};

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" void zt_debug_enableEgressTrace_ipv4(uint64_t nwid, uint32_t ip_nbo, uint32_t marker_id, uint32_t frames);
#endif

#endif
