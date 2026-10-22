#ifndef TESTS_MOCK_LWIP_PBUF_H
#define TESTS_MOCK_LWIP_PBUF_H

#include <stdint.h>
#include <stddef.h>
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PBUF_TRANSPORT,
    PBUF_IP,
    PBUF_LINK,
    PBUF_RAW_TX,
    PBUF_RAW
} pbuf_layer;

typedef enum {
    PBUF_RAM,
    PBUF_ROM,
    PBUF_REF,
    PBUF_POOL
} pbuf_type;

struct pbuf {
    struct pbuf* next;
    void* payload;
    uint16_t tot_len;
    uint16_t len;
    uint8_t type_internal;
    uint8_t flags;
    uint16_t ref;
};

struct pbuf* pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
uint8_t pbuf_free(struct pbuf *p);
uint16_t pbuf_copy_partial(const struct pbuf *p, void *dataptr, uint16_t len, uint16_t offset);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_PBUF_H
