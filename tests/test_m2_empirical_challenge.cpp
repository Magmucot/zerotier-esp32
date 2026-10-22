#include "ESP32NetworkState.hpp"
#include "ESP32EthernetTap.hpp"
#include "lwip/netif.h"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>

using namespace ZeroTier;

// --- Mock Challenger EthernetTap ---
class MockChallengerEthernetTap : public ESP32EthernetTap {
public:
    using ESP32EthernetTap::ESP32EthernetTap;

    static bool s_rejectAll;
    static bool s_rejectSpecificIp;
    static InetAddress s_rejectedIp;
    static unsigned s_addIpCalls;
    static unsigned s_addIpRejections;

    bool addIp(const InetAddress& ip) override {
        ++s_addIpCalls;
        if (s_rejectAll) {
            ++s_addIpRejections;
            char buf[64];
            std::printf("[MOCK_TAP] Rejecting all: addIp(%s) -> false\n", ip.toString(buf));
            return false;
        }
        if (s_rejectSpecificIp && ip.ipsEqual(s_rejectedIp)) {
            ++s_addIpRejections;
            char buf[64];
            std::printf("[MOCK_TAP] Rejecting specific IP: addIp(%s) -> false\n", ip.toString(buf));
            return false;
        }
        return ESP32EthernetTap::addIp(ip);
    }
};

bool MockChallengerEthernetTap::s_rejectAll = false;
bool MockChallengerEthernetTap::s_rejectSpecificIp = false;
InetAddress MockChallengerEthernetTap::s_rejectedIp;
unsigned MockChallengerEthernetTap::s_addIpCalls = 0;
unsigned MockChallengerEthernetTap::s_addIpRejections = 0;

namespace ZeroTier {
EthernetTap::EthernetTap() {}
EthernetTap::~EthernetTap() {}
bool EthernetTap::addIps(std::vector<InetAddress> v) {
    for (auto& i : v) {
        if (!addIp(i)) return false;
    }
    return true;
}
std::string EthernetTap::friendlyName() const { return "challenger_tap"; }

std::shared_ptr<EthernetTap> EthernetTap::newInstance(
    const char*, unsigned int, bool, const char* h, const MAC& m,
    unsigned int mtu, unsigned int metric, uint64_t nwid, const char* name,
    void(*fn)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
    void* a)
{
    return std::shared_ptr<EthernetTap>(new MockChallengerEthernetTap(h, m, mtu, metric, nwid, name, fn, a));
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
    c.mac = 0x020000000088ULL;
    c.mtu = 1280;
    c.status = ZT_NETWORK_STATUS_OK;
    c.type = ZT_NETWORK_TYPE_PRIVATE;
    return c;
}

static void set_config_ip(ZT_VirtualNetworkConfig& c, int idx, const char* cidr_str) {
    InetAddress addr(cidr_str);
    memcpy(&c.assignedAddresses[idx], &addr, sizeof(addr));
}

static std::string get_netif_netmask_str(ESP32NetworkState& state) {
    auto tap = std::dynamic_pointer_cast<MockChallengerEthernetTap>(state.tap());
    if (!tap) return "NO_TAP";
    struct netif* n = tap->netifPtr();
    if (!n) return "NO_NETIF";
    const uint8_t* b = (const uint8_t*)&n->netmask.addr;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return std::string(buf);
}

static std::string get_netif_ip_str(ESP32NetworkState& state) {
    auto tap = std::dynamic_pointer_cast<MockChallengerEthernetTap>(state.tap());
    if (!tap) return "NO_TAP";
    struct netif* n = tap->netifPtr();
    if (!n) return "NO_NETIF";
    const uint8_t* b = (const uint8_t*)&n->ip_addr.addr;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return std::string(buf);
}

// -----------------------------------------------------------------------------
// Challenge 1: Netmask Transitions (/24 -> /16 -> /8 -> /24) on lwIP netif
// -----------------------------------------------------------------------------
static bool test_challenge_netmask_transitions() {
    std::printf("\n============================================================\n");
    std::printf("[CHALLENGE 1] Netmask Transitions (/24 -> /16 -> /8 -> /24)\n");
    std::printf("============================================================\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x1011121314ULL);

    // Initial: 10.147.17.5/24
    cfg.assignedAddressCount = 1;
    set_config_ip(cfg, 0, "10.147.17.5/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    std::string mask = get_netif_netmask_str(state);
    std::string ip = get_netif_ip_str(state);
    std::printf("[STEP 1 Initial /24] netif IP=%s, netmask=%s (tap prefix=/%u)\n",
        ip.c_str(), mask.c_str(), state.tap()->ips()[0].netmaskBits());
    if (mask != "255.255.255.0" || state.tap()->ips()[0].netmaskBits() != 24) {
        std::printf("[FAIL] Initial /24 netmask incorrect! Expected 255.255.255.0, got %s\n", mask.c_str());
        return false;
    }

    // Transition 1: /24 -> /16
    set_config_ip(cfg, 0, "10.147.17.5/16");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    mask = get_netif_netmask_str(state);
    std::printf("[STEP 2 Transition to /16] netif netmask=%s (tap prefix=/%u)\n",
        mask.c_str(), state.tap()->ips()[0].netmaskBits());
    if (mask != "255.255.0.0" || state.tap()->ips()[0].netmaskBits() != 16) {
        std::printf("[FAIL] Transition /16 netmask incorrect! Expected 255.255.0.0, got %s\n", mask.c_str());
        return false;
    }

    // Transition 2: /16 -> /8
    set_config_ip(cfg, 0, "10.147.17.5/8");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    mask = get_netif_netmask_str(state);
    std::printf("[STEP 3 Transition to /8] netif netmask=%s (tap prefix=/%u)\n",
        mask.c_str(), state.tap()->ips()[0].netmaskBits());
    if (mask != "255.0.0.0" || state.tap()->ips()[0].netmaskBits() != 8) {
        std::printf("[FAIL] Transition /8 netmask incorrect! Expected 255.0.0.0, got %s\n", mask.c_str());
        return false;
    }

    // Transition 3: /8 -> /24
    set_config_ip(cfg, 0, "10.147.17.5/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    mask = get_netif_netmask_str(state);
    std::printf("[STEP 4 Transition back to /24] netif netmask=%s (tap prefix=/%u)\n",
        mask.c_str(), state.tap()->ips()[0].netmaskBits());
    if (mask != "255.255.255.0" || state.tap()->ips()[0].netmaskBits() != 24) {
        std::printf("[FAIL] Transition /24 netmask incorrect! Expected 255.255.255.0, got %s\n", mask.c_str());
        return false;
    }

    // Additional boundary: /30 and /32 transitions
    set_config_ip(cfg, 0, "10.147.17.5/30");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    mask = get_netif_netmask_str(state);
    std::printf("[STEP 5 Transition to /30] netif netmask=%s (tap prefix=/%u)\n",
        mask.c_str(), state.tap()->ips()[0].netmaskBits());
    if (mask != "255.255.255.252" || state.tap()->ips()[0].netmaskBits() != 30) {
        std::printf("[FAIL] Transition /30 netmask incorrect! Expected 255.255.255.252, got %s\n", mask.c_str());
        return false;
    }

    std::printf("[PASS] Challenge 1: All netmask transitions verified on lwIP netif!\n");
    return true;
}

// -----------------------------------------------------------------------------
// Challenge 2: Simultaneous 4-IP Swaps (10.147.17.1-4/24 -> 10.200.1.1-4/24)
// -----------------------------------------------------------------------------
static bool test_challenge_simultaneous_4ip_swaps() {
    std::printf("\n============================================================\n");
    std::printf("[CHALLENGE 2] Simultaneous 4-IP Swaps\n");
    std::printf("============================================================\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x2021222324ULL);

    cfg.assignedAddressCount = 4;
    set_config_ip(cfg, 0, "10.147.17.1/24");
    set_config_ip(cfg, 1, "10.147.17.2/24");
    set_config_ip(cfg, 2, "10.147.17.3/24");
    set_config_ip(cfg, 3, "10.147.17.4/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    std::printf("[STEP 1 Initial 4-IP Setup] Managed count: %u, lwIP primary: %s\n",
        state.managedIpCount(), get_netif_ip_str(state).c_str());
    assert(state.managedIpCount() == 4);
    assert(get_netif_ip_str(state) == "10.147.17.1");

    // Simultaneous swap to 10.200.1.1-4/24
    set_config_ip(cfg, 0, "10.200.1.1/24");
    set_config_ip(cfg, 1, "10.200.1.2/24");
    set_config_ip(cfg, 2, "10.200.1.3/24");
    set_config_ip(cfg, 3, "10.200.1.4/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    unsigned count = state.managedIpCount();
    std::string primaryIp = get_netif_ip_str(state);
    std::printf("[STEP 2 Swap Results] Managed count: %u (expected 4), lwIP primary: %s\n", count, primaryIp.c_str());

    if (count != 4) {
        std::printf("[FAIL] Simultaneous 4-IP swap failed! Managed count = %u (table_full drops occurred)\n", count);
        return false;
    }

    std::vector<InetAddress> tapIps = state.tap()->ips();
    std::printf("[INFO] Tap IP count: %zu\n", tapIps.size());
    if (tapIps.size() != 4) {
        std::printf("[FAIL] Tap active IP count = %zu (expected 4)\n", tapIps.size());
        return false;
    }

    for (size_t i = 0; i < tapIps.size(); ++i) {
        char buf[64];
        std::printf("   Tap IP [%zu]: %s\n", i, tapIps[i].toString(buf));
        // Verify none of the old subnet remains
        char ipOnly[32];
        tapIps[i].toIpString(ipOnly);
        if (std::strstr(ipOnly, "10.147.17.") != nullptr) {
            std::printf("[FAIL] Old subnet address %s still found in tap!\n", ipOnly);
            return false;
        }
    }

    // Stress test: Perform 10 rapid successive 4-IP swaps
    std::printf("[STEP 3 Stress] Running 10 rapid successive 4-IP swaps across different subnets...\n");
    for (int cycle = 0; cycle < 10; ++cycle) {
        for (int i = 0; i < 4; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "10.%d.%d.%d/24", 100 + cycle, i + 1, 10 + i);
            set_config_ip(cfg, i, buf);
        }
        state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
        if (state.managedIpCount() != 4 || state.tap()->ips().size() != 4) {
            std::printf("[FAIL] Cycle %d: Managed count dropped to %u!\n", cycle, state.managedIpCount());
            return false;
        }
    }
    std::printf("[PASS] 10 successive 4-IP swaps completed with 0 drops.\n");

    std::printf("[PASS] Challenge 2: Simultaneous 4-IP swap verified with 0 table_full drops!\n");
    return true;
}

// -----------------------------------------------------------------------------
// Challenge 3: Tap Rejection Simulation (Confirm slot not activated)
// -----------------------------------------------------------------------------
static bool test_challenge_tap_rejection_simulation() {
    std::printf("\n============================================================\n");
    std::printf("[CHALLENGE 3] Tap Rejection Simulation\n");
    std::printf("============================================================\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x3031323334ULL);

    // Subtest 3.1: Reject all additions
    std::printf("[SUBTEST 3.1] Testing complete tap rejection (addIp returns false)...\n");
    MockChallengerEthernetTap::s_rejectAll = true;
    cfg.assignedAddressCount = 1;
    set_config_ip(cfg, 0, "10.147.17.10/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    std::printf("[INFO] Managed IP count after rejected add: %u (expected 0)\n", state.managedIpCount());
    if (state.managedIpCount() != 0) {
        std::printf("[FAIL] Slot was activated despite tap->addIp returning false! Issue P2-11 violated!\n");
        MockChallengerEthernetTap::s_rejectAll = false;
        return false;
    }
    std::printf("[PASS] Subtest 3.1: Slot was NOT activated when tap rejected addition.\n");

    // Subtest 3.2: Slot reuse after rejection
    std::printf("[SUBTEST 3.2] Testing slot recovery after tap rejection...\n");
    MockChallengerEthernetTap::s_rejectAll = false; // Allow addition now
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    std::printf("[INFO] Managed IP count after retry: %u (expected 1)\n", state.managedIpCount());
    if (state.managedIpCount() != 1) {
        std::printf("[FAIL] Slot was not available for allocation after previous failure! Count=%u\n", state.managedIpCount());
        return false;
    }
    std::printf("[PASS] Subtest 3.2: Slot successfully recovered and activated.\n");

    // Subtest 3.3: Partial rejection in multi-IP configuration
    // 3 distinct subnets: 10.10.1.1/24 (accepted), 10.20.1.1/24 (rejected), 10.30.1.1/24 (accepted)
    std::printf("[SUBTEST 3.3] Testing partial rejection in 3-IP configuration...\n");
    cfg.assignedAddressCount = 3;
    set_config_ip(cfg, 0, "10.10.1.1/24");
    set_config_ip(cfg, 1, "10.20.1.1/24");
    set_config_ip(cfg, 2, "10.30.1.1/24");

    MockChallengerEthernetTap::s_rejectSpecificIp = true;
    MockChallengerEthernetTap::s_rejectedIp = InetAddress("10.20.1.1/24");

    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    unsigned activeCount = state.managedIpCount();
    std::printf("[INFO] Active managed count: %u (expected 2)\n", activeCount);
    if (activeCount != 2) {
        std::printf("[FAIL] Expected exactly 2 active IPs, got %u!\n", activeCount);
        MockChallengerEthernetTap::s_rejectSpecificIp = false;
        return false;
    }

    // Verify specifically that 10.20.1.1 is NOT in tap IPs
    std::vector<InetAddress> tIps = state.tap()->ips();
    bool foundRejectedInTap = false;
    for (auto& tip : tIps) {
        if (tip.ipsEqual(MockChallengerEthernetTap::s_rejectedIp)) {
            foundRejectedInTap = true;
            break;
        }
    }
    if (foundRejectedInTap) {
        std::printf("[FAIL] Rejected IP was present in tap->ips()!\n");
        MockChallengerEthernetTap::s_rejectSpecificIp = false;
        return false;
    }

    // Verify that managedIpv4Contains does NOT contain 10.20.1.1
    InetAddress checkIp("10.20.1.1");
    uint32_t rawIp;
    memcpy(&rawIp, checkIp.rawIpData(), 4);
    if (state.managedIpv4Contains(rawIp)) {
        std::printf("[FAIL] Rejected IP 10.20.1.1 is reported as managed by state!\n");
        MockChallengerEthernetTap::s_rejectSpecificIp = false;
        return false;
    }
    std::printf("[PASS] Subtest 3.3: Partial rejection verified: only successful IPs activated.\n");

    // Subtest 3.4: Fill remaining slots when rejection cleared
    std::printf("[SUBTEST 3.4] Testing slot reuse for rejected slot...\n");
    MockChallengerEthernetTap::s_rejectSpecificIp = false;
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    std::printf("[INFO] Active managed count after clearing rejection: %u (expected 3)\n", state.managedIpCount());
    if (state.managedIpCount() != 3) {
        std::printf("[FAIL] Slot failed to activate after rejection cleared! Count=%u\n", state.managedIpCount());
        return false;
    }
    std::printf("[PASS] Subtest 3.4: Previously rejected slot successfully populated.\n");

    std::printf("[PASS] Challenge 3: Tap rejection simulation verified (unconfirmed slots never activated)!\n");
    return true;
}

// -----------------------------------------------------------------------------
// Challenge 4: Combined 4-IP Swap with Simultaneous Netmask Mutation
// -----------------------------------------------------------------------------
static bool test_challenge_simultaneous_swap_and_netmask_mutation() {
    std::printf("\n============================================================\n");
    std::printf("[CHALLENGE 4] Combined 4-IP Swap with Netmask Mutation\n");
    std::printf("============================================================\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x4041424344ULL);

    // Initial 4 IPs: all /24
    cfg.assignedAddressCount = 4;
    set_config_ip(cfg, 0, "10.50.1.1/24");
    set_config_ip(cfg, 1, "10.50.1.2/24");
    set_config_ip(cfg, 2, "10.50.1.3/24");
    set_config_ip(cfg, 3, "10.50.1.4/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    assert(state.managedIpCount() == 4);
    assert(get_netif_netmask_str(state) == "255.255.255.0");

    // Mutate: IP 0 kept but netmask changed to /16; IP 1-3 replaced by completely new IPs with /8 and /16
    set_config_ip(cfg, 0, "10.50.1.1/16");
    set_config_ip(cfg, 1, "10.60.2.1/8");
    set_config_ip(cfg, 2, "10.70.3.1/16");
    set_config_ip(cfg, 3, "10.80.4.1/24");
    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);

    unsigned count = state.managedIpCount();
    std::string mask = get_netif_netmask_str(state);
    std::string ip = get_netif_ip_str(state);
    std::printf("[INFO] Post-mutation: Managed count=%u, lwIP primary IP=%s, netmask=%s\n",
        count, ip.c_str(), mask.c_str());

    if (count != 4) {
        std::printf("[FAIL] Managed count is %u instead of 4!\n", count);
        return false;
    }
    if (ip != "10.50.1.1" || mask != "255.255.0.0") {
        std::printf("[FAIL] lwIP primary address or netmask not updated! IP=%s, mask=%s\n", ip.c_str(), mask.c_str());
        return false;
    }

    std::printf("[PASS] Challenge 4: Combined swap and netmask mutation passed cleanly!\n");
    return true;
}

int main(int argc, char** argv) {
    std::printf("============================================================\n");
    std::printf("EMPIRICAL CHALLENGER M2: R3 COMPREHENSIVE VERIFICATION HARNESS\n");
    std::printf("============================================================\n");

    bool ok1 = test_challenge_netmask_transitions();
    bool ok2 = test_challenge_simultaneous_4ip_swaps();
    bool ok3 = test_challenge_tap_rejection_simulation();
    bool ok4 = test_challenge_simultaneous_swap_and_netmask_mutation();

    std::printf("\n============================================================\n");
    std::printf("EMPIRICAL CHALLENGE SUMMARY RESULTS\n");
    std::printf("============================================================\n");
    std::printf("Challenge 1: Netmask Transitions (/24 -> /16 -> /8 -> /24): %s\n", ok1 ? "PASS" : "FAIL");
    std::printf("Challenge 2: Simultaneous 4-IP Swaps:                       %s\n", ok2 ? "PASS" : "FAIL");
    std::printf("Challenge 3: Tap Rejection Simulation:                      %s\n", ok3 ? "PASS" : "FAIL");
    std::printf("Challenge 4: Combined Swap & Netmask Mutation:              %s\n", ok4 ? "PASS" : "FAIL");
    std::printf("============================================================\n");

    if (ok1 && ok2 && ok3 && ok4) {
        std::printf("[OVERALL VERDICT] ALL CHALLENGES PASSED EMPIRICALLY (APPROVE)\n");
        return 0;
    } else {
        std::printf("[OVERALL VERDICT] EMPIRICAL FAILURES DETECTED (REQUEST_CHANGES)\n");
        return 1;
    }
}
