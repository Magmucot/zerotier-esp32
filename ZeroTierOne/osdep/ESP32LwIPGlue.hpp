/* ESP32 lwIP thread-context glue for ZeroTier Lite. */
#ifndef ZT_ESP32LWIPGLUE_HPP
#define ZT_ESP32LWIPGLUE_HPP

#ifdef ESP_PLATFORM
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"
#include <stdint.h>
#endif

namespace ZeroTier {

class ESP32LwIPGlue {
  public:
#ifdef ESP_PLATFORM
	static bool captureTcpipThread(uint32_t timeoutMs);
	static bool addNetif(struct netif* n, void* state, netif_init_fn initFn);
	static void removeNetif(struct netif* n);
	static void setIpv4(struct netif* n, const ip4_addr_t* ip, const ip4_addr_t* mask, const ip4_addr_t* gw);
	static void setUp(struct netif* n, bool up);
	static void setLinkUp(struct netif* n, bool up);
	static err_t input(struct netif* n, struct pbuf* p);
#endif
};

} // namespace ZeroTier

#endif
