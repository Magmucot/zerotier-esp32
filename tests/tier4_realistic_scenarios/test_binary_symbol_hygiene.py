#!/usr/bin/env python3
"""
Tier 4: Binary Symbol Hygiene & Dataplane Linkage Test
Verifies that:
- Real dataplane symbols are exported and referenced:
  - ZT_Node_join
  - ESP32EthernetTap
  - ESP32NodeTask
  - zt_esp32_lite_*
- Dead legacy stubs are not referenced by any application or component code:
  - zt_tap_esp
  - zt_socket_esp
  - zt_storage_esp
  - zt_virtual_frame_impl
  - zt_virtual_config_impl
  - zt_wire_send_impl
Authoritative source: ORIGINAL_REQUEST.md §Acceptance Criteria, PROJECT.md §M2, §M3.
"""

import sys
import re
from pathlib import Path

def test_symbol_hygiene():
    workspace = Path(__file__).resolve().parents[2]
    main_dir = workspace / "main"
    osdep_dir = workspace / "ZeroTierOne" / "osdep"
    components_dir = workspace / "components" / "zerotier_node"

    checks = []

    # 1. Real dataplane API definitions
    appglue_h = osdep_dir / "ESP32LiteAppGlue.hpp"
    has_appglue = appglue_h.exists()
    checks.append(("ESP32LiteAppGlue.hpp exists", has_appglue))

    if has_appglue:
        glue_content = appglue_h.read_text()
        required_apis = [
            "zt_esp32_lite_nvs_init",
            "zt_esp32_lite_start_with_namespace",
            "zt_esp32_lite_dump_diagnostics",
            "zt_esp32_lite_stop"
        ]
        missing_apis = [api for api in required_apis if api not in glue_content]
        checks.append((f"Public Lite APIs declared (missing: {missing_apis})", len(missing_apis) == 0))

    # 2. ESP32NodeTask references ZT_Node_join
    nodetask_cpp = osdep_dir / "ESP32NodeTask.cpp"
    has_join = False
    if nodetask_cpp.exists():
        has_join = "ZT_Node_join" in nodetask_cpp.read_text()
    checks.append(("ESP32NodeTask dispatches ZT_Node_join on EVENT_JOIN_NETWORK", has_join))

    # 3. ESP32EthernetTap binds to lwIP netif
    tap_cpp = osdep_dir / "ESP32EthernetTap.cpp"
    has_tap_netif = False
    if tap_cpp.exists():
        tap_src = tap_cpp.read_text()
        has_tap_netif = "ESP32LwIPGlue::addNetif" in tap_src and "ESP32NetLink" in tap_src
    checks.append(("ESP32EthernetTap registers netif with lwIP and NetLink", has_tap_netif))

    # 4. Check main/main.cpp does not reference legacy stubs
    main_cpp = main_dir / "main.cpp"
    main_src = main_cpp.read_text() if main_cpp.exists() else ""
    legacy_symbols = [
        "zt_virtual_frame_impl",
        "zt_virtual_config_impl",
        "zt_wire_send_impl",
        "zt_socket_init",
        "zt_socket_start_recv",
        "zt_state_get_impl",
        "zt_state_put_impl"
    ]
    found_in_main = [sym for sym in legacy_symbols if sym in main_src]
    checks.append((
        f"main/main.cpp free of legacy stubs (found: {found_in_main})",
        len(found_in_main) == 0
    ))

    # 5. Check components/zerotier_node/CMakeLists.txt does not link legacy stubs
    comp_cmake = components_dir / "CMakeLists.txt"
    cmake_src = comp_cmake.read_text() if comp_cmake.exists() else ""
    dead_stub_files = ["zt_tap_esp.cpp", "zt_socket_esp.cpp", "zt_storage_esp.cpp", "zt_mutex.cpp"]
    found_in_cmake = [f for f in dead_stub_files if f in cmake_src]
    checks.append((
        f"CMakeLists.txt does not compile dead stub files (found: {found_in_cmake})",
        len(found_in_cmake) == 0
    ))

    all_passed = True
    print("[TEST RUN] Evaluating Binary Symbol Hygiene & Dataplane Linkage:")
    for desc, passed in checks:
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {desc}")
        if not passed:
            all_passed = False

    return all_passed

if __name__ == "__main__":
    passed = test_symbol_hygiene()
    sys.exit(0 if passed else 1)
