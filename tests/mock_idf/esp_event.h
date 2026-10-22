#ifndef TESTS_MOCK_ESP_EVENT_H
#define TESTS_MOCK_ESP_EVENT_H

#include "esp_err.h"
#include "esp_netif.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const char* esp_event_base_t;
typedef void* esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void* event_handler_arg,
                                    esp_event_base_t event_base,
                                    int32_t event_id,
                                    void* event_data);

#define ESP_EVENT_ANY_BASE NULL
#define ESP_EVENT_ANY_ID   (-1)

extern const esp_event_base_t WIFI_EVENT;
extern const esp_event_base_t IP_EVENT;

enum {
    WIFI_EVENT_STA_START = 0,
    WIFI_EVENT_STA_CONNECTED,
    WIFI_EVENT_STA_DISCONNECTED,
    WIFI_EVENT_STA_STOP
};

enum {
    IP_EVENT_STA_GOT_IP = 0,
    IP_EVENT_STA_LOST_IP
};

typedef struct {
    esp_netif_t* esp_netif;
} ip_event_got_ip_t;

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void* event_handler_arg,
                                              esp_event_handler_instance_t* instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t instance);
esp_err_t esp_event_post(esp_event_base_t event_base,
                         int32_t event_id,
                         void* event_data,
                         size_t event_data_size,
                         uint32_t ticks_to_wait);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_ESP_EVENT_H
