#!/usr/bin/env python3
"""
Audit relocation target data for semantic mismatches.

For each function at 100% normalized match, compares the DATA at relocation
targets between our .obj and the target .obj. Filters out noise by:
  1. Only comparing when both builds reference the SAME symbol name
  2. Masking bytes covered by relocations within the data (pointer fields)
  3. Only flagging when NON-relocation bytes differ (string content, constants)

This catches bugs like wrong OBJ_CLASSNAME strings, wrong enum constants,
and wrong static initializers — while ignoring linker address layout noise.

Usage:
  python3 scripts/analysis/audit_data_content.py [--unit PATTERN] [--verbose]

  --unit PATTERN   Only check units matching pattern (e.g., "hamobj")
  --verbose        Show details for each mismatch

Requires: report.json with DataValue mode, built .obj files

NOTE: This is a design document / prototype. The actual implementation
requires COFF parsing of both source and target .obj files to extract
section data and relocations. See the approach below.
"""

import json
import struct
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
REPORT_PATH = PROJECT_ROOT / "build" / "373307D9" / "report.json"


def find_data_mismatches():
    """
    Approach for relocation-aware data comparison:

    For each function in both .obj files:
      1. Get all relocations from the function's code section
      2. For each relocation, resolve the target symbol
      3. If target symbol exists in BOTH obj files with same name:
         a. Read the data bytes at the symbol in both
         b. Find relocations WITHIN that data range (embedded pointers)
         c. Mask those relocation-covered bytes (set to 0 in both)
         d. Compare remaining bytes
         e. If they differ → semantic mismatch (real bug)
      4. If target symbol names differ → skip (different symbol, noise)

    The key insight: in a Symbol static local for StaticClassName():
      - Bytes 0-3: const char* pointer (RELOCATION → skip)
      - Bytes 4-7: hash value (computed from string → DIFFERS for wrong name)
      - Other bytes: Symbol internals

    For the OBJ_CLASSNAME bug:
      - Symbol name matches (same mangled static local name)
      - Pointer field differs (points to different .rdata string) → masked
      - Hash field differs (different string → different hash) → FLAGGED
      - String content in .rdata also differs → FLAGGED if we follow the pointer
    """
    pass


def compare_reports():
    """
    Quick analysis using existing report data: find functions where
    normalized=100% but fuzzy<100%. These are candidates for data bugs.
    Cross-reference with known patterns to filter noise.
    """
    with open(REPORT_PATH) as f:
        report = json.load(f)

    candidates = []
    noise_patterns = [
        "??__F",       # dynamic atexit destructors (reference globals with pointers)
        "??__E",       # dynamic initializers (same)
        "??3",         # operator delete (heap metadata)
        "??2",         # operator new
        "??_7",        # vtable symbols
    ]

    for unit in report.get("units", []):
        unit_name = unit.get("name", "")
        for fn in unit.get("functions", []):
            norm = fn.get("match_percent_normalized", 0)
            fuzzy = fn.get("fuzzy_match_percent", 0)
            name = fn.get("name", "")

            if norm >= 99.99 and fuzzy < 99.99:
                # Skip known noise patterns
                is_noise = any(name.startswith(p) for p in noise_patterns)
                if not is_noise:
                    candidates.append({
                        "unit": unit_name,
                        "name": name,
                        "demangled": fn.get("metadata", {}).get("demangled_name", ""),
                        "norm": norm,
                        "fuzzy": fuzzy,
                        "delta": norm - fuzzy,
                    })

    # Sort by delta (largest gap = most suspicious)
    candidates.sort(key=lambda x: -x["delta"])
    return candidates


def main():
    verbose = "--verbose" in sys.argv
    unit_filter = None
    for i, arg in enumerate(sys.argv):
        if arg == "--unit" and i + 1 < len(sys.argv):
            unit_filter = sys.argv[i + 1]

    print("=" * 72)
    print("Data Content Audit — Relocation-Aware Mismatch Detection")
    print("=" * 72)
    print()

    candidates = compare_reports()

    if unit_filter:
        candidates = [c for c in candidates if unit_filter in c["unit"]]

    # Classify candidates
    static_classname = [c for c in candidates if "StaticClassName" in c["demangled"]]
    set_type = [c for c in candidates if "SetType" in c["demangled"]]
    classname = [c for c in candidates if "ClassName" in c["demangled"] and "Static" not in c["demangled"]]
    string_funcs = [c for c in candidates if any(k in c["demangled"] for k in
                    ["HolmesFile", "ServerName", "PhysicalUsage", "CostStr"])]
    other = [c for c in candidates if c not in static_classname + set_type + classname + string_funcs]

    print(f"Total 100%-normalized functions with fuzzy<100%: {len(candidates)} (after noise filter)")
    print(f"  StaticClassName functions: {len(static_classname)}")
    print(f"  SetType functions: {len(set_type)}")
    print(f"  ClassName functions: {len(classname)}")
    print(f"  String/constant functions: {len(string_funcs)}")
    print(f"  Other: {len(other)}")
    print()

    if static_classname:
        print("=== StaticClassName mismatches (likely OBJ_CLASSNAME bugs) ===")
        for c in static_classname:
            print(f"  {c['fuzzy']:5.1f}% fuzzy  {c['demangled']}")
            if verbose:
                print(f"         unit: {c['unit']}")
        print()

    if set_type:
        print("=== SetType mismatches (cascading from OBJ_CLASSNAME) ===")
        for c in set_type:
            print(f"  {c['fuzzy']:5.1f}% fuzzy  {c['demangled']}")
        print()

    if other and verbose:
        print(f"=== Other candidates (first 30) ===")
        for c in other[:30]:
            print(f"  {c['fuzzy']:5.1f}% fuzzy  {c['demangled']}")
            print(f"         unit: {c['unit']}")
        print()

    # Summary
    print("=" * 72)
    print("Noise filtering effectiveness:")

    # Count how many we filtered
    with open(REPORT_PATH) as f:
        report = json.load(f)

    total_with_gap = 0
    for unit in report.get("units", []):
        for fn in unit.get("functions", []):
            norm = fn.get("match_percent_normalized", 0)
            fuzzy = fn.get("fuzzy_match_percent", 0)
            if norm >= 99.99 and fuzzy < 99.99:
                total_with_gap += 1

    print(f"  Total functions with norm=100% fuzzy<100%: {total_with_gap}")
    print(f"  After noise filter: {len(candidates)}")
    print(f"  Filtered out: {total_with_gap - len(candidates)} ({100*(total_with_gap-len(candidates))/max(total_with_gap,1):.0f}%)")
    print("=" * 72)


if __name__ == "__main__":
    main()
