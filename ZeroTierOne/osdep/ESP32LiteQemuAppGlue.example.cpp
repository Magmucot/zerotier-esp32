/* Example app_main() glue for the clean ESP32-Lite port.
 *
 * This is intentionally not wired into the library build. Copy/adapt it into
 * /qemu-project/main/main.cpp after the local QEMU/open_eth/Wi-Fi underlay is
 * initialized and DHCP has provided an underlay IP.
 */
#include "ESP32LiteAppGlue.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef ZT_ESP32_DEFAULT_NWID
#define ZT_ESP32_DEFAULT_NWID 0xdb64858fed0b2566ULL
#endif

extern "C" void app_main(void)
{
    ESP_LOGI("ZT_APP", "initializing NVS");
    if (zt_esp32_lite_nvs_init() != 0) {
        ESP_LOGE("ZT_APP", "NVS init failed");
        return;
    }

    /* Local app responsibility: initialize QEMU open_eth or Wi-Fi and wait for underlay IP before starting ZeroTier. */
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI("ZT_APP", "starting ZeroTier ESP32 Lite");
    int rc = zt_esp32_lite_start_with_namespace(ZT_ESP32_DEFAULT_NWID, "zerotier");
    ESP_LOGI("ZT_APP", "zt start rc=%d node=%llx identity_ready=%d", rc, (unsigned long long)zt_esp32_lite_node_id(), zt_esp32_lite_identity_ready());

    for (;;) {
        zt_esp32_lite_dump_diagnostics();
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
