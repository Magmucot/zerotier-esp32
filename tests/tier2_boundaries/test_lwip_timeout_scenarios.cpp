#include "ESP32LwIPGlue.hpp"
#include "lwip/tcpip.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstdlib>
#include <cassert>

using namespace ZeroTier;

int main(int argc, char** argv) {
    std::printf("[TEST RUN] Starting Tier 2: lwIP Timeout Scenarios & Edge Cases Test\n");

    // Scenario 1: Timeout = 0 edge case with simulated timeout
    mock_lwip_reset();
    mock_lwip_set_callback_delay(true);
    mock_lwip_set_simulate_timeout(true);

    std::printf("[INFO] Scenario 1: captureTcpipThread(0) with timeout...\n");
    bool res0 = ESP32LwIPGlue::captureTcpipThread(0);
    assert(!res0 && "Should return false when timed out with timeout=0");
    std::printf("[PASS] Scenario 1: timeout=0 handled safely\n");

    // Scenario 2: Late callback invocation after timeout=0
    std::printf("[INFO] Scenario 2: Executing late callback after timeout=0...\n");
    mock_lwip_execute_pending_callback();
    std::printf("[PASS] Scenario 2: Late callback executed safely\n");

    // Scenario 3: Idempotency check - now that thread task is captured, subsequent calls return true
    std::printf("[INFO] Scenario 3: Idempotency check (already captured)...\n");
    bool res_subsequent = ESP32LwIPGlue::captureTcpipThread(100);
    assert(res_subsequent && "captureTcpipThread should return true immediately when already captured");
    std::printf("[PASS] Scenario 3: Idempotency confirmed (immediate return true)\n");

    // Scenario 4: Multiple calls in succession do not re-queue or leak
    for (int i = 0; i < 5; ++i) {
        bool res_repeat = ESP32LwIPGlue::captureTcpipThread(50);
        assert(res_repeat);
    }
    std::printf("[PASS] Scenario 4: Repeated calls are stable and safe\n");

    std::printf("[PASS] Tier 2: lwIP timeout scenarios and edge cases test passed!\n");
    return 0;
}
