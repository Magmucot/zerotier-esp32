// ZtRouteDebug.hpp — shared managed-route decision debug facility
// Used by Linux daemon (via local API) and ESP32/QEMU (via GDB-callable wrapper).
// Do not add noisy prints here. This is an explicit debug trigger only.

#ifndef ZT_ROUTE_DEBUG_HPP
#define ZT_ROUTE_DEBUG_HPP

#include <string.h>

// Marker IDs — identical on Linux and ESP32/QEMU.
typedef enum ZtRouteDebugMarker {
    ZT_RDM_AFTER_NETWORK_CONFIG_OK      =  1,
    ZT_RDM_BEFORE_FIRST_PING            =  2,
    ZT_RDM_AFTER_FIRST_PING_FAIL        =  3,
    ZT_RDM_AFTER_PEER_VISIBLE           =  4,
    ZT_RDM_AFTER_SUCCESSFUL_PING        =  5,
    ZT_RDM_QEMU_AFTER_NETIF_UP          = 10,
    ZT_RDM_QEMU_ON_NO_PEER              = 11,
    ZT_RDM_QEMU_ON_NO_PATH              = 12,
    ZT_RDM_QEMU_ON_DROP                 = 13,
    ZT_RDM_EGRESS_ARP_BEFORE_PEER       = 20,
    ZT_RDM_EGRESS_ARP_AFTER_PEER        = 21,
    ZT_RDM_EGRESS_IPV4_ICMP_BEFORE_PEER = 22,
    ZT_RDM_EGRESS_IPV4_ICMP_AFTER_PEER  = 23,
    ZT_RDM_EGRESS_FRAME_DROPPED         = 24,
    ZT_RDM_EGRESS_FRAME_SENT            = 25,
    ZT_RDM_MANUAL_GDB_TRIGGER           = 99,
} ZtRouteDebugMarker;

static inline const char* ZtRouteDebugMarkerName(int id)
{
    switch (id) {
        case  1: return "AFTER_NETWORK_CONFIG_OK";
        case  2: return "BEFORE_FIRST_PING";
        case  3: return "AFTER_FIRST_PING_FAIL";
        case  4: return "AFTER_PEER_VISIBLE";
        case  5: return "AFTER_SUCCESSFUL_PING";
        case 10: return "QEMU_AFTER_NETIF_UP";
        case 11: return "QEMU_ON_NO_PEER";
        case 12: return "QEMU_ON_NO_PATH";
        case 13: return "QEMU_ON_DROP";
        case 20: return "EGRESS_ARP_BEFORE_PEER";
        case 21: return "EGRESS_ARP_AFTER_PEER";
        case 22: return "EGRESS_IPV4_ICMP_BEFORE_PEER";
        case 23: return "EGRESS_IPV4_ICMP_AFTER_PEER";
        case 24: return "EGRESS_FRAME_DROPPED";
        case 25: return "EGRESS_FRAME_SENT";
        case 99: return "MANUAL_GDB_TRIGGER";
        default: return "UNKNOWN";
    }
}

static inline int ZtRouteDebugMarkerByName(const char* name)
{
    if (!name) return -1;
    if (strcmp(name, "AFTER_NETWORK_CONFIG_OK") == 0)       return  1;
    if (strcmp(name, "BEFORE_FIRST_PING") == 0)              return  2;
    if (strcmp(name, "AFTER_FIRST_PING_FAIL") == 0)          return  3;
    if (strcmp(name, "AFTER_PEER_VISIBLE") == 0)              return  4;
    if (strcmp(name, "AFTER_SUCCESSFUL_PING") == 0)           return  5;
    if (strcmp(name, "QEMU_AFTER_NETIF_UP") == 0)             return 10;
    if (strcmp(name, "QEMU_ON_NO_PEER") == 0)                 return 11;
    if (strcmp(name, "QEMU_ON_NO_PATH") == 0)                 return 12;
    if (strcmp(name, "QEMU_ON_DROP") == 0)                    return 13;
    if (strcmp(name, "EGRESS_ARP_BEFORE_PEER") == 0)          return 20;
    if (strcmp(name, "EGRESS_ARP_AFTER_PEER") == 0)           return 21;
    if (strcmp(name, "EGRESS_IPV4_ICMP_BEFORE_PEER") == 0)    return 22;
    if (strcmp(name, "EGRESS_IPV4_ICMP_AFTER_PEER") == 0)     return 23;
    if (strcmp(name, "EGRESS_FRAME_DROPPED") == 0)             return 24;
    if (strcmp(name, "EGRESS_FRAME_SENT") == 0)                return 25;
    if (strcmp(name, "MANUAL_GDB_TRIGGER") == 0)               return 99;
    return -1;
}

// Resolve marker_id from either a numeric string or a name string.
static inline int ZtRouteDebugMarkerResolve(const char* s)
{
    if (!s) return -1;
    // Try numeric first.
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (end && *end == '\0' && end != s) {
        return (int)v;
    }
    return ZtRouteDebugMarkerByName(s);
}

#endif // ZT_ROUTE_DEBUG_HPP
