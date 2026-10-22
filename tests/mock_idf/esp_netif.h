#ifndef TESTS_MOCK_ESP_NETIF_H
#define TESTS_MOCK_ESP_NETIF_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* esp_netif_t;

typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

esp_err_t esp_netif_init(void);
esp_netif_t* esp_netif_create_default_wifi_sta(void);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_ESP_NETIF_H
