#include "web_server.hpp"
#include "web_assets.hpp"

#include "driver/temperature_sensor.h"
#include "ping/ping_sock.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>

// Declarations from mock_m2.cpp
extern httpd_uri_func_t mock_find_handler(const char *uri, httpd_method_t method);
extern std::string g_last_resp_status;
extern std::string g_last_resp_type;
extern std::string g_last_resp_body;
extern const char *g_req_stream_data;
extern size_t g_req_stream_pos;
extern size_t g_req_stream_total;
extern void mock_set_temperature_celsius(float c);
extern bool mock_ota_was_switched_to(const char *label);
extern size_t mock_ota_bytes_written();

static void test_temperature_sensor() {
    std::printf("[TEST] Verifying temperature sensor telemetry...\n");
    esp_err_t err = telemetry_temp_sensor_init();
    assert(err == ESP_OK);

    mock_set_temperature_celsius(38.75f);
    float t = telemetry_get_temperature_celsius();
    assert(t >= 38.7f && t <= 38.8f);
    std::printf("    [PASS] Temperature sensor correctly initialized and read: %.2f C\n", t);
}

static void test_ping_service() {
    std::printf("[TEST] Verifying asynchronous ping service...\n");
    esp_err_t err = ping_service_init("8.8.8.8");
    assert(err == ESP_OK);

    uint32_t rtt = 0;
    bool online = false;
    ping_service_get_status(&rtt, &online);
    assert(online == true);
    assert(rtt == 18);
    std::printf("    [PASS] Ping service correctly started and cached RTT: %u ms, online: %d\n", rtt, (int)online);
}

static void test_web_server_routes() {
    std::printf("[TEST] Verifying web server endpoints and responses...\n");
    esp_err_t err = web_server_start();
    assert(err == ESP_OK);

    // 1. Test GET /
    auto get_index = mock_find_handler("/", HTTP_GET);
    assert(get_index != nullptr);

    httpd_req_t req_index = {};
    req_index.uri = "/";
    req_index.method = HTTP_GET;
    g_last_resp_status = "200 OK";
    g_last_resp_type = "";
    g_last_resp_body = "";

    err = get_index(&req_index);
    assert(err == ESP_OK);
    assert(g_last_resp_type == "text/html");
    assert(g_last_resp_body.find("ZeroTier ESP32-S3") != std::string::npos);
    assert(g_last_resp_body.find("System Telemetry") != std::string::npos);
    assert(g_last_resp_body.find("OTA Firmware Update") != std::string::npos);
    std::printf("    [PASS] GET / returns embedded HTML dashboard (%zu bytes)\n", g_last_resp_body.size());

    // 2. Test GET /api/status
    auto get_status = mock_find_handler("/api/status", HTTP_GET);
    assert(get_status != nullptr);

    httpd_req_t req_status = {};
    req_status.uri = "/api/status";
    req_status.method = HTTP_GET;
    g_last_resp_status = "200 OK";
    g_last_resp_type = "";
    g_last_resp_body = "";

    err = get_status(&req_status);
    assert(err == ESP_OK);
    assert(g_last_resp_type == "application/json");
    assert(g_last_resp_body.find("\"uptime_seconds\":") != std::string::npos);
    assert(g_last_resp_body.find("\"free_heap_internal\":") != std::string::npos);
    assert(g_last_resp_body.find("\"free_psram\":") != std::string::npos);
    assert(g_last_resp_body.find("\"temp_celsius\":") != std::string::npos);
    assert(g_last_resp_body.find("\"ping_latency_ms\":") != std::string::npos);
    assert(g_last_resp_body.find("\"zt_online\":") != std::string::npos);
    assert(g_last_resp_body.find("\"active_slot\":\"ota_0\"") != std::string::npos);
    std::printf("    [PASS] GET /api/status returns telemetry JSON: %s\n", g_last_resp_body.c_str());

    // 3. Test POST /update with invalid magic byte
    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    // Prepare invalid header
    esp_image_header_t bad_hdr = {};
    bad_hdr.magic = 0xAA; // Invalid magic byte (must be 0xE7)
    bad_hdr.chip_id = ESP_CHIP_ID_ESP32S3;

    httpd_req_t req_bad = {};
    req_bad.uri = "/update";
    req_bad.method = HTTP_POST;
    req_bad.content_len = sizeof(bad_hdr);

    g_req_stream_data = (const char*)&bad_hdr;
    g_req_stream_pos = 0;
    g_req_stream_total = sizeof(bad_hdr);
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    err = post_update(&req_bad);
    assert(g_last_resp_status.find("400") != std::string::npos);
    assert(g_last_resp_body.find("magic byte mismatch") != std::string::npos);
    std::printf("    [PASS] POST /update rejects binary with invalid magic byte (400 Bad Request)\n");

    // 4. Test POST /update with invalid chip ID (e.g. ESP32 instead of ESP32-S3)
    esp_image_header_t bad_chip_hdr = {};
    bad_chip_hdr.magic = ESP_IMAGE_HEADER_MAGIC; // 0xE7
    bad_chip_hdr.chip_id = 0x0001;               // ESP32 classic chip ID

    httpd_req_t req_chip_bad = {};
    req_chip_bad.uri = "/update";
    req_chip_bad.method = HTTP_POST;
    req_chip_bad.content_len = sizeof(bad_chip_hdr);

    g_req_stream_data = (const char*)&bad_chip_hdr;
    g_req_stream_pos = 0;
    g_req_stream_total = sizeof(bad_chip_hdr);
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    err = post_update(&req_chip_bad);
    assert(g_last_resp_status.find("400") != std::string::npos);
    assert(g_last_resp_body.find("chip ID mismatch") != std::string::npos);
    std::printf("    [PASS] POST /update rejects binary with invalid chip ID (400 Bad Request)\n");

    // 5. Test POST /update with valid ESP32-S3 image
    char valid_image[8192];
    std::memset(valid_image, 0x42, sizeof(valid_image));
    esp_image_header_t *good_hdr = (esp_image_header_t*)valid_image;
    good_hdr->magic = ESP_IMAGE_HEADER_MAGIC; // 0xE7
    good_hdr->chip_id = ESP_CHIP_ID_ESP32S3; // 0x0009

    httpd_req_t req_good = {};
    req_good.uri = "/update";
    req_good.method = HTTP_POST;
    req_good.content_len = sizeof(valid_image);

    g_req_stream_data = valid_image;
    g_req_stream_pos = 0;
    g_req_stream_total = sizeof(valid_image);
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    err = post_update(&req_good);
    assert(err == ESP_OK);
    assert(g_last_resp_status.find("200") != std::string::npos);
    assert(g_last_resp_body.find("\"status\":\"ok\"") != std::string::npos);
    assert(mock_ota_bytes_written() == sizeof(valid_image));
    assert(mock_ota_was_switched_to("ota_1"));
    std::printf("    [PASS] POST /update flashes valid ESP32-S3 binary (%zu bytes) and switches boot to ota_1\n", mock_ota_bytes_written());

    // 6. Test web_server_stop
    err = web_server_stop();
    assert(err == ESP_OK);
    std::printf("    [PASS] web_server_stop cleanly tears down server.\n");
}

int main() {
    std::printf("================================================================================\n");
    std::printf("Milestone 2: Web Server, Telemetry & OTA Host Functional Verification\n");
    std::printf("================================================================================\n");
    test_temperature_sensor();
    test_ping_service();
    test_web_server_routes();
    std::printf("================================================================================\n");
    std::printf("VERDICT: 100%% PASS — All Milestone 2 functional features verified!\n");
    std::printf("================================================================================\n");
    return 0;
}
