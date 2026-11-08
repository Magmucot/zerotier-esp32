#include "web_server.hpp"
#include "web_assets.hpp"

#include "driver/temperature_sensor.h"
#include "ping/ping_sock.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

#ifndef ESP_ERR_FLASH_OP_TIMEOUT
#define ESP_ERR_FLASH_OP_TIMEOUT 0x6002
#endif

#ifndef ESP_ERR_OTA_VALIDATE_FAILED
#define ESP_ERR_OTA_VALIDATE_FAILED 0x1502
#endif


#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

// Declarations from mock_m2.cpp
extern httpd_uri_func_t mock_find_handler(const char *uri, httpd_method_t method);
extern std::string g_last_resp_status;
extern std::string g_last_resp_type;
extern std::string g_last_resp_body;
extern const char *g_req_stream_data;
extern size_t g_req_stream_pos;
extern size_t g_req_stream_total;

extern int g_mock_req_recv_error_code;
extern size_t g_mock_req_recv_chunk_limit;
extern size_t g_mock_req_recv_fail_after_bytes;

extern esp_err_t g_mock_ota_begin_err;
extern esp_err_t g_mock_ota_write_err;
extern esp_err_t g_mock_ota_end_err;
extern esp_err_t g_mock_ota_set_boot_err;
extern bool g_mock_ota_abort_called;
extern bool g_mock_ota_begin_called;
extern bool g_mock_ota_end_called;
extern bool g_mock_ota_set_boot_called;
extern size_t g_mock_ota_write_count;
extern std::vector<std::string> g_mock_ota_sequence_log;
extern const esp_partition_t *g_mock_ota_override_next_partition;
extern bool g_mock_ota_force_null_partition;

extern void mock_ota_reset();
extern bool mock_ota_was_switched_to(const char *label);
extern size_t mock_ota_bytes_written();
extern std::string mock_get_req_status(httpd_req_t *r);
extern size_t g_mock_ota_write_delay_ms;


// Helper to construct a synthetic ESP image with given magic and chip ID
static void create_firmware_image(std::vector<char> &buf, size_t total_size, uint8_t magic, uint16_t chip_id) {
    buf.assign(total_size, 0x55);
    if (total_size >= sizeof(esp_image_header_t)) {
        esp_image_header_t *hdr = (esp_image_header_t *)buf.data();
        std::memset(hdr, 0, sizeof(esp_image_header_t));
        hdr->magic = magic;
        hdr->chip_id = chip_id;
        hdr->segment_count = 1;
        hdr->entry_addr = 0x40378000;
    }
}

// ============================================================================
// STRESS TEST 1: Invalid Magic Byte Fault Injection
// ============================================================================
static void test_stress_magic_bytes() {
    std::printf("[CHALLENGE 1] OTA Fault Injection: Invalid Magic Byte Stress...\n");
    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    const uint8_t invalid_magics[] = {
        0x00, 0x01, 0x42, 0xAA, 0x55, 0x7E, 0xE6, 0xE8, 0xEF, 0xFE, 0xFF
    };

    for (uint8_t bad_magic : invalid_magics) {
        mock_ota_reset();
        std::vector<char> img;
        create_firmware_image(img, 4096, bad_magic, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);

        // Verification checks:
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("magic byte mismatch") != std::string::npos);
        assert(g_mock_ota_abort_called == true);
        assert(g_mock_ota_end_called == false);
        assert(g_mock_ota_set_boot_called == false);
        assert(mock_ota_was_switched_to("ota_1") == false);

        // Verify lock is released and subsequent request works
        mock_ota_reset();
        std::vector<char> good_img;
        create_firmware_image(good_img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);
        req.content_len = good_img.size();
        g_req_stream_data = good_img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = good_img.size();

        err = post_update(&req);
        assert(err == ESP_OK);
        assert(g_last_resp_status == "200 OK");
        assert(mock_ota_was_switched_to("ota_1") == true);
    }
    std::printf("    [PASS] Verified rejection of 11 distinct invalid magic bytes (all 400 Bad Request, clean abort).\n");
}

// ============================================================================
// STRESS TEST 2: Wrong Chip ID Fault Injection
// ============================================================================
static void test_stress_chip_ids() {
    std::printf("[CHALLENGE 2] OTA Fault Injection: Wrong Chip ID Stress...\n");
    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    struct ChipCase {
        uint16_t id;
        const char *name;
    };

    const ChipCase invalid_chips[] = {
        { 0x0000, "ESP32 Classic (0x0000)" },
        { 0x0001, "ESP32 Classic Alt (0x0001)" },
        { 0x0002, "ESP32-S2 (0x0002)" },
        { 0x0005, "ESP32-C3 (0x0005)" },
        { 0x000C, "ESP32-C2 (0x000C)" },
        { 0x000D, "ESP32-C6 (0x000D)" },
        { 0x0010, "ESP32-H2 (0x0010)" },
        { 0x0012, "ESP32-P4 (0x0012)" },
        { 0xCAFE, "Unknown SoC (0xCAFE)" },
        { 0xFFFF, "Corrupt SoC (0xFFFF)" }
    };

    for (const auto &cc : invalid_chips) {
        mock_ota_reset();
        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, cc.id);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);

        // Verification checks:
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("chip ID mismatch") != std::string::npos);
        assert(g_mock_ota_abort_called == true);
        assert(g_mock_ota_end_called == false);
        assert(g_mock_ota_set_boot_called == false);
        assert(mock_ota_was_switched_to("ota_1") == false);
    }
    std::printf("    [PASS] Verified rejection of 10 non-ESP32-S3 chip IDs (all 400 Bad Request, clean abort).\n");
}

// ============================================================================
// STRESS TEST 3: Oversized Binaries (> 6MB) & Boundary Tests
// ============================================================================
static void test_stress_oversized_binaries() {
    std::printf("[CHALLENGE 3] OTA Binary Size Boundary & Partition Enforcement...\n");
    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    const size_t partition_size = 0x600000; // 6MB = 6,291,456 bytes

    // Subtest 3.1: 6MB + 1 byte (6,291,457 bytes)
    {
        mock_ota_reset();
        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = partition_size + 1; // 6MB + 1 byte

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("exceeds partition size") != std::string::npos);
        assert(g_mock_ota_begin_called == false); // Erase NOT called!
        assert(g_mock_ota_set_boot_called == false);
        std::printf("    [PASS] Subtest 3.1: 6MB+1 byte (6291457 bytes) rejected before flash erase (400 Bad Request)\n");
    }

    // Subtest 3.2: 7MB (7,340,032 bytes)
    {
        mock_ota_reset();
        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = 7 * 1024 * 1024;

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("exceeds partition size") != std::string::npos);
        assert(g_mock_ota_begin_called == false);
        std::printf("    [PASS] Subtest 3.2: 7MB binary rejected before flash erase (400 Bad Request)\n");
    }

    // Subtest 3.3: 16MB (Flash capacity: 16,777,216 bytes)
    {
        mock_ota_reset();
        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = 16 * 1024 * 1024;

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("exceeds partition size") != std::string::npos);
        assert(g_mock_ota_begin_called == false);
        std::printf("    [PASS] Subtest 3.3: 16MB binary rejected before flash erase (400 Bad Request)\n");
    }

    // Subtest 3.4: Exact boundary: exactly 6MB (6,291,456 bytes)
    {
        mock_ota_reset();
        // Construct header-only data and stream 6MB via content_len
        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = partition_size; // exactly 6,291,456 bytes

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size(); // Stream ends after 4KB (simulating early abort or EOF)

        // The size check total_len > update_partition->size MUST NOT reject exactly 6MB!
        // We only care that it passed the size check and proceeded to esp_ota_begin
        esp_err_t err = post_update(&req);
        assert(g_mock_ota_begin_called == true);
        std::printf("    [PASS] Subtest 3.4: Exact 6MB (6291456 bytes) boundary passes size validation into flash engine\n");
    }
}

// ============================================================================
// STRESS TEST 4: Concurrent Uploads & Mutual Exclusion (409 Conflict)
// ============================================================================
static void test_stress_concurrent_uploads() {
    std::printf("[CHALLENGE 4] OTA Concurrency & Mutual Exclusion Stress...\n");
    auto post_update = mock_find_handler("/update", HTTP_POST);
    auto post_api_ota = mock_find_handler("/api/ota", HTTP_POST);
    assert(post_update != nullptr && post_api_ota != nullptr);

    // Subtest 4.1: Alias route check
    {
        mock_ota_reset();
        std::vector<char> good_img;
        create_firmware_image(good_img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/api/ota";
        req.method = HTTP_POST;
        req.content_len = good_img.size();

        g_req_stream_data = good_img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = good_img.size();

        esp_err_t err = post_api_ota(&req);
        assert(err == ESP_OK);
        assert(g_last_resp_status == "200 OK");
        assert(mock_ota_was_switched_to("ota_1") == true);
        std::printf("    [PASS] Subtest 4.1: /api/ota alias functions identically to /update\n");
    }

    // Subtest 4.2: Multithreaded Race Condition Stress Harness
    // Launch 16 worker threads simultaneously invoking post_update / post_api_ota
    {
        std::printf("    [EXEC] Launching 16 threads in concurrent OTA contention race...\n");
        mock_ota_reset();
        std::vector<char> test_img;
        create_firmware_image(test_img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        g_req_stream_data = test_img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = test_img.size();
        g_mock_ota_write_delay_ms = 20; // 20ms write delay to hold session during contention

        std::atomic<int> count_200{0};
        std::atomic<int> count_409{0};
        std::atomic<int> count_other{0};

        std::atomic<bool> start_gate{false};
        std::vector<std::thread> workers;

        for (int i = 0; i < 16; ++i) {
            workers.emplace_back([&, i]() {
                while (!start_gate.load()) {
                    std::this_thread::yield();
                }

                httpd_req_t r = {};
                r.uri = (i % 2 == 0) ? "/update" : "/api/ota";
                r.method = HTTP_POST;
                r.content_len = test_img.size();

                auto handler = (i % 2 == 0) ? post_update : post_api_ota;
                handler(&r);

                std::string st = mock_get_req_status(&r);
                if (st.find("200") != std::string::npos) {
                    count_200.fetch_add(1);
                } else if (st.find("409") != std::string::npos) {
                    count_409.fetch_add(1);
                } else {
                    count_other.fetch_add(1);
                }
            });
        }

        // Release the gate
        start_gate.store(true);
        for (auto &t : workers) {
            t.join();
        }

        std::printf("    [INFO] Contention Results: 200 OK=%d, 409 Conflict=%d, other=%d\n",
                    count_200.load(), count_409.load(), count_other.load());

        // Zero crashes, exactly 1 winner, exactly 15 rejected with 409 Conflict
        assert(count_200.load() == 1);
        assert(count_409.load() == 15);
        assert(count_other.load() == 0);
        std::printf("    [PASS] Subtest 4.2: Multithreaded contention verified: exactly 1 winner (200 OK), 15 rejected (409 Conflict), 0 crashes\n");
    }


    // Subtest 4.3: Post-contention clean state verification
    {
        mock_ota_reset();
        std::vector<char> good_img;
        create_firmware_image(good_img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = good_img.size();

        g_req_stream_data = good_img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = good_img.size();

        esp_err_t err = post_update(&req);
        assert(err == ESP_OK);
        assert(g_last_resp_status == "200 OK");
        assert(mock_ota_was_switched_to("ota_1") == true);
        std::printf("    [PASS] Subtest 4.3: Post-contention OTA session operational and clean\n");
    }
}

// ============================================================================
// STRESS TEST 5: Complete esp_ota_ops Sequence & Phase Fault Injections
// ============================================================================
static void test_stress_ota_sequence_and_fault_injection() {
    std::printf("[CHALLENGE 5] OTA Call Sequence Audit & Fault Injection...\n");
    auto post_update = mock_find_handler("/update", HTTP_POST);
    assert(post_update != nullptr);

    // Subtest 5.1: Sequence Verification on Happy Path
    {
        mock_ota_reset();
        std::vector<char> img;
        create_firmware_image(img, 12288, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3); // 3 x 4096 chunks

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);
        assert(err == ESP_OK);
        assert(g_last_resp_status == "200 OK");

        // Verify sequence: begin -> write -> write -> write -> end -> set_boot
        assert(g_mock_ota_sequence_log.size() >= 5);
        assert(g_mock_ota_sequence_log.front() == "begin");
        assert(g_mock_ota_sequence_log.back() == "set_boot");
        assert(g_mock_ota_sequence_log[g_mock_ota_sequence_log.size() - 2] == "end");
        assert(g_mock_ota_write_count == 3);
        assert(mock_ota_bytes_written() == 12288);
        assert(mock_ota_was_switched_to("ota_1") == true);
        std::printf("    [PASS] Subtest 5.1: Happy path sequence verified: begin -> 3x write -> end -> set_boot\n");
    }

    // Subtest 5.2: Inactive partition missing (NULL partition)
    {
        mock_ota_reset();
        g_mock_ota_force_null_partition = true;

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = 4096;

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "500 Internal Server Error");
        assert(g_last_resp_body.find("No inactive OTA partition found") != std::string::npos);
        assert(g_mock_ota_begin_called == false);
        assert(g_mock_ota_set_boot_called == false);
        std::printf("    [PASS] Subtest 5.2: Missing inactive partition triggers 500 error without calling begin\n");
    }

    // Subtest 5.3: Fault Injection at esp_ota_begin (e.g. Flash erase error)
    {
        mock_ota_reset();
        g_mock_ota_begin_err = ESP_FAIL;

        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "500 Internal Server Error");
        assert(g_last_resp_body.find("Failed to initialize OTA partition") != std::string::npos);
        assert(g_mock_ota_write_count == 0);
        assert(g_mock_ota_end_called == false);
        assert(g_mock_ota_set_boot_called == false);
        std::printf("    [PASS] Subtest 5.3: esp_ota_begin failure handled (500 error, zero writes, zero boot switch)\n");
    }

    // Subtest 5.4: Tiny first chunk (< 24 bytes header)
    {
        mock_ota_reset();
        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();
        g_mock_req_recv_chunk_limit = 10; // Deliver only 10 bytes on first read

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("Chunk too small for firmware header") != std::string::npos);
        assert(g_mock_ota_abort_called == true);
        assert(g_mock_ota_set_boot_called == false);
        std::printf("    [PASS] Subtest 5.4: Tiny first chunk (<24B) rejected with 400 Bad Request and clean abort\n");
    }

    // Subtest 5.5: Mid-stream socket disconnect / network error
    {
        mock_ota_reset();
        std::vector<char> img;
        create_firmware_image(img, 12288, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();
        g_mock_req_recv_fail_after_bytes = 4096; // Fail socket on 2nd chunk

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "500 Internal Server Error");
        assert(g_last_resp_body.find("Network error receiving firmware stream") != std::string::npos);
        assert(g_mock_ota_abort_called == true);
        assert(g_mock_ota_end_called == false);
        assert(g_mock_ota_set_boot_called == false);
        assert(mock_ota_was_switched_to("ota_1") == false);
        std::printf("    [PASS] Subtest 5.5: Mid-stream socket failure aborts OTA without switching boot partition\n");
    }

    // Subtest 5.6: Fault Injection at esp_ota_write (e.g. Flash write timeout)
    {
        mock_ota_reset();
        g_mock_ota_write_err = ESP_ERR_FLASH_OP_TIMEOUT;

        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "500 Internal Server Error");
        assert(g_last_resp_body.find("Flash write error during OTA") != std::string::npos);
        assert(g_mock_ota_abort_called == true);
        assert(g_mock_ota_end_called == false);
        assert(g_mock_ota_set_boot_called == false);
        std::printf("    [PASS] Subtest 5.6: Flash write error aborts OTA safely\n");
    }

    // Subtest 5.7: Fault Injection at esp_ota_end (e.g. Checksum validation failure)
    {
        mock_ota_reset();
        g_mock_ota_end_err = ESP_ERR_OTA_VALIDATE_FAILED;

        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "400 Bad Request");
        assert(g_last_resp_body.find("Image validation failed (checksum mismatch)") != std::string::npos);
        assert(g_mock_ota_end_called == true);
        assert(g_mock_ota_set_boot_called == false);
        assert(mock_ota_was_switched_to("ota_1") == false);
        std::printf("    [PASS] Subtest 5.7: Checksum validation failure in esp_ota_end prevents boot switch (400)\n");
    }

    // Subtest 5.8: Fault Injection at esp_ota_set_boot_partition (e.g. otadata write failure)
    {
        mock_ota_reset();
        g_mock_ota_set_boot_err = ESP_FAIL;

        std::vector<char> img;
        create_firmware_image(img, 4096, ESP_IMAGE_HEADER_MAGIC, ESP_CHIP_ID_ESP32S3);

        httpd_req_t req = {};
        req.uri = "/update";
        req.method = HTTP_POST;
        req.content_len = img.size();

        g_req_stream_data = img.data();
        g_req_stream_pos = 0;
        g_req_stream_total = img.size();

        esp_err_t err = post_update(&req);
        assert(g_last_resp_status == "500 Internal Server Error");
        assert(g_last_resp_body.find("Failed to update boot partition") != std::string::npos);
        std::printf("    [PASS] Subtest 5.8: esp_ota_set_boot_partition failure handled with 500 error\n");
    }
}

int main() {
    std::printf("================================================================================\n");
    std::printf("EMPIRICAL CHALLENGER: MILESTONE 2 OTA ENGINE ADVERSARIAL STRESS HARNESS\n");
    std::printf("================================================================================\n");

    // Initialize web server
    esp_err_t err = web_server_start();
    assert(err == ESP_OK);

    test_stress_magic_bytes();
    test_stress_chip_ids();
    test_stress_oversized_binaries();
    test_stress_concurrent_uploads();
    test_stress_ota_sequence_and_fault_injection();

    web_server_stop();

    std::printf("================================================================================\n");
    std::printf("VERDICT: 100%% PASS — ALL ADVERSARIAL OTA STRESS TESTS PASSED EMPIRICALLY!\n");
    std::printf("================================================================================\n");
    return 0;
}
