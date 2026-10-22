#ifndef TESTS_MOCK_LWIP_NETIF_H
#define TESTS_MOCK_LWIP_NETIF_H

#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "lwip/pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NETIF_MAX_HWADDR_LEN 6U
#define NETIF_FLAG_UP        0x01U
#define NETIF_FLAG_BROADCAST 0x02U
#define NETIF_FLAG_LINK_UP   0x04U
#define NETIF_FLAG_ETHARP    0x08U
#define NETIF_FLAG_ETHERNET  0x10U
#define NETIF_FLAG_IGMP      0x20U
#define NETIF_FLAG_MLD6      0x40U

struct netif;

enum netif_mac_filter_action {
    NETIF_ADD_MAC_FILTER = 0,
    NETIF_DEL_MAC_FILTER = 1
};

typedef err_t (*netif_init_fn)(struct netif *netif);
typedef err_t (*netif_input_fn)(struct pbuf *p, struct netif *inp);
typedef err_t (*netif_output_fn)(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr);
typedef err_t (*netif_linkoutput_fn)(struct netif *netif, struct pbuf *p);
typedef err_t (*netif_igmp_mac_filter_fn)(struct netif *netif, const ip4_addr_t *group, enum netif_mac_filter_action action);

struct netif {
    struct netif *next;
    ip4_addr_t ip_addr;
    ip4_addr_t netmask;
    ip4_addr_t gw;
    netif_input_fn input;
    netif_output_fn output;
    netif_linkoutput_fn linkoutput;
    void *state;
    uint16_t mtu;
    uint8_t hwaddr_len;
    uint8_t hwaddr[NETIF_MAX_HWADDR_LEN];
    uint8_t flags;
    char name[2];
    uint8_t num;
};

struct netif* netif_add(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask,
                        const ip4_addr_t *gw, void *state, netif_init_fn init, netif_input_fn input);
void netif_remove(struct netif * netif);
void netif_set_addr(struct netif *netif, const ip4_addr_t *ipaddr, const ip4_addr_t *netmask, const ip4_addr_t *gw);
void netif_set_up(struct netif *netif);
void netif_set_down(struct netif *netif);
void netif_set_link_up(struct netif *netif);
void netif_set_link_down(struct netif *netif);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_NETIF_H
