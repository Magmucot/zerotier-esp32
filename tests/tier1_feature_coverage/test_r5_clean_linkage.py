#!/usr/bin/env python3
"""
Tier 1: R5 Legacy Stub Removal & Clean Linkage Check
Verifies:
- Dead stubs (zt_tap_esp.cpp, zt_socket_esp.cpp, zt_storage_esp.cpp, zt_mutex.cpp) are removed or not listed in CMakeLists.txt.
- Required stubs (zt_utils_esp.cpp, zt_metrics_esp.cpp, zt_bond_esp.cpp, zt_trace_esp.cpp, zt_osutils_esp.cpp, zt_route_debug_esp.cpp) are retained.
- components/zerotier_node/Kconfig connects ESP32LitePortKconfig.fragment (Issue P3-3).
Authoritative source: ORIGINAL_REQUEST.md §R5, PROJECT.md §Feature 4.
"""

import sys
from pathlib import Path

def test_clean_linkage():
    workspace = Path(__file__).resolve().parents[2]
    component_cmake = workspace / "components" / "zerotier_node" / "CMakeLists.txt"
    component_kconfig = workspace / "components" / "zerotier_node" / "Kconfig"
    port_dir = workspace / "components" / "zerotier_node" / "port"

    if not component_cmake.exists():
        print(f"[FAIL] {component_cmake} not found")
        return False

    cmake_content = component_cmake.read_text()
    checks = []

    # 1. Dead stubs should NOT be compiled in CMakeLists.txt
    dead_stubs = ["zt_tap_esp.cpp", "zt_socket_esp.cpp", "zt_storage_esp.cpp", "zt_mutex.cpp"]
    stubs_in_cmake = [s for s in dead_stubs if s in cmake_content]
    checks.append((
        f"Dead stubs excluded from components/zerotier_node/CMakeLists.txt (found: {stubs_in_cmake})",
        len(stubs_in_cmake) == 0
    ))

    # 2. Required stubs MUST be present in CMakeLists.txt
    required_stubs = [
        "zt_utils_esp.cpp",
        "zt_metrics_esp.cpp",
        "zt_bond_esp.cpp",
        "zt_trace_esp.cpp",
        "zt_osutils_esp.cpp",
        "zt_route_debug_esp.cpp"
    ]
    missing_required = [s for s in required_stubs if s not in cmake_content]
    checks.append((
        f"Required stubs present in CMakeLists.txt (missing: {missing_required})",
        len(missing_required) == 0
    ))

    # 3. Kconfig connects ESP32LitePortKconfig.fragment (Issue P3-3)
    kconfig_ok = False
    if component_kconfig.exists():
        kconfig_content = component_kconfig.read_text()
        kconfig_ok = "ESP32LitePortKconfig.fragment" in kconfig_content
    checks.append((
        "components/zerotier_node/Kconfig references ESP32LitePortKconfig.fragment (P3-3)",
        kconfig_ok
    ))

    all_passed = True
    print("[TEST RUN] Evaluating R5 Legacy Stub Removal & Clean Linkage:")
    for desc, passed in checks:
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {desc}")
        if not passed:
            all_passed = False

    return all_passed

if __name__ == "__main__":
    passed = test_clean_linkage()
    sys.exit(0 if passed else 1)
