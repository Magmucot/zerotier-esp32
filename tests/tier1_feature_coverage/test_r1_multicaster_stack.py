#!/usr/bin/env python3
"""
Tier 1: R1 Multicaster Stack Usage Test
Verifies that ZeroTier::Multicaster::send frame stack usage is <= 512 bytes.
Authoritative source: ORIGINAL_REQUEST.md §R1, PROJECT.md §Multicaster::send.
"""

import os
import sys
import subprocess
import re
from pathlib import Path

def test_multicaster_stack_usage():
    workspace = Path(__file__).resolve().parents[2]
    node_dir = workspace / "ZeroTierOne" / "node"
    osdep_dir = workspace / "ZeroTierOne" / "osdep"
    stubs_dir = workspace / "components" / "zerotier_node" / "port" / "stubs"
    src_file = node_dir / "Multicaster.cpp"
    out_obj = Path("/tmp/test_mc_stack.o")
    out_su = Path("/tmp/test_mc_stack.su")

    if out_obj.exists():
        out_obj.unlink()
    if out_su.exists():
        out_su.unlink()

    cmd = [
        "g++", "-std=c++17", "-fstack-usage", "-c",
        str(src_file),
        f"-I{node_dir}",
        f"-I{osdep_dir}",
        f"-I{stubs_dir}",
        "-o", str(out_obj)
    ]

    print(f"[TEST RUN] Compiling: {' '.join(cmd)}")
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        print(f"[FAIL] Compilation failed with code {proc.returncode}")
        print(proc.stderr)
        return False

    if not out_su.exists():
        print(f"[FAIL] Stack usage file {out_su} was not generated.")
        return False

    # Parse .su file for Multicaster::send
    su_content = out_su.read_text()
    send_pattern = re.compile(r"Multicaster\.cpp:\d+:\d+:void\s+ZeroTier::Multicaster::send\([^)]*\)\s+(\d+)\s+([a-z,]+)")
    match = send_pattern.search(su_content)

    if not match:
        # Fallback search for any Multicaster::send
        for line in su_content.splitlines():
            if "Multicaster" in line and "send" in line:
                parts = line.strip().split("\t")
                if len(parts) >= 2 and parts[1].isdigit():
                    bytes_used = int(parts[1])
                    alloc_type = parts[2] if len(parts) > 2 else "unknown"
                    print(f"[INFO] Found entry: {parts[0]} -> {bytes_used} bytes ({alloc_type})")
                    if bytes_used <= 512:
                        print(f"[PASS] Multicaster::send stack frame is {bytes_used} bytes <= 512 bytes limit.")
                        return True
                    else:
                        print(f"[FAIL] Multicaster::send stack frame is {bytes_used} bytes > 512 bytes limit (P0 Stack Overflow)!")
                        return False

        print(f"[FAIL] Could not locate ZeroTier::Multicaster::send in {out_su}")
        print(su_content)
        return False

    bytes_used = int(match.group(1))
    alloc_type = match.group(2)
    print(f"[INFO] Multicaster::send stack frame: {bytes_used} bytes ({alloc_type})")

    limit = 512
    if bytes_used <= limit:
        print(f"[PASS] Multicaster::send frame size {bytes_used} B <= {limit} B limit.")
        return True
    else:
        print(f"[FAIL] Multicaster::send frame size {bytes_used} B exceeds {limit} B limit! (Violates R1 / P0-1)")
        return False

if __name__ == "__main__":
    passed = test_multicaster_stack_usage()
    sys.exit(0 if passed else 1)
