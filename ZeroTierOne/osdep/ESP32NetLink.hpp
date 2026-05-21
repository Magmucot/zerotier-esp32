/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * ESP32 Lite route backend for ZeroTier managed-route semantics.
 * This is not a Linux NetLink emulation layer. It is the ESP32/lwIP
 * counterpart to LinuxNetLink: target/via/src/device are stored in a
 * bounded route table and consumed by lwIP routing hooks.
 */

#ifndef ZT_ESP32NETLINK_HPP
#define ZT_ESP32NETLINK_HPP

#include "../node/InetAddress.hpp"

#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
struct netif;
struct ip4_addr;
typedef struct ip4_addr ip4_addr_t;
#endif

#ifndef ZT_ESP32_MAX_MANAGED_ROUTES
#define ZT_ESP32_MAX_MANAGED_ROUTES 8
#endif

namespace ZeroTier {

class ESP32NetLink {
  public:
	struct RouteEntry {
		bool active;
		bool supported;
		char unsupportedReason[64];
		InetAddress target;
		InetAddress via;
		InetAddress src;
		char device[32];
		uint32_t targetNbo;
		uint32_t maskNbo;
		uint32_t viaNbo;
		uint32_t srcNbo;
		uint8_t prefixBits;
		uint32_t generation;
	};

	struct LookupResult {
		bool matched;
		bool supported;
		bool hasVia;
		RouteEntry route;
#ifdef ESP_PLATFORM
		struct netif* netifPtr;
		ip4_addr_t gateway;
#endif
	};

	static ESP32NetLink& getInstance();

	bool routeIsSet(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device);
	void addRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device);
	void delRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device);

	void clear();
	unsigned int generation() const;

#ifdef ESP_PLATFORM
	void setZeroTierNetif(struct netif* netifPtr, const char* deviceName);
	struct netif* zeroTierNetif() const;
	const char* zeroTierDeviceName() const;

	LookupResult lookupIpv4(uint32_t destNbo) const;
	struct netif* routeHookIpv4(const ip4_addr_t* src, const ip4_addr_t* dest) const;
	const ip4_addr_t* etharpGetGatewayHook(struct netif* netifPtr, const ip4_addr_t* dest) const;
#endif

	unsigned int copyRoutes(RouteEntry* out, unsigned int capacity) const;

  private:
	ESP32NetLink();
	ESP32NetLink(const ESP32NetLink&);
	ESP32NetLink& operator=(const ESP32NetLink&);

	static bool _normalizeRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device, RouteEntry& out);
	static uint32_t _maskForPrefix(uint8_t prefixBits);
	int _findRouteLocked(const RouteEntry& normalized) const;
	int _findFreeSlotLocked() const;

	mutable volatile bool _lock;
	RouteEntry _routes[ZT_ESP32_MAX_MANAGED_ROUTES];
	uint32_t _generation;
#ifdef ESP_PLATFORM
	struct netif* _ztNetif;
	char _ztDeviceName[32];
	mutable ip4_addr_t _gatewayScratch[4];
	mutable uint8_t _gatewayScratchIndex;
#endif
};

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" struct netif* zt_esp32_lwip_ip4_route_src(const ip4_addr_t* src, const ip4_addr_t* dest);
extern "C" struct netif* zt_esp32_lwip_ip4_route(const ip4_addr_t* dest);
extern "C" const ip4_addr_t* zt_esp32_lwip_etharp_get_gw(struct netif* netifPtr, const ip4_addr_t* dest);
#endif

#endif
