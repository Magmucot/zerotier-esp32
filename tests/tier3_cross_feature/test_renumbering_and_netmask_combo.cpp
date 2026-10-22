#include "ESP32NetworkState.hpp"
#include "ESP32EthernetTap.hpp"
#include "lwip/netif.h"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>

using namespace ZeroTier;

// --- Mock Fixtures ---
namespace ZeroTier {
EthernetTap::EthernetTap() {}
EthernetTap::~EthernetTap() {}
bool EthernetTap::addIps(std::vector<InetAddress> v) {
    for (auto& i : v) {
        if (!addIp(i)) return false;
    }
    return true;
}
std::string EthernetTap::friendlyName() const { return "test_tap"; }

std::shared_ptr<EthernetTap> EthernetTap::newInstance(
    const char*, unsigned int, bool, const char* h, const MAC& m,
    unsigned int mtu, unsigned int metric, uint64_t nwid, const char* name,
    void(*fn)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
    void* a)
{
    return std::shared_ptr<EthernetTap>(new ESP32EthernetTap(h, m, mtu, metric, nwid, name, fn, a));
}

ManagedRoute::ManagedRoute(const InetAddress& t, const InetAddress& v, const InetAddress& s, const char*) {
    _target = t;
    _via = v;
    _src = s;
}
ManagedRoute::~ManagedRoute() {}
bool ManagedRoute::sync() { return true; }
void ManagedRoute::remove() {}
}

extern "C" ZT_ResultCode ZT_Node_multicastSubscribe(ZT_Node*, void*, uint64_t, uint64_t, unsigned long) {
    return ZT_RESULT_OK;
}
extern "C" ZT_ResultCode ZT_Node_multicastUnsubscribe(ZT_Node*, uint64_t, uint64_t, unsigned long) {
    return ZT_RESULT_OK;
}

extern "C" {
ZT_ResultCode ZT_Node_processBackgroundTasks(ZT_Node*, void*, int64_t, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_processWirePacket(ZT_Node*, void*, int64_t, int64_t, const struct sockaddr_storage*, const void*, unsigned int, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_processVirtualNetworkFrame(ZT_Node*, void*, int64_t, uint64_t, uint64_t, uint64_t, unsigned int, unsigned int, const void*, unsigned int, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_join(ZT_Node*, uint64_t, void*, void*) { return ZT_RESULT_OK; }
}

static ZT_VirtualNetworkConfig make_config(uint64_t nwid) {
    ZT_VirtualNetworkConfig c;
    memset(&c, 0, sizeof(c));
    c.nwid = nwid;
    c.mac = 0x020000000003ULL;
    c.mtu = 1280;
    c.status = ZT_NETWORK_STATUS_OK;
    c.type = ZT_NETWORK_TYPE_PRIVATE;
    return c;
}

static void set_config_ip(ZT_VirtualNetworkConfig& c, int idx, const char* cidr_str) {
    InetAddress addr(cidr_str);
    memcpy(&c.assignedAddresses[idx], &addr, sizeof(addr));
}

int main(int argc, char** argv) {
    std::printf("[TEST RUN] Starting Tier 3: Simultaneous IP Renumbering & Netmask Combination Test\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x3031323334ULL);

    // Initial setup: 4 IPs with /24 netmasks
    cfg.assignedAddressCount = 4;
    set_config_ip(cfg, 0, "10.147.17.10/24");
    set_config_ip(cfg, 1, "10.147.17.11/24");
    set_config_ip(cfg, 2, "10.147.17.12/24");
    set_config_ip(cfg, 3, "10.147.17.13/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    assert(state.managedIpCount() == 4);
    std::printf("[INFO] Initial configuration applied with 4 IPs (/24)\n");

    // Single-step cross-feature change:
    // Update netmask on IP 0 & 1 from /24 to /16
    // Replace IP 2 & 3 with new addresses in 10.200.5.x/24
    set_config_ip(cfg, 0, "10.147.17.10/16");
    set_config_ip(cfg, 1, "10.147.17.11/16");
    set_config_ip(cfg, 2, "10.200.5.20/24");
    set_config_ip(cfg, 3, "10.200.5.21/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    unsigned count_after = state.managedIpCount();
    std::printf("[INFO] Managed IP count after combined update: %u (expected 4)\n", count_after);
    if (count_after != 4) {
        std::printf("[FAIL] Combined update resulted in %u active IPs instead of 4!\n", count_after);
        return 1;
    }

    // Verify netmask on retained primary IP was updated
    uint8_t primary_mask = state.tap()->ips()[0].netmaskBits();
    std::printf("[INFO] Primary IP prefix after combined update: /%u (expected 16)\n", primary_mask);
    if (primary_mask != 16) {
        std::printf("[FAIL] Primary IP netmask was not updated to /16 during combination test!\n");
        return 1;
    }

    std::printf("[PASS] Tier 3: Simultaneous renumbering and netmask changes passed!\n");
    return 0;
}
