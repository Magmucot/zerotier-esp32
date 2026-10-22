#include "ESP32NetworkState.hpp"
#include "ESP32EthernetTap.hpp"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>

using namespace ZeroTier;

// --- Test Fixtures ---
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
    c.mac = 0x020000000004ULL;
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
    std::printf("[TEST RUN] Starting Tier 3: Virtual Network Frame Dispatch During State Sync\n");

    ESP32NetworkState state("/", nullptr, nullptr);
    ZT_VirtualNetworkConfig cfg = make_config(0x4041424344ULL);
    cfg.assignedAddressCount = 2;
    set_config_ip(cfg, 0, "10.147.17.1/24");
    set_config_ip(cfg, 1, "10.147.17.2/24");

    state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    assert(state.tap() != nullptr);

    std::atomic<bool> running(true);
    std::atomic<unsigned> frames_dispatched(0);

    // Frame injector thread: continuously sends frames through tap->put()
    std::thread frame_thread([&]() {
        uint8_t payload[64];
        memset(payload, 0xaa, sizeof(payload));
        MAC from(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);
        MAC to(0x02, 0x00, 0x00, 0x00, 0x00, 0x02);

        while (running.load()) {
            if (state.tap()) {
                state.tap()->put(from, to, 0x0800, payload, sizeof(payload));
                frames_dispatched++;
            }
            std::this_thread::yield();
        }
    });

    // Main thread: performs 50 rapid configuration sync updates
    std::printf("[INFO] Executing 50 configuration state sync cycles during active frame injection...\n");
    for (int cycle = 0; cycle < 50; ++cycle) {
        if (cycle % 2 == 0) {
            set_config_ip(cfg, 0, "10.147.17.1/16");
            set_config_ip(cfg, 1, "10.147.17.3/24");
        } else {
            set_config_ip(cfg, 0, "10.147.17.1/24");
            set_config_ip(cfg, 1, "10.147.17.2/24");
        }
        state.applyConfigFromOwnerTask((ZT_Node*)1, nullptr, ZT_VIRTUAL_NETWORK_CONFIG_OPERATION_CONFIG_UPDATE, &cfg);
    }

    running.store(false);
    frame_thread.join();

    std::printf("[INFO] Completed without deadlocks. Total frames dispatched during sync: %u\n", frames_dispatched.load());
    assert(frames_dispatched.load() > 0);

    std::printf("[PASS] Tier 3: Frame dispatch during state sync completed safely!\n");
    return 0;
}
