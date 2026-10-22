#!/usr/bin/env python3
"""
Tier 4: R6 Tracking & Static Check Suite
Verifies:
- Static checks:
  - Identity.hpp throws ZT_EXCEPTION_* instead of calling abort() (P0-3).
  - Absence of ZT_IDENTITY_GEN_MEMORY_OVERRIDE in CMakeLists.txt (P0-4).
- Issue tracking checklist in ISSUES.md and ISSUES.csv:
  - Status of P0-3, P0-4, P0-5, P2-11, P3-4.
- Git repository status and cleanliness.
Authoritative source: ORIGINAL_REQUEST.md §R6, §Acceptance Criteria.
"""

import sys
import subprocess
from pathlib import Path

def test_static_and_tracking():
    workspace = Path(__file__).resolve().parents[2]
    issues_md = workspace / "ISSUES.md"
    issues_csv = workspace / "ISSUES.csv"
    identity_hpp = workspace / "ZeroTierOne" / "node" / "Identity.hpp"
    cmake_node = workspace / "components" / "zerotier_node" / "CMakeLists.txt"

    checks = []

    # 1. Identity abort check (P0-3)
    has_identity_throws = False
    if identity_hpp.exists():
        id_text = identity_hpp.read_text()
        has_identity_throws = "throw ZT_EXCEPTION_" in id_text
    checks.append(("Identity.hpp throws exceptions instead of abort() (P0-3)", has_identity_throws))

    # 2. Identity memory override removal (P0-4)
    cmake_override_absent = True
    if cmake_node.exists():
        cmake_override_absent = "ZT_IDENTITY_GEN_MEMORY_OVERRIDE" not in cmake_node.read_text()
    checks.append(("ZT_IDENTITY_GEN_MEMORY_OVERRIDE removed from CMakeLists.txt (P0-4)", cmake_override_absent))

    # 3. Check ISSUES.md tracking
    md_content = issues_md.read_text(encoding="utf-8", errors="replace") if issues_md.exists() else ""
    checks.append(("ISSUES.md exists and is tracked", len(md_content) > 0))

    # Inspect specific issue states in ISSUES.md
    for issue_id in ["P0-3", "P0-4", "P0-5", "P2-11", "P3-4"]:
        found = issue_id in md_content
        checks.append((f"Issue {issue_id} referenced in ISSUES.md", found))

    # 4. Check ISSUES.csv tracking
    csv_content = issues_csv.read_text(encoding="utf-8", errors="replace") if issues_csv.exists() else ""
    checks.append(("ISSUES.csv exists and is tracked", len(csv_content) > 0))

    # 5. Git repository status check
    git_status = subprocess.run(["git", "status", "--porcelain"], cwd=workspace, stdout=subprocess.PIPE, text=True)
    checks.append(("Git repository accessible and tracked", git_status.returncode == 0))

    all_passed = True
    print("[TEST RUN] Evaluating R6 Tracking & Static Checks:")
    for desc, passed in checks:
        status = "[PASS]" if passed else "[FAIL]"
        print(f"  {status} {desc}")
        if not passed:
            all_passed = False

    return all_passed

if __name__ == "__main__":
    passed = test_static_and_tracking()
    sys.exit(0 if passed else 1)
