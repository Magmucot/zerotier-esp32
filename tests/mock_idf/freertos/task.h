#ifndef TESTS_MOCK_FREERTOS_TASK_H
#define TESTS_MOCK_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);

TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       const uint32_t usStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char * const pcName,
                                   const uint32_t usStackDepth,
                                   void * const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t * const pxCreatedTask,
                                   const BaseType_t xCoreID);
void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskDelay(const TickType_t xTicksToDelay);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);

// Test hook to set the mock current task handle
void mock_freertos_set_current_task(TaskHandle_t task);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_FREERTOS_TASK_H
