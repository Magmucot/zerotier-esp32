#pragma once

#include "esp_err.h"
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* esp_ping_handle_t;

#define ESP_PING_COUNT_INFINITE 0

typedef enum {
    ESP_PING_PROF_SEQNO,
    ESP_PING_PROF_TTL,
    ESP_PING_PROF_REQUEST,
    ESP_PING_PROF_REPLY,
    ESP_PING_PROF_IPADDR,
    ESP_PING_PROF_SIZE,
    ESP_PING_PROF_TIMEGAP,
    ESP_PING_PROF_DURATION
} esp_ping_profile_t;

typedef struct {
    ip_addr_t target_addr;
    uint32_t count;
    uint32_t interval_ms;
    uint32_t timeout_ms;
    uint32_t data_size;
    uint8_t tos;
    int8_t interface;
    uint32_t task_stack_size;
    uint32_t task_prio;
} esp_ping_config_t;

#define ESP_PING_DEFAULT_CONFIG() { \
    .target_addr = IPADDR4_INIT(0), \
    .count = 5, \
    .interval_ms = 1000, \
    .timeout_ms = 1000, \
    .data_size = 64, \
    .tos = 0, \
    .interface = 0, \
    .task_stack_size = 2048, \
    .task_prio = 2 \
}

typedef struct {
    void *cb_args;
    void (*on_ping_success)(esp_ping_handle_t hdl, void *args);
    void (*on_ping_timeout)(esp_ping_handle_t hdl, void *args);
    void (*on_ping_end)(esp_ping_handle_t hdl, void *args);
} esp_ping_callbacks_t;

esp_err_t esp_ping_new_session(const esp_ping_config_t *config, const esp_ping_callbacks_t *cbs, esp_ping_handle_t *hdl_out);
esp_err_t esp_ping_start(esp_ping_handle_t hdl);
esp_err_t esp_ping_stop(esp_ping_handle_t hdl);
esp_err_t esp_ping_delete_session(esp_ping_handle_t hdl);
esp_err_t esp_ping_get_profile(esp_ping_handle_t hdl, esp_ping_profile_t profile_type, void *data, uint32_t size);

#ifdef __cplusplus
}
#endif
