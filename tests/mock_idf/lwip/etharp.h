#ifndef TESTS_MOCK_LWIP_ETHARP_H
#define TESTS_MOCK_LWIP_ETHARP_H

#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

err_t etharp_output(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_ETHARP_H
