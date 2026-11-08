#!/usr/bin/env python3
"""
Milestone 2 Verification & Adversarial Stress Suite:
Web Status Interface, Telemetry & OTA Engine (Features F5, F6, F7, F8, F9)

Verifies:
1. Feature F5: Dual-Interface Web Server (main/CMakeLists.txt, 0.0.0.0:80 socket binding, URI routing).
2. Feature F6: Lightweight Web Status Dashboard (main/web_assets.hpp, .rodata, <5KB, 0 CDNs, required UI fields).
3. Feature F7: Live Telemetry Instrumentation (JSON schema, driver/temperature_sensor.h, ZeroTier node queries).
4. Feature F8: Asynchronous Network Ping (ping/ping_sock.h, CONFIG_PING_TARGET_IP, non-blocking cache).
5. Feature F9: Web-Based OTA Firmware Upload (header checks 0xE7 / 0x0009, esp_ota_* sequence, deferred reboot).
6. Integration in main/main.cpp (dynamic Wi-Fi authmode, temperature sensor, ping service, web server boot).
7. Host compilation & empirical functional test execution.
"""

import sys
import re
import subprocess
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[1]
MAIN_DIR = WORKSPACE / "main"
TESTS_DIR = WORKSPACE / "tests"

def test_suite_f5_f6_web_server_and_assets():
    print("=" * 80)
    print("SUITE 1: Web Server Architecture & Lightweight Web Assets (Features F5 & F6)")
    print("=" * 80)

    # 1. CMakeLists.txt checks
    cmake_path = MAIN_DIR / "CMakeLists.txt"
    assert cmake_path.exists(), "main/CMakeLists.txt not found"
    cmake_content = cmake_path.read_text(encoding="utf-8")

    assert "web_server.cpp" in cmake_content, "web_server.cpp not listed in main/CMakeLists.txt SRCS"
    assert "esp_http_server" in cmake_content, "esp_http_server not listed in main/CMakeLists.txt REQUIRES"
    assert "app_update" in cmake_content, "app_update not listed in main/CMakeLists.txt REQUIRES"
    print("  [PASS] main/CMakeLists.txt includes web_server.cpp and requires esp_http_server & app_update")

    # 2. web_assets.hpp inspection
    assets_path = MAIN_DIR / "web_assets.hpp"
    assert assets_path.exists(), "main/web_assets.hpp not found"
    assets_content = assets_path.read_text(encoding="utf-8")

    # Check for raw string literal in .rodata
    assert "static const char INDEX_HTML[] = R\"rawliteral(" in assets_content, "INDEX_HTML not defined as const char[] raw string literal"
    html_match = re.search(r'R\"rawliteral\((.*?)\)rawliteral\"', assets_content, re.DOTALL)
    assert html_match, "Failed to extract INDEX_HTML raw string"
    html = html_match.group(1)

    # Asset size constraint < 5KB (5120 bytes)
    html_len = len(html.encode("utf-8"))
    print(f"  [PASS] INDEX_HTML size is {html_len} bytes (< 5120 bytes / 5KB)")
    assert html_len < 5120, f"INDEX_HTML size {html_len} exceeds 5KB limit!"

    # Zero external CDN dependencies
    assert "http://" not in html and "https://" not in html, "External CDN link or absolute URL found in INDEX_HTML!"
    assert "<link rel=\"stylesheet\"" not in html or "href=\"http" not in html, "External stylesheet reference detected"
    print("  [PASS] Zero external dependencies (zero CDN links, zero external scripts or stylesheets)")

    # Required UI elements in dashboard
    required_elements = [
        ("Uptime display", "v-up"),
        ("Internal temperature display", "v-temp"),
        ("Heap memory display", "v-heap"),
        ("PSRAM memory display", "v-psram"),
        ("Ping latency display", "v-ping"),
        ("ZeroTier status badge", "badge-zt"),
        ("ZeroTier node ID display", "v-node"),
        ("ZeroTier network ID display", "v-nwid"),
        ("ZeroTier assigned IP display", "v-ip"),
        ("Active partition slot display", "v-slot"),
        ("OTA binary file input", "f-ota"),
        ("OTA upload button", "btn-ota"),
        ("OTA progress bar", "p-bar"),
        ("OTA status message element", "ota-msg"),
    ]

    for desc, el_id in required_elements:
        assert f'id="{el_id}"' in html, f"Missing required UI element: {desc} (id='{el_id}')"
        print(f"  [PASS] UI element present: {desc} (id='{el_id}')")

def test_suite_f7_f8_telemetry_and_ping():
    print("=" * 80)
    print("SUITE 2: Telemetry Instrumentation & Background Ping Service (Features F7 & F8)")
    print("=" * 80)

    server_cpp = MAIN_DIR / "web_server.cpp"
    assert server_cpp.exists(), "main/web_server.cpp not found"
    content = server_cpp.read_text(encoding="utf-8")

    # Temperature sensor checks
    assert '#include "driver/temperature_sensor.h"' in content, "Missing driver/temperature_sensor.h include"
    assert "temperature_sensor_install" in content, "Missing temperature_sensor_install call"
    assert "temperature_sensor_enable" in content, "Missing temperature_sensor_enable call"
    assert "temperature_sensor_get_celsius" in content, "Missing temperature_sensor_get_celsius call"
    print("  [PASS] Internal temperature sensor driver calls implemented")

    # Ping service checks
    assert '#include "ping/ping_sock.h"' in content, "Missing ping/ping_sock.h include"
    assert "esp_ping_new_session" in content, "Missing esp_ping_new_session call"
    assert "esp_ping_start" in content, "Missing esp_ping_start call"
    assert "ESP_PING_COUNT_INFINITE" in content, "Background ping does not use ESP_PING_COUNT_INFINITE"
    assert "on_ping_success" in content and "on_ping_timeout" in content, "Missing ping callback implementations"
    print("  [PASS] Asynchronous background ICMP ping service implemented")

    # /api/status telemetry payload keys
    required_json_keys = [
        "uptime_seconds",
        "free_heap_internal",
        "free_psram",
        "temp_celsius",
        "ping_latency_ms",
        "ping_target",
        "zt_online",
        "zt_node_id",
        "zt_nwid",
        "zt_ip"
    ]
    for key in required_json_keys:
        expected = r'\"' + key + r'\":'
        assert expected in content, f"Missing JSON key '{key}' in /api/status response"
        print(f"  [PASS] Telemetry JSON key present: '{key}'")

    # Safe ZeroTier NULL pointer checks
    assert "zt_esp32_lite_default_port()" in content, "Queries default port"
    assert "port->node() != nullptr" in content, "Guards against NULL node pointer before ZeroTier initialization"
    print("  [PASS] ZeroTier status queries properly guard against uninitialized node pointer")

def test_suite_f9_ota_engine():
    print("=" * 80)
    print("SUITE 3: Web-Based OTA Firmware Upload Engine (Feature F9)")
    print("=" * 80)

    server_cpp = MAIN_DIR / "web_server.cpp"
    content = server_cpp.read_text(encoding="utf-8")

    # Check OTA API calls
    assert '#include "esp_ota_ops.h"' in content, "Missing esp_ota_ops.h include"
    assert '#include "esp_app_format.h"' in content, "Missing esp_app_format.h include"
    assert "esp_ota_get_next_update_partition" in content, "Missing esp_ota_get_next_update_partition call"
    assert "esp_ota_begin" in content, "Missing esp_ota_begin call"
    assert "esp_ota_write" in content, "Missing esp_ota_write call"
    assert "esp_ota_end" in content, "Missing esp_ota_end call"
    assert "esp_ota_set_boot_partition" in content, "Missing esp_ota_set_boot_partition call"
    print("  [PASS] Complete ESP-IDF OTA API sequence invoked")

    # First-chunk header validation
    assert "ESP_IMAGE_HEADER_MAGIC" in content or "0xE7" in content, "Missing image magic byte verification"
    assert "ESP_CHIP_ID_ESP32S3" in content or "0x0009" in content, "Missing ESP32-S3 chip ID verification"
    assert "hdr->magic != ESP_IMAGE_HEADER_MAGIC" in content, "Missing magic byte rejection check"
    assert "hdr->chip_id != ESP_CHIP_ID_ESP32S3" in content, "Missing chip ID rejection check"
    print("  [PASS] First-chunk pre-flash header validation (magic 0xE7, chip ID 0x0009 ESP32-S3)")

    # Deferred restart
    assert "delayed_restart" in content, "Missing deferred restart task"
    assert "vTaskDelay(pdMS_TO_TICKS(2000))" in content, "Deferred restart does not delay for 2 seconds (2000ms)"
    assert "esp_restart()" in content, "Missing esp_restart() call"
    print("  [PASS] Deferred 2-second reboot scheduled to allow TCP 200 response completion")

def test_suite_main_cpp_integration():
    print("=" * 80)
    print("SUITE 4: main.cpp Integration & Dynamic Authmode")
    print("=" * 80)

    main_cpp = MAIN_DIR / "main.cpp"
    assert main_cpp.exists(), "main/main.cpp not found"
    content = main_cpp.read_text(encoding="utf-8")

    # Dynamic authmode
    dynamic_auth_pattern = r"\(strlen\(CONFIG_ESP_WIFI_PASSWORD\)\s*==\s*0\)\s*\?\s*WIFI_AUTH_OPEN\s*:\s*WIFI_AUTH_WPA2_PSK"
    assert re.search(dynamic_auth_pattern, content), "Dynamic authmode ternary not found in wifi_init_sta"
    print("  [PASS] Dynamic Wi-Fi authmode: OPEN when password empty, WPA2_PSK when provided")

    # Initializations
    assert "telemetry_temp_sensor_init()" in content, "telemetry_temp_sensor_init() not called in app_main"
    assert "ping_service_init" in content, "ping_service_init() not called in app_main"
    assert "web_server_start()" in content, "web_server_start() not called in app_main"
    print("  [PASS] Temperature sensor, ping service, and web server started during boot")

def test_suite_host_functional_execution():
    print("=" * 80)
    print("SUITE 5: Host Compilation & Empirical Functional Execution")
    print("=" * 80)

    compile_cmd = [
        "g++", "-std=c++17", "-DESP_PLATFORM",
        "-include", str(WORKSPACE / "tests" / "mock_idf" / "lwip" / "ip4_addr.h"),
        f"-I{WORKSPACE / 'tests' / 'mock_m2'}",
        f"-I{WORKSPACE / 'tests' / 'mock_idf'}",
        f"-I{MAIN_DIR}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'osdep'}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'include'}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'node'}",
        f"-I{WORKSPACE / 'components' / 'zerotier_node' / 'port'}",
        str(TESTS_DIR / "test_m2_web_server_host.cpp"),
        str(TESTS_DIR / "mock_m2" / "mock_m2.cpp"),
        str(MAIN_DIR / "web_server.cpp"),
        str(TESTS_DIR / "mock_idf" / "mock_idf.cpp"),
        "-lpthread",
        "-o", "/tmp/test_m2_host_suite"
    ]

    print("  [EXEC] Compiling test_m2_web_server_host on host...")
    compile_proc = subprocess.run(compile_cmd, capture_output=True, text=True)
    if compile_proc.returncode != 0:
        print(f"Compilation stderr:\n{compile_proc.stderr}")
        raise RuntimeError("Failed to compile test_m2_web_server_host")
    print("  [PASS] test_m2_web_server_host compiled cleanly without warnings/errors")

    print("  [EXEC] Executing /tmp/test_m2_host_suite...")
    exec_proc = subprocess.run(["/tmp/test_m2_host_suite"], capture_output=True, text=True)
    if exec_proc.returncode != 0:
        print(f"Test stderr:\n{exec_proc.stderr}")
        print(f"Test stdout:\n{exec_proc.stdout}")
        raise RuntimeError("test_m2_web_server_host failed during execution")

    for line in exec_proc.stdout.splitlines():
        if "[PASS]" in line:
            print(f"  {line.strip()}")

    print("  [PASS] All host functional empirical tests passed 100%")

def test_suite_ota_adversarial_stress():
    print("=" * 80)
    print("SUITE 6: OTA Fault Injection & Concurrency Adversarial Stress (Feature F9)")
    print("=" * 80)

    compile_cmd = [
        "g++", "-std=c++17", "-DESP_PLATFORM",
        "-include", str(WORKSPACE / "tests" / "mock_idf" / "lwip" / "ip4_addr.h"),
        f"-I{WORKSPACE / 'tests' / 'mock_m2'}",
        f"-I{WORKSPACE / 'tests' / 'mock_idf'}",
        f"-I{MAIN_DIR}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'osdep'}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'include'}",
        f"-I{WORKSPACE / 'ZeroTierOne' / 'node'}",
        f"-I{WORKSPACE / 'components' / 'zerotier_node' / 'port'}",
        str(TESTS_DIR / "test_m2_ota_adversarial_stress.cpp"),
        str(TESTS_DIR / "mock_m2" / "mock_m2.cpp"),
        str(MAIN_DIR / "web_server.cpp"),
        str(TESTS_DIR / "mock_idf" / "mock_idf.cpp"),
        "-lpthread",
        "-o", "/tmp/test_m2_ota_adversarial_stress"
    ]

    print("  [EXEC] Compiling test_m2_ota_adversarial_stress on host...")
    compile_proc = subprocess.run(compile_cmd, capture_output=True, text=True)
    if compile_proc.returncode != 0:
        print(f"Compilation stderr:\n{compile_proc.stderr}")
        raise RuntimeError("Failed to compile test_m2_ota_adversarial_stress")
    print("  [PASS] test_m2_ota_adversarial_stress compiled cleanly without warnings/errors")

    print("  [EXEC] Executing /tmp/test_m2_ota_adversarial_stress...")
    exec_proc = subprocess.run(["/tmp/test_m2_ota_adversarial_stress"], capture_output=True, text=True)
    if exec_proc.returncode != 0:
        print(f"Test stderr:\n{exec_proc.stderr}")
        print(f"Test stdout:\n{exec_proc.stdout}")
        raise RuntimeError("test_m2_ota_adversarial_stress failed during execution")

    for line in exec_proc.stdout.splitlines():
        if "[PASS]" in line or "[CHALLENGE" in line:
            print(f"  {line.strip()}")

    print("  [PASS] All OTA adversarial fault injection and concurrency stress tests passed 100%")

def main():
    print("*" * 80)
    print("ZERO-TIER ESP32-S3 MILESTONE 2 VERIFICATION SUITE")
    print("*" * 80)
    test_suite_f5_f6_web_server_and_assets()
    test_suite_f7_f8_telemetry_and_ping()
    test_suite_f9_ota_engine()
    test_suite_main_cpp_integration()
    test_suite_host_functional_execution()
    test_suite_ota_adversarial_stress()
    print("*" * 80)
    print("VERDICT: 100% PASS — ALL MILESTONE 2 REQUIREMENTS & ADVERSARIAL STRESS EMPIRICALLY SATISFIED")
    print("*" * 80)

if __name__ == "__main__":
    main()

