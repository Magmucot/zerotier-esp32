/* Small C ABI helpers for ESP-IDF apps/QEMU harnesses.
 *
 * These wrappers let app_main() start the clean ESP32-Lite port without knowing
 * C++ class details. They intentionally do not compose QEMU flash images or NVS
 * partitions; the app/test harness still owns board/QEMU setup.
 */
#ifndef ZT_ESP32LITEAPPGLUE_HPP
#define ZT_ESP32LITEAPPGLUE_HPP

#include <stdint.h>
#include "ESP32LitePort.hpp"

#ifdef __cplusplus
extern "C" {
#endif

ZeroTier::ESP32LitePort* zt_esp32_lite_default_port(void);
int zt_esp32_lite_nvs_init(void);
int zt_esp32_lite_start_default(uint64_t nwid);
int zt_esp32_lite_start_with_namespace(uint64_t nwid, const char* nvs_namespace);
int zt_esp32_lite_join(uint64_t nwid);
void zt_esp32_lite_stop(void);
int zt_esp32_lite_identity_ready(void);
uint64_t zt_esp32_lite_node_id(void);
void zt_esp32_lite_dump_diagnostics(void);

#ifdef __cplusplus
}
#endif

#endif
