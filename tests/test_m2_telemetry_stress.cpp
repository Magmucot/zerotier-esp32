#include "web_server.hpp"
#include "web_assets.hpp"

#include "driver/temperature_sensor.h"
#include "ping/ping_sock.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "lwip/netif.h"
#include "ESP32LiteAppGlue.hpp"
#include "ESP32LitePort.hpp"
#include "ESP32NetLink.hpp"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <chrono>
#include <string>
#include <vector>

// Declarations of mock state & controllers from mock_m2.cpp
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

// -----------------------------------------------------------------------------
// Test 1: Ping Service Non-Blocking Guarantees & Invalid Target Handling
// -----------------------------------------------------------------------------
static void test_ping_non_blocking_and_invalid_targets() {
    std::printf("[ADVERSARIAL_TEST] 1. Ping Non-Blocking Guarantees & Target Validation...\n");

    // 1.1 Invalid IP addresses must return ESP_ERR_INVALID_ARG
    esp_err_t err = ping_service_init("999.999.999.999");
    assert(err == ESP_ERR_INVALID_ARG);
    std::printf("    [PASS] Rejected invalid IP '999.999.999.999' with ESP_ERR_INVALID_ARG\n");

    err = ping_service_init("not-an-ip");
    assert(err == ESP_ERR_INVALID_ARG);
    std::printf("    [PASS] Rejected malformed string 'not-an-ip' with ESP_ERR_INVALID_ARG\n");

    // 1.2 Valid IP initializes cleanly
    err = ping_service_init("1.1.1.1");
    assert(err == ESP_OK);
    std::printf("    [PASS] Initialized valid IP '1.1.1.1'\n");

    // 1.3 Measure execution time of ping_service_get_status (must be sub-microsecond)
    const int ITERS = 100000;
    auto t0 = std::chrono::high_resolution_clock::now();
    uint32_t rtt = 0;
    bool online = false;
    for (int i = 0; i < ITERS; ++i) {
        ping_service_get_status(&rtt, &online);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double per_call_ns = total_ns / ITERS;
    std::printf("    [PASS] ping_service_get_status avg latency: %.2f ns/call (zero blocking, pure O(1))\n", per_call_ns);
    assert(per_call_ns < 1000.0); // Strict guarantee: < 1 microsecond

    // 1.4 NULL pointers safety
    ping_service_get_status(nullptr, nullptr);
    ping_service_get_status(&rtt, nullptr);
    ping_service_get_status(nullptr, &online);
    std::printf("    [PASS] ping_service_get_status safe against NULL argument pointers\n");
}

// -----------------------------------------------------------------------------
// Test 2: Telemetry API Robustness & ZeroTier Unstarted / Unconfigured States
// -----------------------------------------------------------------------------
static void test_telemetry_unstarted_and_edge_states() {
    std::printf("[ADVERSARIAL_TEST] 2. Telemetry API Unstarted & Edge States...\n");

    esp_err_t err = web_server_start();
    assert(err == ESP_OK);

    auto get_status = mock_find_handler("/api/status", HTTP_GET);
    assert(get_status != nullptr);

    // Scenario A: Default unconfigured / unstarted state (port is nullptr, node is nullptr)
    httpd_req_t req = {};
    req.uri = "/api/status";
    req.method = HTTP_GET;
    g_last_resp_status = "200 OK";
    g_last_resp_type = "";
    g_last_resp_body = "";

    err = get_status(&req);
    assert(err == ESP_OK);
    assert(g_last_resp_type == "application/json");

    // Print raw JSON to stdout so python test harness can parse and validate schema
    std::printf("TELEMETRY_JSON_UNSTARTED: %s\n", g_last_resp_body.c_str());

    // Verify key fallback values when unstarted
    assert(g_last_resp_body.find("\"zt_online\":false") != std::string::npos);
    assert(g_last_resp_body.find("\"zt_status\":\"OFFLINE\"") != std::string::npos);
    assert(g_last_resp_body.find("\"zt_nwid\":\"\"") != std::string::npos);
    assert(g_last_resp_body.find("\"zt_ip\":\"\"") != std::string::npos);
    std::printf("    [PASS] Handled unstarted ZeroTier node safely without crashing or hanging\n");

    // Scenario B: Temperature sensor failure / negative value handling
    mock_set_temperature_celsius(-40.25f);
    err = get_status(&req);
    assert(err == ESP_OK);
    std::printf("TELEMETRY_JSON_SUBZERO: %s\n", g_last_resp_body.c_str());
    assert(g_last_resp_body.find("\"temp_celsius\":-40.25") != std::string::npos);
    std::printf("    [PASS] Handled negative temperature (-40.25 C) with valid JSON formatting\n");

    // Reset temperature
    mock_set_temperature_celsius(42.50f);
}

// -----------------------------------------------------------------------------
// Test 3: Buffer Capacity & Overflow Resilience of json_buf[512]
// -----------------------------------------------------------------------------
static void test_json_buffer_capacity() {
    std::printf("[ADVERSARIAL_TEST] 3. Telemetry JSON Buffer Capacity...\n");

    auto get_status = mock_find_handler("/api/status", HTTP_GET);
    assert(get_status != nullptr);

    // Change ping target to maximum length (31 chars)
    ping_service_init("255.255.255.255");

    httpd_req_t req = {};
    req.uri = "/api/status";
    req.method = HTTP_GET;
    g_last_resp_body = "";

    esp_err_t err = get_status(&req);
    assert(err == ESP_OK);

    size_t len = g_last_resp_body.length();
    std::printf("    [INFO] Generated JSON length: %zu bytes (buffer capacity: 512 bytes)\n", len);
    assert(len > 0);
    assert(len < 512); // Must strictly fit within 512-byte buffer without truncation!

    // Verify JSON terminates properly with closing brace
    assert(g_last_resp_body.front() == '{');
    assert(g_last_resp_body.back() == '}');
    std::printf("    [PASS] JSON buffer capacity verified: %zu bytes used, %zu bytes safety margin\n",
                len, 512 - len);
}

// -----------------------------------------------------------------------------
// Test 4: OTA Endpoint Adversarial Stress (Size limits, malformed headers, locks)
// -----------------------------------------------------------------------------
static void test_ota_adversarial_stress() {
    std::printf("[ADVERSARIAL_TEST] 4. OTA Endpoint Adversarial Stress...\n");

    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    // 4.1 Zero-byte upload (content_len = 0)
    httpd_req_t req_empty = {};
    req_empty.uri = "/update";
    req_empty.method = HTTP_POST;
    req_empty.content_len = 0;
    g_req_stream_data = "";
    g_req_stream_pos = 0;
    g_req_stream_total = 0;
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    esp_err_t err = post_update(&req_empty);
    assert(g_last_resp_status.find("400") != std::string::npos || g_last_resp_status.find("500") != std::string::npos);
    std::printf("    [PASS] Rejected zero-byte firmware upload (status: %s)\n", g_last_resp_status.c_str());

    // 4.2 Truncated header (1 byte, smaller than esp_image_header_t)
    char one_byte = 0xE7;
    httpd_req_t req_trunc = {};
    req_trunc.uri = "/update";
    req_trunc.method = HTTP_POST;
    req_trunc.content_len = 1;
    g_req_stream_data = &one_byte;
    g_req_stream_pos = 0;
    g_req_stream_total = 1;
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    err = post_update(&req_trunc);
    assert(g_last_resp_status.find("400") != std::string::npos);
    assert(g_last_resp_body.find("too small for firmware header") != std::string::npos);
    std::printf("    [PASS] Rejected truncated header (1 byte) with 400 Bad Request\n");

    // 4.3 Malformed Magic Bytes: test multiple bad magic bytes (0x00, 0xFF, 0x12)
    const uint8_t bad_magics[] = { 0x00, 0xFF, 0x12, 0xAA };
    for (uint8_t m : bad_magics) {
        esp_image_header_t bad_hdr = {};
        bad_hdr.magic = m;
        bad_hdr.chip_id = ESP_CHIP_ID_ESP32S3;

        httpd_req_t req_bad = {};
        req_bad.uri = "/update";
        req_bad.method = HTTP_POST;
        req_bad.content_len = sizeof(bad_hdr);
        g_req_stream_data = (const char*)&bad_hdr;
        g_req_stream_pos = 0;
        g_req_stream_total = sizeof(bad_hdr);
        g_last_resp_status = "200 OK";

        err = post_update(&req_bad);
        assert(g_last_resp_status.find("400") != std::string::npos);
        assert(g_last_resp_body.find("magic byte mismatch") != std::string::npos);
    }
    std::printf("    [PASS] Rejected all corrupt magic bytes (0x00, 0xFF, 0x12, 0xAA)\n");

    // 4.4 Wrong Chip IDs: test ESP32 (0x0000), ESP32-S2 (0x0002), ESP32-C3 (0x0005)
    const uint16_t wrong_chips[] = { 0x0000, 0x0001, 0x0002, 0x0005 };
    for (uint16_t c : wrong_chips) {
        esp_image_header_t bad_chip_hdr = {};
        bad_chip_hdr.magic = ESP_IMAGE_HEADER_MAGIC;
        bad_chip_hdr.chip_id = c;

        httpd_req_t req_bad = {};
        req_bad.uri = "/update";
        req_bad.method = HTTP_POST;
        req_bad.content_len = sizeof(bad_chip_hdr);
        g_req_stream_data = (const char*)&bad_chip_hdr;
        g_req_stream_pos = 0;
        g_req_stream_total = sizeof(bad_chip_hdr);
        g_last_resp_status = "200 OK";

        err = post_update(&req_bad);
        assert(g_last_resp_status.find("400") != std::string::npos);
        assert(g_last_resp_body.find("chip ID mismatch") != std::string::npos);
    }
    std::printf("    [PASS] Rejected non-ESP32-S3 chip IDs (ESP32, ESP32-S2, ESP32-C3)\n");

    // 4.5 Binary size exceeding partition size (6MB = 0x600000)
    httpd_req_t req_oversized = {};
    req_oversized.uri = "/update";
    req_oversized.method = HTTP_POST;
    req_oversized.content_len = 0x600001; // 1 byte over 6MB
    g_last_resp_status = "200 OK";
    g_last_resp_body = "";

    err = post_update(&req_oversized);
    assert(g_last_resp_status.find("400") != std::string::npos);
    assert(g_last_resp_body.find("exceeds partition size") != std::string::npos);
    std::printf("    [PASS] Rejected oversized binary (> 6MB) before flash erase\n");

    // 4.6 Alias endpoint /api/ota works identically
    auto post_alias = mock_find_handler("/api/ota", HTTP_POST);
    assert(post_alias != nullptr);
    httpd_req_t req_alias = {};
    req_alias.uri = "/api/ota";
    req_alias.method = HTTP_POST;
    req_alias.content_len = sizeof(esp_image_header_t);
    esp_image_header_t bad_alias_hdr = {};
    bad_alias_hdr.magic = 0x11; // bad magic
    g_req_stream_data = (const char*)&bad_alias_hdr;
    g_req_stream_pos = 0;
    g_req_stream_total = sizeof(bad_alias_hdr);
    g_last_resp_status = "200 OK";
    err = post_alias(&req_alias);
    assert(g_last_resp_status.find("400") != std::string::npos);
    std::printf("    [PASS] Confirmed /api/ota alias endpoint route registered and functional\n");
}

int main() {
    std::printf("================================================================================\n");
    std::printf("Milestone 2 Adversarial Stress & Telemetry Validation Suite\n");
    std::printf("================================================================================\n");
    test_ping_non_blocking_and_invalid_targets();
    test_telemetry_unstarted_and_edge_states();
    test_json_buffer_capacity();
    test_ota_adversarial_stress();
    std::printf("================================================================================\n");
    std::printf("[SUCCESS] All C++ adversarial stress tests passed cleanly!\n");
    std::printf("================================================================================\n");
    return 0;
}
