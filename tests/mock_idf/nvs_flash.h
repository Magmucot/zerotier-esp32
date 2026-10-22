#ifndef TESTS_MOCK_NVS_FLASH_H
#define TESTS_MOCK_NVS_FLASH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_flash_deinit(void);

#define ESP_ERR_NVS_NO_FREE_PAGES 0x110d
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x110e

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_NVS_FLASH_H
