#!/usr/bin/env python3
"""
Quick progress status showing All/Game Code/Milo Engine split.

Usage:
    python3 scripts/progress_status.py [options] [baseline_report]

Examples:
    python3 scripts/progress_status.py                                    # Current status only
    python3 scripts/progress_status.py ../og-dc3-decomp/build/373307D9/report.json  # Compare to baseline
    python3 scripts/progress_status.py --breakdown                        # Show subsystem breakdown
    python3 scripts/progress_status.py --breakdown --sort=percent         # Sort by completion %
"""

import argparse
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


def get_subsystem(name: str) -> str:
    """Extract subsystem from unit name (e.g., 'default/system/char/Foo' -> 'system/char')."""
    parts = name.split("/")
    if len(parts) >= 3:
        return f"{parts[1]}/{parts[2]}"
    return name


def aggregate_by_subsystem(units: list) -> dict:
    """Aggregate unit stats by subsystem."""
    agg = {}
    for u in units:
        sub = get_subsystem(u["name"])
        if sub not in agg:
            agg[sub] = {
                "matched_code": 0,
                "total_code": 0,
                "matched_funcs": 0,
                "total_funcs": 0,
            }
        measures = u.get("measures", {})
        agg[sub]["matched_code"] += int(measures.get("matched_code", 0) or 0)
        agg[sub]["total_code"] += int(measures.get("total_code", 0) or 0)
        agg[sub]["matched_funcs"] += int(measures.get("matched_functions", 0) or 0)
        agg[sub]["total_funcs"] += int(measures.get("total_functions", 0) or 0)
    return agg


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

    categories = ["All", "Game Code", "Milo Engine Code", "Third-Party Libraries", "XDK Code"]
    categories = [c for c in categories if c in current_stats]

    if baseline_stats:
        headers = ["Category", "Baseline", "Current", "Change", "Bytes Gained"]
        rows = []
        for name in categories:
            curr = current_stats[name]
            curr_pct = 100 * curr["matched_code"] / curr["total_code"] if curr["total_code"] > 0 else 0
            if name in baseline_stats:
                base = baseline_stats[name]
                base_pct = 100 * base["matched_code"] / base["total_code"] if base["total_code"] > 0 else 0
                diff_pct = curr_pct - base_pct
                diff_bytes = curr["matched_code"] - base["matched_code"]
                rows.append([name, f"{base_pct:.2f}%", f"{curr_pct:.2f}%", f"{diff_pct:+.2f}%", fmt_diff(diff_bytes)])
        align = [False, True, True, True, True]
    else:
        headers = ["Category", "Matched", "Total", "%", "Functions"]
        rows = []
        for name in categories:
            curr = current_stats[name]
            curr_pct = 100 * curr["matched_code"] / curr["total_code"] if curr["total_code"] > 0 else 0
            rows.append([
                name,
                fmt_bytes(curr["matched_code"]),
                fmt_bytes(curr["total_code"]),
                f"{curr_pct:.2f}%",
                f"{curr['matched_funcs']}/{curr['total_funcs']}",
            ])
        align = [False, True, True, True, True]

    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def fmt_row(cells, align_right=None):
        if align_right is None:
            align_right = [False] * len(cells)
        parts = []
        for i, cell in enumerate(cells):
            if align_right[i]:
                parts.append(cell.rjust(widths[i]))
            else:
                parts.append(cell.ljust(widths[i]))
        return "| " + " | ".join(parts) + " |"

    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))

    print()


# Subsystems to exclude by default (third-party, XDK, tiny standalone files)
EXCLUDED_PREFIXES = ("xdk/", "lib/", "default/")


def print_breakdown(report: dict, sort_by: str = "name", min_size: int = 10240, show_all: bool = False):
    """Print subsystem breakdown table."""
    agg = aggregate_by_subsystem(report.get("units", []))

    # Build results list
    results = []
    for sub, stats in agg.items():
        if stats["total_code"] > 0:
            # Filter out excluded prefixes and small subsystems unless --all
            if not show_all:
                if any(sub.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
                    continue
                if stats["total_code"] < min_size:
                    continue

            pct = 100 * stats["matched_code"] / stats["total_code"]
            results.append({
                "subsystem": sub,
                "matched_code": stats["matched_code"],
                "total_code": stats["total_code"],
                "percent": pct,
                "matched_funcs": stats["matched_funcs"],
                "total_funcs": stats["total_funcs"],
            })

    # Sort
    if sort_by == "percent":
        results.sort(key=lambda x: x["percent"], reverse=True)
    elif sort_by == "size":
        results.sort(key=lambda x: x["total_code"], reverse=True)
    elif sort_by == "matched":
        results.sort(key=lambda x: x["matched_code"], reverse=True)
    else:  # name
        results.sort(key=lambda x: x["subsystem"])

    print()
    headers = ["Subsystem", "Matched", "Total", "%", "Functions"]
    rows = []
    for r in results:
        rows.append([
            r["subsystem"],
            fmt_bytes(r["matched_code"]),
            fmt_bytes(r["total_code"]),
            f"{r['percent']:.2f}%",
            f"{r['matched_funcs']}/{r['total_funcs']}",
        ])

    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            widths[i] = max(widths[i], len(cell))

    def fmt_row(cells, align_right=None):
        if align_right is None:
            align_right = [False] * len(cells)
        parts = []
        for i, cell in enumerate(cells):
            if align_right[i]:
                parts.append(cell.rjust(widths[i]))
            else:
                parts.append(cell.ljust(widths[i]))
        return "| " + " | ".join(parts) + " |"

    align = [False, True, True, True, True]
    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Quick progress status showing All/Game Code/Milo Engine split"
    )
    parser.add_argument(
        "baseline",
        nargs="?",
        type=Path,
        help="Optional baseline report.json for comparison",
    )
    parser.add_argument(
        "--breakdown", "-b",
        action="store_true",
        help="Show progress breakdown by subsystem",
    )
    parser.add_argument(
        "--sort",
        choices=["name", "percent", "size", "matched"],
        default="name",
        help="Sort breakdown by: name (default), percent, size, or matched bytes",
    )
    parser.add_argument(
        "--all", "-a",
        action="store_true",
        help="Show all subsystems (including xdk, lib, tiny ones)",
    )

    args = parser.parse_args()

    current_report = Path("build/373307D9/report.json")

    if not current_report.exists():
        print(f"Error: Current report not found: {current_report}")
        print("Run 'ninja' first to generate the report.")
        sys.exit(1)

    current = load_report(current_report)

    if args.breakdown:
        print_breakdown(current, sort_by=args.sort, show_all=args.all)
    else:
        current_stats = get_category_stats(current)

        baseline_stats = None
        if args.baseline:
            if not args.baseline.exists():
                print(f"Error: Baseline report not found: {args.baseline}")
                sys.exit(1)
            baseline = load_report(args.baseline)
            baseline_stats = get_category_stats(baseline)

        print_status(current_stats, baseline_stats)


if __name__ == "__main__":
    main()
