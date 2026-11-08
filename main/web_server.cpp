#include "web_server.hpp"
#include "web_assets.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <atomic>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "driver/temperature_sensor.h"
#include "ping/ping_sock.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ESP32LiteAppGlue.hpp"
#include "ESP32LitePort.hpp"
#include "ESP32NetLink.hpp"
#include "ZeroTierOne.h"

static const char *TAG = "web_server";

#ifndef ESP_IMAGE_HEADER_MAGIC
#define ESP_IMAGE_HEADER_MAGIC 0xE7
#endif

#ifndef ESP_CHIP_ID_ESP32S3
#define ESP_CHIP_ID_ESP32S3 0x0009
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Declared in esp_heap_caps.h on ESP-IDF; declaration fallback for custom toolchains/mocks
size_t heap_caps_get_total_size(uint32_t caps);
#ifdef __cplusplus
}
#endif

// --- Temperature Sensor State ---
static temperature_sensor_handle_t s_temp_sensor = NULL;

esp_err_t telemetry_temp_sensor_init(void) {
    if (s_temp_sensor != NULL) {
        return ESP_OK;
    }
    temperature_sensor_config_t temp_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    esp_err_t err = temperature_sensor_install(&temp_config, &s_temp_sensor);
    if (err == ESP_OK) {
        err = temperature_sensor_enable(s_temp_sensor);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "temperature_sensor_enable failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGW(TAG, "temperature_sensor_install failed: %s", esp_err_to_name(err));
    }
    return err;
}

float telemetry_get_temperature_celsius(void) {
    if (s_temp_sensor == NULL) {
        telemetry_temp_sensor_init();
    }
    if (s_temp_sensor == NULL) {
        return -100.0f;
    }
    float celsius = 0.0f;
    esp_err_t err = temperature_sensor_get_celsius(s_temp_sensor, &celsius);
    return (err == ESP_OK) ? celsius : -100.0f;
}

// --- Asynchronous Network Ping State ---
static esp_ping_handle_t s_ping_handle = NULL;
static uint32_t s_cached_ping_latency_ms = 0;
static bool s_cached_ping_online = false;
static char s_ping_target_ip[32] = "1.1.1.1";

static void on_ping_success(esp_ping_handle_t hdl, void *args) {
    uint32_t timegap = 0;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &timegap, sizeof(timegap));
    s_cached_ping_latency_ms = timegap;
    s_cached_ping_online = true;
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args) {
    s_cached_ping_online = false;
}

esp_err_t ping_service_init(const char *target_ip) {
    if (target_ip && strlen(target_ip) > 0) {
        strncpy(s_ping_target_ip, target_ip, sizeof(s_ping_target_ip) - 1);
        s_ping_target_ip[sizeof(s_ping_target_ip) - 1] = '\0';
    }

    ip_addr_t target_addr;
    memset(&target_addr, 0, sizeof(target_addr));
    if (!ipaddr_aton(s_ping_target_ip, &target_addr)) {
        ESP_LOGE(TAG, "Invalid ping target IP: %s", s_ping_target_ip);
        return ESP_ERR_INVALID_ARG;
    }

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = ESP_PING_COUNT_INFINITE; // Continuous background probing
    ping_config.interval_ms = 3000;              // Ping every 3 seconds
    ping_config.timeout_ms = 1500;               // 1.5 second response timeout

    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = NULL
    };

    if (s_ping_handle != NULL) {
        esp_ping_stop(s_ping_handle);
        esp_ping_delete_session(s_ping_handle);
        s_ping_handle = NULL;
    }

    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &s_ping_handle);
    if (err == ESP_OK) {
        err = esp_ping_start(s_ping_handle);
        ESP_LOGI(TAG, "Ping service started targeting %s (rc=%d)", s_ping_target_ip, err);
    } else {
        ESP_LOGE(TAG, "esp_ping_new_session failed: %s", esp_err_to_name(err));
    }
    return err;
}

void ping_service_get_status(uint32_t *out_latency_ms, bool *out_online) {
    if (out_latency_ms) *out_latency_ms = s_cached_ping_latency_ms;
    if (out_online) *out_online = s_cached_ping_online;
}

// --- Deferred OTA Reboot Task ---
static void delayed_restart_task(void *param) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    ESP_LOGI(TAG, "Rebooting into newly flashed firmware slot...");
    esp_restart();
}

// --- Web Server URI Handlers ---

// GET / - Serves embedded dashboard UI
static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// GET /api/status - Live JSON Telemetry
static esp_err_t api_status_get_handler(httpd_req_t *req) {
    // 1. Uptime
    uint64_t uptime_sec = (uint64_t)(esp_timer_get_time() / 1000000ULL);

    // 2. Memory
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    // 3. Temperature
    float temp_c = telemetry_get_temperature_celsius();

    // 4. Ping latency
    uint32_t ping_rtt = 0;
    bool ping_online = false;
    ping_service_get_status(&ping_rtt, &ping_online);

    // 5. ZeroTier Status
    uint64_t node_id = zt_esp32_lite_node_id();
    char node_id_str[16] = "";
    if (node_id != 0) {
        snprintf(node_id_str, sizeof(node_id_str), "%010" PRIx64, node_id);
    }

    uint64_t nwid = 0;
    ZeroTier::ESP32LitePort *port = zt_esp32_lite_default_port();
    if (port != nullptr) {
        nwid = port->config().nwid;
    }
    char nwid_str[24] = "";
    if (nwid != 0) {
        snprintf(nwid_str, sizeof(nwid_str), "%016" PRIx64, nwid);
    }

    bool zt_online = false;
    char zt_status_str[32] = "OFFLINE";
    char zt_ip_str[32] = "";

    if (port != nullptr && port->node() != nullptr) {
        ZT_Node *node = port->node();
        ZT_NodeStatus status;
        memset(&status, 0, sizeof(status));
        ZT_Node_status(node, &status);
        zt_online = (status.online != 0);

        if (nwid != 0) {
            ZT_VirtualNetworkConfig *nwconf = ZT_Node_networkConfig(node, nwid);
            if (nwconf != nullptr) {
                switch (nwconf->status) {
                    case ZT_NETWORK_STATUS_REQUESTING_CONFIGURATION:
                        strncpy(zt_status_str, "REQUESTING_CONFIG", sizeof(zt_status_str) - 1);
                        break;
                    case ZT_NETWORK_STATUS_OK:
                        strncpy(zt_status_str, "OK", sizeof(zt_status_str) - 1);
                        break;
                    case ZT_NETWORK_STATUS_ACCESS_DENIED:
                        strncpy(zt_status_str, "ACCESS_DENIED", sizeof(zt_status_str) - 1);
                        break;
                    case ZT_NETWORK_STATUS_NOT_FOUND:
                        strncpy(zt_status_str, "NOT_FOUND", sizeof(zt_status_str) - 1);
                        break;
                    case ZT_NETWORK_STATUS_PORT_ERROR:
                        strncpy(zt_status_str, "PORT_ERROR", sizeof(zt_status_str) - 1);
                        break;
                    case ZT_NETWORK_STATUS_CLIENT_TOO_OLD:
                        strncpy(zt_status_str, "CLIENT_TOO_OLD", sizeof(zt_status_str) - 1);
                        break;
                    default:
                        strncpy(zt_status_str, "UNKNOWN", sizeof(zt_status_str) - 1);
                        break;
                }

                for (unsigned int i = 0; i < nwconf->assignedAddressCount; ++i) {
                    if (nwconf->assignedAddresses[i].ss_family == AF_INET) {
                        struct sockaddr_in *sin = (struct sockaddr_in *)&(nwconf->assignedAddresses[i]);
                        inet_ntop(AF_INET, &(sin->sin_addr), zt_ip_str, sizeof(zt_ip_str));
                        break;
                    }
                }
                ZT_Node_freeQueryResult(node, nwconf);
            }
        }
    }

    // Fallback: check ZeroTier NetLink netif if IP not in nwconf query
    if (strlen(zt_ip_str) == 0) {
        struct netif *zt_if = ZeroTier::ESP32NetLink::getInstance().zeroTierNetif();
        if (zt_if != nullptr && zt_if->ip_addr.addr != 0) {
            inet_ntop(AF_INET, &(zt_if->ip_addr), zt_ip_str, sizeof(zt_ip_str));
        }
    }

    // 6. Active Partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    const char *active_slot = running ? running->label : "ota_0";

    char json_buf[512];
    snprintf(json_buf, sizeof(json_buf),
        "{"
        "\"uptime_seconds\":%" PRIu64 ","
        "\"free_heap_internal\":%u,"
        "\"total_heap_internal\":%u,"
        "\"free_psram\":%u,"
        "\"total_psram\":%u,"
        "\"temp_celsius\":%.2f,"
        "\"ping_latency_ms\":%u,"
        "\"ping_target\":\"%s\","
        "\"ping_online\":%s,"
        "\"zt_online\":%s,"
        "\"zt_status\":\"%s\","
        "\"zt_node_id\":\"%s\","
        "\"zt_nwid\":\"%s\","
        "\"zt_ip\":\"%s\","
        "\"active_slot\":\"%s\""
        "}",
        uptime_sec,
        (unsigned int)free_heap,
        (unsigned int)total_heap,
        (unsigned int)free_psram,
        (unsigned int)total_psram,
        (double)temp_c,
        (unsigned int)ping_rtt,
        s_ping_target_ip,
        ping_online ? "true" : "false",
        zt_online ? "true" : "false",
        zt_status_str,
        node_id_str,
        nwid_str,
        zt_ip_str,
        active_slot
    );

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json_buf, HTTPD_RESP_USE_STRLEN);
}

// POST /update - OTA Firmware Binary Flashing
static std::atomic<bool> s_ota_in_progress{false};

static esp_err_t ota_post_handler(httpd_req_t *req) {
    bool expected = false;
    if (!s_ota_in_progress.compare_exchange_strong(expected, true)) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"OTA update already in progress\"}");
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        s_ota_in_progress.store(false);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"No inactive OTA partition found\"}");
    }

    int total_len = req->content_len;
    if (total_len > (int)update_partition->size) {
        s_ota_in_progress.store(false);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Firmware binary exceeds partition size\"}");
    }

    esp_ota_handle_t ota_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition,
                                  total_len > 0 ? (size_t)total_len : OTA_SIZE_UNKNOWN,
                                  &ota_handle);
    if (err != ESP_OK) {
        s_ota_in_progress.store(false);
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to initialize OTA partition\"}");
    }

    const size_t buf_size = 4096;
    char *buf = (char *)malloc(buf_size);
    if (!buf) {
        esp_ota_abort(ota_handle);
        s_ota_in_progress.store(false);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Out of memory allocating OTA buffer\"}");
    }

    int remaining = total_len;
    bool is_first_chunk = true;

    while (remaining > 0 || total_len <= 0) {
        int to_read = (total_len > 0 && remaining < (int)buf_size) ? remaining : (int)buf_size;
        int recv_bytes = httpd_req_recv(req, buf, to_read);
        if (recv_bytes <= 0) {
            if (recv_bytes == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Socket receive error during upload: %d", recv_bytes);
            free(buf);
            esp_ota_abort(ota_handle);
            s_ota_in_progress.store(false);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Network error receiving firmware stream\"}");
        }

        if (is_first_chunk) {
            is_first_chunk = false;
            if (recv_bytes < (int)sizeof(esp_image_header_t)) {
                free(buf);
                esp_ota_abort(ota_handle);
                s_ota_in_progress.store(false);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Chunk too small for firmware header\"}");
            }

            const esp_image_header_t *hdr = (const esp_image_header_t *)buf;
            if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) {
                free(buf);
                esp_ota_abort(ota_handle);
                s_ota_in_progress.store(false);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid firmware image: magic byte mismatch (expected 0xE7)\"}");
            }
            if (hdr->chip_id != ESP_CHIP_ID_ESP32S3) {
                free(buf);
                esp_ota_abort(ota_handle);
                s_ota_in_progress.store(false);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Invalid firmware image: chip ID mismatch (expected ESP32-S3)\"}");
            }
        }

        err = esp_ota_write(ota_handle, buf, recv_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            s_ota_in_progress.store(false);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Flash write error during OTA\"}");
        }

        if (total_len > 0) {
            remaining -= recv_bytes;
        } else if (recv_bytes == 0) {
            break;
        }
    }
    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        s_ota_in_progress.store(false);
        ESP_LOGE(TAG, "esp_ota_end validation failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Image validation failed (checksum mismatch)\"}");
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        s_ota_in_progress.store(false);
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"Failed to update boot partition\"}");
    }

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Update successful! Rebooting device in 2 seconds...\"}");

    // Spawn deferred restart task
    xTaskCreate(delayed_restart_task, "delayed_restart", 2048, NULL, 5, NULL);
    s_ota_in_progress.store(false);
    return ESP_OK;
}

// --- Web Server Lifecycle ---
static httpd_handle_t s_server = NULL;

esp_err_t web_server_start(void) {
    if (s_server != NULL) {
        ESP_LOGI(TAG, "Web server already active");
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting dual-interface HTTP server on 0.0.0.0:%d", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // 1. Dashboard UI
    httpd_uri_t uri_index = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = index_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &uri_index);

    // 2. Status API
    httpd_uri_t uri_status = {
        .uri       = "/api/status",
        .method    = HTTP_GET,
        .handler   = api_status_get_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &uri_status);

    // 3. OTA Update Endpoint
    httpd_uri_t uri_update = {
        .uri       = "/update",
        .method    = HTTP_POST,
        .handler   = ota_post_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &uri_update);

    // 4. OTA Update Alias
    httpd_uri_t uri_update_alias = {
        .uri       = "/api/ota",
        .method    = HTTP_POST,
        .handler   = ota_post_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(s_server, &uri_update_alias);

    ESP_LOGI(TAG, "Web server listening on port 80 across all interfaces (st0, zt0)");
    return ESP_OK;
}

esp_err_t web_server_stop(void) {
    if (s_server == NULL) {
        return ESP_OK;
    }
    esp_err_t ret = httpd_stop(s_server);
    s_server = NULL;
    s_ota_in_progress.store(false);
    return ret;
}
