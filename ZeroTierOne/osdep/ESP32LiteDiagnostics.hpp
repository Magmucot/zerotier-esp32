#ifndef ZT_ESP32LITEDIAGNOSTICS_HPP
#define ZT_ESP32LITEDIAGNOSTICS_HPP

#include "ESP32LitePort.hpp"

namespace ZeroTier {

class ESP32LiteDiagnostics {
  public:
	static void dumpAll(ESP32LitePort* port);
	static void dumpHeapAndStack(const char* phase);
	static void dumpRoutes();
	static void dumpFramePool();
};

} // namespace ZeroTier

#endif
