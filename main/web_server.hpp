#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the ESP32-S3 internal temperature sensor.
 * @return ESP_OK on success, or error code.
 */
esp_err_t telemetry_temp_sensor_init(void);

/**
 * @brief Read the current internal junction temperature in degrees Celsius.
 * @return Temperature in Celsius, or -100.0f on error.
 */
float telemetry_get_temperature_celsius(void);

/**
 * @brief Initialize and start the background asynchronous ICMP ping service.
 * @param target_ip IPv4 target string (e.g. "1.1.1.1").
 * @return ESP_OK on success, or error code.
 */
esp_err_t ping_service_init(const char *target_ip);

/**
 * @brief Get the latest cached ping latency and status without blocking.
 * @param out_latency_ms Pointer to receive RTT in milliseconds.
 * @param out_online Pointer to receive online status (true if last ping succeeded).
 */
void ping_service_get_status(uint32_t *out_latency_ms, bool *out_online);

/**
 * @brief Start the dual-interface HTTP web server on 0.0.0.0:80.
 * @return ESP_OK on success, or error code.
 */
esp_err_t web_server_start(void);

/**
 * @brief Stop the HTTP web server.
 * @return ESP_OK on success, or error code.
 */
esp_err_t web_server_stop(void);

#ifdef __cplusplus
}
#endif
