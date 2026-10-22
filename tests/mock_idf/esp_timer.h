#ifndef TESTS_MOCK_ESP_TIMER_H
#define TESTS_MOCK_ESP_TIMER_H

#include <stdint.h>
#include <chrono>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_ESP_TIMER_H
