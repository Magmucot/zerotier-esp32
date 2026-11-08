#include "driver/temperature_sensor.h"
#include "ping/ping_sock.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "lwip/netif.h"
#include "ESP32LiteAppGlue.hpp"
#include "ESP32LitePort.hpp"
#include "ESP32NetLink.hpp"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>

// --- System & Heap Mock ---
extern "C" {
void esp_restart(void) {
    std::printf("[MOCK_SYSTEM] esp_restart() invoked!\n");
}

size_t heap_caps_get_total_size(uint32_t caps) {
    if (caps & MALLOC_CAP_SPIRAM) {
        return 8 * 1024 * 1024; // 8MB PSRAM
    }
    return 384 * 1024; // ~384KB Internal SRAM
}

// --- ZeroTier Symbols Mock ---
uint64_t zt_esp32_lite_node_id(void) {
    return 0x154a350c86ULL;
}

static ZeroTier::ESP32LitePort *s_mock_port = nullptr;

ZeroTier::ESP32LitePort* zt_esp32_lite_default_port(void) {
    return s_mock_port;
}

void ZT_Node_status(ZT_Node *node, ZT_NodeStatus *status) {
    if (status) {
        status->online = 1;
        status->address = 0x154a350c86ULL;
    }
}

ZT_VirtualNetworkConfig* ZT_Node_networkConfig(ZT_Node *node, uint64_t nwid) {
    return nullptr;
}

void ZT_Node_freeQueryResult(ZT_Node *node, void *res) {
}
}

namespace ZeroTier {

ZT_Node* ESP32LitePort::node() const {
    return nullptr;
}

const ESP32LitePort::Config& ESP32LitePort::config() const {
    return _config;
}

ESP32NetLink::ESP32NetLink() : _lock(false), _generation(0), _ztNetif(nullptr), _gatewayScratchIndex(0) {
    std::memset(_routes, 0, sizeof(_routes));
    std::memset(_ztDeviceName, 0, sizeof(_ztDeviceName));
}

ESP32NetLink& ESP32NetLink::getInstance() {
    static ESP32NetLink instance;
    return instance;
}

struct netif* ESP32NetLink::zeroTierNetif() const {
    return nullptr;
}
}

// --- Temperature Sensor Mock ---
static bool s_mock_temp_installed = false;
static bool s_mock_temp_enabled = false;
static float s_mock_temp_value = 42.5f;

esp_err_t temperature_sensor_install(const temperature_sensor_config_t *tsens_config, temperature_sensor_handle_t *ret_tsens) {
    s_mock_temp_installed = true;
    if (ret_tsens) *ret_tsens = (void*)0x1234;
    return ESP_OK;
}

esp_err_t temperature_sensor_enable(temperature_sensor_handle_t tsens) {
    if (!s_mock_temp_installed) return ESP_FAIL;
    s_mock_temp_enabled = true;
    return ESP_OK;
}

esp_err_t temperature_sensor_disable(temperature_sensor_handle_t tsens) {
    s_mock_temp_enabled = false;
    return ESP_OK;
}

esp_err_t temperature_sensor_uninstall(temperature_sensor_handle_t tsens) {
    s_mock_temp_installed = false;
    return ESP_OK;
}

esp_err_t temperature_sensor_get_celsius(temperature_sensor_handle_t tsens, float *celsius) {
    if (!s_mock_temp_enabled) return ESP_FAIL;
    if (celsius) *celsius = s_mock_temp_value;
    return ESP_OK;
}

void mock_set_temperature_celsius(float c) {
    s_mock_temp_value = c;
}

// --- Ping Mock ---
struct MockPingSession {
    esp_ping_config_t config;
    esp_ping_callbacks_t cbs;
    bool running;
    uint32_t simulated_latency_ms;
};

static MockPingSession s_mock_ping = {};

esp_err_t esp_ping_new_session(const esp_ping_config_t *config, const esp_ping_callbacks_t *cbs, esp_ping_handle_t *hdl_out) {
    if (config) s_mock_ping.config = *config;
    if (cbs) s_mock_ping.cbs = *cbs;
    s_mock_ping.running = false;
    s_mock_ping.simulated_latency_ms = 18;
    if (hdl_out) *hdl_out = &s_mock_ping;
    return ESP_OK;
}

esp_err_t esp_ping_start(esp_ping_handle_t hdl) {
    s_mock_ping.running = true;
    if (s_mock_ping.cbs.on_ping_success) {
        s_mock_ping.cbs.on_ping_success(hdl, s_mock_ping.cbs.cb_args);
    }
    return ESP_OK;
}

esp_err_t esp_ping_stop(esp_ping_handle_t hdl) {
    s_mock_ping.running = false;
    return ESP_OK;
}

esp_err_t esp_ping_delete_session(esp_ping_handle_t hdl) {
    s_mock_ping.running = false;
    return ESP_OK;
}

esp_err_t esp_ping_get_profile(esp_ping_handle_t hdl, esp_ping_profile_t profile_type, void *data, uint32_t size) {
    if (profile_type == ESP_PING_PROF_TIMEGAP && data && size >= sizeof(uint32_t)) {
        *(uint32_t*)data = s_mock_ping.simulated_latency_ms;
        return ESP_OK;
    }
    return ESP_OK;
}

// --- HTTP Server Mock ---
struct MockHttpServer {
    bool started = false;
    httpd_config_t config;
    std::vector<httpd_uri_t> handlers;
};

static MockHttpServer s_mock_http;

// Simulated Request Context
std::string g_last_resp_status = "200 OK";
std::string g_last_resp_type = "";
std::string g_last_resp_body = "";
const char *g_req_stream_data = nullptr;
size_t g_req_stream_pos = 0;
size_t g_req_stream_total = 0;

esp_err_t httpd_start(httpd_handle_t *handle, const httpd_config_t *config) {
    s_mock_http.started = true;
    if (config) s_mock_http.config = *config;
    if (handle) *handle = &s_mock_http;
    return ESP_OK;
}

esp_err_t httpd_stop(httpd_handle_t handle) {
    s_mock_http.started = false;
    s_mock_http.handlers.clear();
    return ESP_OK;
}

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler) {
    if (uri_handler) {
        s_mock_http.handlers.push_back(*uri_handler);
    }
    return ESP_OK;
}

#include <mutex>
#include <thread>
#include <chrono>
static std::mutex s_mock_mutex;


esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (type) g_last_resp_type = type;
    return ESP_OK;
}

static std::map<httpd_req_t*, std::string> s_req_status_map;

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (status) {
        g_last_resp_status = status;
        if (r) s_req_status_map[r] = status;
    }
    return ESP_OK;
}

std::string mock_get_req_status(httpd_req_t *r) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (r && s_req_status_map.find(r) != s_req_status_map.end()) {
        return s_req_status_map[r];
    }
    return g_last_resp_status;
}


esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, ssize_t buf_len) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (buf) {
        if (buf_len == HTTPD_RESP_USE_STRLEN) {
            g_last_resp_body = buf;
        } else {
            g_last_resp_body = std::string(buf, buf_len);
        }
    }
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (str) g_last_resp_body = str;
    return ESP_OK;
}

int g_mock_req_recv_error_code = 0;
size_t g_mock_req_recv_chunk_limit = 0;
size_t g_mock_req_recv_fail_after_bytes = 0;

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    if (g_mock_req_recv_error_code != 0) {
        return g_mock_req_recv_error_code;
    }
    if (g_mock_req_recv_fail_after_bytes > 0 && g_req_stream_pos >= g_mock_req_recv_fail_after_bytes) {
        return -1; // Socket error
    }
    if (!g_req_stream_data || g_req_stream_pos >= g_req_stream_total) {
        return 0;
    }
    size_t avail = g_req_stream_total - g_req_stream_pos;
    size_t limit = buf_len;
    if (g_mock_req_recv_chunk_limit > 0 && g_mock_req_recv_chunk_limit < limit) {
        limit = g_mock_req_recv_chunk_limit;
    }
    size_t to_copy = (avail < limit) ? avail : limit;
    std::memcpy(buf, g_req_stream_data + g_req_stream_pos, to_copy);
    g_req_stream_pos += to_copy;
    return (int)to_copy;
}


httpd_uri_func_t mock_find_handler(const char *uri, httpd_method_t method) {
    for (const auto &h : s_mock_http.handlers) {
        if (h.method == method && std::string(h.uri) == uri) {
            return h.handler;
        }
    }
    return nullptr;
}

// --- OTA Mock & Fault Injection Hooks ---
static esp_partition_t s_ota_0 = { "ota_0", 0x600000, 0x20000, 0x00, 0x10 };
static esp_partition_t s_ota_1 = { "ota_1", 0x600000, 0x620000, 0x00, 0x11 };
static const esp_partition_t *s_running_partition = &s_ota_0;
static const esp_partition_t *s_boot_partition = &s_ota_0;
static bool s_ota_begin_called = false;
static bool s_ota_end_called = false;
static size_t s_ota_bytes_written = 0;

esp_err_t g_mock_ota_begin_err = ESP_OK;
esp_err_t g_mock_ota_write_err = ESP_OK;
esp_err_t g_mock_ota_end_err = ESP_OK;
esp_err_t g_mock_ota_set_boot_err = ESP_OK;
bool g_mock_ota_abort_called = false;
bool g_mock_ota_begin_called = false;
bool g_mock_ota_end_called = false;
bool g_mock_ota_set_boot_called = false;
size_t g_mock_ota_write_count = 0;
std::vector<std::string> g_mock_ota_sequence_log;
const esp_partition_t *g_mock_ota_override_next_partition = nullptr;
bool g_mock_ota_force_null_partition = false;
size_t g_mock_ota_write_delay_ms = 0;

void mock_ota_reset() {

    g_mock_ota_begin_err = ESP_OK;
    g_mock_ota_write_err = ESP_OK;
    g_mock_ota_end_err = ESP_OK;
    g_mock_ota_set_boot_err = ESP_OK;
    g_mock_ota_abort_called = false;
    g_mock_ota_begin_called = false;
    g_mock_ota_end_called = false;
    g_mock_ota_set_boot_called = false;
    g_mock_ota_write_count = 0;
    g_mock_ota_sequence_log.clear();
    g_mock_ota_override_next_partition = nullptr;
    g_mock_ota_force_null_partition = false;

    g_mock_req_recv_error_code = 0;
    g_mock_req_recv_chunk_limit = 0;
    g_mock_req_recv_fail_after_bytes = 0;
    g_mock_ota_write_delay_ms = 0;


    s_running_partition = &s_ota_0;
    s_boot_partition = &s_ota_0;
    s_ota_begin_called = false;
    s_ota_end_called = false;
    s_ota_bytes_written = 0;

    g_last_resp_status = "200 OK";
    g_last_resp_type = "";
    g_last_resp_body = "";
    g_req_stream_data = nullptr;
    g_req_stream_pos = 0;
    g_req_stream_total = 0;
    s_req_status_map.clear();
}


const esp_partition_t* esp_ota_get_running_partition(void) {
    return s_running_partition;
}

const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t *start_from) {
    if (g_mock_ota_force_null_partition) {
        return nullptr;
    }
    if (g_mock_ota_override_next_partition) {
        return g_mock_ota_override_next_partition;
    }
    if (s_running_partition == &s_ota_0) {
        return &s_ota_1;
    }
    return &s_ota_0;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    g_mock_ota_sequence_log.push_back("begin");
    g_mock_ota_begin_called = true;
    s_ota_begin_called = true;
    s_ota_bytes_written = 0;
    if (g_mock_ota_begin_err != ESP_OK) {
        return g_mock_ota_begin_err;
    }
    if (out_handle) *out_handle = 1;
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size) {

    if (g_mock_ota_write_delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(g_mock_ota_write_delay_ms));
    }
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    g_mock_ota_sequence_log.push_back("write");
    g_mock_ota_write_count++;
    s_ota_bytes_written += size;
    if (g_mock_ota_write_err != ESP_OK) {
        return g_mock_ota_write_err;
    }
    return ESP_OK;
}


esp_err_t esp_ota_end(esp_ota_handle_t handle) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    g_mock_ota_sequence_log.push_back("end");
    g_mock_ota_end_called = true;
    s_ota_end_called = true;
    if (g_mock_ota_end_err != ESP_OK) {
        return g_mock_ota_end_err;
    }
    return ESP_OK;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    g_mock_ota_sequence_log.push_back("abort");
    g_mock_ota_abort_called = true;
    s_ota_begin_called = false;
    return ESP_OK;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition) {
    std::lock_guard<std::mutex> lock(s_mock_mutex);
    g_mock_ota_sequence_log.push_back("set_boot");
    g_mock_ota_set_boot_called = true;
    if (g_mock_ota_set_boot_err != ESP_OK) {
        return g_mock_ota_set_boot_err;
    }
    s_boot_partition = partition;
    return ESP_OK;
}


bool mock_ota_was_switched_to(const char *label) {
    return s_boot_partition && (std::string(s_boot_partition->label) == label);
}

size_t mock_ota_bytes_written() {
    return s_ota_bytes_written;
}

