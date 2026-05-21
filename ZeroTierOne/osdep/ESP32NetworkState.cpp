/* Minimal ESP32 network state/route service. */

#include "ESP32NetworkState.hpp"

#include "../node/MAC.hpp"
#include "../node/MulticastGroup.hpp"
#include "../node/Utils.hpp"

#include <stdio.h>
#include <string.h>
#include <vector>

namespace ZeroTier {

ESP32NetworkState::ESP32NetworkState(
	const char* homePath,
	void (*tapFrameHandler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
	void* tapFrameArg)
	: _homePath(homePath ? homePath : ""), _tapFrameHandler(tapFrameHandler), _tapFrameArg(tapFrameArg), _nwid(0)
{
	memset(_managedIpActive, 0, sizeof(_managedIpActive));
	memset(_routes, 0, sizeof(_routes));
}

ESP32NetworkState::~ESP32NetworkState()
{
	_clearRoutes();
	_tap.reset();
}

std::shared_ptr<EthernetTap> ESP32NetworkState::tap() const
{
	return _tap;
}

uint64_t ESP32NetworkState::nwid() const
{
	return _nwid;
}

unsigned int ESP32NetworkState::managedIpCount() const
{
	unsigned int n = 0;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i]) { ++n; }
	}
	return n;
}

unsigned int ESP32NetworkState::routeObjectCount() const
{
	unsigned int n = 0;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (_routes[i].active && _routes[i].route) { ++n; }
	}
	return n;
}

bool ESP32NetworkState::managedIpv4Contains(uint32_t destNbo) const
{
	InetAddress dest;
	dest.set((const void*)&destNbo, 4, 0);
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i] && _managedIps[i].isV4() && _managedIps[i].containsAddress(dest)) {
			return true;
		}
	}
	return false;
}

int ESP32NetworkState::applyConfigFromOwnerTask(ZT_Node* node, void* tptr, enum ZT_VirtualNetworkConfigOperation op, const ZT_VirtualNetworkConfig* config)
{
	if ((! config) || ((op == ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_DOWN) || (op == ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_DESTROY))) {
		if (_tap) {
			_tap->setEnabled(false);
		}
		_clearRoutes();
		_clearIps();
		return 0;
	}

	_nwid = config->nwid;
	if (! _ensureTap(*config)) {
		return -1;
	}
	_syncIps(*config);
	_syncMulticast(node, tptr, *config);
	_syncRoutes(*config);
	_tap->setMtu(config->mtu ? config->mtu : 1280);
	_tap->setEnabled(true);
	return 0;
}

bool ESP32NetworkState::_ensureTap(const ZT_VirtualNetworkConfig& config)
{
	if (_tap) {
		return true;
	}
	MAC mac(config.mac);
	_tap = EthernetTap::newInstance(
		(const char*)0,
		1,
		false,
		_homePath.c_str(),
		mac,
		config.mtu ? config.mtu : 1280,
		0,
		config.nwid,
		config.name,
		_tapFrameHandler,
		_tapFrameArg);
	if (! _tap) {
		fprintf(stderr, "[ZT_ESP32_NETSTATE] failed to create EthernetTap nwid=%llx\n", (unsigned long long)config.nwid);
		return false;
	}
	fprintf(stderr, "[ZT_ESP32_NETSTATE] tap created nwid=%llx device=%s\n", (unsigned long long)config.nwid, _tap->deviceName().c_str());
	return true;
}

void ESP32NetworkState::_clearRoutes()
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		_destroyRoute(_routes[i]);
		memset(&_routes[i], 0, sizeof(_routes[i]));
	}
}

void ESP32NetworkState::_clearIps()
{
	if (_tap) {
		for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
			if (_managedIpActive[i]) {
				_tap->removeIp(_managedIps[i]);
			}
		}
	}
	memset(_managedIpActive, 0, sizeof(_managedIpActive));
}

bool ESP32NetworkState::_managedIpExists(const InetAddress& ip) const
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i] && _managedIps[i].ipsEqual(ip)) {
			return true;
		}
	}
	return false;
}

void ESP32NetworkState::_syncIps(const ZT_VirtualNetworkConfig& config)
{
	bool stillPresent[ZT_ESP32_MAX_MANAGED_IPS];
	memset(stillPresent, 0, sizeof(stillPresent));

	for (unsigned int c = 0; c < config.assignedAddressCount; ++c) {
		InetAddress ip(config.assignedAddresses[c]);
		if (! ip.isV4()) {
			continue;
		}
		bool found = false;
		for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
			if (_managedIpActive[i] && _managedIps[i].ipsEqual(ip)) {
				stillPresent[i] = true;
				found = true;
				break;
			}
		}
		if (found) {
			continue;
		}
		unsigned int slot = ZT_ESP32_MAX_MANAGED_IPS;
		for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
			if (! _managedIpActive[i]) { slot = i; break; }
		}
		if (slot == ZT_ESP32_MAX_MANAGED_IPS) {
			char buf[64];
			fprintf(stderr, "[ZT_ESP32_NETSTATE] managed IP drop reason=table_full ip=%s\n", ip.toString(buf));
			continue;
		}
		_managedIps[slot] = ip;
		_managedIpActive[slot] = true;
		stillPresent[slot] = true;
		_tap->addIp(ip);
		char buf[64];
		fprintf(stderr, "[ZT_ESP32_NETSTATE] managed IP add ip=%s\n", ip.toString(buf));
	}

	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i] && (! stillPresent[i])) {
			char buf[64];
			fprintf(stderr, "[ZT_ESP32_NETSTATE] managed IP remove ip=%s\n", _managedIps[i].toString(buf));
			_tap->removeIp(_managedIps[i]);
			_managedIpActive[i] = false;
		}
	}
}

void ESP32NetworkState::_syncMulticast(ZT_Node* node, void* tptr, const ZT_VirtualNetworkConfig& config)
{
	if ((! node) || (! _tap)) {
		return;
	}
	std::vector<MulticastGroup> added;
	std::vector<MulticastGroup> removed;
	_tap->scanMulticastGroups(added, removed);
	for (std::vector<MulticastGroup>::const_iterator r(removed.begin()); r != removed.end(); ++r) {
		ZT_Node_multicastUnsubscribe(node, config.nwid, r->mac().toInt(), r->adi());
		fprintf(stderr, "[ZT_ESP32_NETSTATE] multicast unsubscribe nwid=%llx mac=%llx adi=%lu\n", (unsigned long long)config.nwid, (unsigned long long)r->mac().toInt(), (unsigned long)r->adi());
	}
	for (std::vector<MulticastGroup>::const_iterator a(added.begin()); a != added.end(); ++a) {
		ZT_Node_multicastSubscribe(node, tptr, config.nwid, a->mac().toInt(), a->adi());
		fprintf(stderr, "[ZT_ESP32_NETSTATE] multicast subscribe nwid=%llx mac=%llx adi=%lu\n", (unsigned long long)config.nwid, (unsigned long long)a->mac().toInt(), (unsigned long)a->adi());
	}
}

bool ESP32NetworkState::_routeAllowed(const InetAddress& target) const
{
	if (! target.isV4()) {
		return false;
	}
	if (target.isDefaultRoute()) {
		return false;
	}
	if (! target.netmaskBitsValid()) {
		return false;
	}
	return true;
}

bool ESP32NetworkState::_myIpExists(const InetAddress* ips, unsigned int count, const InetAddress& ip) const
{
	for (unsigned int i = 0; i < count; ++i) {
		if (ips[i].ipsEqual(ip)) {
			return true;
		}
	}
	return false;
}

bool ESP32NetworkState::_isImpliedByManagedIp(const InetAddress& target) const
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i] && (target.netmaskBits() == _managedIps[i].netmaskBits()) && target.containsAddress(_managedIps[i])) {
			return true;
		}
	}
	return false;
}

const InetAddress* ESP32NetworkState::_chooseSource(const InetAddress* ips, unsigned int count, const InetAddress& target) const
{
	const InetAddress* src = (const InetAddress*)0;
	unsigned int mostMatchingPrefixBits = 0;
	for (unsigned int i = 0; i < count; ++i) {
		const unsigned int matchingPrefixBits = ips[i].matchingPrefixBits(target);
		if ((matchingPrefixBits >= mostMatchingPrefixBits) && ((target.isV4() && ips[i].isV4()) || (target.isV6() && ips[i].isV6()))) {
			mostMatchingPrefixBits = matchingPrefixBits;
			src = &ips[i];
		}
	}
	return src;
}

ESP32NetworkState::RouteSlot* ESP32NetworkState::_findRouteSlot(const InetAddress& target)
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (_routes[i].active && _routes[i].target == target) {
			return &_routes[i];
		}
	}
	return (RouteSlot*)0;
}

ESP32NetworkState::RouteSlot* ESP32NetworkState::_findFreeRouteSlot()
{
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (! _routes[i].active) {
			return &_routes[i];
		}
	}
	return (RouteSlot*)0;
}

ManagedRoute* ESP32NetworkState::_constructRoute(RouteSlot& slot, const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device)
{
	return new (slot.routeStorage) ManagedRoute(target, via, src, device);
}

void ESP32NetworkState::_destroyRoute(RouteSlot& slot)
{
	if (slot.route) {
		slot.route->remove();
		slot.route->~ManagedRoute();
		slot.route = (ManagedRoute*)0;
	}
}

void ESP32NetworkState::_syncRoutes(const ZT_VirtualNetworkConfig& config)
{
	if (! _tap) {
		return;
	}
	const std::string tapdev(_tap->deviceName());
	InetAddress myIps[ZT_ESP32_MAX_MANAGED_IPS + ZT_ESP32_MAX_ROUTE_CANDIDATES];
	unsigned int myIpCount = 0;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_IPS; ++i) {
		if (_managedIpActive[i] && (myIpCount < (sizeof(myIps) / sizeof(myIps[0])))) {
			myIps[myIpCount++] = _managedIps[i];
		}
	}

	InetAddress desiredTargets[ZT_ESP32_MAX_MANAGED_ROUTES];
	bool desiredActive[ZT_ESP32_MAX_MANAGED_ROUTES];
	memset(desiredActive, 0, sizeof(desiredActive));
	unsigned int desiredCount = 0;

	for (unsigned int i = 0; i < config.routeCount; ++i) {
		const InetAddress target(config.routes[i].target);
		const InetAddress via(config.routes[i].via);

		if (! _routeAllowed(target)) {
			continue;
		}
		if ((via.ss_family == target.ss_family) && _myIpExists(myIps, myIpCount, via)) {
			continue;
		}
		const InetAddress* src = _chooseSource(myIps, myIpCount, target);
		if (! src) {
			continue;
		}
		/* Linux skips connected routes implied by managed IPs. On ESP32 lwIP's
		 * netif address/netmask provides the same connected route, so keep parity. */
		if (_isImpliedByManagedIp(target)) {
			continue;
		}
		if (desiredCount >= ZT_ESP32_MAX_MANAGED_ROUTES) {
			char buf[64];
			fprintf(stderr, "[ZT_ESP32_NETSTATE] route drop reason=desired_table_full target=%s\n", target.toString(buf));
			continue;
		}

		desiredTargets[desiredCount] = target;
		desiredActive[desiredCount] = true;
		++desiredCount;

		RouteSlot* slot = _findRouteSlot(target);
		if (slot && slot->route && ((slot->via != via) || (slot->src != *src))) {
			_destroyRoute(*slot);
			slot->active = false;
			slot = (RouteSlot*)0;
		}
		if (! slot) {
			slot = _findFreeRouteSlot();
		}
		if (! slot) {
			char buf[64];
			fprintf(stderr, "[ZT_ESP32_NETSTATE] route drop reason=route_slots_full target=%s\n", target.toString(buf));
			continue;
		}
		if (! slot->route) {
			slot->target = target;
			slot->via = via;
			slot->src = *src;
			slot->route = _constructRoute(*slot, target, via, *src, tapdev.c_str());
			slot->active = (slot->route != 0);
		}
	}

	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (! _routes[i].active) {
			continue;
		}
		bool keep = false;
		for (unsigned int j = 0; j < desiredCount; ++j) {
			if (desiredActive[j] && (_routes[i].target == desiredTargets[j])) {
				keep = true;
				break;
			}
		}
		if (! keep) {
			_destroyRoute(_routes[i]);
			memset(&_routes[i], 0, sizeof(_routes[i]));
		}
	}

	/* Keep Linux parity: sync direct/no-gateway routes before via routes. */
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (_routes[i].active && _routes[i].route && (! _routes[i].route->via())) {
			_routes[i].route->sync();
		}
	}
	for (unsigned int i = 0; i < ZT_ESP32_MAX_MANAGED_ROUTES; ++i) {
		if (_routes[i].active && _routes[i].route && _routes[i].route->via()) {
			_routes[i].route->sync();
		}
	}
}

} // namespace ZeroTier
