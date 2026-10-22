#include "Multicaster.hpp"
#include "RuntimeEnvironment.hpp"
#include "Address.hpp"
#include "MAC.hpp"
#include "MulticastGroup.hpp"
#include "Network.hpp"
#include "Node.hpp"
#include "Topology.hpp"
#include "Switch.hpp"
#include "Peer.hpp"
#include "Packet.hpp"

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <vector>

using namespace ZeroTier;

// --- Mock Minimal Environment for Multicaster Testing ---
namespace ZeroTier {

bool Network::subscribedToMulticastGroup(const MulticastGroup&, bool) const {
    return false;
}
Network::~Network() {}

uint64_t Node::prng() {
    return (uint64_t)rand();
}

SharedPtr<Peer> Topology::getUpstreamPeer(uint64_t) {
    return SharedPtr<Peer>();
}

SharedPtr<Path> Peer::getAppropriatePath(int64_t, bool, int) {
    return SharedPtr<Path>();
}

bool Path::send(const RuntimeEnvironment*, void*, const void*, unsigned int, int64_t) {
    return true;
}

void Switch::send(void*, Packet&, bool, uint64_t, int) {}

bool Packet::compress() { return true; }
void Packet::armor(void const*, bool, bool, ZeroTier::AES const*, ZeroTier::Identity const&) {}

bool Network::filterOutgoingPacket(void*, bool, const Address&, const Address&, const MAC&, const MAC&, const uint8_t*, unsigned int, unsigned int, unsigned int, uint8_t&) { return true; }
Membership::Membership() {}
Membership& Network::_membership(const Address&) { static Membership m; return m; }
void Membership::pushCredentials(const RuntimeEnvironment*, void*, int64_t, const Address&, const NetworkConfig&) {}

} // namespace ZeroTier

int main(int argc, char** argv) {
    std::printf("[TEST RUN] Starting Tier 2: Multicast Subscriber Boundaries Test\n");

    RuntimeEnvironment renv((Node*)nullptr);
    Multicaster mc(&renv);
    const uint64_t nwid = 0x1234567890ULL;
    const MulticastGroup mg(MAC(0xff, 0xff, 0xff, 0xff, 0xff, 0xff), 0);

    // 1. Boundary: 0 subscribers
    std::vector<Address> m0 = mc.getMembers(nwid, mg, 64);
    std::printf("[INFO] 0 subscribers test: members count = %zu\n", m0.size());
    assert(m0.empty());
    std::printf("[PASS] Boundary 0 subscribers: correctly empty\n");

    // 2. Boundary: 1 subscriber
    Address a1(1ULL);
    mc.add(nullptr, 1000, nwid, mg, a1);
    std::vector<Address> m1 = mc.getMembers(nwid, mg, 64);
    std::printf("[INFO] 1 subscriber test: members count = %zu\n", m1.size());
    assert(m1.size() == 1);
    assert(m1[0] == a1);
    std::printf("[PASS] Boundary 1 subscriber: correctly retrieved\n");

    // 3. Boundary: Exactly 32 subscribers (Capacity of static idxbuf[32])
    for (uint64_t i = 2; i <= 32; ++i) {
        mc.add(nullptr, 1000 + i, nwid, mg, Address(i));
    }
    std::vector<Address> m32 = mc.getMembers(nwid, mg, 64);
    std::printf("[INFO] 32 subscribers test (stack buffer boundary): members count = %zu\n", m32.size());
    assert(m32.size() == 32);
    std::printf("[PASS] Boundary 32 subscribers: stack buffer capacity matches\n");

    // 4. Boundary: 33 subscribers (Heap allocation transition)
    Address a33(33ULL);
    mc.add(nullptr, 2000, nwid, mg, a33);
    std::vector<Address> m33 = mc.getMembers(nwid, mg, 64);
    std::printf("[INFO] 33 subscribers test (heap fallback threshold): members count = %zu\n", m33.size());
    assert(m33.size() == 33);
    std::printf("[PASS] Boundary 33 subscribers: heap transition handled safely\n");

    // 5. Boundary: 64 subscribers
    for (uint64_t i = 34; i <= 64; ++i) {
        mc.add(nullptr, 3000 + i, nwid, mg, Address(i));
    }
    std::vector<Address> m64 = mc.getMembers(nwid, mg, 64);
    std::printf("[INFO] 64 subscribers test: members count = %zu\n", m64.size());
    assert(m64.size() == 64);
    std::printf("[PASS] Boundary 64 subscribers: all members tracked properly\n");

    // Removal test
    mc.remove(nwid, mg, a33);
    std::vector<Address> m63 = mc.getMembers(nwid, mg, 64);
    assert(m63.size() == 63);
    std::printf("[PASS] Removal of member verified, count = %zu\n", m63.size());

    std::printf("[PASS] Tier 2: Multicast subscriber boundaries tests passed!\n");
    return 0;
}
