#ifndef TESTS_MOCK_ESP_WIFI_H
#define TESTS_MOCK_ESP_WIFI_H

#include "esp_err.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MODE_NULL = 0,
    WIFI_MODE_STA,
    WIFI_MODE_AP,
    WIFI_MODE_APSTA,
    WIFI_MODE_MAX
} wifi_mode_t;

typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t scan_method;
    uint8_t bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint16_t listen_interval;
    uint8_t sort_method;
    uint8_t threshold;
    uint8_t pmf_cfg;
    uint32_t rm_enabled:1;
    uint32_t btm_enabled:1;
    uint32_t mbo_enabled:1;
    uint32_t ft_enabled:1;
    uint32_t reserved:28;
} wifi_sta_config_t;

typedef union {
    wifi_sta_config_t sta;
} wifi_config_t;

typedef struct {
    int dummy;
} wifi_init_config_t;

#define WIFI_INIT_CONFIG_DEFAULT() { 0 }

esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_mode(wifi_mode_t mode);
esp_err_t esp_wifi_set_config(uint32_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);

#define ESP_IF_WIFI_STA 0

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_ESP_WIFI_H
