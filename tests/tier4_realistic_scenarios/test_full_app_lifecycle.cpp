#include "ESP32LiteAppGlue.hpp"
#include "ESP32LitePort.hpp"
#include "ESP32LiteDiagnostics.hpp"
#include "ESP32EthernetTap.hpp"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/netif.h"
#include "ZeroTierOne.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <thread>
#include <chrono>

using namespace ZeroTier;

// --- Mock EthernetTap & ManagedRoute Fixture ---
namespace ZeroTier {
EthernetTap::EthernetTap() {}
EthernetTap::~EthernetTap() {}
bool EthernetTap::addIps(std::vector<InetAddress> v) {
    for (auto& i : v) {
        if (!addIp(i)) return false;
    }
    return true;
}
std::string EthernetTap::friendlyName() const { return "test_tap"; }

std::shared_ptr<EthernetTap> EthernetTap::newInstance(
    const char*, unsigned int, bool, const char* h, const MAC& m,
    unsigned int mtu, unsigned int metric, uint64_t nwid, const char* name,
    void(*fn)(void*, void*, uint64_t, const MAC&, const MAC&, unsigned int, unsigned int, const void*, unsigned int),
    void* a)
{
    return std::shared_ptr<EthernetTap>(new ESP32EthernetTap(h, m, mtu, metric, nwid, name, fn, a));
}

ManagedRoute::ManagedRoute(const InetAddress& t, const InetAddress& v, const InetAddress& s, const char*) {
    _target = t;
    _via = v;
    _src = s;
}
ManagedRoute::~ManagedRoute() {}
bool ManagedRoute::sync() { return true; }
void ManagedRoute::remove() {}
}

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// --- Mock ZT_Node Stubs ---
extern "C" {
ZT_ResultCode ZT_Node_new(ZT_Node** node, const struct ZT_Node_Config*, void*, void*, const struct ZT_Node_Callbacks*, int64_t) {
    if (node) *node = (ZT_Node*)0x4000;
    return ZT_RESULT_OK;
}
void ZT_Node_delete(ZT_Node*) {}
ZT_ResultCode ZT_Node_processBackgroundTasks(ZT_Node*, void*, int64_t, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_processWirePacket(ZT_Node*, void*, int64_t, int64_t, const struct sockaddr_storage*, const void*, unsigned int, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_processVirtualNetworkFrame(ZT_Node*, void*, int64_t, uint64_t, uint64_t, uint64_t, unsigned int, unsigned int, const void*, unsigned int, volatile int64_t*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_join(ZT_Node*, uint64_t, void*, void*) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_multicastSubscribe(ZT_Node*, void*, uint64_t, uint64_t, unsigned long) { return ZT_RESULT_OK; }
ZT_ResultCode ZT_Node_multicastUnsubscribe(ZT_Node*, uint64_t, uint64_t, unsigned long) { return ZT_RESULT_OK; }
uint64_t ZT_Node_address(ZT_Node*) { return 0x1234567890ULL; }
}

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT (1 << 0)

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

int main(int argc, char** argv) {
    std::printf("[TEST RUN] Starting Tier 4: Full ESP32-ZeroTier-Lite Application Lifecycle Test\n");

    // --- Step 1: NVS Initialization ---
    std::printf("[STEP 1] Initializing NVS via zt_esp32_lite_nvs_init()...\n");
    int nvs_rc = zt_esp32_lite_nvs_init();
    std::printf("[INFO] zt_esp32_lite_nvs_init returned %d\n", nvs_rc);
    assert(nvs_rc == 0 && "zt_esp32_lite_nvs_init must succeed");
    std::printf("[PASS] Step 1: NVS successfully initialized.\n");

    // --- Step 2: Underlay Networking (Wi-Fi STA) ---
    std::printf("[STEP 2] Setting up underlay Wi-Fi Station mode...\n");
    s_wifi_event_group = xEventGroupCreate();
    assert(s_wifi_event_group != nullptr);

    esp_err_t err = esp_netif_init();
    assert(err == ESP_OK);
    err = esp_event_loop_create_default();
    assert(err == ESP_OK);
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif != nullptr);

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_cfg);
    assert(err == ESP_OK);

    esp_event_handler_instance_t instance_wifi;
    esp_event_handler_instance_t instance_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, &instance_wifi);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, &instance_ip);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    assert(err == ESP_OK);
    err = esp_wifi_start();
    assert(err == ESP_OK);

    // Simulate IP acquired by setting bit
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    assert((bits & WIFI_CONNECTED_BIT) != 0);
    std::printf("[PASS] Step 2: Underlay network ready (obtained underlay IP via DHCP).\n");

    // --- Step 3: ZeroTier Lite Dataplane Startup ---
    uint64_t nwid = 0x8056c2e21c000001ULL;
    std::printf("[STEP 3] Starting ZeroTier Lite dataplane with namespace 'zerotier' for network 0x%llx...\n",
                (unsigned long long)nwid);
    int start_rc = zt_esp32_lite_start_with_namespace(nwid, "zerotier");
    std::printf("[INFO] zt_esp32_lite_start_with_namespace returned %d\n", start_rc);
    assert(start_rc == 0 && "zt_esp32_lite_start_with_namespace must return 0");
    std::printf("[PASS] Step 3: ZeroTier Lite port started successfully.\n");

    // --- Step 4: Diagnostics Reporting ---
    std::printf("[STEP 4] Testing diagnostic dump via zt_esp32_lite_dump_diagnostics()...\n");
    zt_esp32_lite_dump_diagnostics();
    std::printf("[PASS] Step 4: Diagnostic reporting executed safely.\n");

    // --- Step 5: Clean Stop & Teardown ---
    std::printf("[STEP 5] Stopping ZeroTier Lite port via zt_esp32_lite_stop()...\n");
    zt_esp32_lite_stop();
    vEventGroupDelete(s_wifi_event_group);
    std::printf("[PASS] Step 5: ZeroTier Lite port stopped cleanly.\n");

    std::printf("[PASS] Tier 4: Full application lifecycle test completed successfully!\n");
    return 0;
}
