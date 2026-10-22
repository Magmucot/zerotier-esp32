#ifndef TESTS_MOCK_FREERTOS_SEMPHR_H
#define TESTS_MOCK_FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef QueueHandle_t SemaphoreHandle_t;

#define xSemaphoreCreateBinary() xQueueCreate(1, 0)
#define xSemaphoreCreateMutex() xQueueCreate(1, 0)
#define xSemaphoreTake(xSemaphore, xBlockTime) xQueueReceive(xSemaphore, NULL, xBlockTime)
#define xSemaphoreGive(xSemaphore) xQueueSend(xSemaphore, NULL, 0)
#define vSemaphoreDelete(xSemaphore) vQueueDelete(xSemaphore)

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_FREERTOS_SEMPHR_H
