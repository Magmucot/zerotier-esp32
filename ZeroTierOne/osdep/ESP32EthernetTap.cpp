/* ESP32/lwIP EthernetTap backend for ZeroTier Lite. */

#include "ESP32EthernetTap.hpp"
#include "ESP32LwIPGlue.hpp"
#include "ESP32NodeTask.hpp"
#include "ESP32FramePool.hpp"
#include "ESP32EgressTrace.hpp"

#include "../node/MulticastGroup.hpp"
#include "../node/Utils.hpp"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/ip4_addr.h"
#endif

namespace ZeroTier {

ESP32EthernetTap::ESP32EthernetTap(
	const char* homePath,
	const MAC& mac,
	unsigned int mtu,
	unsigned int metric,
	uint64_t nwid,
	const char* friendlyName,
	void (*handler)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
	void* arg)
	: _enabled(false), _mac(mac), _mtu(mtu), _nwid(nwid), _friendlyName(friendlyName ? friendlyName : "zt-esp32"), _handler(handler), _arg(arg)
{
	(void)homePath;
	(void)metric;
	Utils::scopy(_deviceName, sizeof(_deviceName), "ztesp32");
	memset(_ipActive, 0, sizeof(_ipActive));
	memset(_multicastActive, 0, sizeof(_multicastActive));
#ifdef ESP_PLATFORM
	memset(&_netif, 0, sizeof(_netif));
	if (ESP32LwIPGlue::addNetif(&_netif, this, &ESP32EthernetTap::_netifInit)) {
		ESP32NetLink::getInstance().setZeroTierNetif(&_netif, _deviceName);
		fprintf(stderr, "[ZT_ESP32_TAP] created device=%s mtu=%u nwid=%llx\n", _deviceName, (unsigned int)_mtu, (unsigned long long)_nwid);
	}
	else {
		fprintf(stderr, "[ZT_ESP32_TAP] netif_add failed device=%s\n", _deviceName);
	}
#else
	fprintf(stderr, "[ZT_ESP32_TAP] constructed without ESP_PLATFORM; this backend is inert\n");
#endif
}

ESP32EthernetTap::~ESP32EthernetTap()
{
#ifdef ESP_PLATFORM
	ESP32LwIPGlue::setUp(&_netif, false);
	ESP32LwIPGlue::removeNetif(&_netif);
#endif
	ESP32NetLink::getInstance().setZeroTierNetif(nullptr, nullptr);
}

void ESP32EthernetTap::setEnabled(bool en)
{
	Mutex::Lock l(_lock);
	_enabled = en;
#ifdef ESP_PLATFORM
	if (en) {
		ESP32LwIPGlue::setLinkUp(&_netif, true);
		_applyPrimaryIpv4Locked();
	}
	else {
		ESP32LwIPGlue::setUp(&_netif, false);
		ESP32LwIPGlue::setLinkUp(&_netif, false);
	}
#endif
}

bool ESP32EthernetTap::enabled() const
{
	Mutex::Lock l(_lock);
	return _enabled;
}

bool ESP32EthernetTap::addIp(const InetAddress& ip)
{
	Mutex::Lock l(_lock);
	unsigned int freeSlot = ZT_ESP32_MAX_TAP_IPS;
	unsigned int count = 0;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
		if (_ipActive[i]) {
			++count;
			if (_ips[i].ipsEqual(ip)) {
				if (_ips[i].netmaskBits() != ip.netmaskBits()) {
					_ips[i] = ip;
#ifdef ESP_PLATFORM
					if (ip.isV4()) {
						_applyPrimaryIpv4Locked();
					}
#endif
				}
				return true;
			}
		}
		else if (freeSlot == ZT_ESP32_MAX_TAP_IPS) {
			freeSlot = i;
		}
	}
	if (freeSlot == ZT_ESP32_MAX_TAP_IPS) {
		char buf[64];
		fprintf(stderr, "[ZT_ESP32_TAP] addIp drop reason=table_full ip=%s\n", ip.toString(buf));
		return false;
	}
	_ips[freeSlot] = ip;
	_ipActive[freeSlot] = true;
	++count;
#ifdef ESP_PLATFORM
	if (ip.isV4()) {
		_applyPrimaryIpv4Locked();
	}
#endif
	char buf[64];
	fprintf(stderr, "[ZT_ESP32_TAP] addIp ip=%s count=%u\n", ip.toString(buf), count);
	return true;
}

bool ESP32EthernetTap::removeIp(const InetAddress& ip)
{
	Mutex::Lock l(_lock);
	unsigned int count = 0;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
		if (_ipActive[i]) {
			++count;
		}
	}
	for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
		if (_ipActive[i] && _ips[i].ipsEqual(ip)) {
			_ipActive[i] = false;
			if (count > 0) { --count; }
#ifdef ESP_PLATFORM
			_applyPrimaryIpv4Locked();
#endif
			char buf[64];
			fprintf(stderr, "[ZT_ESP32_TAP] removeIp ip=%s count=%u\n", ip.toString(buf), count);
			return true;
		}
	}
	return false;
}

std::vector<InetAddress> ESP32EthernetTap::ips() const
{
	Mutex::Lock l(_lock);
	std::vector<InetAddress> out;
	out.reserve(ZT_ESP32_MAX_TAP_IPS);
	for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
		if (_ipActive[i]) {
			out.push_back(_ips[i]);
		}
	}
	return out;
}

void ESP32EthernetTap::put(const MAC& from, const MAC& to, unsigned int etherType, const void* data, unsigned int len)
{
#ifdef ESP_PLATFORM
	if (len > _mtu) {
		fprintf(stderr, "[ZT_ESP32_TAP] put drop reason=frame_too_large len=%u mtu=%u\n", len, _mtu);
		return;
	}
	if ((!to.isBroadcast()) && (!to.isMulticast()) && (to != _mac)) {
		fprintf(stderr, "[ZT_ESP32_TAP] put drop reason=dest_mac_mismatch\n");
		return;
	}
	const unsigned int frameLen = 14 + len;
	struct pbuf* p = pbuf_alloc(PBUF_RAW, frameLen, PBUF_RAM);
	if (! p) {
		fprintf(stderr, "[ZT_ESP32_TAP] put drop reason=pbuf_alloc_failed len=%u\n", frameLen);
		return;
	}
	unsigned char* frame = (unsigned char*)p->payload;
	to.copyTo(frame, 6);
	from.copyTo(frame + 6, 6);
	frame[12] = (unsigned char)((etherType >> 8) & 0xff);
	frame[13] = (unsigned char)(etherType & 0xff);
	if ((data) && (len > 0)) {
		memcpy(frame + 14, data, len);
	}
	if (ESP32LwIPGlue::input(&_netif, p) != ERR_OK) {
		pbuf_free(p);
		fprintf(stderr, "[ZT_ESP32_TAP] put drop reason=tcpip_input_failed len=%u\n", frameLen);
	}
#else
	(void)from; (void)to; (void)etherType; (void)data; (void)len;
#endif
}

std::string ESP32EthernetTap::deviceName() const
{
	return std::string(_deviceName);
}

void ESP32EthernetTap::setFriendlyName(const char* friendlyName)
{
	_friendlyName = friendlyName ? friendlyName : "zt-esp32";
}

std::string ESP32EthernetTap::friendlyName() const
{
	return _friendlyName;
}

void ESP32EthernetTap::scanMulticastGroups(std::vector<MulticastGroup>& added, std::vector<MulticastGroup>& removed)
{
	MulticastGroup next[ZT_ESP32_MAX_MULTICAST_GROUPS];
	bool nextActive[ZT_ESP32_MAX_MULTICAST_GROUPS];
	memset(nextActive, 0, sizeof(nextActive));
	unsigned int nextCount = 0;
	{
		Mutex::Lock l(_lock);
		for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
			if (! _ipActive[i]) {
				continue;
			}
			MulticastGroup mg = MulticastGroup::deriveMulticastGroupForAddressResolution(_ips[i]);
			if (mg.mac() && (nextCount < ZT_ESP32_MAX_MULTICAST_GROUPS)) {
				next[nextCount] = mg;
				nextActive[nextCount] = true;
				++nextCount;
			}
		}
		for (unsigned int n = 0; n < nextCount; ++n) {
			bool have = false;
			for (unsigned int o = 0; o < ZT_ESP32_MAX_MULTICAST_GROUPS; ++o) {
				if (_multicastActive[o] && (next[n] == _multicastGroups[o])) {
					have = true;
					break;
				}
			}
			if (! have) {
				added.push_back(next[n]);
			}
		}
		for (unsigned int o = 0; o < ZT_ESP32_MAX_MULTICAST_GROUPS; ++o) {
			if (! _multicastActive[o]) {
				continue;
			}
			bool still = false;
			for (unsigned int n = 0; n < nextCount; ++n) {
				if (_multicastGroups[o] == next[n]) {
					still = true;
					break;
				}
			}
			if (! still) {
				removed.push_back(_multicastGroups[o]);
			}
		}
		memset(_multicastActive, 0, sizeof(_multicastActive));
		for (unsigned int i = 0; i < nextCount; ++i) {
			_multicastGroups[i] = next[i];
			_multicastActive[i] = true;
		}
	}
	fprintf(stderr, "[ZT_ESP32_TAP] scanMulticastGroups added=%u removed=%u total=%u\n", (unsigned int)added.size(), (unsigned int)removed.size(), nextCount);
}

void ESP32EthernetTap::setMtu(unsigned int mtu)
{
	Mutex::Lock l(_lock);
	_mtu = mtu;
#ifdef ESP_PLATFORM
	_netif.mtu = (u16_t)mtu;
#endif
}

void ESP32EthernetTap::setDns(const char* domain, const std::vector<InetAddress>& servers)
{
	(void)domain;
	(void)servers;
	fprintf(stderr, "[ZT_ESP32_TAP] setDns ignored in ESP32-Lite v1\n");
}

#ifdef ESP_PLATFORM
struct netif* ESP32EthernetTap::netifPtr()
{
	return &_netif;
}

err_t ESP32EthernetTap::_netifInit(struct netif* netifPtr)
{
	ESP32EthernetTap* tap = (ESP32EthernetTap*)netifPtr->state;
	netifPtr->name[0] = 'z';
	netifPtr->name[1] = 't';
	netifPtr->output = etharp_output;
	netifPtr->linkoutput = &ESP32EthernetTap::_linkOutput;
	netifPtr->mtu = (u16_t)(tap ? tap->_mtu : 1280);
	netifPtr->hwaddr_len = 6;
	if (tap) {
		tap->_mac.copyTo(netifPtr->hwaddr, 6);
	}
	netifPtr->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
	return ERR_OK;
}

err_t ESP32EthernetTap::_linkOutput(struct netif* netifPtr, struct pbuf* pbuf)
{
	if ((! netifPtr) || (! pbuf) || (pbuf->tot_len < 14)) {
		return ERR_IF;
	}
	ESP32EthernetTap* tap = (ESP32EthernetTap*)netifPtr->state;
	if (! tap) {
		return ERR_IF;
	}
	unsigned char hdr[14];
	if (pbuf_copy_partial(pbuf, hdr, sizeof(hdr), 0) != sizeof(hdr)) {
		return ERR_IF;
	}
	MAC dest(hdr, 6);
	MAC src(hdr + 6, 6);
	const unsigned int etherType = (((unsigned int)hdr[12]) << 8) | hdr[13];
	const unsigned int payloadLen = pbuf->tot_len - 14;

	if (payloadLen > ZT_ESP32_MAX_VIRTUAL_FRAME_SIZE) {
		fprintf(stderr, "[ZT_ESP32_TAP] linkOutput drop reason=frame_too_large payloadLen=%u\n", payloadLen);
		return ERR_IF;
	}
	ESP32FramePool::Buffer* b = ESP32FramePool::instance().acquireEmpty(payloadLen, 0);
	if (! b) {
		fprintf(stderr, "[ZT_ESP32_TAP] linkOutput drop reason=frame_pool_empty payloadLen=%u\n", payloadLen);
		return ERR_MEM;
	}
	if (payloadLen > 0) {
		pbuf_copy_partial(pbuf, b->data, payloadLen, 14);
	}
	ESP32EgressTrace::traceFrame(tap->_nwid, src, dest, etherType, b->data, payloadLen);
	ESP32NodeTask* task = ESP32NodeTask::defaultTask();
	if (! task || (! task->enqueueVirtualFrameBuffer(tap->_nwid, src.toInt(), dest.toInt(), etherType, b, 0))) {
		ESP32FramePool::instance().release(b);
		return ERR_IF;
	}
	return ERR_OK;
}

bool ESP32EthernetTap::_applyPrimaryIpv4Locked()
{
	InetAddress primary;
	for (unsigned int i = 0; i < ZT_ESP32_MAX_TAP_IPS; ++i) {
		if (_ipActive[i] && _ips[i].isV4()) {
			primary = _ips[i];
			break;
		}
	}
	if (! primary.isV4()) {
		ESP32LwIPGlue::setUp(&_netif, false);
		return true;
	}
	ip4_addr_t ipaddr;
	ip4_addr_t netmask;
	ip4_addr_t gw;
	memcpy(&ipaddr.addr, primary.rawIpData(), sizeof(ipaddr.addr));
	InetAddress nm = primary.netmask();
	memcpy(&netmask.addr, nm.rawIpData(), sizeof(netmask.addr));
	IP4_ADDR(&gw, 0, 0, 0, 0);
	ESP32LwIPGlue::setIpv4(&_netif, &ipaddr, &netmask, &gw);
	if (_enabled) {
		ESP32LwIPGlue::setUp(&_netif, true);
		ESP32LwIPGlue::setLinkUp(&_netif, true);
	}
	return true;
}
#endif

} // namespace ZeroTier
