#!/usr/bin/env python3
"""
Tier 1: R4 main.cpp ESP32-ZeroTier-Lite Dataplane Flow & Symbol Check
Verifies that main/main.cpp follows the ESP32-ZeroTier-Lite flow:
- zt_esp32_lite_nvs_init()
- Wi-Fi Station setup & IP_EVENT_STA_GOT_IP wait
- zt_esp32_lite_start_with_namespace(nwid, "zerotier")
- zt_esp32_lite_dump_diagnostics()
- Removal of legacy stubs and Swedish logs.
Authoritative source: ORIGINAL_REQUEST.md §R4, PROJECT.md §Milestone M3.
"""

import sys
import re
from pathlib import Path

def test_main_dataplane_flow():
    workspace = Path(__file__).resolve().parents[2]
    main_cpp = workspace / "main" / "main.cpp"
    main_cmake = workspace / "main" / "CMakeLists.txt"

    if not main_cpp.exists():
        print(f"[FAIL] {main_cpp} not found")
        return False

    content = main_cpp.read_text(encoding="utf-8", errors="replace")

    checks = []

    # 1. zt_esp32_lite_nvs_init
    has_lite_nvs = "zt_esp32_lite_nvs_init" in content
    checks.append(("Calls zt_esp32_lite_nvs_init()", has_lite_nvs))

    # 2. Wi-Fi Station underlay initialization
    has_wifi_init = "esp_wifi_init" in content and "WIFI_MODE_STA" in content
    checks.append(("Initializes Wi-Fi Station mode", has_wifi_init))

    # 3. Wait for IP_EVENT_STA_GOT_IP
    has_got_ip = "IP_EVENT_STA_GOT_IP" in content
    checks.append(("Waits for underlay IP (IP_EVENT_STA_GOT_IP)", has_got_ip))

    # 4. zt_esp32_lite_start_with_namespace or start_default
    has_lite_start = "zt_esp32_lite_start_with_namespace" in content or "zt_esp32_lite_start_default" in content
    checks.append(("Starts ZeroTier via zt_esp32_lite_start_*", has_lite_start))

    # 5. Diagnostics reporting
    has_diagnostics = "zt_esp32_lite_dump_diagnostics" in content
    checks.append(("Includes zt_esp32_lite_dump_diagnostics()", has_diagnostics))

    # 6. Absence of legacy raw stubs declarations
    legacy_stubs = ["zt_state_get_impl", "zt_wire_send_impl", "zt_virtual_frame_impl", "zt_socket_init"]
    found_legacy = [stub for stub in legacy_stubs if stub in content]
    checks.append(("Elimination of legacy raw stub calls in main.cpp", len(found_legacy) == 0))

    # 7. Absence of Swedish strings
    swedish_markers = ["är online", "misslyckades", "ledigt före", "förbrukat av"]
    found_swedish = [m for m in swedish_markers if m in content]
    checks.append(("Clean professional logging (no Swedish placeholder logs)", len(found_swedish) == 0))

    # Check main/CMakeLists.txt dependencies
    cmake_content = main_cmake.read_text() if main_cmake.exists() else ""
    has_cmake_deps = all(dep in cmake_content for dep in ["esp_wifi", "esp_netif", "nvs_flash", "zerotier_node"])
    checks.append(("main/CMakeLists.txt declares esp_wifi, esp_netif, nvs_flash, zerotier_node", has_cmake_deps))

    all_passed = True
    print("[TEST RUN] Evaluating main/main.cpp dataplane flow:")
    for description, passed in checks:
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {description}")
        if not passed:
            all_passed = False

    return all_passed

if __name__ == "__main__":
    passed = test_main_dataplane_flow()
    sys.exit(0 if passed else 1)
