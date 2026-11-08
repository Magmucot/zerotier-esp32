# ZeroTier Native Client for ESP32-S3 (ESP32-ZeroTier-Lite)

An open-source, production-grade native [ZeroTier](https://zerotier.com) SD-WAN client and telemetry node for Espressif **ESP32-S3** microcontrollers. The client runs [ZeroTierOne](https://github.com/zerotier/zerotierone) logic directly on the bare-metal ESP-IDF / lwIP stack without requiring an external gateway, Linux router, Raspberry Pi, or Home Assistant bridge.

The firmware features an integrated dual-interface HTTP server serving a lightweight (<5KB, Flash `.rodata`, fully air-gapped) real-time telemetry dashboard, web and CLI Over-The-Air (OTA) firmware updates, non-blocking ICMP ping measurement, on-chip junction temperature sensing, safe Kconfig-based credential management, and strict Git secret hygiene.

---

## Key Capabilities

- **Native ZeroTier Dataplane**: Runs directly on ESP32-S3 via ESP-IDF and lwIP (`zt0` overlay interface alongside `st0` Wi-Fi station).
- **Verified Hardware Platform**: Verified operational on the **ESP32-S3 (QFN56) with 8MB Octal PSRAM** (ESP32-S3-WROOM-1 / N16R8, 240MHz Dual-Core CPU, 80MHz Octal PSRAM, 16MB Quad SPI Flash).
- **Dual-Interface Web Status Server**: Embedded `esp_http_server` bound to `0.0.0.0:80`, accessible concurrently over the local Wi-Fi LAN and over the encrypted ZeroTier overlay network.
- **Air-Gapped Telemetry Dashboard**: Lightweight HTML5/CSS3 Single-Page App (<5KB) embedded in Flash `.rodata`. Zero external CDN, script, font, or CSS dependencies.
- **RESTful Telemetry API**: Endpoint `GET /api/status` provides live JSON metrics: board uptime, free internal heap, free Octal PSRAM, junction temperature, ICMP ping latency, and ZeroTier node/network state.
- **Robust Web & CLI Dual-Slot OTA**: Web-based and `curl`-based firmware flashing to dual 6.0MB partitions (`ota_0`, `ota_1`) with pre-flash image validation, atomic boot switching, and deferred reboot.
- **Safe Kconfig Configuration & Secret Hygiene**: All Wi-Fi credentials and ZeroTier network IDs are configured via `idf.py menuconfig`. No secrets hardcoded in source code; `sdkconfig` excluded from Git.

---

## Hardware Platform & Architecture

### Target Platform Specifications

The project has been tested, validated, and verified operational on the **ESP32-S3 (QFN56)** system-on-chip with **8MB Octal PSRAM**:

| Parameter | Specification |
|---|---|
| **SoC** | Espressif ESP32-S3 (QFN56 package, dual-core Xtensa LX7 @ 240MHz) |
| **Module** | ESP32-S3-WROOM-1 / N16R8 |
| **Embedded Flash** | 16 MB Quad SPI Flash (configured in `partitions.csv` and `sdkconfig.defaults`) |
| **External RAM** | 8 MB Octal SPI PSRAM (80 MHz Octal mode: `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_SPIRAM_SPEED_80M=y`) |
| **Internal RAM** | 512 KB SRAM (~380 KB internal heap available at boot) |
| **On-Chip Sensors** | Integrated on-chip SAR ADC junction temperature sensor (`driver/temperature_sensor.h`) |
| **Network Interfaces** | 2.4 GHz 802.11 b/g/n Wi-Fi Station (`st0`) + Virtual ZeroTier Ethernet (`zt0`) |

### Memory Allocation Strategy

ZeroTier node cryptography (Curve25519, Salsa20, Poly1305), network packet buffers, peer tables, and route registries require substantial heap memory. To achieve stability without starving the core ESP-IDF network stack, the firmware employs a segregated memory strategy:

1. **External 8MB Octal PSRAM (`MALLOC_CAP_SPIRAM`)**:
   - ZeroTier node C++ object instances (`sizeof(ZeroTier::Node)` ~24KB).
   - Peer, path, network configuration, and multicast subscription registries.
   - Large packet buffers, TCP reassembly queues, and transient HTTP/OTA streaming buffers.
   - Automatic allocation is enabled via `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`.
2. **Internal SRAM (`MALLOC_CAP_INTERNAL`)**:
   - Strictly reserved for DMA-capable Wi-Fi MAC layer transmit/receive descriptors.
   - Real-time FreeRTOS task stacks and scheduler metadata.
   - Interrupt service routines (ISRs) and high-priority lwIP core timers.
   - Free internal heap remains monitored via telemetry to avoid heap fragmentation or starvation.

---

## Kconfig Configuration Guide

Credentials and device settings are decoupled from source code and managed via standard ESP-IDF Kconfig (`main/Kconfig.projbuild`).

### Interactive Configuration

Launch the configuration menu:

```bash
idf.py menuconfig
```

Navigate to:
```text
Component config --->
  ZeroTier ESP32 Application Configuration --->
```

### Configuration Options

| Option Symbol | Type | Default | Description |
|---|---|---|---|
| `CONFIG_ESP_WIFI_SSID` | String | `""` (empty) | Wi-Fi network SSID for station mode. Must not be empty to initiate Wi-Fi connection. |
| `CONFIG_ESP_WIFI_PASSWORD` | String | `""` (empty) | Wi-Fi network WPA/WPA2/WPA3 passphrase. If left empty, the device uses open authentication (`WIFI_AUTH_OPEN`). When provided, `WIFI_AUTH_WPA2_PSK` is enforced. |
| `CONFIG_ESP_MAXIMUM_RETRY` | Integer | `10` | Maximum Wi-Fi connection retry attempts before reporting failure (range: `1` to `50`). |
| `CONFIG_ZT_NETWORK_ID` | String | `""` (empty) | 16-character hexadecimal ZeroTier Network ID (e.g. `0123456789abcdef` or `0x0123456789abcdef`). If left empty, the node initializes without joining a network. |
| `CONFIG_PING_TARGET_IP` | String | `"1.1.1.1"` | Target IPv4 address for continuous background ICMP latency measurement. |

### CLI / Headless Configuration

For automated builds or continuous deployment, override options in `sdkconfig` or use configuration files:

```bash
# Example non-interactive configuration
cat << 'EOF' >> sdkconfig
CONFIG_ESP_WIFI_SSID="YourWiFiSSID"
CONFIG_ESP_WIFI_PASSWORD="YourWiFiPassword"
CONFIG_ZT_NETWORK_ID="0123456789abcdef"
CONFIG_PING_TARGET_IP="1.1.1.1"
EOF

idf.py reconfigure
```

---

## Web Status Interface & Telemetry

### Dual-Interface Architecture

The embedded web server (`esp_http_server`) binds to `INADDR_ANY` (`0.0.0.0:80`). It accepts incoming TCP connections concurrently from two distinct network planes:
- **Local Wi-Fi LAN**: `http://<wifi-station-ip>/`
- **ZeroTier Overlay Network**: `http://<zerotier-assigned-ip>/`

This ensures full out-of-band management: if local Wi-Fi router settings change or local access is restricted, the device remains fully manageable across the ZeroTier virtual network worldwide.

### Lightweight Embedded Web UI

- **Zero CDN Dependencies**: The single-page dashboard (`main/web_assets.hpp`) is embedded as a raw string literal in Flash `.rodata`. It contains zero external stylesheets, fonts, icons, or JavaScript libraries.
- **Air-Gapped Reliability**: Functions cleanly in isolated OT networks, field environments, or behind firewalls where public internet access is prohibited.
- **Footprint**: Compiled HTML5/CSS3/JS payload is **under 5 KB** (4,976 bytes).
- **Responsive Telemetry**: Automatically polls `GET /api/status` every 2.5 seconds using native Fetch API.

### Telemetry Instrumentation

The firmware continuously monitors system health across both physical and overlay domains:
- **Board Uptime**: Measured in microsecond precision via `esp_timer_get_time()`, formatted as days, hours, minutes, and seconds.
- **Internal Heap & Octal PSRAM**: Real-time free and total capacity querying via `heap_caps_get_free_size()` and `heap_caps_get_total_size()` for both `MALLOC_CAP_INTERNAL` and `MALLOC_CAP_SPIRAM`.
- **ESP32-S3 Core Temperature**: Sampled from the on-die temperature sensor using ESP-IDF `driver/temperature_sensor.h` in degrees Celsius.
- **Asynchronous Ping Latency**: An asynchronous background ICMP socket task (`ping/ping_sock.h`) sends ICMP echo requests to `CONFIG_PING_TARGET_IP` every 3 seconds. The web server reads cached round-trip latency in O(1) time without blocking HTTP handler threads.
- **ZeroTier Network Status**: Live inspection of ZeroTier port and node structures, reporting node ID (10 hex digits), network ID (16 hex digits), membership status (`OK`, `REQUESTING_CONFIG`, `ACCESS_DENIED`, etc.), and assigned IPv4 address.
- **Active Boot Partition**: Identifies whether the current firmware is running from `ota_0` or `ota_1`.

### REST API Documentation

#### Endpoint: `GET /api/status`

Returns live telemetry metrics in JSON format.

**Response Headers:**
```http
HTTP/1.1 200 OK
Content-Type: application/json
```

**JSON Schema:**
```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": [
    "uptime_seconds",
    "free_heap_internal",
    "total_heap_internal",
    "free_psram",
    "total_psram",
    "temp_celsius",
    "ping_latency_ms",
    "ping_target",
    "ping_online",
    "zt_online",
    "zt_status",
    "zt_node_id",
    "zt_nwid",
    "zt_ip",
    "active_slot"
  ],
  "properties": {
    "uptime_seconds":     { "type": "integer", "description": "Seconds since device boot" },
    "free_heap_internal": { "type": "integer", "description": "Free internal SRAM in bytes" },
    "total_heap_internal":{ "type": "integer", "description": "Total internal SRAM in bytes" },
    "free_psram":         { "type": "integer", "description": "Free external Octal PSRAM in bytes" },
    "total_psram":        { "type": "integer", "description": "Total external Octal PSRAM in bytes" },
    "temp_celsius":       { "type": "number",  "description": "Internal core junction temperature in °C" },
    "ping_latency_ms":    { "type": "integer", "description": "Round-trip ping time to target in ms" },
    "ping_target":        { "type": "string",  "description": "Configured ICMP ping target IPv4 address" },
    "ping_online":        { "type": "boolean", "description": "True if last ping probe succeeded" },
    "zt_online":          { "type": "boolean", "description": "True if ZeroTier node is online" },
    "zt_status":          { "type": "string",  "description": "ZeroTier network membership state (OK, OFFLINE, etc.)" },
    "zt_node_id":         { "type": "string",  "description": "10-character hexadecimal ZeroTier Node ID" },
    "zt_nwid":            { "type": "string",  "description": "16-character hexadecimal ZeroTier Network ID" },
    "zt_ip":              { "type": "string",  "description": "Assigned overlay IPv4 address (e.g. 10.147.17.42)" },
    "active_slot":        { "type": "string",  "description": "Running firmware partition ('ota_0' or 'ota_1')" }
  }
}
```

**Sample Response:**
```json
{
  "uptime_seconds": 3842,
  "free_heap_internal": 268412,
  "total_heap_internal": 393216,
  "free_psram": 7942180,
  "total_psram": 8388608,
  "temp_celsius": 41.25,
  "ping_latency_ms": 14,
  "ping_target": "1.1.1.1",
  "ping_online": true,
  "zt_online": true,
  "zt_status": "OK",
  "zt_node_id": "154a350c86",
  "zt_nwid": "0123456789abcdef",
  "zt_ip": "10.147.17.42",
  "active_slot": "ota_0"
}
```

---

## Over-The-Air (OTA) Firmware Updates

The project implements a resilient, dual-slot A/B OTA update mechanism specifically designed for 16MB flash configurations.

### 16MB Partition Layout (`partitions.csv`)

The partition table allocates dual 6.0MB application slots to accommodate the complete ZeroTier C++ runtime with full debug telemetry and symbol tables, while aligning app partitions to 64KB MMU page boundaries:

```csv
# ESP-IDF Partition Table for 16MB Flash
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x6000,
otadata,   data, ota,     0xf000,   0x2000,
phy_init,  data, phy,     0x11000,  0x1000,
ota_0,     app,  ota_0,   0x20000,  0x600000,
ota_1,     app,  ota_1,   0x620000, 0x600000,
storage,   data, spiffs,  0xc20000, 0x3e0000,
```

- `nvs` (24 KB): Non-Volatile Storage for Wi-Fi calibration, PHY data, and persistent ZeroTier node identities.
- `otadata` (8 KB): Two 4KB sectors tracking the active boot partition index and boot counters for rollback safety.
- `phy_init` (4 KB): Radio RF calibration parameters.
- `ota_0` (6.0 MB, `0x20000`): Primary application partition (64KB MMU aligned).
- `ota_1` (6.0 MB, `0x620000`): Secondary application partition (64KB MMU aligned).
- `storage` (3.875 MB, `0xc20000`): SPIFFS/LittleFS storage region extending to the 16MB flash limit (`0x1000000`).

### Update Procedures

#### Method 1: Web Browser Dashboard

1. Build the firmware binary: `idf.py build`.
2. Open a web browser to `http://<device-ip>/` (accessible via Wi-Fi LAN or ZeroTier IP).
3. Under the **OTA Firmware Update** card, click **Choose File** and select `build/zt_esp32.bin`.
4. Click **Upload & Flash**.
5. The progress bar reflects real-time byte streaming. Once flashing and verification complete, the web interface reports success and the device reboots into the new partition within 2 seconds.

#### Method 2: Command Line using `curl`

Upload the compiled `.bin` image directly using `curl`:

```bash
# Upload via local Wi-Fi LAN IP
curl -X POST --data-binary @build/zt_esp32.bin \
     -H "Content-Type: application/octet-stream" \
     http://192.168.1.150/update

# Or upload remotely over the ZeroTier overlay network (zt0)
curl -X POST --data-binary @build/zt_esp32.bin \
     -H "Content-Type: application/octet-stream" \
     http://10.147.17.42/update
```

Both `/update` and `/api/ota` are registered as valid upload endpoints.

### Pre-Flash Safety & Fault Tolerance

The OTA engine (`main/web_server.cpp`) enforces strict safety checks before and during flashing:

1. **Mutual Exclusion Lock**: Only one OTA operation can proceed at a time. Concurrent upload requests receive `409 Conflict`.
2. **Target Partition Selection**: Calls `esp_ota_get_next_update_partition()` to automatically identify and write to the inactive partition slot (`ota_1` when running on `ota_0`, and vice versa), leaving the active firmware untouched.
3. **Partition Size Check**: Verifies that incoming `Content-Length` does not exceed the target partition boundary (6,291,456 bytes). Oversized files are rejected immediately with `400 Bad Request` prior to flash erasure.
4. **First-Chunk Header Validation**:
   - Inspects the first 4096-byte chunk before writing to flash.
   - **Magic Byte Validation**: Validates `hdr->magic == ESP_IMAGE_HEADER_MAGIC` (`0xE7`). Non-firmware files or corrupt data are rejected.
   - **Chip ID Validation**: Validates `hdr->chip_id == ESP_CHIP_ID_ESP32S3` (`0x0009`). Images compiled for other architectures (e.g. standard ESP32, ESP32-C3) are rejected with `400 Bad Request` before flash modifications occur.
5. **Checksum & Integrity Verification**: On upload completion, `esp_ota_end()` computes and validates the image SHA256 checksum and cryptographic descriptors before permitting boot switching.
6. **Atomic Boot Switch**: Only after full verification succeeds does `esp_ota_set_boot_partition()` update the `otadata` record.
7. **Deferred 2-Second Reboot**: A background task delays `esp_restart()` by 2,000 ms, ensuring the HTTP server cleanly flushes the `200 OK` JSON response and completes TCP FIN/ACK teardown before restarting.

---

## Secret Hygiene & Security Architecture

### Git Ignore & Credential Policy

To prevent accidental leaks of Wi-Fi passwords, private keys, and ZeroTier network IDs, strict Git hygiene rules are enforced:

- **`.gitignore` Rules**:
  ```gitignore
  # ESP-IDF local sdkconfig files containing secrets
  sdkconfig
  sdkconfig.old
  sdkconfig.*
  !sdkconfig.defaults
  !sdkconfig.defaults.*
  .env*
  *.pem
  *.key
  ```
- **Untracked `sdkconfig`**: The auto-generated `sdkconfig` containing local Wi-Fi passwords and ZeroTier credentials is removed from the Git index and excluded from source control.
- **`sdkconfig.defaults` Cleanliness**: Contains only hardware definitions (`esp32s3`, 240MHz CPU, Octal PSRAM, 16MB flash). No private credentials or network tokens are stored in defaults.

### Runtime Security Guards

- **SSID Validation**: `wifi_init_sta()` checks `strlen(CONFIG_ESP_WIFI_SSID)`. If unconfigured, Wi-Fi initialization logs an informative warning and gracefully returns `ESP_ERR_INVALID_STATE` without spamming connection attempts to empty SSIDs.
- **Network ID Validation**: ZeroTier initialization checks `CONFIG_ZT_NETWORK_ID`. If empty, the node safely boots in standalone mode with `0x0` without crashing.
- **Dynamic Wi-Fi Authentication**: Automatically switches between `WIFI_AUTH_OPEN` (when password is empty) and `WIFI_AUTH_WPA2_PSK` (when a passphrase is provided).

---

## Building and Flashing

### Prerequisites

- **ESP-IDF v5.1+** (v5.3.5 recommended).
- Target set to `esp32s3`:
  ```bash
  idf.py set-target esp32s3
  ```

### Build Instructions

1. Configure application settings:
   ```bash
   idf.py menuconfig
   ```
2. Build the bootloader, partition table, and application binary:
   ```bash
   idf.py build
   ```
3. Flash the binary to the ESP32-S3 over USB/serial and open the monitor:
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor
   ```

---

## Verification & Test Suite

The repository includes a comprehensive, multi-tiered test suite covering unit behavior, boundary stress, multi-threading, telemetry serialization, OTA fault injection, and Git hygiene.

### Running Test Suites

Execute all tests from the repository root:

```bash
# 1. Full regression test suite (Tiers 1-4) in strict mode
python3 tests/run_all_tests.py --strict

# 2. Milestone 2 Web, Telemetry, and OTA verification suite
python3 tests/test_m2_web_telemetry_ota.py

# 3. Kconfig configuration and secret hygiene test
python3 tests/test_kconfig_adversarial.py

# 4. Partition table 16MB boundary and MMU alignment test
python3 tests/test_partition_table_adversarial.py

# 5. Milestone 1 adversarial challenge test
python3 tests/test_m1_adversarial_challenge.py

# 6. Deep adversarial telemetry & OTA fault injection suite
python3 tests/test_m2_adversarial_deep.py
```

### Git Hygiene Audit

Verify that no credentials or secrets exist in Git staging or working tree diffs:

```bash
# Verify no secrets or sensitive files are staged or modified
git status
git diff
```

---

## Repository Structure

```text
/home/magus/code/projects/zerotier-esp32
├── components/
│   └── zerotier_node/              # ESP-IDF component wrapper for ZeroTierOne
├── ZeroTierOne/                    # ZeroTierOne core source tree
│   ├── node/                       # Cryptographic core and protocol state machine
│   └── osdep/                      # ESP32-S3 OS-dependent implementation files
│       ├── ESP32LiteAppGlue.cpp    # High-level application glue and node lifecycle
│       ├── ESP32LitePort.cpp       # Low-level lwIP netif port and packet handler
│       └── ESP32NetLink.cpp        # ZeroTier virtual network interface link
├── main/
│   ├── CMakeLists.txt              # Component build registration and dependencies
│   ├── Kconfig.projbuild           # Application Kconfig menu definition
│   ├── main.cpp                    # Application entry point, Wi-Fi, and node startup
│   ├── web_server.hpp              # HTTP web server and telemetry API interface
│   ├── web_server.cpp              # HTTP handlers, REST API, ping service, and OTA engine
│   └── web_assets.hpp              # Single-page HTML5/CSS3 UI embedded in Flash .rodata
├── tests/                          # Comprehensive host and mock test suites
│   ├── mock_idf/                   # ESP-IDF API mocks for host-based verification
│   ├── mock_m2/                    # OTA, web server, and telemetry mocks
│   ├── run_all_tests.py            # Primary test runner for Tiers 1-4
│   ├── test_kconfig_adversarial.py # Kconfig syntax and secret purge test
│   ├── test_m2_web_telemetry_ota.py# Web, telemetry, and OTA test suite
│   └── test_partition_table_adversarial.py # 16MB partition table validator
├── partitions.csv                  # 16MB partition layout with dual 6MB OTA slots
├── sdkconfig.defaults              # ESP32-S3 hardware defaults (240MHz, Octal PSRAM)
└── README.md                       # Project documentation
```

---

## License

This project includes and modifies ZeroTierOne source code. ZeroTierOne is licensed under the [Mozilla Public License 2.0 (MPL-2.0)](https://www.mozilla.org/en-US/MPL/2.0/). See the upstream ZeroTierOne repository and license notices for full licensing terms.
