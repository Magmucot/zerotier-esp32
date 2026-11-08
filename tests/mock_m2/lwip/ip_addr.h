#pragma once

#include "lwip/ip4_addr.h"
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ip4_addr_t ip_addr_t;

#define IPADDR4_INIT(u32val) { u32val }

static inline int ipaddr_aton(const char *cp, ip_addr_t *addr) {
    if (!cp || !addr) return 0;
    return inet_aton(cp, (struct in_addr*)addr);
}

#ifdef __cplusplus
}
#endif
