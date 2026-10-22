#ifndef TESTS_MOCK_FREERTOS_FREERTOS_H
#define TESTS_MOCK_FREERTOS_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;

#define pdFALSE ((BaseType_t)0)
#define pdTRUE  ((BaseType_t)1)
#define pdPASS  (pdTRUE)
#define pdFAIL  (pdFALSE)

#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define portTICK_PERIOD_MS ((TickType_t)10)
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_FREERTOS_FREERTOS_H
