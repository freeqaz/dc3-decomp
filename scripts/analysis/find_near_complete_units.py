#!/usr/bin/env python3
"""Find units that are close to 100% completion.

Lists units where most functions match 100% but a few remain,
making them good candidates for focused decomp work.
"""
import argparse
import json
import sys


def main():
    parser = argparse.ArgumentParser(description="Find near-complete units")
    parser.add_argument(
        "--report", default="build/373307D9/report.json",
        help="Path to report.json (default: build/373307D9/report.json)"
    )
    parser.add_argument(
        "--max-remaining", type=int, default=3,
        help="Max non-100%% functions to show (default: 3)"
    )
    parser.add_argument(
        "--min-functions", type=int, default=2,
        help="Minimum total functions in unit (default: 2)"
    )
    parser.add_argument(
        "--show-all", action="store_true",
        help="Show all non-100%% functions (not just implementable ones)"
    )
    args = parser.parse_args()

    with open(args.report) as f:
        data = json.load(f)

    candidates = []
    for unit in data["units"]:
        name = unit.get("name", "")
        funcs = unit.get("functions", [])
        if len(funcs) < args.min_functions:
            continue

        total = len(funcs)
        complete = sum(1 for f in funcs if (f.get("fuzzy_match_percent") or 0) >= 99.9)
        remaining = [f for f in funcs if (f.get("fuzzy_match_percent") or 0) < 99.9]

        if not remaining or len(remaining) > args.max_remaining:
            continue

        # Check if any remaining functions are potentially implementable
        # (not just merged/boilerplate symbols)
        implementable = []
        unimplementable = []
        for f in remaining:
            fname = f["name"]
            size = f.get("size", 0)
            pct = f.get("fuzzy_match_percent") or 0
            is_merged = "merged_" in fname
            is_boilerplate = any(x in fname for x in ["__E", "__F", "??_9", "`vector"])
            if is_merged or is_boilerplate:
                unimplementable.append(f)
            else:
                implementable.append(f)

        if not args.show_all and not implementable:
            continue

        pct_complete = (complete / total * 100) if total else 0
        candidates.append((pct_complete, name, total, complete, implementable, unimplementable))

    candidates.sort(key=lambda x: (-x[0], len(x[4])))

    for pct, name, total, complete, impl, unimpl in candidates:
        remaining_count = len(impl) + len(unimpl)
        print(f"=== {name} ({complete}/{total} complete, {remaining_count} remaining) ===")
        for f in impl:
            fpct = f.get("fuzzy_match_percent") or 0
            print(f"  {fpct:5.1f}%  {f['name']}  ({f.get('size', 0)} bytes)")
        for f in unimpl:
            fpct = f.get("fuzzy_match_percent") or 0
            tag = "[merged]" if "merged_" in f["name"] else "[boilerplate]"
            print(f"  {fpct:5.1f}%  {f['name']}  ({f.get('size', 0)} bytes) {tag}")
        print()


if __name__ == "__main__":
    main()
