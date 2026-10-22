#ifndef TESTS_MOCK_LWIP_IP4_ADDR_H
#define TESTS_MOCK_LWIP_IP4_ADDR_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ip4_addr {
    uint32_t addr;
};
typedef struct ip4_addr ip4_addr_t;

#define IP4_ADDR(ipaddr, a, b, c, d) \
    (ipaddr)->addr = ((uint32_t)((a) & 0xff) | \
                     ((uint32_t)((b) & 0xff) << 8) | \
                     ((uint32_t)((c) & 0xff) << 16) | \
                     ((uint32_t)((d) & 0xff) << 24))

#define ip4_addr_copy(dest, src) ((dest).addr = (src).addr)
#define ip4_addr_set_zero(ipaddr) ((ipaddr)->addr = 0)
#define ip4_addr_isany_val(addr1) ((addr1).addr == 0)

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_IP4_ADDR_H
