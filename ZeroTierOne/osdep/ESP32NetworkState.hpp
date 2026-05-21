/* Minimal ESP32 network state/route service.
 *
 * This is the ESP32-Lite counterpart to the route/IP parts of
 * OneService::syncManagedStuff(). It must be called from the ZT owner task,
 * not directly from ZeroTier config callbacks.
 */

#ifndef ZT_ESP32NETWORKSTATE_HPP
#define ZT_ESP32NETWORKSTATE_HPP

#include "EthernetTap.hpp"
#include "ManagedRoute.hpp"
#include "../include/ZeroTierOne.h"
#include "../node/InetAddress.hpp"
#include "../node/MulticastGroup.hpp"
#include "../node/SharedPtr.hpp"

#include <memory>
#include <string>
#include <new>

#ifndef ZT_ESP32_MAX_MANAGED_ROUTES
#define ZT_ESP32_MAX_MANAGED_ROUTES 8
#endif
#ifndef ZT_ESP32_MAX_MANAGED_IPS
#define ZT_ESP32_MAX_MANAGED_IPS ZT_MAX_ZT_ASSIGNED_ADDRESSES
#endif
#ifndef ZT_ESP32_MAX_ROUTE_CANDIDATES
#define ZT_ESP32_MAX_ROUTE_CANDIDATES ZT_ESP32_MAX_MANAGED_ROUTES
#endif

namespace ZeroTier {

class ESP32NetworkState {
  public:
	ESP32NetworkState(
		const char* homePath,
		void (*tapFrameHandler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
		void* tapFrameArg);
	~ESP32NetworkState();

	int applyConfigFromOwnerTask(ZT_Node* node, void* tptr, enum ZT_VirtualNetworkConfigOperation op, const ZT_VirtualNetworkConfig* config);
	std::shared_ptr<EthernetTap> tap() const;
	uint64_t nwid() const;
	unsigned int managedIpCount() const;
	unsigned int routeObjectCount() const;
	bool managedIpv4Contains(uint32_t destNbo) const;

  private:
	struct RouteSlot {
		bool active;
		InetAddress target;
		InetAddress via;
		InetAddress src;
		ManagedRoute* route;
		alignas(ManagedRoute) unsigned char routeStorage[sizeof(ManagedRoute)];
	};

	ESP32NetworkState(const ESP32NetworkState&);
	ESP32NetworkState& operator=(const ESP32NetworkState&);

	bool _ensureTap(const ZT_VirtualNetworkConfig& config);
	void _clearRoutes();
	void _clearIps();
	void _syncIps(const ZT_VirtualNetworkConfig& config);
	void _syncMulticast(ZT_Node* node, void* tptr, const ZT_VirtualNetworkConfig& config);
	void _syncRoutes(const ZT_VirtualNetworkConfig& config);
	bool _routeAllowed(const InetAddress& target) const;
	bool _managedIpExists(const InetAddress& ip) const;
	bool _myIpExists(const InetAddress* ips, unsigned int count, const InetAddress& ip) const;
	bool _isImpliedByManagedIp(const InetAddress& target) const;
	const InetAddress* _chooseSource(const InetAddress* ips, unsigned int count, const InetAddress& target) const;
	RouteSlot* _findRouteSlot(const InetAddress& target);
	RouteSlot* _findFreeRouteSlot();
	ManagedRoute* _constructRoute(RouteSlot& slot, const InetAddress& target, const InetAddress& via, const InetAddress& src, const char* device);
	void _destroyRoute(RouteSlot& slot);

	std::string _homePath;
	std::shared_ptr<EthernetTap> _tap;
	InetAddress _managedIps[ZT_ESP32_MAX_MANAGED_IPS];
	bool _managedIpActive[ZT_ESP32_MAX_MANAGED_IPS];
	RouteSlot _routes[ZT_ESP32_MAX_MANAGED_ROUTES];
	void (*_tapFrameHandler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int);
	void* _tapFrameArg;
	uint64_t _nwid;
};

} // namespace ZeroTier

#endif
