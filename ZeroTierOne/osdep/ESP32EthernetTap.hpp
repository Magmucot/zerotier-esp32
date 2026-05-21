/* ESP32/lwIP EthernetTap backend for ZeroTier Lite. */

#ifndef ZT_ESP32ETHERNETTAP_HPP
#define ZT_ESP32ETHERNETTAP_HPP

#include "EthernetTap.hpp"
#include "ESP32NetLink.hpp"
#include "../node/Mutex.hpp"

#include <vector>

#ifndef ZT_ESP32_MAX_TAP_IPS
#define ZT_ESP32_MAX_TAP_IPS ZT_MAX_ZT_ASSIGNED_ADDRESSES
#endif
#ifndef ZT_ESP32_MAX_MULTICAST_GROUPS
#define ZT_ESP32_MAX_MULTICAST_GROUPS (ZT_ESP32_MAX_TAP_IPS + 2)
#endif

#ifdef ESP_PLATFORM
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#endif

namespace ZeroTier {

class ESP32EthernetTap : public EthernetTap {
  public:
	ESP32EthernetTap(
		const char* homePath,
		const MAC& mac,
		unsigned int mtu,
		unsigned int metric,
		uint64_t nwid,
		const char* friendlyName,
		void (*handler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
		void* arg);
	~ESP32EthernetTap();

	void setEnabled(bool en);
	bool enabled() const;
	bool addIp(const InetAddress& ip);
	bool removeIp(const InetAddress& ip);
	std::vector<InetAddress> ips() const;
	void put(const MAC& from, const MAC& to, unsigned int etherType, const void* data, unsigned int len);
	std::string deviceName() const;
	void setFriendlyName(const char* friendlyName);
	std::string friendlyName() const;
	void scanMulticastGroups(std::vector<MulticastGroup>& added, std::vector<MulticastGroup>& removed);
	void setMtu(unsigned int mtu);
	void setDns(const char* domain, const std::vector<InetAddress>& servers);

#ifdef ESP_PLATFORM
	struct netif* netifPtr();
#endif

  private:
	ESP32EthernetTap(const ESP32EthernetTap&);
	ESP32EthernetTap& operator=(const ESP32EthernetTap&);

#ifdef ESP_PLATFORM
	static err_t _netifInit(struct netif* netifPtr);
	static err_t _linkOutput(struct netif* netifPtr, struct pbuf* pbuf);
	bool _applyPrimaryIpv4Locked();
#endif

	mutable Mutex _lock;
	bool _enabled;
	MAC _mac;
	unsigned int _mtu;
	uint64_t _nwid;
	char _deviceName[16];
	std::string _friendlyName;
	InetAddress _ips[ZT_ESP32_MAX_TAP_IPS];
	bool _ipActive[ZT_ESP32_MAX_TAP_IPS];
	MulticastGroup _multicastGroups[ZT_ESP32_MAX_MULTICAST_GROUPS];
	bool _multicastActive[ZT_ESP32_MAX_MULTICAST_GROUPS];
	void (*_handler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int);
	void* _arg;
#ifdef ESP_PLATFORM
	struct netif _netif;
#endif
};

} // namespace ZeroTier

#ifdef ESP_PLATFORM
/*
 * Ownership boundary: lwIP linkoutput must not call ZeroTier directly.
 * The platform app/port layer should override this weak function and enqueue
 * the frame to the ZT owner task, which will call ZT_Node_processVirtualNetworkFrame.
 */
extern "C" int zt_esp32_enqueue_virtual_frame_from_lwip(uint64_t nwid, uint64_t sourceMac, uint64_t destMac, unsigned int etherType, const void* frameData, unsigned int frameLength) __attribute__((weak));
#endif

#endif
