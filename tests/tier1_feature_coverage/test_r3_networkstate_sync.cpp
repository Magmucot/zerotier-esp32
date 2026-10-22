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

// --- Test Fixtures for EthernetTap and ManagedRoute on Host ---
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

static unsigned s_subscribes = 0;
extern "C" ZT_ResultCode ZT_Node_multicastSubscribe(ZT_Node*, void*, uint64_t, uint64_t, unsigned long) {
    ++s_subscribes;
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
    c.mac = 0x020000000001ULL;
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
    std::printf("[TEST RUN] Starting Tier 1 R3: NetworkState netmask & two-phase IP sync test\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x1012131415ULL);

    // --- Subtest 1: Netmask Update Propagation ---
    std::printf("[SUBTEST 1] Testing netmask update propagation (/24 -> /16)...\n");
    cfg.assignedAddressCount = 1;
    set_config_ip(cfg, 0, "10.147.17.5/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    assert(state.tap() != nullptr && "Tap should be initialized");
    assert(state.managedIpCount() == 1 && "Should have 1 managed IP");
    uint8_t initial_prefix = state.tap()->ips()[0].netmaskBits();
    std::printf("[INFO] Initial prefix: /%u (expected 24)\n", initial_prefix);
    assert(initial_prefix == 24);

    // Change netmask on same IP to /16
    set_config_ip(cfg, 0, "10.147.17.5/16");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    uint8_t updated_prefix = state.tap()->ips()[0].netmaskBits();
    std::printf("[INFO] Updated prefix: /%u (expected 16)\n", updated_prefix);
    if (updated_prefix != 16) {
        std::printf("[FAIL] Netmask update was dropped! Tap still has /%u instead of /16 (Issue P2-11)\n", updated_prefix);
        return 1;
    }
    std::printf("[PASS] Subtest 1: Netmask update correctly propagated to tap interface.\n");

    // --- Subtest 2: Two-Phase Sync / Address Replacement ---
    std::printf("[SUBTEST 2] Testing two-phase IP sync (simultaneous replacement of 4 IPs)...\n");
    cfg.assignedAddressCount = 4;
    for (int i = 0; i < 4; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "10.147.17.%d/24", 10 + i);
        set_config_ip(cfg, i, buf);
    }
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    std::printf("[INFO] Setup: managed IP count = %u (expected 4)\n", state.managedIpCount());
    assert(state.managedIpCount() == 4);

    // Now renumber all 4 IPs simultaneously to a new subnet
    for (int i = 0; i < 4; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "10.200.1.%d/24", 20 + i);
        set_config_ip(cfg, i, buf);
    }
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    unsigned count_after_replacement = state.managedIpCount();
    std::printf("[INFO] Managed IP count after 4-IP replacement: %u (expected 4)\n", count_after_replacement);

    if (count_after_replacement != 4) {
        std::printf("[FAIL] Address replacement failed! Expected 4 IPs, got %u (Issue P1-5 / table_full drop)\n", count_after_replacement);
        return 1;
    }
    std::printf("[PASS] Subtest 2: Two-phase IP sync successfully replaced all 4 addresses without drops.\n");

    // --- Subtest 3: Clean Address Teardown ---
    std::printf("[SUBTEST 3] Testing address removal down to 0...\n");
    cfg.assignedAddressCount = 0;
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    std::printf("[INFO] Managed IP count after teardown: %u (expected 0)\n", state.managedIpCount());
    assert(state.managedIpCount() == 0);
    std::printf("[PASS] Subtest 3: All IPs removed successfully.\n");

    std::printf("[PASS] Tier 1 R3: All NetworkState netmask & IP sync tests passed!\n");
    return 0;
}
