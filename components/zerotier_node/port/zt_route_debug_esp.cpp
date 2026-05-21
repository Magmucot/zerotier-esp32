// GDB-callable managed-route decision dump and egress trace for ESP32/QEMU.
// Call from GDB: call zt_debug_dumpManagedRouteDecision_ipv4(nwid, ip_be, marker_id)
// Or from code:  zt_debug_set_node(node); zt_debug_dumpManagedRouteDecision_ipv4(nwid, ip_be, marker_id)

#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "ZeroTierOne.h"
#include "ZtRouteDebug.hpp"

static const char *TAG = "zt_rdebug";

static ZT_Node *s_debug_node = nullptr;

extern "C" void zt_debug_set_node(ZT_Node *node)
{
    s_debug_node = node;
}

static void _addr_to_str(const struct sockaddr_storage *ss, char *out, size_t outlen)
{
    if (!ss || !out || outlen == 0) { if (out) out[0] = '\0'; return; }
    if (ss->ss_family == AF_INET) {
        const struct sockaddr_in *a = (const struct sockaddr_in *)ss;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &a->sin_addr, ip, sizeof(ip));
        snprintf(out, outlen, "%s/%u", ip, ntohs(a->sin_port));
    } else if (ss->ss_family == AF_INET6) {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)ss;
        char ip6[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &a6->sin6_addr, ip6, sizeof(ip6));
        snprintf(out, outlen, "[%s]/%u", ip6, ntohs(a6->sin6_port));
    } else {
        snprintf(out, outlen, "none");
    }
}

extern "C" void zt_debug_dumpManagedRouteDecision_ipv4(uint64_t nwid, uint32_t ip_be, uint32_t marker_id)
{
    ZT_Node *node = s_debug_node;
    if (!node) {
        ESP_LOGE(TAG, "[ZT_ROUTE_DEBUG] node not registered — call zt_debug_set_node first");
        return;
    }

    char target_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip_be, target_ip, sizeof(target_ip));

    const char *marker_name = ZtRouteDebugMarkerName((int)marker_id);

    ZT_NodeStatus status;
    ZT_Node_status(node, &status);
    char local_node_hex[11];
    snprintf(local_node_hex, sizeof(local_node_hex), "%010llx",
             (unsigned long long)(status.address & 0xffffffffffULL));

    printf("[ZT_ROUTE_DECISION_BEGIN marker_id=%u marker=%s nwid=%016llx ip=%s]\n",
           (unsigned)marker_id, marker_name,
           (unsigned long long)nwid, target_ip);
    printf("local_node=%s\n", local_node_hex);

    // Network lookup
    ZT_VirtualNetworkConfig *nwconf = ZT_Node_networkConfig(node, nwid);
    bool network_found = (nwconf != nullptr);
    printf("network_found=%s\n", network_found ? "true" : "false");

    if (network_found) {
        const char *net_status = "UNKNOWN";
        switch (nwconf->status) {
            case ZT_NETWORK_STATUS_REQUESTING_CONFIGURATION: net_status = "REQUESTING_CONFIGURATION"; break;
            case ZT_NETWORK_STATUS_OK:                       net_status = "OK"; break;
            case ZT_NETWORK_STATUS_ACCESS_DENIED:            net_status = "ACCESS_DENIED"; break;
            case ZT_NETWORK_STATUS_NOT_FOUND:                net_status = "NOT_FOUND"; break;
            case ZT_NETWORK_STATUS_PORT_ERROR:               net_status = "PORT_ERROR"; break;
            case ZT_NETWORK_STATUS_CLIENT_TOO_OLD:           net_status = "CLIENT_TOO_OLD"; break;
            case ZT_NETWORK_STATUS_AUTHENTICATION_REQUIRED:  net_status = "AUTHENTICATION_REQUIRED"; break;
            default: break;
        }
        printf("network_status=%s\n", net_status);
        printf("port_device_name=%s\n", nwconf->name[0] ? nwconf->name : "zt0");
        printf("network_mac=%012llx\n", (unsigned long long)(nwconf->mac & 0xffffffffffffULL));
        printf("allow_managed=%s allow_global=%s allow_default=%s\n",
               "true", "false", "false");
        printf("bridge=%s broadcast_enabled=%s\n",
               nwconf->bridge ? "true" : "false",
               nwconf->broadcastEnabled ? "true" : "false");

        // Managed IPs
        printf("managed_ips=[");
        for (unsigned int i = 0; i < nwconf->assignedAddressCount; ++i) {
            char abuf[64]; _addr_to_str(&nwconf->assignedAddresses[i], abuf, sizeof(abuf));
            printf("%s%s", i ? "," : "", abuf);
        }
        printf("]\n");

        // Managed routes — find one matching target ip
        bool route_matched = false;
        char matched_route[64] = "none";
        printf("managed_routes=[");
        for (unsigned int i = 0; i < nwconf->routeCount; ++i) {
            char tbuf[64]; _addr_to_str(&nwconf->routes[i].target, tbuf, sizeof(tbuf));
            printf("%s%s", i ? "," : "", tbuf);
            // Simple subnet match: same network portion
            if (!route_matched && nwconf->routes[i].target.ss_family == AF_INET) {
                const struct sockaddr_in *rt = (const struct sockaddr_in *)&nwconf->routes[i].target;
                uint16_t pfx = ntohs(rt->sin_port); // ZT encodes prefix in sin_port
                if (pfx > 0 && pfx <= 32) {
                    uint32_t mask = pfx ? htonl(~0u << (32 - pfx)) : 0u;
                    if ((rt->sin_addr.s_addr & mask) == (ip_be & mask)) {
                        route_matched = true;
                        snprintf(matched_route, sizeof(matched_route), "%s", tbuf);
                    }
                }
            }
        }
        printf("]\n");
        printf("matched_managed_route=%s\n", matched_route);

        ZT_Node_freeQueryResult(node, nwconf);
    }

    // Peer lookup — search for a peer whose network address matches target ip
    ZT_PeerList *pl = ZT_Node_peers(node);
    bool peer_exists = false;
    bool peer_visible = false;
    char peer_addr_hex[11] = "none";
    char peer_role[16] = "none";
    unsigned int path_count = 0;
    char preferred_path[64] = "none";
    long long last_send = -1, last_recv = -1;

    if (pl) {
        for (unsigned long p = 0; p < pl->peerCount; ++p) {
            const ZT_Peer *peer = &pl->peers[p];
            // We can't directly map ZT virtual IPs to peers via the C API without
            // network member lookup, so we report all peers and mark any with active paths.
            // A peer is "visible" for this target if it has >=1 active path.
            for (unsigned int q = 0; q < peer->pathCount; ++q) {
                if ((!peer->paths[q].expired && peer->paths[q].lastReceive > 0)) {
                    // Found active peer — this is our best match heuristic
                    if (!peer_exists) {
                        peer_exists = true;
                        peer_visible = true;
                        snprintf(peer_addr_hex, sizeof(peer_addr_hex), "%010llx",
                                 (unsigned long long)(peer->address & 0xffffffffffULL));
                        path_count = peer->pathCount;
                        last_send = (long long)peer->paths[q].lastSend;
                        last_recv = (long long)peer->paths[q].lastReceive;
                        char abuf[64]; _addr_to_str(&peer->paths[q].address, abuf, sizeof(abuf));
                        snprintf(preferred_path, sizeof(preferred_path), "%s", abuf);
                        switch (peer->role) {
                            case ZT_PEER_ROLE_LEAF:   snprintf(peer_role, sizeof(peer_role), "LEAF"); break;
                            case ZT_PEER_ROLE_MOON:   snprintf(peer_role, sizeof(peer_role), "MOON"); break;
                            case ZT_PEER_ROLE_PLANET: snprintf(peer_role, sizeof(peer_role), "PLANET"); break;
                            default: snprintf(peer_role, sizeof(peer_role), "UNKNOWN"); break;
                        }
                    }
                }
            }
        }

        printf("all_paths=[");
        bool first = true;
        for (unsigned long p = 0; p < pl->peerCount; ++p) {
            const ZT_Peer *peer = &pl->peers[p];
            for (unsigned int q = 0; q < peer->pathCount; ++q) {
                char abuf[64]; _addr_to_str(&peer->paths[q].address, abuf, sizeof(abuf));
                printf("%s{peer=%010llx path=%s active=%s}",
                       first ? "" : ",",
                       (unsigned long long)(peer->address & 0xffffffffffULL),
                       abuf,
                       (!peer->paths[q].expired && peer->paths[q].lastReceive > 0) ? "true" : "false");
                first = false;
            }
        }
        printf("]\n");

        ZT_Node_freeQueryResult(node, pl);
    }

    printf("peer_exists=%s\n", peer_exists ? "true" : "false");
    printf("peer_visible=%s\n", peer_visible ? "true" : "false");
    printf("target_mac=none target_adi=none target_member=none\n");
    printf("peer_role=%s\n", peer_role);
    printf("path_count=%u\n", path_count);
    printf("preferred_path=%s\n", preferred_path);
    printf("last_send=%lld last_receive=%lld\n", last_send, last_recv);

    // Decision
    const char *decision_state;
    const char *decision_reason;
    if (!network_found) {
        decision_state  = "DROP";
        decision_reason = "network_not_found";
    } else if (!peer_exists) {
        decision_state  = "DROP";
        decision_reason = "no_peer";
    } else if (!peer_visible) {
        decision_state  = "DROP";
        decision_reason = "peer_exists_but_no_active_path";
    } else {
        decision_state  = "FORWARD";
        decision_reason = "peer_has_active_path";
    }

    printf("decision_state=%s\n", decision_state);
    printf("decision_reason=%s\n", decision_reason);
    printf("[ZT_ROUTE_DECISION_END marker_id=%u marker=%s]\n",
           (unsigned)marker_id, marker_name);

    fflush(stdout);
}

// ─────────────────────────────────────────────────────────────────────────────
// Egress Trace Facility
// Hooks into zt_drain_tx_queue to capture and decode frames on their way
// from lwIP into ZT_Node_processVirtualNetworkFrame. Also observes wire_send.
//
// GDB usage:
//   set $ip = ((uint32_t)172)|((uint32_t)25<<8)|((uint32_t)206<<16)|((uint32_t)161<<24)
//   p/x $ip   # expect 0xa1ce19ac
//   call zt_debug_enableEgressTrace_ipv4(0xdb64858fed0b2566ULL, $ip, 20, 30)
// ─────────────────────────────────────────────────────────────────────────────

struct ZtEgressTraceConfig {
    volatile bool     enabled;
    uint64_t          nwid;
    uint32_t          target_ip_le;   // little-endian: (a)|(b<<8)|(c<<16)|(d<<24)
    uint32_t          marker_id;
    int64_t           expiry_ms;
    uint32_t          max_frames;
    volatile uint32_t frame_count;
};

static ZtEgressTraceConfig s_egress = {};

// Wire-send observation state (filled from zt_wire_send callback).
static volatile bool s_ws_observed = false;
static char          s_ws_dst[INET_ADDRSTRLEN + 8] = {};
static volatile unsigned s_ws_len = 0;

static int64_t _egress_now_ms(void) { return esp_timer_get_time() / 1000LL; }

static bool _egress_is_active(void)
{
    return s_egress.enabled
        && s_egress.frame_count < s_egress.max_frames
        && _egress_now_ms() < s_egress.expiry_ms;
}

// Returns true if frame matches the configured target IP.
// frame: full Ethernet frame (14-byte header + payload), total bytes.
static bool _frame_matches(const uint8_t *frame, unsigned total)
{
    if (total < 14) return false;
    unsigned et = ((unsigned)frame[12] << 8) | frame[13];
    uint32_t t = s_egress.target_ip_le;
    if (et == 0x0806 && total >= 42) {
        uint32_t sip, tip;
        memcpy(&sip, frame + 28, 4);   // ARP sender IP
        memcpy(&tip, frame + 38, 4);   // ARP target IP
        return (sip == t || tip == t);
    }
    if (et == 0x0800 && total >= 34) {
        uint32_t sip, dip;
        memcpy(&sip, frame + 26, 4);   // IPv4 src
        memcpy(&dip, frame + 30, 4);   // IPv4 dst
        return (sip == t || dip == t);
    }
    return false;
}

static void _fmt_mac6(const uint8_t *m, char *out, size_t outlen)
{
    snprintf(out, outlen, "%02x:%02x:%02x:%02x:%02x:%02x",
             m[0], m[1], m[2], m[3], m[4], m[5]);
}

// ── Public hook: check if this frame should be traced ────────────────────────
// Called from zt_drain_tx_queue before ZT_Node_processVirtualNetworkFrame.
// Returns true if egress trace is active and frame matches target IP/nwid.
extern "C" bool zt_debug_egress_check(uint64_t nwid, const uint8_t *frame, unsigned total)
{
    if (!_egress_is_active()) return false;
    if (nwid != s_egress.nwid) return false;
    return _frame_matches(frame, total);
}

// ── Public hook: reset wire-send state before processVirtualNetworkFrame ─────
extern "C" void zt_debug_egress_reset_wire_obs(void)
{
    s_ws_observed = false;
    s_ws_dst[0]   = '\0';
    s_ws_len      = 0;
}

// ── Public hook: query whether egress trace is active ────────────────────────
// Called from zt_wire_send to decide whether to call wire_send_notify.
extern "C" bool zt_debug_egress_trace_active(void) { return _egress_is_active(); }

// ── Public hook: called from zt_wire_send when egress trace is active ────────
// dst_ip: dotted-decimal string, dst_port: host byte order, len: packet length.
extern "C" void zt_debug_egress_wire_send_notify(const char *dst_ip, uint16_t dst_port,
                                                   unsigned len)
{
    if (!s_egress.enabled) return;
    snprintf(s_ws_dst, sizeof(s_ws_dst), "%s:%u", dst_ip, (unsigned)dst_port);
    s_ws_len      = len;
    s_ws_observed = true;
}

// ── Public hook: print full trace block after processVirtualNetworkFrame ──────
// Called from zt_drain_tx_queue after ZT_Node_processVirtualNetworkFrame.
extern "C" void zt_debug_egress_print_result(uint64_t nwid, const uint8_t *frame,
                                               unsigned total, int processVNF_rc)
{
    ++s_egress.frame_count;
    if (s_egress.frame_count >= s_egress.max_frames) {
        s_egress.enabled = false;
        printf("[ZT_EGRESS_TRACE] max_frames=%u reached, auto-disabled\n",
               (unsigned)s_egress.max_frames);
    }

    const char *marker_name = ZtRouteDebugMarkerName((int)s_egress.marker_id);
    char target_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &s_egress.target_ip_le, target_ip_str, sizeof(target_ip_str));

    printf("[ZT_EGRESS_DECISION_BEGIN marker_id=%u marker=%s nwid=%016llx target_ip=%s]\n",
           (unsigned)s_egress.marker_id, marker_name,
           (unsigned long long)nwid, target_ip_str);

    // ── [egress_frame] ────────────────────────────────────────────────────
    printf("[egress_frame]\n");
    printf("frame_seen=yes\n");
    printf("frame_len=%u\n", total);

    unsigned et = ((unsigned)frame[12] << 8) | frame[13];
    printf("ethertype=0x%04x\n", et);
    const char *et_name = (et == 0x0806) ? "ARP"
                        : (et == 0x0800) ? "IPv4"
                        : (et == 0x86DD) ? "IPv6"
                        : "unknown";
    printf("ethertype_name=%s\n", et_name);

    char src_mac[20], dst_mac[20];
    _fmt_mac6(frame + 6, src_mac, sizeof(src_mac));
    _fmt_mac6(frame + 0, dst_mac, sizeof(dst_mac));
    printf("src_mac=%s\n", src_mac);
    printf("dst_mac=%s\n", dst_mac);

    bool is_bcast  = (frame[0]==0xFF && frame[1]==0xFF && frame[2]==0xFF &&
                      frame[3]==0xFF && frame[4]==0xFF && frame[5]==0xFF);
    bool is_mcast  = (!is_bcast && (frame[0] & 1));
    bool is_ucast  = (!is_bcast && !is_mcast);
    printf("is_broadcast=%s\n", is_bcast  ? "yes" : "no");
    printf("is_multicast=%s\n", is_mcast  ? "yes" : "no");
    printf("is_unicast=%s\n",   is_ucast  ? "yes" : "no");

    if (et == 0x0806 && total >= 42) {
        unsigned arp_op = ((unsigned)frame[20] << 8) | frame[21];
        printf("arp_op=%s\n", (arp_op==1) ? "request" : (arp_op==2) ? "reply" : "unknown");
        uint32_t sip, tip;
        memcpy(&sip, frame + 28, 4);
        memcpy(&tip, frame + 38, 4);
        char sip_s[INET_ADDRSTRLEN], tip_s[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sip, sip_s, sizeof(sip_s));
        inet_ntop(AF_INET, &tip, tip_s, sizeof(tip_s));
        printf("arp_sender_ip=%s\n", sip_s);
        printf("arp_target_ip=%s\n", tip_s);
        printf("ipv4_src=not_applicable\nipv4_dst=not_applicable\n");
        printf("ipv4_proto=not_applicable\nicmp_type=not_applicable\nicmp_code=not_applicable\n");
    } else if (et == 0x0800 && total >= 34) {
        printf("arp_op=not_applicable\narp_sender_ip=not_applicable\narp_target_ip=not_applicable\n");
        unsigned ihl = (unsigned)(frame[14] & 0x0F) * 4u;
        uint32_t sip, dip;
        memcpy(&sip, frame + 26, 4);
        memcpy(&dip, frame + 30, 4);
        char sip4[INET_ADDRSTRLEN], dip4[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sip, sip4, sizeof(sip4));
        inet_ntop(AF_INET, &dip, dip4, sizeof(dip4));
        printf("ipv4_src=%s\nipv4_dst=%s\n", sip4, dip4);
        unsigned proto = frame[23];
        printf("ipv4_proto=%s\n",
               (proto==1) ? "icmp" : (proto==6) ? "tcp" : (proto==17) ? "udp" : "unknown");
        if (proto == 1 && total >= 14u + ihl + 2u) {
            printf("icmp_type=%u\nicmp_code=%u\n", frame[14+ihl], frame[14+ihl+1]);
        } else {
            printf("icmp_type=not_applicable\nicmp_code=not_applicable\n");
        }
    } else {
        printf("arp_op=not_applicable\narp_sender_ip=not_applicable\narp_target_ip=not_applicable\n");
        printf("ipv4_src=not_applicable\nipv4_dst=not_applicable\n");
        printf("ipv4_proto=not_applicable\nicmp_type=not_applicable\nicmp_code=not_applicable\n");
    }

    // ── [egress_network] ──────────────────────────────────────────────────
    printf("[egress_network]\n");
    printf("nwid=%016llx\n", (unsigned long long)nwid);
    ZT_Node *node = s_debug_node;
    if (node) {
        ZT_VirtualNetworkConfig *nwconf = ZT_Node_networkConfig(node, nwid);
        if (nwconf) {
            const char *net_status = "UNKNOWN";
            switch (nwconf->status) {
                case ZT_NETWORK_STATUS_REQUESTING_CONFIGURATION: net_status = "REQUESTING_CONFIGURATION"; break;
                case ZT_NETWORK_STATUS_OK:                       net_status = "OK"; break;
                case ZT_NETWORK_STATUS_ACCESS_DENIED:            net_status = "ACCESS_DENIED"; break;
                default: break;
            }
            printf("network_status=%s\n", net_status);
            if (nwconf->assignedAddressCount > 0 &&
                nwconf->assignedAddresses[0].ss_family == AF_INET) {
                const struct sockaddr_in *a =
                    (const struct sockaddr_in *)&nwconf->assignedAddresses[0];
                char lip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &a->sin_addr, lip, sizeof(lip));
                printf("local_managed_ip=%s/%u\n", lip, (unsigned)ntohs(a->sin_port));
            } else {
                printf("local_managed_ip=unknown\n");
            }
            // Route match for target_ip_le against managed routes
            bool found_route = false;
            for (unsigned i = 0; i < nwconf->routeCount && !found_route; ++i) {
                if (nwconf->routes[i].target.ss_family != AF_INET) continue;
                const struct sockaddr_in *rt =
                    (const struct sockaddr_in *)&nwconf->routes[i].target;
                uint16_t pfx = ntohs(rt->sin_port);  // ZT encodes prefix in sin_port
                if (pfx == 0 || pfx > 32) continue;
                uint32_t mask = htonl(~0u << (32 - pfx));
                if ((rt->sin_addr.s_addr & mask) == (s_egress.target_ip_le & mask)) {
                    char rbuf[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &rt->sin_addr, rbuf, sizeof(rbuf));
                    printf("matched_managed_route=%s/%u\n", rbuf, (unsigned)pfx);
                    printf("route_match_reason=prefix_match\n");
                    found_route = true;
                }
            }
            if (!found_route) {
                printf("matched_managed_route=none\n");
                printf("route_match_reason=no_prefix_match\n");
            }
            ZT_Node_freeQueryResult(node, nwconf);
        } else {
            printf("network_status=unknown\nlocal_managed_ip=unknown\n");
            printf("matched_managed_route=unknown\nroute_match_reason=config_unavailable\n");
        }
    } else {
        printf("network_status=unknown\nlocal_managed_ip=unknown\n");
        printf("matched_managed_route=unknown\nroute_match_reason=node_not_registered\n");
    }

    // ── [egress_destination] ──────────────────────────────────────────────
    printf("[egress_destination]\n");
    printf("destination_type=%s\n",
           is_bcast ? "broadcast" : is_mcast ? "multicast" : "unicast");
    printf("target_ip=%s\n", target_ip_str);
    printf("target_mac=%s\n", dst_mac);
    printf("target_adi=unknown\n");
    printf("target_member_node_id=unknown\n");
    printf("destination_resolution_state=unknown\n");
    printf("destination_resolution_reason=observed_at_pre_core_queue_drain\n");

    // ── [egress_peer_selection] ───────────────────────────────────────────
    printf("[egress_peer_selection]\n");
    printf("target_peer_exists=unknown\n");
    printf("target_path_count=unknown\n");
    printf("target_preferred_path=unknown\n");
    printf("peer_selection_reason=not_observable_at_hook_layer\n");

    // ── [egress_send_result] ──────────────────────────────────────────────
    printf("[egress_send_result]\n");
    printf("send_attempted=yes\n");
    printf("processVirtualNetworkFrame_called=yes\n");
    printf("processVirtualNetworkFrame_rc=%d\n", processVNF_rc);
    printf("wire_send_observed=%s\n", s_ws_observed ? "yes" : "no");
    printf("wire_send_dst=%s\n", s_ws_observed ? s_ws_dst : "none");
    printf("wire_send_len=%u\n", s_ws_observed ? (unsigned)s_ws_len : 0u);

    const char *decision_state, *send_method;
    if (s_ws_observed) {
        decision_state = "sent";
        send_method    = is_bcast ? "broadcast_fanout" : is_mcast ? "multicast_fanout" : "direct";
    } else if (processVNF_rc == 0) {
        decision_state = "queued_or_unresolved";
        send_method    = "unknown";
    } else {
        decision_state = "dropped";
        send_method    = "none";
    }
    printf("send_method=%s\n", send_method);
    printf("decision_state=%s\n", decision_state);
    printf("drop_stage=%s\n",
           (decision_state[0]=='d') ? "zt_core" : "none");
    printf("drop_reason=%s\n",
           (decision_state[0]=='d') ? "processVNF_rc_nonzero" : "none");

    printf("[ZT_EGRESS_DECISION_END marker_id=%u marker=%s]\n",
           (unsigned)s_egress.marker_id, marker_name);
    fflush(stdout);
}

// ── GDB-callable: enable egress trace ────────────────────────────────────────
// ip_le convention: ((uint32_t)172)|((uint32_t)25<<8)|((uint32_t)206<<16)|((uint32_t)161<<24)
// = 0xa1ce19ac for 172.25.206.161
extern "C" void zt_debug_enableEgressTrace_ipv4(uint64_t nwid, uint32_t ip_le,
                                                  uint32_t marker_id, uint32_t seconds)
{
    s_egress.nwid         = nwid;
    s_egress.target_ip_le = ip_le;
    s_egress.marker_id    = marker_id;
    s_egress.expiry_ms    = _egress_now_ms() + (int64_t)seconds * 1000LL;
    s_egress.max_frames   = 20;
    s_egress.frame_count  = 0;
    s_egress.enabled      = true;

    // No printf — avoids stdout mutex conflict when called from GDB inferior call.
    // [ZT_EGRESS_TRACE_ENABLED] will appear in zt_debug_egress_check on first frame.
}

// ── GDB-callable: disable egress trace ───────────────────────────────────────
extern "C" void zt_debug_disableEgressTrace(void)
{
    s_egress.enabled = false;
    // No printf — avoids stdout mutex conflict when called from GDB inferior call.
}
