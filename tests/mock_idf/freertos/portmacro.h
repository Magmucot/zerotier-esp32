#ifndef TESTS_MOCK_FREERTOS_PORTMACRO_H
#define TESTS_MOCK_FREERTOS_PORTMACRO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0

static inline void taskENTER_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
static inline void taskEXIT_CRITICAL(portMUX_TYPE *mux) { (void)mux; }
#define portENTER_CRITICAL(mux) taskENTER_CRITICAL(mux)
#define portEXIT_CRITICAL(mux) taskEXIT_CRITICAL(mux)

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_FREERTOS_PORTMACRO_H
