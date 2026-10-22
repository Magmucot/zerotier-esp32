#ifndef TESTS_MOCK_LWIP_TCPIP_H
#define TESTS_MOCK_LWIP_TCPIP_H

#include "lwip/sys.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*tcpip_callback_fn)(void *ctx);

err_t tcpip_callback(tcpip_callback_fn function, void *ctx);
err_t tcpip_input(struct pbuf *p, struct netif *inp);

// Test probe control hooks
void mock_lwip_reset(void);
void mock_lwip_set_callback_delay(bool delay);
void mock_lwip_set_simulate_timeout(bool timeout);
bool mock_lwip_has_pending_callback(void);
void mock_lwip_execute_pending_callback(void);
tcpip_callback_fn mock_lwip_get_last_callback(void);
void* mock_lwip_get_last_callback_arg(void);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_TCPIP_H
