#!/usr/bin/env python3
"""
Adversarial Stress Test Harness for ESP-IDF Partition Table (partitions.csv)
Target: ESP32-S3 (16MB Flash, Dual OTA, 64KB MMU alignment)

Validates:
1. Lexical and semantic parsing of partitions.csv
2. Zero overlapping memory address ranges
3. 4KB sector alignment for all partition offsets and sizes
4. 64KB MMU cache page alignment for app partitions (ota_0, ota_1)
5. Proper otadata partition configuration (type=data, subtype=ota, size=0x2000 / 2 sectors)
6. Symmetric dual OTA partition sizing
7. Exact boundary fit to 16MB flash capacity (0x1000000 bytes)
8. Negative injection test suite (oracle validation proving faults are detected)
"""

import sys
import os
import re
from pathlib import Path

WORKSPACE = Path(__file__).resolve().parents[1]
PARTITIONS_CSV = WORKSPACE / "partitions.csv"

FLASH_SIZE_16MB = 16 * 1024 * 1024  # 0x1000000 = 16777216 bytes
SECTOR_SIZE = 4096                  # 4KB sector alignment
MMU_PAGE_SIZE = 65536               # 64KB MMU page alignment
MIN_USER_OFFSET = 0x9000            # Below 0x9000 is bootloader and partition table
OTADATA_REQUIRED_SIZE = 0x2000      # 2 sectors (8192 bytes) for OTA ping-pong

def parse_size_or_offset(val_str: str) -> int:
    val_str = val_str.strip()
    if not val_str:
        return 0
    mult = 1
    if val_str.endswith("M") or val_str.endswith("m"):
        mult = 1024 * 1024
        val_str = val_str[:-1]
    elif val_str.endswith("K") or val_str.endswith("k"):
        mult = 1024
        val_str = val_str[:-1]
    if val_str.startswith("0x") or val_str.startswith("0X"):
        return int(val_str, 16) * mult
    return int(val_str, 10) * mult

def parse_partition_table(csv_path: Path):
    if not csv_path.exists():
        raise FileNotFoundError(f"Partition table file not found: {csv_path}")

    partitions = []
    with open(csv_path, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                raise ValueError(f"Line {line_num}: malformed line with fewer than 5 columns: {line}")
            name = parts[0]
            ptype = parts[1]
            subtype = parts[2]
            offset = parse_size_or_offset(parts[3])
            size = parse_size_or_offset(parts[4])
            flags = parts[5].strip() if len(parts) > 5 else ""

            partitions.append({
                "line": line_num,
                "name": name,
                "type": ptype,
                "subtype": subtype,
                "offset": offset,
                "size": size,
                "flags": flags
            })
    return partitions

def validate_partitions(partitions, expected_flash_size=FLASH_SIZE_16MB):
    errors = []
    warnings = []

    if not partitions:
        errors.append("No partitions parsed from file.")
        return errors, warnings

    names_seen = set()
    sorted_parts = sorted(partitions, key=lambda p: p["offset"])

    for p in sorted_parts:
        name = p["name"]
        offset = p["offset"]
        size = p["size"]
        ptype = p["type"]
        subtype = p["subtype"]

        # 1. Unique name & name length
        if name in names_seen:
            errors.append(f"Duplicate partition name '{name}'")
        names_seen.add(name)
        if len(name) > 16:
            errors.append(f"Partition name '{name}' exceeds ESP-IDF maximum length of 16 characters ({len(name)})")

        # 2. Minimum offset constraint
        if offset < MIN_USER_OFFSET:
            errors.append(f"Partition '{name}' offset 0x{offset:x} collides with bootloader / partition table area (< 0x{MIN_USER_OFFSET:x})")

        # 3. 4KB Sector alignment
        if offset % SECTOR_SIZE != 0:
            errors.append(f"Partition '{name}' offset 0x{offset:x} is NOT 4KB sector aligned (offset % 0x{SECTOR_SIZE:x} = 0x{offset % SECTOR_SIZE:x})")
        if size % SECTOR_SIZE != 0:
            errors.append(f"Partition '{name}' size 0x{size:x} is NOT 4KB sector aligned (size % 0x{SECTOR_SIZE:x} = 0x{size % SECTOR_SIZE:x})")

        # 4. 64KB MMU alignment for app partitions
        if ptype == "app":
            if offset % MMU_PAGE_SIZE != 0:
                errors.append(f"App partition '{name}' offset 0x{offset:x} is NOT 64KB MMU page aligned (offset % 0x{MMU_PAGE_SIZE:x} = 0x{offset % MMU_PAGE_SIZE:x})")
            if size % MMU_PAGE_SIZE != 0:
                warnings.append(f"App partition '{name}' size 0x{size:x} is not a multiple of 64KB (size % 0x{MMU_PAGE_SIZE:x} = 0x{size % MMU_PAGE_SIZE:x})")

    # 5. Overlap detection
    for i in range(len(sorted_parts)):
        curr = sorted_parts[i]
        curr_start = curr["offset"]
        curr_end = curr_start + curr["size"]

        for j in range(i + 1, len(sorted_parts)):
            other = sorted_parts[j]
            other_start = other["offset"]
            other_end = other_start + other["size"]

            # Overlap check
            if max(curr_start, other_start) < min(curr_end, other_end):
                errors.append(
                    f"OVERLAP DETECTED between '{curr['name']}' [0x{curr_start:x}..0x{curr_end:x}) "
                    f"and '{other['name']}' [0x{other_start:x}..0x{other_end:x})"
                )

    # 6. otadata partition verification
    otadata_parts = [p for p in partitions if p["name"] == "otadata" or p["subtype"] == "ota"]
    if not otadata_parts:
        errors.append("Missing required 'otadata' partition for dual OTA operation.")
    else:
        for ota in otadata_parts:
            if ota["size"] != OTADATA_REQUIRED_SIZE:
                errors.append(f"otadata partition '{ota['name']}' size 0x{ota['size']:x} must be exactly 0x{OTADATA_REQUIRED_SIZE:x} (2 flash sectors for ping-pong).")
            if ota["type"] != "data":
                errors.append(f"otadata partition '{ota['name']}' type must be 'data', got '{ota['type']}'")
            if ota["subtype"] != "ota":
                errors.append(f"otadata partition '{ota['name']}' subtype must be 'ota', got '{ota['subtype']}'")

    # 7. Dual OTA slots check
    ota_apps = {p["name"]: p for p in partitions if p["type"] == "app"}
    if "ota_0" not in ota_apps:
        errors.append("Missing 'ota_0' app partition.")
    if "ota_1" not in ota_apps:
        errors.append("Missing 'ota_1' app partition.")

    if "ota_0" in ota_apps and "ota_1" in ota_apps:
        if ota_apps["ota_0"]["size"] != ota_apps["ota_1"]["size"]:
            errors.append(f"OTA slot size mismatch: ota_0 (0x{ota_apps['ota_0']['size']:x}) != ota_1 (0x{ota_apps['ota_1']['size']:x})")

    # 8. Flash boundary & total allocation
    highest_partition = sorted_parts[-1]
    total_end = highest_partition["offset"] + highest_partition["size"]

    if total_end > expected_flash_size:
        errors.append(f"Total partition table end (0x{total_end:x}) exceeds flash capacity (0x{expected_flash_size:x}) by {total_end - expected_flash_size} bytes")
    elif total_end < expected_flash_size:
        errors.append(f"Partition table underfills 16MB flash: ends at 0x{total_end:x}, remaining unused: {expected_flash_size - total_end} bytes")

    return errors, warnings

def test_negative_oracles():
    """Adversarially inject faults to verify our validator catches every failure mode."""
    print("[ORACLE STRESS] Testing adversarial fault detection on synthetic bad tables...")

    # Case 1: Overlapping partitions
    bad_overlap = [
        {"name": "nvs", "type": "data", "subtype": "nvs", "offset": 0x9000, "size": 0x6000},
        {"name": "bad", "type": "data", "subtype": "ota", "offset": 0xc000, "size": 0x4000}, # overlaps nvs
        {"name": "ota_0", "type": "app", "subtype": "ota_0", "offset": 0x20000, "size": 0x600000},
        {"name": "ota_1", "type": "app", "subtype": "ota_1", "offset": 0x620000, "size": 0x600000},
        {"name": "storage", "type": "data", "subtype": "spiffs", "offset": 0xc20000, "size": 0x3e0000},
    ]
    errs, _ = validate_partitions(bad_overlap)
    assert any("OVERLAP DETECTED" in e for e in errs), "Failed to detect overlapping partitions"

    # Case 2: Misaligned 4KB sector
    bad_4kb = [
        {"name": "nvs", "type": "data", "subtype": "nvs", "offset": 0x9100, "size": 0x6000}, # 0x9100 not 4KB aligned
    ]
    errs, _ = validate_partitions(bad_4kb)
    assert any("NOT 4KB sector aligned" in e for e in errs), "Failed to detect 4KB misalignment"

    # Case 3: App partition not 64KB MMU aligned
    bad_mmu = [
        {"name": "ota_0", "type": "app", "subtype": "ota_0", "offset": 0x12000, "size": 0x600000}, # 0x12000 not 64KB
    ]
    errs, _ = validate_partitions(bad_mmu)
    assert any("NOT 64KB MMU page aligned" in e for e in errs), "Failed to detect MMU misalignment"

    # Case 4: Invalid otadata size
    bad_otadata = [
        {"name": "otadata", "type": "data", "subtype": "ota", "offset": 0xf000, "size": 0x1000}, # 1 sector instead of 2
    ]
    errs, _ = validate_partitions(bad_otadata)
    assert any("must be exactly 0x2000" in e for e in errs), "Failed to detect invalid otadata size"

    # Case 5: Flash overflow (>16MB)
    bad_overflow = [
        {"name": "nvs", "type": "data", "subtype": "nvs", "offset": 0x9000, "size": 0x6000},
        {"name": "otadata", "type": "data", "subtype": "ota", "offset": 0xf000, "size": 0x2000},
        {"name": "ota_0", "type": "app", "subtype": "ota_0", "offset": 0x20000, "size": 0x800000},
        {"name": "ota_1", "type": "app", "subtype": "ota_1", "offset": 0x820000, "size": 0x800000}, # 0x820000 + 0x800000 = 0x1020000 (>16MB)
    ]
    errs, _ = validate_partitions(bad_overflow)
    assert any("exceeds flash capacity" in e for e in errs), "Failed to detect flash overflow"

    print("    [PASS] All 5 adversarial fault injection oracles verified successfully.")

def main():
    print("=" * 80)
    print("ESP32-S3 Partition Table Adversarial Stress Test")
    print(f"Target file: {PARTITIONS_CSV}")
    print("=" * 80)

    # 1. Test the negative oracles first to ensure test sensitivity
    test_negative_oracles()

    # 2. Parse actual partitions.csv
    print(f"\n[PARSING] Reading {PARTITIONS_CSV}...")
    partitions = parse_partition_table(PARTITIONS_CSV)
    print(f"Parsed {len(partitions)} partition entries:")
    print(f"{'Line':<5} | {'Name':<10} | {'Type':<6} | {'SubType':<8} | {'Offset':<10} | {'Size':<10} | {'End Offset':<10} | {'Size (KB/MB)'}")
    print("-" * 80)
    for p in partitions:
        end_offset = p["offset"] + p["size"]
        human_size = f"{p['size'] / (1024*1024):.3f} MB" if p["size"] >= 1024*1024 else f"{p['size'] // 1024} KB"
        print(f"{p['line']:<5} | {p['name']:<10} | {p['type']:<6} | {p['subtype']:<8} | 0x{p['offset']:06x}   | 0x{p['size']:06x}   | 0x{end_offset:06x}   | {human_size}")

    # 3. Validate against all constraints
    print("\n[VALIDATION] Running strict adversarial validation checks...")
    errors, warnings = validate_partitions(partitions)

    for w in warnings:
        print(f"  [WARN] {w}")

    if errors:
        print("\n[FAIL] Validation errors encountered:")
        for e in errors:
            print(f"  - {e}")
        sys.exit(1)

    print("\n[GAP ANALYSIS] Inspecting gaps between partitions:")
    sorted_parts = sorted(partitions, key=lambda p: p["offset"])
    for i in range(len(sorted_parts) - 1):
        c_end = sorted_parts[i]["offset"] + sorted_parts[i]["size"]
        n_start = sorted_parts[i+1]["offset"]
        if n_start > c_end:
            gap = n_start - c_end
            print(f"  Gap between '{sorted_parts[i]['name']}' and '{sorted_parts[i+1]['name']}': 0x{gap:x} ({gap} bytes / {gap/1024:.1f} KB)")
            if sorted_parts[i+1]["type"] == "app":
                print(f"    -> Verified intentional alignment padding for 64KB MMU page boundary (0x{n_start:x} % 0x10000 == 0).")
            else:
                print(f"    -> Warning: Unexplained gap of {gap} bytes.")
        elif n_start == c_end:
            print(f"  Continuous boundary: '{sorted_parts[i]['name']}' -> '{sorted_parts[i+1]['name']}' (at 0x{c_end:x})")

    print("\n" + "=" * 80)
    print("VERDICT: 100% PASS — Partition table satisfies all ESP32-S3 & OTA requirements.")
    print("=" * 80)
    sys.exit(0)

if __name__ == "__main__":
    main()
