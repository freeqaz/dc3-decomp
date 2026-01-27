#!/usr/bin/env python3
"""
Quick progress status showing All/Game Code/Milo Engine split.

Usage:
    python3 scripts/progress_status.py [baseline_report]

Examples:
    python3 scripts/progress_status.py                                    # Current status only
    python3 scripts/progress_status.py ../og-dc3-decomp/build/373307D9/report.json  # Compare to baseline
"""

import json
import sys
from pathlib import Path


def load_report(path: Path) -> dict:
    with open(path) as f:
        return json.load(f)


def fmt_bytes(b: int) -> str:
    if abs(b) >= 1024 * 1024:
        return f"{b/1024/1024:.2f} MB"
    if abs(b) >= 1024:
        return f"{b/1024:.1f} KB"
    return f"{b} B"


def fmt_diff(b: int) -> str:
    if abs(b) >= 1024 * 1024:
        return f"{b/1024/1024:+.2f} MB"
    if abs(b) >= 1024:
        return f"{b/1024:+.1f} KB"
    return f"{b:+d} B"


def get_category_stats(report: dict) -> dict:
    """Extract category stats from report."""
    stats = {}

    # Overall
    m = report.get("measures", {})
    stats["All"] = {
        "matched_code": int(m.get("matched_code", 0) or 0),
        "total_code": int(m.get("total_code", 0) or 0),
        "matched_funcs": int(m.get("matched_functions", 0) or 0),
        "total_funcs": int(m.get("total_functions", 0) or 0),
    }

    # Categories
    for cat in report.get("categories", []):
        name = cat.get("name", cat.get("id", "Unknown"))
        m = cat.get("measures", {})
        stats[name] = {
            "matched_code": int(m.get("matched_code", 0) or 0),
            "total_code": int(m.get("total_code", 0) or 0),
            "matched_funcs": int(m.get("matched_functions", 0) or 0),
            "total_funcs": int(m.get("total_functions", 0) or 0),
        }

    return stats


def print_status(current_stats: dict, baseline_stats: dict = None):
    """Print status table."""
    print()

    if baseline_stats:
        print("| Category | Baseline | Current | Change | Bytes Gained |")
        print("|----------|----------|---------|--------|--------------|")
    else:
        print("| Category | Matched | Total | % | Functions |")
        print("|----------|---------|-------|---|-----------|")

    for name in ["All", "Game Code", "Milo Engine Code", "Third-Party Libraries", "XDK Code"]:
        if name not in current_stats:
            continue
        curr = current_stats[name]
        curr_pct = 100 * curr["matched_code"] / curr["total_code"] if curr["total_code"] > 0 else 0

        if baseline_stats and name in baseline_stats:
            base = baseline_stats[name]
            base_pct = 100 * base["matched_code"] / base["total_code"] if base["total_code"] > 0 else 0
            diff_pct = curr_pct - base_pct
            diff_bytes = curr["matched_code"] - base["matched_code"]
            print(f"| {name} | {base_pct:.2f}% | {curr_pct:.2f}% | {diff_pct:+.2f}% | {fmt_diff(diff_bytes)} |")
        else:
            print(f"| {name} | {fmt_bytes(curr['matched_code'])} | {fmt_bytes(curr['total_code'])} | {curr_pct:.2f}% | {curr['matched_funcs']}/{curr['total_funcs']} |")

    print()


def main():
    current_report = Path("build/373307D9/report.json")
    baseline_report = Path(sys.argv[1]) if len(sys.argv) > 1 else None

    if not current_report.exists():
        print(f"Error: Current report not found: {current_report}")
        print("Run 'ninja' first to generate the report.")
        sys.exit(1)

    current = load_report(current_report)
    current_stats = get_category_stats(current)

    baseline_stats = None
    if baseline_report:
        if not baseline_report.exists():
            print(f"Error: Baseline report not found: {baseline_report}")
            sys.exit(1)
        baseline = load_report(baseline_report)
        baseline_stats = get_category_stats(baseline)

    print_status(current_stats, baseline_stats)


if __name__ == "__main__":
    main()
