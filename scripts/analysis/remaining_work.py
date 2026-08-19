#!/usr/bin/env python3
"""Analyze remaining decomp work in complete units.

Identifies functions in units marked 'complete' that have no source
implementation (0% match after the objdiff stub fix). Groups by subsystem
and prioritizes by completion percentage and remaining bytes.

WHY THE HEADLINE NOW CARRIES A DENOMINATOR
==========================================
This scanner used to print

    ## Remaining Work: 140 functions (19 KB) across 21 units

with no hint that a hardcoded skip list had removed 363 functions (69,176 B)
and a `--min-bytes 500` threshold another 314 (28,452 B) — 83% of the 817
remaining-work functions in complete units, gone before anyone counted. Same
shape as the `data_symbol_scan --max-symbols` defect, different knob. Every
filter is now counted and printed; see `scripts/analysis/coverage.py`.

Usage:
    python3 scripts/analysis/remaining_work.py                  # markdown summary table
    python3 scripts/analysis/remaining_work.py --symbols        # list all missing symbols with file paths
    python3 scripts/analysis/remaining_work.py --format json -s # JSON with full symbol details
    python3 scripts/analysis/remaining_work.py --min-bytes 0    # NO byte threshold (full pool)
    python3 scripts/analysis/remaining_work.py --no-skip-list   # keep the hardcoded-skip units
    python3 scripts/analysis/remaining_work.py --symbols --max-percent 95  # stubs + partials ≤95%
"""
import argparse
import json
import os
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402


# Units/subsystems to skip (SDK wrappers, platform backends, not game logic).
# These are OPINIONS baked into the tool, not facts about the binary — which is
# why `--no-skip-list` exists and why their cost is printed in the headline.
SKIP_PREFIXES = ("xdk/", "lib/", "link_glue")
SKIP_SUBSYSTEMS = ("system/synth_xbox", "system/rnddx9")
SKIP_UNITS = {
    "system/os/PlatformMgr_Xbox",
    "system/moviebink/BinkMovieImpl",
}
SKIP_KEYWORDS = ("DepthBuffer",)


def skip_reason(unit_name: str) -> str:
    """Which hardcoded skip rule (if any) removes this unit. '' = keep."""
    if any(unit_name.startswith(p) for p in SKIP_PREFIXES):
        return "skiplist-prefix"
    if any(unit_name.startswith(p) for p in SKIP_SUBSYSTEMS):
        return "skiplist-subsystem"
    if unit_name in SKIP_UNITS:
        return "skiplist-unit"
    if any(k in unit_name for k in SKIP_KEYWORDS):
        return "skiplist-keyword"
    return ""


def classify_unit(unit: dict, max_percent: float) -> dict:
    """Split one unit's functions into done / partial / stub buckets.

    `fuzzy_match_percent` is ABSENT — not zero — for every function objdiff
    could not score, which on this tree is every function we never wrote a body
    for (16,780 rows repo-wide). The old code let `None` fall into the `else`
    branch and relabelled it `pct 0.0`, conflating "objdiff emitted no score"
    with "objdiff scored it 0%". Both still land in the same bucket (that is
    correct — both are remaining work) but they are now counted apart.
    """
    done = 0
    partial = 0
    stubs = []
    n_unscored = 0
    n_scored_zero = 0
    for func in unit.get("functions", []):
        pct = func.get("fuzzy_match_percent")
        size = int(func.get("size", 0))
        mangled = func.get("name", "")
        demangled = func.get("metadata", {}).get("demangled_name", "") or mangled
        if pct is not None and pct > max_percent:
            done += 1
        elif pct is not None and pct > 0.0:
            partial += 1
            stubs.append({"name": demangled, "mangled": mangled, "size": size,
                          "pct": pct, "scored": True})
        else:
            if pct is None:
                n_unscored += 1
            else:
                n_scored_zero += 1
            stubs.append({"name": demangled, "mangled": mangled, "size": size,
                          "pct": 0.0, "scored": pct is not None})
    return {
        "done": done,
        "partial": partial,
        "stubs": stubs,
        "n_functions": done + len(stubs),
        "unscored": n_unscored,
        "scored_zero": n_scored_zero,
    }


def priority_label(pct_done: float) -> str:
    if pct_done > 85:
        return "Near-complete"
    elif pct_done > 60:
        return "Medium"
    else:
        return "Large gap"


def analyze_report(report_path: str, min_bytes: int = 500, max_percent: float = 0.0,
                   cov: CoverageReport = None, use_skip_list: bool = True) -> dict:
    with open(report_path) as f:
        report = json.load(f)

    all_units = report.get("units", [])
    total_funcs = sum(len(u.get("functions", [])) for u in all_units)
    if cov is not None:
        cov.universe(total_funcs,
                     f"function rows across all {len(all_units)} units in report.json")
        cov.extra("report_path", report_path)
        cov.extra("units_in_report", len(all_units))

    units = {}
    # Funnel bookkeeping, in the units people actually quote (stub functions and
    # bytes), kept alongside the row-level coverage arithmetic.
    funnel = {
        "units_in_report": len(all_units),
        "units_not_complete": 0,
        "units_complete": 0,
        "skipped_units": 0, "skipped_stubs": 0, "skipped_bytes": 0,
        "below_min_units": 0, "below_min_stubs": 0, "below_min_bytes": 0,
        "no_remaining_units": 0,
        "unscored": 0, "scored_zero": 0,
    }

    for unit in all_units:
        n_func = len(unit.get("functions", []))
        if not unit.get("metadata", {}).get("complete", False):
            funnel["units_not_complete"] += 1
            if cov is not None:
                cov.drop("unit-not-complete", n_func,
                         note="unit metadata.complete is false — never a candidate here")
            continue
        funnel["units_complete"] += 1
        unit_name = unit["name"].replace("default/", "", 1)

        reason = skip_reason(unit_name) if use_skip_list else ""
        info = classify_unit(unit, max_percent)
        stubs = info["stubs"]
        stub_bytes = sum(s["size"] for s in stubs)

        if reason:
            funnel["skipped_units"] += 1
            funnel["skipped_stubs"] += len(stubs)
            funnel["skipped_bytes"] += stub_bytes
            if cov is not None:
                cov.drop(reason, n_func,
                         note="hardcoded SKIP list in this file — pass --no-skip-list to keep")
            continue

        if not stubs:
            funnel["no_remaining_units"] += 1
            if cov is not None:
                cov.drop("unit-has-no-remaining-work", n_func,
                         note="every function already above --max-percent")
            continue

        if stub_bytes < min_bytes:
            funnel["below_min_units"] += 1
            funnel["below_min_stubs"] += len(stubs)
            funnel["below_min_bytes"] += stub_bytes
            if cov is not None:
                cov.drop("unit-below-min-bytes", n_func,
                         note=f"--min-bytes {min_bytes}; pass --min-bytes 0 to keep")
            continue

        if cov is not None:
            cov.examine(n_func)
        funnel["unscored"] += info["unscored"]
        funnel["scored_zero"] += info["scored_zero"]

        done = info["done"]
        partial = info["partial"]
        # ── RESULT-CHANGING FIX (a) ──────────────────────────────────────────
        # Was `total = done + partial + len(stubs)`.  `stubs` ALREADY contains
        # every partial (they are appended to it), so `total` double-counted the
        # partial bucket and inflated every unit's denominator.  At
        # `--max-percent 95` that is 216 of 218 units; 58 of them then get the
        # wrong `priority_label`.  `total_legacy` is kept only so the tool can
        # report how many labels the fix moves.
        total = done + len(stubs)
        total_legacy = done + partial + len(stubs)
        # ── end RESULT-CHANGING FIX (a) ──────────────────────────────────────
        pct_done = round(done / total * 100, 1) if total else 0
        pct_done_legacy = round(done / total_legacy * 100, 1) if total_legacy else 0
        # Deterministic: size DESC, then name, then mangled — no ties left open.
        sorted_stubs = sorted(stubs, key=lambda x: (-x["size"], x["name"], x["mangled"]))
        units[unit_name] = {
            "done": done,
            "partial": partial,
            "stubs": len(stubs),
            "stub_bytes": stub_bytes,
            "total": total,
            "total_legacy": total_legacy,
            "pct_done": pct_done,
            "pct_done_legacy": pct_done_legacy,
            "label_changed_by_fix": priority_label(pct_done) != priority_label(pct_done_legacy),
            "unscored": info["unscored"],
            "scored_zero": info["scored_zero"],
            "source_path": unit.get("metadata", {}).get("source_path"),
            "all_stubs": sorted_stubs,
            "top_stubs": sorted_stubs[:5],
        }

    # Group by subsystem
    categories = defaultdict(lambda: {"units": [], "total_stubs": 0, "total_bytes": 0})
    for unit_name in sorted(units):
        info = units[unit_name]
        parts = unit_name.split("/")
        cat = "/".join(parts[:2]) if len(parts) >= 2 else parts[0]
        categories[cat]["units"].append((unit_name, info))
        categories[cat]["total_stubs"] += info["stubs"]
        categories[cat]["total_bytes"] += info["stub_bytes"]

    total_stubs = sum(i["stubs"] for i in units.values())
    total_bytes = sum(i["stub_bytes"] for i in units.values())

    funnel["reported_units"] = len(units)
    funnel["reported_stubs"] = total_stubs
    funnel["reported_bytes"] = total_bytes
    funnel["pool_stubs"] = (funnel["reported_stubs"] + funnel["skipped_stubs"]
                            + funnel["below_min_stubs"])
    funnel["pool_bytes"] = (funnel["reported_bytes"] + funnel["skipped_bytes"]
                            + funnel["below_min_bytes"])
    funnel["units_relabelled_by_total_fix"] = sum(
        1 for i in units.values() if i["label_changed_by_fix"])
    funnel["units_with_inflated_legacy_total"] = sum(
        1 for i in units.values() if i["total_legacy"] != i["total"])
    funnel["min_bytes"] = min_bytes
    funnel["max_percent"] = max_percent
    funnel["skip_list_active"] = use_skip_list

    if cov is not None:
        for k, v in funnel.items():
            cov.extra(f"funnel_{k}", v)
        if max_percent <= 0.0:
            cov.note("--max-percent 0 (default): the `partial` bucket is STRUCTURALLY "
                     "empty — nothing between 0% and 100% can be reported. Pass "
                     "--max-percent 95 to see partial matches.")
        cov.note(f"reported {total_stubs} of {funnel['pool_stubs']} remaining-work "
                 f"functions in complete units "
                 f"({funnel['skipped_stubs']} removed by the hardcoded skip list, "
                 f"{funnel['below_min_stubs']} by --min-bytes {min_bytes})")
        cov.note(f"of the {total_stubs} reported, {funnel['unscored']} have NO "
                 f"`fuzzy_match_percent` (objdiff emitted no score — no body was "
                 f"written) and {funnel['scored_zero']} were scored exactly 0.0%")

    return {
        "total_stubs": total_stubs,
        "total_bytes": total_bytes,
        "total_units": len(units),
        "funnel": funnel,
        "categories": dict(categories),
    }


def _fmt_denominator(f: dict) -> list:
    """The lines that make the headline a total instead of a sample."""
    lines = []
    lines.append("")
    lines.append(
        f"**Denominator:** {f['pool_stubs']:,} remaining-work functions "
        f"({f['pool_bytes']:,} B) in {f['units_complete']:,} complete units, before "
        f"this tool's filters. {f['units_in_report'] - f['units_complete']:,} of "
        f"{f['units_in_report']:,} report units are not `metadata.complete` and were "
        f"never candidates."
    )
    lines.append("")
    lines.append("| Removed by | Units | Functions | Bytes |")
    lines.append("|------------|-------|-----------|-------|")
    skip_note = ("hardcoded SKIP list (`--no-skip-list` keeps them)"
                 if f["skip_list_active"] else "hardcoded SKIP list — DISABLED")
    lines.append(f"| {skip_note} | {f['skipped_units']:,} | {f['skipped_stubs']:,} "
                 f"| {f['skipped_bytes']:,} |")
    lines.append(f"| `--min-bytes {f['min_bytes']}` (`--min-bytes 0` keeps them) "
                 f"| {f['below_min_units']:,} | {f['below_min_stubs']:,} "
                 f"| {f['below_min_bytes']:,} |")
    lines.append(f"| **reported below** | **{f['reported_units']:,}** "
                 f"| **{f['reported_stubs']:,}** | **{f['reported_bytes']:,}** |")
    lines.append("")
    lines.append(
        f"Of the {f['reported_stubs']:,} reported functions, **{f['unscored']:,} have no "
        f"`fuzzy_match_percent` at all** (objdiff emitted no score — no body was written) "
        f"and {f['scored_zero']:,} were scored exactly 0.0%. The old code printed both as "
        f"`0.0%`."
    )
    if f["max_percent"] <= 0.0:
        lines.append("")
        lines.append("> `--max-percent` is 0 (the default), so the `partial` bucket is "
                     "structurally empty: only never-written functions can appear. Pass "
                     "`--max-percent 95` for partial matches.")
    if f["units_with_inflated_legacy_total"]:
        lines.append("")
        lines.append(
            f"> Denominator fix: `total` no longer double-counts partials. "
            f"{f['units_with_inflated_legacy_total']:,} of {f['reported_units']:,} units had "
            f"an inflated `Done N/total`; {f['units_relabelled_by_total_fix']:,} change "
            f"priority label as a result."
        )
    lines.append("")
    return lines


def format_markdown(data: dict, max_per_cat: int = 5, max_near_complete: int = 15) -> str:
    lines = []
    lines.append(
        f"## Remaining Work: {data['total_stubs']:,} functions "
        f"({data['total_bytes'] / 1024:.0f} KB) across {data['total_units']} units"
    )
    lines += _fmt_denominator(data["funnel"])
    lines.append("| Category | Unit | Done | Left | Bytes | Priority |")
    lines.append("|----------|------|------|------|-------|----------|")

    sorted_cats = sorted(
        data["categories"].items(),
        key=lambda x: (-x[1]["total_bytes"], x[0]),
    )
    for cat, cat_info in sorted_cats:
        sorted_u = sorted(cat_info["units"], key=lambda x: (-x[1]["stub_bytes"], x[0]))
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
    near_complete.sort(key=lambda x: (-x[1]["stub_bytes"], x[0]))
    for u, info in near_complete[:max_near_complete]:
        short = u.split("/")[-1]
        lines.append(
            f"| {short} | {info['done']}/{info['total']} "
            f"| {info['stubs']} | {info['stub_bytes']:,} |"
        )
    # An unlabelled display slice reads as a total. Same residual line the
    # per-category table already emits.
    if len(near_complete) > max_near_complete:
        rest = near_complete[max_near_complete:]
        lines.append(
            f"| *+{len(rest)} more* | | {sum(i['stubs'] for _, i in rest)} "
            f"| {sum(i['stub_bytes'] for _, i in rest):,} |"
        )

    return "\n".join(lines)


def format_symbols(data: dict) -> str:
    f = data["funnel"]
    lines = [
        f"# {data['total_stubs']:,} of {f['pool_stubs']:,} remaining-work functions "
        f"({f['skipped_stubs']:,} removed by the hardcoded skip list, "
        f"{f['below_min_stubs']:,} by --min-bytes {f['min_bytes']})",
        "",
    ]
    sorted_cats = sorted(
        data["categories"].items(),
        key=lambda x: (-x[1]["total_bytes"], x[0]),
    )
    for cat, cat_info in sorted_cats:
        sorted_u = sorted(cat_info["units"], key=lambda x: (-x[1]["stub_bytes"], x[0]))
        for unit_name, info in sorted_u:
            short = unit_name.split("/")[-1]
            src = info.get("source_path") or f"src/{unit_name}.cpp"
            lines.append(f"### {short} — {info['stubs']} functions, {info['stub_bytes']:,} bytes")
            lines.append(f"File: `{src}`")
            lines.append("")
            for stub in info["all_stubs"]:
                size_str = f"({stub['size']}B)"
                pct = stub.get("pct", 0.0)
                if not stub.get("scored", True):
                    pct_str = " [no objdiff score]"
                elif pct > 0.0:
                    pct_str = f" [{pct:.1f}%]"
                else:
                    pct_str = " [0.0%]"
                lines.append(f"- {stub['name']} {size_str}{pct_str}")
                if stub["mangled"] != stub["name"]:
                    lines.append(f"  `{stub['mangled']}`")
            lines.append("")
    return "\n".join(lines)


def format_json(data: dict, include_symbols: bool = False, coverage: dict = None) -> str:
    # Flatten for JSON: list of units with category info
    units_list = []
    for cat in sorted(data["categories"]):
        cat_info = data["categories"][cat]
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
                "total_legacy_double_counted": info["total_legacy"],
                "pct_done": info["pct_done"],
                "pct_done_legacy": info["pct_done_legacy"],
                "priority": priority_label(info["pct_done"]),
                "priority_legacy": priority_label(info["pct_done_legacy"]),
                "unscored": info["unscored"],
                "scored_zero": info["scored_zero"],
            }
            if include_symbols:
                entry["symbols"] = info["all_stubs"]
            else:
                entry["top_stubs"] = info["top_stubs"]
            units_list.append(entry)
    units_list.sort(key=lambda x: (-x["stub_bytes"], x["unit"]))
    output = {
        "summary": {
            "total_stubs": data["total_stubs"],
            "total_bytes": data["total_bytes"],
            "total_units": data["total_units"],
        },
        "funnel": data["funnel"],
        "units": units_list,
    }
    if coverage is not None:
        output["_coverage"] = coverage
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
        help="Minimum remaining bytes per unit to include (default: 500 — UNCHANGED. "
             "This threshold removed 314 functions / 28,452 B at the default; the count "
             "is now printed. Use 0 for the whole pool.)",
    )
    parser.add_argument(
        "--no-skip-list",
        action="store_true",
        help="Do NOT apply the hardcoded SKIP_PREFIXES/SUBSYSTEMS/UNITS/KEYWORDS list "
             "(which removes 65 units / 363 functions / 69,176 B at the default)",
    )
    parser.add_argument(
        "--max-per-cat",
        type=int,
        default=5,
        help="Max units shown per category in markdown (default: 5). Display only — "
             "the residual is printed as a '+N more' row.",
    )
    parser.add_argument(
        "--max-near-complete",
        type=int,
        default=15,
        help="Max rows in the near-complete table (default: 15). Display only — "
             "the residual is printed as a '+N more' row.",
    )
    parser.add_argument(
        "--max-percent",
        type=float,
        default=0.0,
        help="Include functions at or below this match%% (default: 0 = stubs only — "
             "UNCHANGED. At 0 the 'partial' bucket is structurally empty.)",
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
    add_coverage_args(parser)
    args = parser.parse_args()

    cov = CoverageReport("remaining_work", args=args)
    data = analyze_report(args.report, min_bytes=args.min_bytes,
                          max_percent=args.max_percent, cov=cov,
                          use_skip_list=not args.no_skip_list)

    if args.format == "json":
        result = format_json(data, include_symbols=args.symbols, coverage=cov.as_dict())
    elif args.symbols:
        result = format_symbols(data)
    else:
        result = format_markdown(data, max_per_cat=args.max_per_cat,
                                 max_near_complete=args.max_near_complete)

    if args.output:
        with open(args.output, "w") as f:
            f.write(result + "\n")
        print(f"Wrote {args.output}", file=sys.stderr)
    else:
        print(result)

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
