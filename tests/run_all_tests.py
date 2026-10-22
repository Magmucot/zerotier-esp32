#!/usr/bin/env python3
"""
ZeroTier-ESP32 Unified Test Runner
Executes all host tests across Tiers 1-4:
- Tier 1: Requirement & Feature Coverage (R1 - R5)
- Tier 2: Boundaries & Edge Cases (multicast scale, IP limits, lwIP timeouts)
- Tier 3: Cross-Feature Interactions (renumbering + netmask, frame dispatch during sync)
- Tier 4: Realistic Scenarios & System Hygiene (full app lifecycle, symbol hygiene, R6 static checks)

Usage:
    python3 tests/run_all_tests.py [--tier {1,2,3,4}] [--strict] [--verbose]
"""

import sys
import os
import time
import argparse
import subprocess
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[1]
MOCK_IDF = WORKSPACE / "tests" / "mock_idf"
NODE_DIR = WORKSPACE / "ZeroTierOne" / "node"
OSDEP_DIR = WORKSPACE / "ZeroTierOne" / "osdep"
INCLUDE_DIR = WORKSPACE / "ZeroTierOne" / "include"
PORT_DIR = WORKSPACE / "components" / "zerotier_node" / "port"
STUBS_DIR = PORT_DIR / "stubs"

# Base compilation flags
COMMON_INCLUDES = [
    f"-I{MOCK_IDF}",
    f"-I{INCLUDE_DIR}",
    f"-I{NODE_DIR}",
    f"-I{OSDEP_DIR}",
    f"-I{PORT_DIR}",
    f"-I{STUBS_DIR}",
]

# Definition of all tests across Tiers 1 to 4
TEST_DEFINITIONS = [
    # --- Tier 1: Feature Coverage ---
    {
        "id": "T1-R1",
        "tier": 1,
        "name": "R1 Multicaster Stack Usage",
        "type": "python",
        "script": WORKSPACE / "tests" / "tier1_feature_coverage" / "test_r1_multicaster_stack.py",
        "expected_pass_unpatched": True,
        "description": "Verifies Multicaster::send stack usage <= 512 bytes via -fstack-usage"
    },
    {
        "id": "T1-R2",
        "tier": 1,
        "name": "R2 lwIP Capture Thread Safety",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier1_feature_coverage" / "test_r2_lwip_capture_safety.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-fsanitize=address,undefined", "-g", "-O1"],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "ASan verification of captureTcpipThread late callback without UAR/UAF"
    },
    {
        "id": "T1-R3",
        "tier": 1,
        "name": "R3 NetworkState Netmask & Two-Phase Sync",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier1_feature_coverage" / "test_r3_networkstate_sync.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32NetworkState.cpp",
            OSDEP_DIR / "ESP32EthernetTap.cpp",
            OSDEP_DIR / "ESP32NetLink.cpp",
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            OSDEP_DIR / "ESP32FramePool.cpp",
            OSDEP_DIR / "ESP32EgressTrace.cpp",
            OSDEP_DIR / "ESP32NodeTask.cpp",
            NODE_DIR / "InetAddress.cpp",
            NODE_DIR / "Utils.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-include", str(MOCK_IDF / "lwip" / "ip4_addr.h")],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": False,  # Known bug P2-11 in unpatched code (netmask drop)
        "description": "Validates netmask update propagation (/24 -> /16) and 4-IP sync"
    },
    {
        "id": "T1-R4",
        "tier": 1,
        "name": "R4 main.cpp ESP32-ZeroTier-Lite Flow",
        "type": "python",
        "script": WORKSPACE / "tests" / "tier1_feature_coverage" / "test_r4_main_symbols.py",
        "expected_pass_unpatched": False,  # main.cpp not migrated yet in unpatched code
        "description": "Validates zt_esp32_lite_* calls, Wi-Fi STA wait, and absence of legacy stubs in main.cpp"
    },
    {
        "id": "T1-R5",
        "tier": 1,
        "name": "R5 Legacy Stub Removal & Clean Linkage",
        "type": "python",
        "script": WORKSPACE / "tests" / "tier1_feature_coverage" / "test_r5_clean_linkage.py",
        "expected_pass_unpatched": False,  # Dead stubs still listed in components CMakeLists.txt
        "description": "Validates removal of dead stubs from CMakeLists.txt and Kconfig fragment hook"
    },

    # --- Tier 2: Boundaries & Edge Cases ---
    {
        "id": "T2-MC-BOUNDS",
        "tier": 2,
        "name": "Multicast Subscriber Boundaries (0, 1, 32, 33, 64)",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier2_boundaries" / "test_multicast_subscriber_boundaries.cpp",
        "extra_srcs": [
            NODE_DIR / "Multicaster.cpp",
            NODE_DIR / "Utils.cpp",
            NODE_DIR / "OutboundMulticast.cpp",
            NODE_DIR / "Salsa20.cpp",
            PORT_DIR / "zt_metrics_esp.cpp"
        ],
        "cxx_flags": ["-std=c++17"],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "Boundary counts: 0 (empty), 1 (single), 32 (stack limit), 33 (heap fallback), 64 (scale)"
    },
    {
        "id": "T2-IP-LIMITS",
        "tier": 2,
        "name": "Address Table Limits & 4-IP Renumbering",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier2_boundaries" / "test_address_table_limits.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32NetworkState.cpp",
            OSDEP_DIR / "ESP32EthernetTap.cpp",
            OSDEP_DIR / "ESP32NetLink.cpp",
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            OSDEP_DIR / "ESP32FramePool.cpp",
            OSDEP_DIR / "ESP32EgressTrace.cpp",
            OSDEP_DIR / "ESP32NodeTask.cpp",
            NODE_DIR / "InetAddress.cpp",
            NODE_DIR / "Utils.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-include", str(MOCK_IDF / "lwip" / "ip4_addr.h")],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "Address table bounds: 0 IPs, 4 IPs max, capacity enforcement, and simultaneous 4-IP swap"
    },
    {
        "id": "T2-LWIP-TIMEOUTS",
        "tier": 2,
        "name": "lwIP Timeout Scenarios (0, Delayed, Idempotency)",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier2_boundaries" / "test_lwip_timeout_scenarios.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-fsanitize=address,undefined", "-g", "-O1"],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "Timeout=0 edge case, post-timeout late callback safety, and idempotency"
    },

    # --- Tier 3: Cross-Feature Combinations ---
    {
        "id": "T3-RENUMBER-MASK",
        "tier": 3,
        "name": "Simultaneous Renumbering & Netmask Update",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier3_cross_feature" / "test_renumbering_and_netmask_combo.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32NetworkState.cpp",
            OSDEP_DIR / "ESP32EthernetTap.cpp",
            OSDEP_DIR / "ESP32NetLink.cpp",
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            OSDEP_DIR / "ESP32FramePool.cpp",
            OSDEP_DIR / "ESP32EgressTrace.cpp",
            OSDEP_DIR / "ESP32NodeTask.cpp",
            NODE_DIR / "InetAddress.cpp",
            NODE_DIR / "Utils.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-include", str(MOCK_IDF / "lwip" / "ip4_addr.h")],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": False,  # Fails due to netmask drop during IP renumbering
        "description": "Simultaneous 2-IP swap and netmask update (/24 -> /16) in single config update"
    },
    {
        "id": "T3-CONCURRENT-SYNC",
        "tier": 3,
        "name": "Virtual Frame Dispatch During State Sync",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier3_cross_feature" / "test_virtual_frame_during_sync.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32NetworkState.cpp",
            OSDEP_DIR / "ESP32EthernetTap.cpp",
            OSDEP_DIR / "ESP32NetLink.cpp",
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            OSDEP_DIR / "ESP32FramePool.cpp",
            OSDEP_DIR / "ESP32EgressTrace.cpp",
            OSDEP_DIR / "ESP32NodeTask.cpp",
            NODE_DIR / "InetAddress.cpp",
            NODE_DIR / "Utils.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-include", str(MOCK_IDF / "lwip" / "ip4_addr.h")],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "Concurrent frame injection thread during 50 rapid config updates without deadlock"
    },

    # --- Tier 4: Realistic Scenarios & System Hygiene ---
    {
        "id": "T4-APP-LIFECYCLE",
        "tier": 4,
        "name": "Full Application Lifecycle (NVS -> Wi-Fi -> ZT -> Stop)",
        "type": "cpp",
        "src": WORKSPACE / "tests" / "tier4_realistic_scenarios" / "test_full_app_lifecycle.cpp",
        "extra_srcs": [
            OSDEP_DIR / "ESP32LiteAppGlue.cpp",
            OSDEP_DIR / "ESP32LitePort.cpp",
            OSDEP_DIR / "ESP32LiteDiagnostics.cpp",
            OSDEP_DIR / "ESP32NodeTask.cpp",
            OSDEP_DIR / "ESP32EthernetTap.cpp",
            OSDEP_DIR / "ESP32NetworkState.cpp",
            OSDEP_DIR / "ESP32NetLink.cpp",
            OSDEP_DIR / "ESP32LwIPGlue.cpp",
            OSDEP_DIR / "ESP32FramePool.cpp",
            OSDEP_DIR / "ESP32EgressTrace.cpp",
            NODE_DIR / "InetAddress.cpp",
            NODE_DIR / "Utils.cpp",
            MOCK_IDF / "mock_idf.cpp"
        ],
        "cxx_flags": ["-std=c++17", "-DESP_PLATFORM", "-include", str(MOCK_IDF / "lwip" / "ip4_addr.h")],
        "ld_flags": ["-lpthread"],
        "expected_pass_unpatched": True,
        "description": "Complete lifecycle: NVS init, Wi-Fi STA wait, start_with_namespace, diagnostics, clean stop"
    },
    {
        "id": "T4-SYMBOL-HYGIENE",
        "tier": 4,
        "name": "Binary Symbol Hygiene & Linkage Audit",
        "type": "python",
        "script": WORKSPACE / "tests" / "tier4_realistic_scenarios" / "test_binary_symbol_hygiene.py",
        "expected_pass_unpatched": False,  # Flags legacy stubs in main.cpp and CMakeLists.txt
        "description": "Verifies real dataplane symbols exported and no references to legacy stubs"
    },
    {
        "id": "T4-R6-STATIC-CHECKS",
        "tier": 4,
        "name": "R6 Tracking & Static Code Checks",
        "type": "python",
        "script": WORKSPACE / "tests" / "tier4_realistic_scenarios" / "test_r6_tracking_and_static_checks.py",
        "expected_pass_unpatched": True,
        "description": "Verifies Identity exception throws (P0-3), no memory override (P0-4), and ISSUES tracking"
    }
]

def compile_cpp_test(test_def, bin_path, verbose=False):
    srcs = [str(test_def["src"])] + [str(s) for s in test_def.get("extra_srcs", [])]
    cmd = [
        "g++",
        *test_def.get("cxx_flags", ["-std=c++17"]),
        *COMMON_INCLUDES,
        *srcs,
        *test_def.get("ld_flags", []),
        "-o", str(bin_path)
    ]
    if verbose:
        print(f"[BUILD] {' '.join(cmd)}")
    t0 = time.time()
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    dt = time.time() - t0
    return res.returncode == 0, res.stdout, res.stderr, dt

def run_test(test_def, verbose=False):
    t_id = test_def["id"]
    t_type = test_def["type"]
    name = test_def["name"]
    expected_pass = test_def.get("expected_pass_unpatched", True)

    t0 = time.time()
    if t_type == "python":
        script = str(test_def["script"])
        cmd = [sys.executable, script]
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        dt = time.time() - t0
        passed = (proc.returncode == 0)
        output = proc.stdout + ("\nSTDERR:\n" + proc.stderr if proc.stderr else "")
    elif t_type == "cpp":
        bin_path = Path(f"/tmp/{t_id.lower().replace('-', '_')}_bin")
        if bin_path.exists():
            bin_path.unlink()
        build_ok, b_out, b_err, b_dt = compile_cpp_test(test_def, bin_path, verbose=verbose)
        if not build_ok:
            dt = time.time() - t0
            passed = False
            output = f"COMPILATION ERROR (took {b_dt:.2f}s):\nSTDOUT:\n{b_out}\nSTDERR:\n{b_err}"
            return passed, expected_pass, dt, output
        # Execute binary
        run_proc = subprocess.run([str(bin_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        dt = time.time() - t0
        passed = (run_proc.returncode == 0)
        output = run_proc.stdout + ("\nSTDERR:\n" + run_proc.stderr if run_proc.stderr else "")
    else:
        passed = False
        dt = 0
        output = f"Unknown test type: {t_type}"

    return passed, expected_pass, dt, output

def main():
    parser = argparse.ArgumentParser(description="ZeroTier-ESP32 Test Runner")
    parser.add_argument("--tier", type=int, choices=[1, 2, 3, 4], help="Run only specific tier")
    parser.add_argument("--strict", action="store_true", help="Fail if any test fails, ignoring expected unpatched status")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show verbose outputs and compilation commands")
    args = parser.parse_args()

    tests_to_run = TEST_DEFINITIONS
    if args.tier:
        tests_to_run = [t for t in TEST_DEFINITIONS if t["tier"] == args.tier]

    print("=" * 80)
    print("ZeroTier-ESP32 Test Suite Execution")
    print(f"Targeting {len(tests_to_run)} tests across {'Tier ' + str(args.tier) if args.tier else 'Tiers 1-4'}")
    print(f"Mode: {'STRICT' if args.strict else 'DIAGNOSTIC (flags known unpatched bugs as EXPECTED_FAIL)'}")
    print("=" * 80)

    results = []
    total_start = time.time()

    for t in tests_to_run:
        print(f"\n>>> Running [{t['id']}] {t['name']} (Tier {t['tier']})...")
        passed, exp_pass, dt, output = run_test(t, verbose=args.verbose)

        # Categorize status
        if passed:
            status_str = "PASS"
        else:
            if not exp_pass and not args.strict:
                status_str = "EXPECTED_FAIL (bug detected)"
            else:
                status_str = "FAIL"

        print(f"    Result: {status_str} ({dt:.2f}s)")
        if args.verbose or (status_str == "FAIL"):
            print("-" * 60)
            print(output.strip())
            print("-" * 60)

        results.append({
            "def": t,
            "passed": passed,
            "expected_pass": exp_pass,
            "status_str": status_str,
            "duration": dt,
            "output": output
        })

    total_duration = time.time() - total_start

    print("\n" + "=" * 80)
    print("TEST EXECUTION SUMMARY MATRIX")
    print("=" * 80)
    print(f"{'ID':<18} | {'Tier':<4} | {'Status':<28} | {'Time (s)':<8} | {'Description'}")
    print("-" * 80)

    pass_count = 0
    exp_fail_count = 0
    unexpected_fail_count = 0

    for r in results:
        t = r["def"]
        s = r["status_str"]
        dt = r["duration"]
        desc = t["description"][:50]
        print(f"{t['id']:<18} | T{t['tier']:<3} | {s:<28} | {dt:>7.2f}s | {desc}")

        if r["passed"]:
            pass_count += 1
        elif not r["expected_pass"] and not args.strict:
            exp_fail_count += 1
        else:
            unexpected_fail_count += 1

    print("-" * 80)
    print(f"Total: {len(results)} | Passed: {pass_count} | Expected Bug Catches: {exp_fail_count} | Unexpected Failures: {unexpected_fail_count} | Total Duration: {total_duration:.2f}s")
    print("=" * 80)

    if unexpected_fail_count > 0:
        print(f"\n[FAIL] Test suite completed with {unexpected_fail_count} unexpected failures.")
        sys.exit(1)
    else:
        print("\n[SUCCESS] All test cases executed correctly and verified expected behavior.")
        if exp_fail_count > 0:
            print(f"Note: {exp_fail_count} tests accurately caught open bugs on unpatched files (proving test sensitivity).")
        sys.exit(0)

if __name__ == "__main__":
    main()
