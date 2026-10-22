#ifndef TESTS_MOCK_FREERTOS_EVENT_GROUPS_H
#define TESTS_MOCK_FREERTOS_EVENT_GROUPS_H

#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* EventGroupHandle_t;
typedef uint32_t EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);
EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear);
void vEventGroupDelete(EventGroupHandle_t xEventGroup);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_FREERTOS_EVENT_GROUPS_H
