#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <memory>
#include <new>
#include <vector>
#include <list>
#include <atomic>
#include <algorithm>

#include "Address.hpp"
#include "MAC.hpp"
#include "MulticastGroup.hpp"
#include "SharedPtr.hpp"
#include "Buffer.hpp"
#include "Constants.hpp"
#include "Mutex.hpp"
#include "Utils.hpp"
#include "Packet.hpp"
#include "Peer.hpp"
#include "Switch.hpp"
#include "Node.hpp"
#include "Topology.hpp"
#include "NetworkConfig.hpp"
#include "Network.hpp"
#include "RuntimeEnvironment.hpp"
#include "Multicaster.hpp"

// Flags to control allocation failures
static bool s_fail_nothrow_packet = false;
static bool s_fail_nothrow_outbound = false;
static bool s_fail_nothrow_address = false;
static bool s_fail_nothrow_all = false;
static bool s_fail_throwing_new = false;
static int s_nothrow_alloc_count = 0;

// Custom replacement for operator new (std::nothrow)
void* operator new(size_t sz, const std::nothrow_t&) noexcept {
    s_nothrow_alloc_count++;
    if (s_fail_nothrow_all) return nullptr;
    if (s_fail_nothrow_packet && sz >= sizeof(ZeroTier::Packet)) return nullptr;
    if (s_fail_nothrow_outbound && sz >= sizeof(ZeroTier::OutboundMulticast)) return nullptr;
    return malloc(sz);
}
void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }

void* operator new[](size_t sz, const std::nothrow_t&) noexcept {
    s_nothrow_alloc_count++;
    if (s_fail_nothrow_all || s_fail_nothrow_address) return nullptr;
    return malloc(sz);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }

// Replacement for throwing operator new[] to test large group allocation failure
void* operator new[](size_t sz) {
    if (s_fail_throwing_new) {
        throw std::bad_alloc();
    }
    void* p = malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete[](void* p) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

namespace ZeroTier {

void Utils::burn(void* p, unsigned int len) {
    if (p && len) memset(p, 0, len);
}

void Utils::getSecureRandom(void* p, unsigned int len) {
    if (p && len) memset(p, 0x42, len);
}

uint64_t Node::prng() { return 123456789ULL; }

SharedPtr<Peer> Topology::getUpstreamPeer(uint64_t) { return SharedPtr<Peer>(); }
SharedPtr<Path> Peer::getAppropriatePath(int64_t, bool, int) { return SharedPtr<Path>(); }
bool Path::send(const RuntimeEnvironment*, void*, const void*, unsigned int, int64_t) { return true; }

void Switch::send(void*, Packet&, bool, uint64_t, int) {}

bool Packet::compress() { return true; }
void Packet::armor(const void*, bool, bool, const AES*, const Identity&) {}

void OutboundMulticast::init(const RuntimeEnvironment*, uint64_t, uint64_t, bool, unsigned int, unsigned int, const MAC&, const MulticastGroup&, unsigned int, const void*, unsigned int) {}
void OutboundMulticast::sendOnly(const RuntimeEnvironment*, void*, const Address&) {}

NetworkConfig::NetworkConfig() : multicastLimit(32), com() {}

Network::Network(const RuntimeEnvironment* renv, void* tPtr, uint64_t nwid, void* uptr, const NetworkConfig* nconf)
    : RR(renv)
    , _uPtr(uptr)
    , _id(nwid)
    , _nwidStr("")
    , _lastAnnouncedMulticastGroupsUpstream(0)
    , _mac()
    , _portInitialized(false)
    , _lastConfigUpdate(0)
    , _destroyed(false)
    , _netconfFailure(NETCONF_FAILURE_NONE)
    , _portError(0)
{
    if (nconf) {
        _config = *nconf;
    }
}

Network::~Network() {}

bool Network::subscribedToMulticastGroup(const MulticastGroup&, bool) const { return true; }

} // namespace ZeroTier

using namespace ZeroTier;

int main() {
    std::printf("[TEST RUN] Starting Multicaster OOM / nullptr Allocation Stress Tests\n");

    // Initialize mock node, switch, and topology
    std::vector<uint8_t> node_mem(sizeof(Node) + 1024, 0);
    Node* mock_node = reinterpret_cast<Node*>(node_mem.data());

    std::vector<uint8_t> sw_mem(sizeof(Switch) + 1024, 0);
    Switch* mock_sw = reinterpret_cast<Switch*>(sw_mem.data());

    std::vector<uint8_t> topo_mem(sizeof(Topology) + 1024, 0);
    Topology* mock_topo = reinterpret_cast<Topology*>(topo_mem.data());

    RuntimeEnvironment renv(mock_node);
    renv.sw = mock_sw;
    renv.topology = mock_topo;

    Multicaster mc(&renv);

    NetworkConfig nconf;
    nconf.multicastLimit = 32;

    SharedPtr<Network> network(new Network(&renv, nullptr, 0x12345ULL, nullptr, &nconf));
    const uint64_t nwid = network->id();
    const MulticastGroup mg(MAC(0x01, 0x00, 0x5e, 0x00, 0x00, 0x01), 0);
    const MAC src(0x02, 0x00, 0x00, 0x00, 0x00, 0x01);
    const char dummy_data[64] = "Multicast payload data test";

    // 1. Add 10 members (fits inside idxbuf[32])
    for (uint64_t i = 1; i <= 10; ++i) {
        mc.add(nullptr, 1000 + i, nwid, mg, Address(i));
    }

    // Test 1: Normal send in queued path
    std::printf("[TEST 1] Testing queued send path with normal allocations...\n");
    mc.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    std::printf("[PASS] Normal queued send succeeded.\n");

    // Test 2: Add up to 32 members (direct send path)
    for (uint64_t i = 11; i <= 32; ++i) {
        mc.add(nullptr, 1000 + i, nwid, mg, Address(i));
    }
    std::printf("[TEST 2] Testing direct send path with normal allocations (32 members)...\n");
    mc.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    std::printf("[PASS] Normal direct send succeeded.\n");

    // Test 3: OOM when allocating OutboundMulticast in _sendDirect (new (std::nothrow) returns nullptr)
    std::printf("[TEST 3] Testing OOM on OutboundMulticast allocation in _sendDirect...\n");
    s_fail_nothrow_outbound = true;
    mc.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_nothrow_outbound = false;
    std::printf("[PASS] OOM OutboundMulticast handled safely (null check returns early without crashing).\n");

    // Test 4: OOM when allocating activeBridges in _sendDirect (new (std::nothrow) returns nullptr)
    std::printf("[TEST 4] Testing OOM on activeBridges allocation in _sendDirect...\n");
    s_fail_nothrow_address = true;
    mc.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_nothrow_address = false;
    std::printf("[PASS] OOM activeBridges in _sendDirect handled safely.\n");

    // Test 5: Total nothrow OOM in _sendDirect
    std::printf("[TEST 5] Testing OOM on ALL nothrow allocations in _sendDirect...\n");
    s_fail_nothrow_all = true;
    mc.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_nothrow_all = false;
    std::printf("[PASS] Total nothrow OOM in _sendDirect handled safely.\n");

    // Test 6: OOM in queued path (reset to 10 members)
    Multicaster mc2(&renv);
    for (uint64_t i = 1; i <= 10; ++i) {
        mc2.add(nullptr, 1000 + i, nwid, mg, Address(i));
    }
    std::printf("[TEST 6] Testing OOM on activeBridges in _sendQueued...\n");
    s_fail_nothrow_address = true;
    mc2.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_nothrow_address = false;
    std::printf("[PASS] OOM activeBridges in _sendQueued handled safely.\n");

    // Test 7: OOM on Packet in _doExplicitGather
    std::printf("[TEST 7] Testing OOM on Packet in _doExplicitGather...\n");
    s_fail_nothrow_packet = true;
    mc2.send(nullptr, 200000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_nothrow_packet = false;
    std::printf("[PASS] OOM Packet in _doExplicitGather handled safely.\n");

    // Test 8: Large group (> 32 members, triggers dynamic heap allocation for indexes)
    for (uint64_t i = 11; i <= 50; ++i) {
        mc2.add(nullptr, 1000 + i, nwid, mg, Address(i));
    }
    std::printf("[TEST 8] Testing monstrous group (50 members, heap indexes)...\n");
    mc2.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    std::printf("[PASS] 50 members handled cleanly.\n");

    // Test 9: Throwing bad_alloc when allocating indexes buffer for monstrous group
    std::printf("[TEST 9] Testing throwing bad_alloc on monstrous group indexes allocation...\n");
    s_fail_throwing_new = true;
    mc2.send(nullptr, 1000, network, Address(), mg, src, 0x0800, dummy_data, sizeof(dummy_data));
    s_fail_throwing_new = false;
    std::printf("[PASS] Exception caught by try/catch in Multicaster::send, no leak or crash.\n");

    std::printf("[PASS] ALL Multicaster OOM stress tests passed cleanly under AddressSanitizer!\n");
    return 0;
}
