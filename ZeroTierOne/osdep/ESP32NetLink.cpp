/* See ESP32NetLink.hpp. */

#include "ESP32NetLink.hpp"

#include "../node/Utils.hpp"

#include <stdio.h>

#ifdef ESP_PLATFORM
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#endif

namespace ZeroTier {

#ifdef ESP_PLATFORM
static portMUX_TYPE _esp32NetLinkMux = portMUX_INITIALIZER_UNLOCKED;
#define ZT_ESP32_NETLINK_LOCK() portENTER_CRITICAL(&_esp32NetLinkMux)
#define ZT_ESP32_NETLINK_UNLOCK() portEXIT_CRITICAL(&_esp32NetLinkMux)
#else
#define ZT_ESP32_NETLINK_LOCK() do { } while (0)
#define ZT_ESP32_NETLINK_UNLOCK() do { } while (0)
#endif

ESP32NetLink& ESP32NetLink::getInstance()
{
	static ESP32NetLink instance;
	return instance;
}

ESP32NetLink::ESP32NetLink() : _lock(false), _generation(0)
#ifdef ESP_PLATFORM
	, _ztNetif((struct netif*)0)
#endif
{
	memset(_routes, 0, sizeof(_routes));
#ifdef ESP_PLATFORM
	_ztDeviceName[0] = (char)0;
	memset(_gatewayScratch, 0, sizeof(_gatewayScratch));
	_gatewayScratchIndex = 0;
#endif
}

uint32_t ESP32NetLink::_maskForPrefix(uint8_t prefixBits)
{
	if (prefixBits == 0) {
		return 0;
	}
	if (prefixBits >= 32) {
		return 0xffffffffU;
	}
	return (uint32_t)(0xffffffffU << (32 - prefixBits));
}

bool ESP32NetLink::_normalizeRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device, RouteEntry& out)
{
	memset(&out, 0, sizeof(out));
	out.target = target;
	out.via = via;
	out.src = src;
	if (device) {
		Utils::scopy(out.device, sizeof(out.device), device);
	}
	else {
		out.device[0] = (char)0;
	}

	out.supported = true;
	out.unsupportedReason[0] = (char)0;

	if (! target.isV4()) {
		out.supported = false;
		Utils::scopy(out.unsupportedReason, sizeof(out.unsupportedReason), "unsupported_non_ipv4_target");
		return false;
	}

	const unsigned int prefix = target.netmaskBits();
	out.prefixBits = (prefix > 32) ? 32 : (uint8_t)prefix;
	const uint32_t maskHost = _maskForPrefix(out.prefixBits);
	uint32_t targetNbo = 0;
	memcpy(&targetNbo, target.rawIpData(), sizeof(targetNbo));
	const uint32_t targetHost = Utils::ntoh(targetNbo);
	out.targetNbo = Utils::hton(targetHost & maskHost);
	out.maskNbo = Utils::hton(maskHost);

	if (target.isDefaultRoute() || out.prefixBits == 0) {
		out.supported = false;
		Utils::scopy(out.unsupportedReason, sizeof(out.unsupportedReason), "unsupported_default_route");
	}

	if (via.isV4()) {
		memcpy(&out.viaNbo, via.rawIpData(), sizeof(out.viaNbo));
	}
	else {
		out.viaNbo = 0;
	}
	if (src.isV4()) {
		memcpy(&out.srcNbo, src.rawIpData(), sizeof(out.srcNbo));
	}
	else {
		out.srcNbo = 0;
	}

	out.active = true;
	return true;
}

int ESP32NetLink::_findRouteLocked(const RouteEntry& normalized) const
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		const RouteEntry& r = _routes[i];
		if (! r.active) {
			continue;
		}
		if ((r.targetNbo == normalized.targetNbo) && (r.maskNbo == normalized.maskNbo) && (r.viaNbo == normalized.viaNbo) && (r.srcNbo == normalized.srcNbo) && (strcmp(r.device, normalized.device) == 0)) {
			return (int)i;
		}
	}
	return -1;
}

int ESP32NetLink::_findFreeSlotLocked() const
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (! _routes[i].active) {
			return (int)i;
		}
	}
	return -1;
}

bool ESP32NetLink::routeIsSet(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device)
{
	RouteEntry n;
	_normalizeRoute(target, via, src, device, n);
	ZT_ESP32_NETLINK_LOCK();
	const bool found = (_findRouteLocked(n) >= 0);
	ZT_ESP32_NETLINK_UNLOCK();
	return found;
}

void ESP32NetLink::addRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device)
{
	RouteEntry n;
	_normalizeRoute(target, via, src, device, n);

	ZT_ESP32_NETLINK_LOCK();
	int idx = _findRouteLocked(n);
	if (idx < 0) {
		idx = _findFreeSlotLocked();
	}
	if (idx < 0) {
		ZT_ESP32_NETLINK_UNLOCK();
		char tbuf[64];
		fprintf(stderr, "[ZT_ESP32_NETLINK] addRoute failed: table_full target=%s\n", target.toString(tbuf));
		return;
	}
	n.generation = ++_generation;
	_routes[idx] = n;
	ZT_ESP32_NETLINK_UNLOCK();

	char tbuf[64];
	char vbuf[64];
	fprintf(stderr, "[ZT_ESP32_NETLINK] addRoute target=%s via=%s device=%s supported=%s reason=%s generation=%u\n", target.toString(tbuf), via ? via.toString(vbuf) : "none", n.device, n.supported ? "yes" : "no", n.unsupportedReason, (unsigned int)_generation);
}

void ESP32NetLink::delRoute(const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device)
{
	RouteEntry n;
	_normalizeRoute(target, via, src, device, n);
	ZT_ESP32_NETLINK_LOCK();
	const int idx = _findRouteLocked(n);
	uint32_t gen = _generation;
	if (idx >= 0) {
		_routes[idx].active = false;
		_routes[idx].generation = ++_generation;
		gen = _generation;
	}
	ZT_ESP32_NETLINK_UNLOCK();
	if (idx >= 0) {
		char tbuf[64];
		fprintf(stderr, "[ZT_ESP32_NETLINK] delRoute target=%s device=%s generation=%u\n", target.toString(tbuf), n.device, (unsigned int)gen);
	}
}

void ESP32NetLink::clear()
{
	ZT_ESP32_NETLINK_LOCK();
	memset(_routes, 0, sizeof(_routes));
	++_generation;
	ZT_ESP32_NETLINK_UNLOCK();
}

unsigned int ESP32NetLink::generation() const
{
	return (unsigned int)_generation;
}

unsigned int ESP32NetLink::copyRoutes(RouteEntry* out, unsigned int capacity) const
{
	if ((! out) || (capacity == 0)) {
		return 0;
	}
	unsigned int count = 0;
	ZT_ESP32_NETLINK_LOCK();
	for (unsigned int i = 0; (i < ZT_ESP32_MAX_MANAGED_ROUTES) && (count < capacity); ++i) {
		if (_routes[i].active) {
			out[count++] = _routes[i];
		}
	}
	ZT_ESP32_NETLINK_UNLOCK();
	return count;
}

#ifdef ESP_PLATFORM
void ESP32NetLink::setZeroTierNetif(struct netif* netifPtr, const char* deviceName)
{
	_ztNetif = netifPtr;
	if (deviceName) {
		Utils::scopy(_ztDeviceName, sizeof(_ztDeviceName), deviceName);
	}
}

struct netif* ESP32NetLink::zeroTierNetif() const
{
	return _ztNetif;
}

const char* ESP32NetLink::zeroTierDeviceName() const
{
	return _ztDeviceName;
}

ESP32NetLink::LookupResult ESP32NetLink::lookupIpv4(uint32_t destNbo) const
{
	LookupResult r;
	memset(&r, 0, sizeof(r));
	r.netifPtr = _ztNetif;

	const uint32_t destHost = Utils::ntoh(destNbo);
	int best = -1;
	uint8_t bestPrefix = 0;
	ZT_ESP32_NETLINK_LOCK();
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		const RouteEntry& e = _routes[i];
		if ((! e.active) || (! e.supported) || (e.prefixBits == 0)) {
			continue;
		}
		const uint32_t maskHost = Utils::ntoh(e.maskNbo);
		const uint32_t targetHost = Utils::ntoh(e.targetNbo);
		if ((destHost & maskHost) == targetHost) {
			if ((best < 0) || (e.prefixBits > bestPrefix)) {
				best = (int)i;
				bestPrefix = e.prefixBits;
			}
		}
	}

	if (best >= 0) {
		r.matched = true;
		r.supported = true;
		r.route = _routes[best];
		r.hasVia = (_routes[best].viaNbo != 0);
		if (r.hasVia) {
			r.gateway.addr = _routes[best].viaNbo;
		}
	}
	ZT_ESP32_NETLINK_UNLOCK();
	return r;
}

struct netif* ESP32NetLink::routeHookIpv4(const ip4_addr_t* src, const ip4_addr_t* dest) const
{
	(void)src;
	if ((! dest) || (! _ztNetif)) {
		return (struct netif*)0;
	}
	LookupResult r = lookupIpv4(dest->addr);
	return (r.matched && r.supported) ? _ztNetif : (struct netif*)0;
}

const ip4_addr_t* ESP32NetLink::etharpGetGatewayHook(struct netif* netifPtr, const ip4_addr_t* dest) const
{
	if ((! dest) || (!_ztNetif) || (netifPtr != _ztNetif)) {
		return (const ip4_addr_t*)0;
	}
	LookupResult r = lookupIpv4(dest->addr);
	if (r.matched && r.supported && r.hasVia) {
		/* lwIP expects a stable pointer for the duration of etharp_output(). Use a
		 * tiny rotating scratch ring instead of one global address so nested or
		 * diagnostic calls do not immediately overwrite the returned value. The
		 * hook itself still assumes lwIP core serialization. */
		ZT_ESP32_NETLINK_LOCK();
		_gatewayScratchIndex = (uint8_t)((_gatewayScratchIndex + 1U) & 3U);
		_gatewayScratch[_gatewayScratchIndex].addr = r.route.viaNbo;
		const ip4_addr_t* gw = &_gatewayScratch[_gatewayScratchIndex];
		ZT_ESP32_NETLINK_UNLOCK();
		return gw;
	}
	return (const ip4_addr_t*)0;
}
#endif

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" struct netif* zt_esp32_lwip_ip4_route_src(const ip4_addr_t* src, const ip4_addr_t* dest)
{
	return ZeroTier::ESP32NetLink::getInstance().routeHookIpv4(src, dest);
}

extern "C" struct netif* zt_esp32_lwip_ip4_route(const ip4_addr_t* dest)
{
	return ZeroTier::ESP32NetLink::getInstance().routeHookIpv4((const ip4_addr_t*)0, dest);
}

extern "C" const ip4_addr_t* zt_esp32_lwip_etharp_get_gw(struct netif* netifPtr, const ip4_addr_t* dest)
{
	return ZeroTier::ESP32NetLink::getInstance().etharpGetGatewayHook(netifPtr, dest);
}
#endif
