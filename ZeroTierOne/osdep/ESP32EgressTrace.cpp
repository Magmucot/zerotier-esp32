#include "ESP32EgressTrace.hpp"

#include <stdio.h>
#include <string.h>

namespace ZeroTier {

static volatile bool _traceEnabled = false;
static uint64_t _traceNwid = 0;
static uint32_t _traceTargetIpv4Nbo = 0;
static uint32_t _traceMarkerId = 0;
static uint32_t _traceRemaining = 0;

void ESP32EgressTrace::enable(uint64_t nwid, uint32_t targetIpv4Nbo, uint32_t markerId, uint32_t maxFrames)
{
	_traceNwid = nwid;
	_traceTargetIpv4Nbo = targetIpv4Nbo;
	_traceMarkerId = markerId;
	_traceRemaining = maxFrames ? maxFrames : 1;
	_traceEnabled = true;
	fprintf(stderr, "[ZT_EGRESS_TRACE_ENABLE marker_id=%u nwid=%llx target_nbo=%08x frames=%u]\n", (unsigned)markerId, (unsigned long long)nwid, (unsigned)targetIpv4Nbo, (unsigned)_traceRemaining);
}

void ESP32EgressTrace::disable()
{
	_traceEnabled = false;
	_traceRemaining = 0;
	fprintf(stderr, "[ZT_EGRESS_TRACE_DISABLE marker_id=%u]\n", (unsigned)_traceMarkerId);
}

bool ESP32EgressTrace::enabled()
{
	return _traceEnabled && (_traceRemaining > 0);
}

void ESP32EgressTrace::_printIpv4(uint32_t nbo)
{
	const unsigned char* p = (const unsigned char*)&nbo;
	fprintf(stderr, "%u.%u.%u.%u", (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
}

bool ESP32EgressTrace::_ipv4Matches(uint32_t ipNbo)
{
	return (_traceTargetIpv4Nbo == 0) || (_traceTargetIpv4Nbo == ipNbo);
}

void ESP32EgressTrace::traceFrame(uint64_t nwid, const MAC& src, const MAC& dst, unsigned int etherType, const void* payload, unsigned int payloadLen)
{
	if (! enabled()) {
		return;
	}
	if (_traceNwid && (_traceNwid != nwid)) {
		return;
	}
	const unsigned char* p = (const unsigned char*)payload;
	uint32_t arpSender = 0;
	uint32_t arpTarget = 0;
	uint32_t ipv4Src = 0;
	uint32_t ipv4Dst = 0;
	unsigned int ipv4Proto = 0;
	unsigned int icmpType = 0xff;
	unsigned int icmpCode = 0xff;
	bool targetRelevant = (_traceTargetIpv4Nbo == 0);

	if ((etherType == 0x0806) && p && (payloadLen >= 28)) {
		memcpy(&arpSender, p + 14, 4);
		memcpy(&arpTarget, p + 24, 4);
		targetRelevant = _ipv4Matches(arpSender) || _ipv4Matches(arpTarget);
	}
	else if ((etherType == 0x0800) && p && (payloadLen >= 20)) {
		const unsigned int ihl = (p[0] & 0x0fU) * 4U;
		if ((ihl >= 20) && (payloadLen >= ihl)) {
			ipv4Proto = p[9];
			memcpy(&ipv4Src, p + 12, 4);
			memcpy(&ipv4Dst, p + 16, 4);
			targetRelevant = _ipv4Matches(ipv4Src) || _ipv4Matches(ipv4Dst);
			if ((ipv4Proto == 1) && (payloadLen >= ihl + 2)) {
				icmpType = p[ihl];
				icmpCode = p[ihl + 1];
			}
		}
	}
	if (! targetRelevant) {
		return;
	}

	char smac[32];
	char dmac[32];
	fprintf(stderr, "[ZT_EGRESS_DECISION_BEGIN marker_id=%u marker=ESP32_EGRESS nwid=%llx]\n", (unsigned)_traceMarkerId, (unsigned long long)nwid);
	fprintf(stderr, "[egress_frame]\n");
	fprintf(stderr, "frame_seen=yes\n");
	fprintf(stderr, "frame_len=%u\n", payloadLen + 14U);
	fprintf(stderr, "ethertype=0x%04x\n", etherType);
	fprintf(stderr, "ethertype_name=%s\n", (etherType == 0x0806) ? "ARP" : ((etherType == 0x0800) ? "IPv4" : ((etherType == 0x86dd) ? "IPv6" : "unknown")));
	fprintf(stderr, "src_mac=%s\n", src.toString(smac));
	fprintf(stderr, "dst_mac=%s\n", dst.toString(dmac));
	fprintf(stderr, "is_broadcast=%s\n", dst.isBroadcast() ? "yes" : "no");
	fprintf(stderr, "is_multicast=%s\n", dst.isMulticast() ? "yes" : "no");
	fprintf(stderr, "is_unicast=%s\n", (!dst.isBroadcast() && !dst.isMulticast()) ? "yes" : "no");
	if (etherType == 0x0806) {
		fprintf(stderr, "arp_sender_ip="); _printIpv4(arpSender); fprintf(stderr, "\n");
		fprintf(stderr, "arp_target_ip="); _printIpv4(arpTarget); fprintf(stderr, "\n");
	}
	else if (etherType == 0x0800) {
		fprintf(stderr, "ipv4_src="); _printIpv4(ipv4Src); fprintf(stderr, "\n");
		fprintf(stderr, "ipv4_dst="); _printIpv4(ipv4Dst); fprintf(stderr, "\n");
		fprintf(stderr, "ipv4_proto=%u\n", ipv4Proto);
		if (ipv4Proto == 1) {
			fprintf(stderr, "icmp_type=%u\n", icmpType);
			fprintf(stderr, "icmp_code=%u\n", icmpCode);
		}
	}
	fprintf(stderr, "[egress_send_result]\n");
	fprintf(stderr, "send_attempted=yes\n");
	fprintf(stderr, "send_method=queued_to_zt_owner_task\n");
	fprintf(stderr, "decision_state=queued\n");
	fprintf(stderr, "drop_stage=none\n");
	fprintf(stderr, "drop_reason=none\n");
	fprintf(stderr, "[ZT_EGRESS_DECISION_END marker_id=%u marker=ESP32_EGRESS]\n", (unsigned)_traceMarkerId);

	if (_traceRemaining > 0) {
		--_traceRemaining;
	}
	if (_traceRemaining == 0) {
		disable();
	}
}

} // namespace ZeroTier

#ifdef ESP_PLATFORM
extern "C" void zt_debug_enableEgressTrace_ipv4(uint64_t nwid, uint32_t ip_nbo, uint32_t marker_id, uint32_t frames)
{
	ZeroTier::ESP32EgressTrace::enable(nwid, ip_nbo, marker_id, frames);
}
#endif
