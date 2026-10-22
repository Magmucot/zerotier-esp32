#ifndef TESTS_MOCK_ESP_ERR_H
#define TESTS_MOCK_ESP_ERR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107

const char* esp_err_to_name(esp_err_t code);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_ESP_ERR_H
