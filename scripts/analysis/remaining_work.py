#!/usr/bin/env python3
"""Analyze remaining decomp work in complete units.

Identifies functions in units marked 'complete' that have no source
implementation (0% match after the objdiff stub fix). Groups by subsystem
and prioritizes by completion percentage and remaining bytes.

Usage:
    python3 scripts/analysis/remaining_work.py                  # markdown summary table
    python3 scripts/analysis/remaining_work.py --symbols        # list all missing symbols with file paths
    python3 scripts/analysis/remaining_work.py --format json -s # JSON with full symbol details
    python3 scripts/analysis/remaining_work.py --min-bytes 1000 # only units with >1KB remaining
"""
import argparse
import json
import sys
from collections import defaultdict


# Units/subsystems to skip (SDK wrappers, platform backends, not game logic)
SKIP_PREFIXES = ("xdk/", "lib/", "link_glue")
SKIP_SUBSYSTEMS = ("system/synth_xbox", "system/rnddx9")
SKIP_UNITS = {
    "system/os/PlatformMgr_Xbox",
    "system/moviebink/BinkMovieImpl",
}
SKIP_KEYWORDS = ("DepthBuffer",)


def analyze_report(report_path: str, min_bytes: int = 500) -> dict:
    with open(report_path) as f:
        report = json.load(f)

    units = {}
    for unit in report.get("units", []):
        if not unit.get("metadata", {}).get("complete", False):
            continue
        unit_name = unit["name"].replace("default/", "", 1)

        if any(unit_name.startswith(p) for p in SKIP_PREFIXES):
            continue
        if any(unit_name.startswith(p) for p in SKIP_SUBSYSTEMS):
            continue
        if unit_name in SKIP_UNITS:
            continue
        if any(k in unit_name for k in SKIP_KEYWORDS):
            continue

        done = 0
        partial = 0
        source_path = unit.get("metadata", {}).get("source_path")
        stubs = []
        for func in unit.get("functions", []):
            pct = func.get("fuzzy_match_percent")
            size = int(func.get("size", 0))
            mangled = func.get("name", "")
            demangled = func.get("metadata", {}).get("demangled_name", "") or mangled
            if pct is not None and pct == 100.0:
                done += 1
            elif pct is not None and pct > 0.0:
                partial += 1
            else:
                stubs.append({"name": demangled, "mangled": mangled, "size": size})

        if not stubs:
            continue
        stub_bytes = sum(s["size"] for s in stubs)
        if stub_bytes < min_bytes:
            continue

        total = done + partial + len(stubs)
        sorted_stubs = sorted(stubs, key=lambda x: x["size"], reverse=True)
        units[unit_name] = {
            "done": done,
            "partial": partial,
            "stubs": len(stubs),
            "stub_bytes": stub_bytes,
            "total": total,
            "pct_done": round(done / total * 100, 1) if total else 0,
            "source_path": source_path,
            "all_stubs": sorted_stubs,
            "top_stubs": sorted_stubs[:5],
        }

    # Group by subsystem
    categories = defaultdict(lambda: {"units": [], "total_stubs": 0, "total_bytes": 0})
    for unit_name, info in units.items():
        parts = unit_name.split("/")
        cat = "/".join(parts[:2]) if len(parts) >= 2 else parts[0]
        categories[cat]["units"].append((unit_name, info))
        categories[cat]["total_stubs"] += info["stubs"]
        categories[cat]["total_bytes"] += info["stub_bytes"]

    total_stubs = sum(i["stubs"] for i in units.values())
    total_bytes = sum(i["stub_bytes"] for i in units.values())

    return {
        "total_stubs": total_stubs,
        "total_bytes": total_bytes,
        "total_units": len(units),
        "categories": dict(categories),
    }


def priority_label(pct_done: float) -> str:
    if pct_done > 85:
        return "Near-complete"
    elif pct_done > 60:
        return "Medium"
    else:
        return "Large gap"


def format_markdown(data: dict, max_per_cat: int = 5) -> str:
    lines = []
    lines.append(
        f"## Remaining Work: {data['total_stubs']:,} functions "
        f"({data['total_bytes'] / 1024:.0f} KB) across {data['total_units']} units"
    )
    lines.append("")
    lines.append("| Category | Unit | Done | Left | Bytes | Priority |")
    lines.append("|----------|------|------|------|-------|----------|")

    sorted_cats = sorted(
        data["categories"].items(),
        key=lambda x: x[1]["total_bytes"],
        reverse=True,
    )
    for cat, cat_info in sorted_cats:
        sorted_u = sorted(cat_info["units"], key=lambda x: x[1]["stub_bytes"], reverse=True)
        for j, (u, info) in enumerate(sorted_u[:max_per_cat]):
            short = u.split("/")[-1]
            pri = priority_label(info["pct_done"])
            cat_label = (
                f"**{cat}** ({cat_info['total_stubs']}f/{cat_info['total_bytes'] / 1024:.0f}KB)"
                if j == 0
                else ""
            )
            lines.append(
                f"| {cat_label} | {short} | {info['done']}/{info['total']} "
                f"| {info['stubs']} | {info['stub_bytes']:,} | {pri} |"
            )
        if len(sorted_u) > max_per_cat:
            remaining = len(sorted_u) - max_per_cat
            rem_stubs = sum(i["stubs"] for _, i in sorted_u[max_per_cat:])
            rem_bytes = sum(i["stub_bytes"] for _, i in sorted_u[max_per_cat:])
            lines.append(f"| | *+{remaining} more* | | {rem_stubs} | {rem_bytes:,} | |")

    # Near-complete summary
    lines.append("")
    lines.append("### Best bang-for-buck (near-complete)")
    lines.append("")
    lines.append("| Unit | Done | Left | Bytes |")
    lines.append("|------|------|------|-------|")
    near_complete = []
    for cat_info in data["categories"].values():
        for u, info in cat_info["units"]:
            if info["pct_done"] > 85 and info["stubs"] <= 20:
                near_complete.append((u, info))
    near_complete.sort(key=lambda x: x[1]["stub_bytes"], reverse=True)
    for u, info in near_complete[:15]:
        short = u.split("/")[-1]
        lines.append(
            f"| {short} | {info['done']}/{info['total']} "
            f"| {info['stubs']} | {info['stub_bytes']:,} |"
        )

    return "\n".join(lines)


def format_symbols(data: dict) -> str:
    lines = []
    sorted_cats = sorted(
        data["categories"].items(),
        key=lambda x: x[1]["total_bytes"],
        reverse=True,
    )
    for cat, cat_info in sorted_cats:
        sorted_u = sorted(cat_info["units"], key=lambda x: x[1]["stub_bytes"], reverse=True)
        for unit_name, info in sorted_u:
            short = unit_name.split("/")[-1]
            src = info.get("source_path") or f"src/{unit_name}.cpp"
            lines.append(f"### {short} — {info['stubs']} functions, {info['stub_bytes']:,} bytes")
            lines.append(f"File: `{src}`")
            lines.append("")
            for stub in info["all_stubs"]:
                size_str = f"({stub['size']}B)"
                lines.append(f"- {stub['name']} {size_str}")
                if stub["mangled"] != stub["name"]:
                    lines.append(f"  `{stub['mangled']}`")
            lines.append("")
    return "\n".join(lines)


def format_json(data: dict, include_symbols: bool = False) -> str:
    # Flatten for JSON: list of units with category info
    units_list = []
    for cat, cat_info in data["categories"].items():
        for unit_name, info in cat_info["units"]:
            entry = {
                "category": cat,
                "unit": unit_name,
                "source_path": info.get("source_path") or f"src/{unit_name}.cpp",
                "done": info["done"],
                "partial": info["partial"],
                "stubs": info["stubs"],
                "stub_bytes": info["stub_bytes"],
                "total": info["total"],
                "pct_done": info["pct_done"],
                "priority": priority_label(info["pct_done"]),
            }
            if include_symbols:
                entry["symbols"] = info["all_stubs"]
            else:
                entry["top_stubs"] = info["top_stubs"]
            units_list.append(entry)
    units_list.sort(key=lambda x: x["stub_bytes"], reverse=True)
    output = {
        "summary": {
            "total_stubs": data["total_stubs"],
            "total_bytes": data["total_bytes"],
            "total_units": data["total_units"],
        },
        "units": units_list,
    }
    return json.dumps(output, indent=2)


def main():
    parser = argparse.ArgumentParser(description="Analyze remaining decomp work")
    parser.add_argument(
        "--report",
        default="build/373307D9/report.json",
        help="Path to report.json",
    )
    parser.add_argument(
        "--format",
        choices=["markdown", "json"],
        default="markdown",
        help="Output format (default: markdown)",
    )
    parser.add_argument(
        "--min-bytes",
        type=int,
        default=500,
        help="Minimum remaining bytes per unit to include (default: 500)",
    )
    parser.add_argument(
        "--max-per-cat",
        type=int,
        default=5,
        help="Max units shown per category in markdown (default: 5)",
    )
    parser.add_argument(
        "--symbols", "-s",
        action="store_true",
        help="List all unimplemented symbols per unit with file paths",
    )
    parser.add_argument(
        "-o", "--output",
        help="Output file (default: stdout)",
    )
    args = parser.parse_args()

    data = analyze_report(args.report, min_bytes=args.min_bytes)

    if args.format == "json":
        result = format_json(data, include_symbols=args.symbols)
    elif args.symbols:
        result = format_symbols(data)
    else:
        result = format_markdown(data, max_per_cat=args.max_per_cat)

    if args.output:
        with open(args.output, "w") as f:
            f.write(result + "\n")
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(result)


if __name__ == "__main__":
    main()
