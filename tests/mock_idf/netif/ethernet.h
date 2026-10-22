#ifndef TESTS_MOCK_NETIF_ETHERNET_H
#define TESTS_MOCK_NETIF_ETHERNET_H

#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

err_t ethernet_input(struct pbuf *p, struct netif *netif);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_NETIF_ETHERNET_H
