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
    std::printf("[TEST RUN] Starting Tier 1 R2: lwIP captureTcpipThread safety probe\n");

    // Phase 1: Test timeout scenario with delayed callback
    mock_lwip_reset();
    mock_lwip_set_callback_delay(true);        // Do not execute callback immediately
    mock_lwip_set_simulate_timeout(true);      // Simulate sys_arch_sem_wait timing out

    std::printf("[INFO] Calling captureTcpipThread with simulated timeout...\n");
    bool captured_during_timeout = ESP32LwIPGlue::captureTcpipThread(10);
    std::printf("[INFO] captureTcpipThread returned: %d (expected 0 on timeout)\n", captured_during_timeout);
    assert(!captured_during_timeout && "captureTcpipThread should return false on timeout");

    // Verify callback was registered
    assert(mock_lwip_has_pending_callback() && "tcpip_callback should have queued a callback");

    // Phase 2: Delayed callback execution after caller has returned
    // On unpatched code with stack allocation, this causes Stack-Use-After-Return (or UAF on freed semaphore).
    std::printf("[INFO] Executing delayed callback post-timeout in tcpip_thread context...\n");
    mock_lwip_execute_pending_callback();
    std::printf("[INFO] Delayed callback executed safely without crash.\n");

    // Phase 3: Subsequent captureTcpipThread should succeed now that task was captured
    mock_lwip_set_simulate_timeout(false);
    mock_lwip_set_callback_delay(false);
    bool captured_now = ESP32LwIPGlue::captureTcpipThread(10);
    std::printf("[INFO] Subsequent captureTcpipThread returned: %d (expected 1)\n", captured_now);
    assert(captured_now && "captureTcpipThread should return true after task handle is captured");

    std::printf("[PASS] Tier 1 R2: lwIP captureTcpipThread safety probe passed with 0 memory errors.\n");
    return 0;
}
