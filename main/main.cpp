#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "ESP32LiteAppGlue.hpp"

static const char *TAG = "zt_main";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = nullptr;
static int s_retry_num = 0;
static const int MAXIMUM_RETRY = 5;

#ifndef CONFIG_ESP_WIFI_SSID
#define CONFIG_ESP_WIFI_SSID "myssid"
#endif

#ifndef CONFIG_ESP_WIFI_PASSWORD
#define CONFIG_ESP_WIFI_PASSWORD "mypassword"
#endif

#ifndef CONFIG_ZT_NETWORK_ID
#define CONFIG_ZT_NETWORK_ID "0xdb64858fed0b2566"
#endif

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry connecting to AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to AP failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char*)wifi_config.sta.ssid, CONFIG_ESP_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, CONFIG_ESP_WIFI_PASSWORD, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished, waiting for connection...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID: %s", CONFIG_ESP_WIFI_SSID);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGW(TAG, "Failed to connect to SSID: %s", CONFIG_ESP_WIFI_SSID);
        return ESP_FAIL;
    } else {
        ESP_LOGW(TAG, "Timeout connecting to Wi-Fi AP");
        return ESP_ERR_TIMEOUT;
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting ZeroTier ESP32-Lite Node");

    ESP_LOGI(TAG, "Initializing NVS");
    if (zt_esp32_lite_nvs_init() != 0) {
        ESP_LOGE(TAG, "NVS initialization failed");
        return;
    }

    ESP_LOGI(TAG, "Initializing Wi-Fi Station");
    esp_err_t wifi_ret = wifi_init_sta();
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "Underlay Wi-Fi not connected yet, proceeding with startup");
    }

    uint64_t nwid = 0;
#ifdef CONFIG_ZT_NETWORK_ID
    nwid = strtoull(CONFIG_ZT_NETWORK_ID, NULL, 16);
#endif
    if (nwid == 0) {
        nwid = 0xdb64858fed0b2566ULL;
    }

    ESP_LOGI(TAG, "Starting ZeroTier Lite port for network 0x%016llx", (unsigned long long)nwid);
    int rc = zt_esp32_lite_start_with_namespace(nwid, "zerotier");
    ESP_LOGI(TAG, "ZeroTier Lite started rc=%d node_id=%010llx identity_ready=%d",
             rc, (unsigned long long)zt_esp32_lite_node_id(), zt_esp32_lite_identity_ready());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        zt_esp32_lite_dump_diagnostics();
    }
}
