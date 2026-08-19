#!/usr/bin/env python3
"""
Compare decomp progress between two report.json files, or show current snapshot.

Usage:
    python3 scripts/analysis/compare_progress.py <baseline_report> <current_report>
    python3 scripts/analysis/compare_progress.py --snapshot [report.json]

Examples:
    # Compare against baseline
    python3 scripts/analysis/compare_progress.py ../og-dc3-decomp/build/373307D9/report.json build/373307D9/report.json

    # Show detailed unit breakdown
    python3 scripts/analysis/compare_progress.py --detailed ../og-dc3-decomp/build/373307D9/report.json build/373307D9/report.json

    # Show function-level changes (regressions and improvements)
    python3 scripts/analysis/compare_progress.py --functions baseline.json current.json

    # Only show regressions across all views
    python3 scripts/analysis/compare_progress.py --regressions --functions --detailed baseline.json current.json

    # Show current snapshot (all subsystems)
    python3 scripts/analysis/compare_progress.py --snapshot
    python3 scripts/analysis/compare_progress.py --snapshot --sort=percent

    # Filter to specific paths using glob patterns
    python3 scripts/analysis/compare_progress.py --snapshot --filter 'system/ui/*'
    python3 scripts/analysis/compare_progress.py --filter 'system/char/*' --functions baseline.json current.json
    python3 scripts/analysis/compare_progress.py --snapshot --filter '*/synth/*' --filter '*/midi/*'
"""

import argparse
import fnmatch
import json
import os
import re
import sys
from pathlib import Path

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402


# Subsystems to exclude by default (third-party, XDK, tiny standalone files)
EXCLUDED_PREFIXES = ("xdk/", "lib/", "default/")
DEFAULT_MIN_SIZE = 10240  # 10KB


# --------------------------------------------------------------------------- #
# THE RULER.  Read this before changing any percentage in this file.
#
# report.json carries TWO per-function percentages and they are NOT the same
# number:
#
#   fuzzy_match_percent        the RAW scorer.  Relocation-SENSITIVE, so ICF /
#                              atexit-thunk churn moves it without any source
#                              change -- the phantom-regression source.  objdiff
#                              OMITS THE KEY ENTIRELY for functions we never
#                              defined: 16,920 of 48,344 rows in the current
#                              report have no `fuzzy_match_percent` at all.
#   match_percent_normalized   the canonical scorer.  Present on ALL 48,344
#                              rows.  395 functions are normalized==100 while
#                              fuzzy<100; every one of those reads as a
#                              regression under the fuzzy ruler.
#
# NORMALIZED IS CANONICAL HERE, with fuzzy as the fallback -- the same order
# `count_matched_functions` has always used.
#
# The UNIT-level `measures.fuzzy_match_percent` is a different animal despite
# the shared name: objdiff computes it as the size-weighted mean of the
# per-function *normalized* values (objdiff-cli report.rs:1096 +
# calc_fuzzy_match_percent), verified here against 471/471 units of the current
# report.  So the unit tables are ALREADY on the canonical ruler; it is only
# the `matched_code_percent` fallback that is raw.  Do not "fix" it by
# swapping the key -- state which one you used, which is what
# `unit_match_percent_with_ruler` now does.
# --------------------------------------------------------------------------- #

RULER_NORMALIZED = "normalized"
RULER_FUZZY = "fuzzy"
RULER_NONE = "none"

# A rendered percentage ROUNDS, and this project has already lost two real bugs
# to `99.97` printing as `100.0`.  Mirror of scripts/sync_match_percent.py's
# `_round_pct`: rounding may never REACH 100 from below.
def clamp_below_100(v: float, decimals: int = 2) -> float:
    """round(v, decimals), except that a sub-100 value never becomes 100."""
    r = round(v, decimals)
    if r >= 100.0 and v < 100.0:
        return 100.0 - 10.0 ** (-decimals)
    return r


def fmt_pct(v: float | None, decimals: int = 2, sign: bool = False) -> str:
    """Render a percentage that can never lie upward across the 100 boundary."""
    if v is None:
        return "-"
    r = clamp_below_100(v, decimals)
    return f"{r:+.{decimals}f}%" if sign else f"{r:.{decimals}f}%"


def function_percent_with_ruler(fn: dict) -> tuple[float | None, str]:
    """(percent, ruler) for ONE report.json function row.

    Returns `(None, "none")` when the row carries neither percentage, so the
    caller can COUNT that population instead of silently coercing it to 0 --
    the None->0 coercion is what made "we finally wrote a body" and "+95%
    improvement" print identically.
    """
    n = fn.get("match_percent_normalized")
    if n is not None:
        return float(n), RULER_NORMALIZED
    f = fn.get("fuzzy_match_percent")
    if f is not None:
        return float(f), RULER_FUZZY
    return None, RULER_NONE


# Default map file for merged symbol resolution
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent
DEFAULT_MAP_FILE = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"


class MergedSymbolResolver:
    """Resolve merged_<addr> names to actual mangled symbol names via the linker map."""

    def __init__(self, map_file: Path):
        self._address_to_symbols: dict[str, list[str]] = {}
        self._loaded = False
        self._map_file = map_file

    def _ensure_loaded(self):
        if self._loaded:
            return
        if not self._map_file.exists():
            self._loaded = True
            return
        pattern = re.compile(
            r'^\s*\d{4}:[0-9a-fA-F]+\s+'
            r'(\S+)\s+'
            r'([0-9a-fA-F]{8})\s+'
        )
        with open(self._map_file, 'r') as f:
            for line in f:
                match = pattern.match(line)
                if match:
                    symbol = match.group(1)
                    address = match.group(2).upper()
                    if address not in self._address_to_symbols:
                        self._address_to_symbols[address] = []
                    self._address_to_symbols[address].append(symbol)
        self._loaded = True

    def resolve(self, merged_name: str) -> list[str]:
        """Given 'merged_825FDA60', return list of mangled symbol names at that address."""
        self._ensure_loaded()
        addr = merged_name[7:].upper() if merged_name.startswith("merged_") else merged_name.upper()
        return self._address_to_symbols.get(addr, [])


def count_matched_functions(unit: dict) -> tuple[int, int]:
    """Count matched/total functions using normalized match percent.

    Uses match_percent_normalized (which excludes arg-only diffs like
    register/offset swaps) if available, otherwise falls back to
    fuzzy_match_percent.  This was already the correct ruler order before the
    honesty pass; `function_percent_with_ruler` is the same policy, factored
    out so the function-level comparison can share it.
    """
    functions = unit.get("functions", [])
    total = len(functions)
    matched = 0
    for f in functions:
        pct, _ruler = function_percent_with_ruler(f)
        if pct is not None and pct >= 100.0:
            matched += 1
    return matched, total


def get_subsystem(name: str) -> str:
    """Extract subsystem from unit name."""
    parts = name.split("/")
    if len(parts) >= 3:
        return f"{parts[1]}/{parts[2]}"
    return name


def fmt_bytes(b: int) -> str:
    """Format byte count with sign and units."""
    if abs(b) >= 1024:
        return f"{b/1024:+.1f} KB"
    return f"{b:+d} B"


def fmt_bytes_plain(b: int) -> str:
    """Format byte count without sign."""
    if abs(b) >= 1024 * 1024:
        return f"{b/1024/1024:.2f} MB"
    if abs(b) >= 1024:
        return f"{b/1024:.1f} KB"
    return f"{b} B"


def expand_filter_patterns(patterns: list) -> list:
    """Expand filter patterns for convenience.

    If a pattern doesn't start with '*' or contain '/' at the start,
    prepend '*/' so 'system/ui/*' matches 'default/system/ui/Foo'.
    """
    expanded = []
    for p in patterns:
        expanded.append(p)
        # Also try with '*/' prefix if pattern doesn't already start with '*' or '/'
        if not p.startswith("*") and not p.startswith("/"):
            expanded.append("*/" + p)
    return expanded


def filter_units(units: list, patterns: list) -> list:
    """Filter units by glob patterns matched against their name."""
    if not patterns:
        return units
    expanded = expand_filter_patterns(patterns)
    filtered = []
    for u in units:
        name = u["name"]
        if any(fnmatch.fnmatch(name, p) for p in expanded):
            filtered.append(u)
    return filtered


def load_report(path: Path) -> dict:
    """Load a report.json file."""
    with open(path) as f:
        return json.load(f)


def unit_match_percent_with_ruler(measures: dict) -> tuple[float | None, str]:
    """(percent, ruler) for a unit's `measures` block.

    `measures.fuzzy_match_percent` is misnamed upstream: objdiff builds it as
    the size-weighted mean of the per-function `match_percent_normalized`
    values, so it is the CANONICAL ruler even though the key says fuzzy
    (objdiff-cli report.rs:1096; verified against 471/471 units of the current
    report.json).  `matched_code_percent` -- the fallback -- is the raw one:
    matched_code counts only symbols whose RAW match_percent hit 100.
    """
    fp = measures.get("fuzzy_match_percent", None)
    if fp is not None:
        return float(fp), RULER_NORMALIZED
    mcp = measures.get("matched_code_percent", None)
    if mcp is not None:
        return float(mcp), RULER_FUZZY
    # If we have matched_code/total_code, compute it (still the raw ruler)
    tc = int(measures.get("total_code", 0) or 0)
    mc = int(measures.get("matched_code", 0) or 0)
    if tc > 0 and mc > 0:
        return 100.0 * mc / tc, RULER_FUZZY
    return None, RULER_NONE


def get_unit_match_percent(measures: dict) -> float | None:
    """Back-compat wrapper: percentage only, ruler discarded.

    Prefer `unit_match_percent_with_ruler` in new code so the caller can SAY
    which ruler it used.
    """
    return unit_match_percent_with_ruler(measures)[0]


def aggregate_by_subsystem(units: list, ruler_counts: dict | None = None) -> dict:
    """Aggregate unit stats by subsystem using best available match percentages.

    Prefers the normalized-weighted `measures.fuzzy_match_percent`, falls back
    to the raw `matched_code_percent` (see `unit_match_percent_with_ruler`).
    Units without any match data are counted for total_functions but not
    for percentage calculations.

    Pass `ruler_counts` (a dict) to learn how many units landed on each ruler;
    that is the number a caller needs in order to state which ruler a table is
    actually on rather than assuming.
    """
    agg = {}
    for u in units:
        sub = get_subsystem(u["name"])
        if sub not in agg:
            agg[sub] = {
                "fuzzy_code": 0,
                "weighted_fuzzy": 0.0,
                "total_code": 0,
                "total_functions": 0,
                "matched_functions": 0,
            }
        measures = u.get("measures", {})
        tc = int(measures.get("total_code", 0) or 0)
        pct, ruler = unit_match_percent_with_ruler(measures)
        if ruler_counts is not None:
            ruler_counts[ruler] = ruler_counts.get(ruler, 0) + 1
        agg[sub]["total_code"] += tc
        if pct is not None and tc > 0:
            agg[sub]["fuzzy_code"] += tc
            agg[sub]["weighted_fuzzy"] += pct * tc
        matched, total = count_matched_functions(u)
        agg[sub]["total_functions"] += total
        agg[sub]["matched_functions"] += matched
    return agg


def compare_subsystems(baseline: dict, current: dict) -> list:
    """Compare aggregated subsystem stats using fuzzy match percentages."""
    baseline_agg = aggregate_by_subsystem(baseline["units"])
    current_agg = aggregate_by_subsystem(current["units"])

    results = []
    for sub, curr in current_agg.items():
        if sub in baseline_agg and curr["fuzzy_code"] > 0 and baseline_agg[sub]["fuzzy_code"] > 0:
            base = baseline_agg[sub]
            base_pct = base["weighted_fuzzy"] / base["fuzzy_code"] if base["fuzzy_code"] > 0 else 0
            curr_pct = curr["weighted_fuzzy"] / curr["fuzzy_code"] if curr["fuzzy_code"] > 0 else 0
            diff_pct = curr_pct - base_pct
            diff_funcs = curr["matched_functions"] - base["matched_functions"]

            if abs(diff_pct) > 0.005:
                results.append({
                    "subsystem": sub,
                    "base_pct": base_pct,
                    "curr_pct": curr_pct,
                    "diff_pct": diff_pct,
                    "diff_funcs": diff_funcs,
                    "total_code": curr["total_code"],
                })

    # Full tie-break: `diff_pct` alone leaves ties in float-comparison order,
    # which is not stable across runs of a dict-ordered aggregation.
    results.sort(key=lambda x: (-x["diff_pct"], x["subsystem"]))
    return results


def compare_units(baseline: dict, current: dict, min_diff: float = 0.01) -> list:
    """Compare individual unit stats."""
    baseline_units = {u["name"]: u for u in baseline["units"]}
    current_units = {u["name"]: u for u in current["units"]}

    results = []
    for name, curr in sorted(current_units.items()):
        if name in baseline_units:
            base = baseline_units[name]
            base_measures = base.get("measures", {})
            curr_measures = curr.get("measures", {})
            base_pct = get_unit_match_percent(base_measures) or 0
            curr_pct = get_unit_match_percent(curr_measures) or 0
            diff = curr_pct - base_pct

            if abs(diff) > min_diff:
                base_matched, base_total = count_matched_functions(base)
                curr_matched, curr_total = count_matched_functions(curr)
                results.append({
                    "name": name,
                    "base_pct": base_pct,
                    "curr_pct": curr_pct,
                    "diff_pct": diff,
                    "base_funcs": f"{base_matched}/{base_total}",
                    "curr_funcs": f"{curr_matched}/{curr_total}",
                })

    results.sort(key=lambda x: (-x["diff_pct"], x["name"]))
    return results


def compare_functions(baseline: dict, current: dict, min_diff: float = 0.5,
                      merged_resolver: MergedSymbolResolver = None,
                      cov: CoverageReport = None) -> dict:
    """Compare individual function match percentages between two reports.

    Returns a dict of populations, not a bare list, because "changed by N%" is
    only ONE of the things that can happen to a function between two reports
    and the other four used to vanish silently:

        changed       both sides scored; |diff| >= min_diff
        unchanged     both sides scored; |diff| <  min_diff   (COUNT only)
        appeared      no percent in baseline, a percent now   ("we wrote a body")
        vanished      a percent in baseline, none now
        only_current  the key is absent from the baseline map entirely
        only_baseline the key is absent from the current map entirely
        no_percent    neither side carries any percent

    RULER: normalized-first with a fuzzy fallback (`function_percent_with_ruler`),
    mirroring `count_matched_functions`.  The previous version read
    `fuzzy_match_percent` ONLY, which (a) is relocation-sensitive, so ICF and
    atexit-thunk churn manufactured regressions, and (b) is absent on 16,920 of
    the 48,344 rows in the current report -- those became `pct or 0`, so a
    function that gained a body at 95% printed as a +95% improvement and one
    whose key moved printed as a -95% regression.
    """
    # Build function lookup: (unit_name, func_name) -> entry
    def build_func_map(report):
        fmap = {}
        merged_entries = []  # (unit_name, merged_name, entry) for second pass
        for unit in report.get("units", []):
            unit_name = unit["name"]
            for func in unit.get("functions", []):
                fname = func.get("name", "")
                pct, ruler = function_percent_with_ruler(func)
                # Carry BOTH raw rulers alongside the resolved one.  The
                # headline diff is now computed on `pct` (normalized-first),
                # but 5ecc641d9's phantom classification still needs the two
                # rulers separately: fuzzy_match_percent is relocation-
                # sensitive, so a pure .text layout shuffle (ICF re-folding, an
                # atexit/dynamic-init thunk moving) moves it while
                # match_percent_normalized does not budge.
                norm = func.get("match_percent_normalized", None)
                fuzzy = func.get("fuzzy_match_percent", None)
                demangled = func.get("metadata", {}).get("demangled_name", "")
                entry = {
                    "pct": pct,
                    "ruler": ruler,
                    "norm": norm,
                    "fuzzy": fuzzy,
                    "size": int(func.get("size", 0)),
                    "demangled": demangled,
                }
                fmap[(unit_name, fname)] = entry
                if fname.startswith("merged_"):
                    merged_entries.append((unit_name, fname, entry))
        # Resolve merged_<addr> names to actual symbols
        if merged_resolver and merged_entries:
            for unit_name, merged_name, entry in merged_entries:
                for symbol in merged_resolver.resolve(merged_name):
                    alt_key = (unit_name, symbol)
                    if alt_key not in fmap:
                        fmap[alt_key] = entry
        return fmap

    base_funcs = build_func_map(baseline)
    curr_funcs = build_func_map(current)

    all_keys = sorted(set(base_funcs) | set(curr_funcs))
    if cov is not None:
        cov.universe(len(all_keys),
                     "distinct (unit, symbol) keys in the UNION of the two reports")
        cov.note(f"function ruler: normalized-first, fuzzy fallback "
                 f"(baseline rows={len(base_funcs)}, current rows={len(curr_funcs)})")

    out = {
        "changed": [],
        "appeared": [],
        "vanished": [],
        "only_current": [],
        "only_baseline": [],
        "unchanged": 0,
        "ruler_used": {},
    }

    def _bump(r):
        out["ruler_used"][r] = out["ruler_used"].get(r, 0) + 1

    for key in all_keys:
        unit_name, func_name = key
        base = base_funcs.get(key)
        curr = curr_funcs.get(key)

        if base is None:
            # Present now, absent from the baseline map: a new / renamed /
            # re-ICF'd symbol.  There is no comparable baseline percent, so
            # this is NOT a +100% improvement -- it is its own population.
            if cov is not None:
                cov.drop("absent-from-baseline",
                         note="new, renamed or re-ICF'd key; no baseline percent to diff")
            out["only_current"].append({
                "unit": unit_name, "name": func_name,
                "display": curr["demangled"] or func_name,
                "curr_pct": curr["pct"], "size": curr["size"],
            })
            continue
        if curr is None:
            if cov is not None:
                cov.drop("absent-from-current",
                         note="deleted, renamed or re-ICF'd key; no current percent to diff")
            out["only_baseline"].append({
                "unit": unit_name, "name": func_name,
                "display": base["demangled"] or func_name,
                "base_pct": base["pct"], "size": base["size"],
            })
            continue

        if base["pct"] is None and curr["pct"] is None:
            if cov is not None:
                cov.drop("no-percent-either-side",
                         note="neither report scored this row")
            continue

        if cov is not None:
            cov.examine()
        _bump(curr["ruler"] if curr["pct"] is not None else base["ruler"])

        if base["pct"] is None or curr["pct"] is None:
            # One-sided.  Coercing the missing side to 0 is what made
            # "a body appeared" indistinguishable from a +95% improvement.
            bucket = "appeared" if base["pct"] is None else "vanished"
            out[bucket].append({
                "unit": unit_name, "name": func_name,
                "display": (curr["demangled"] or base["demangled"] or func_name),
                "base_pct": base["pct"], "curr_pct": curr["pct"],
                "size": curr["size"] or base["size"],
            })
            continue

        diff = curr["pct"] - base["pct"]
        if abs(diff) < min_diff:
            out["unchanged"] += 1
            continue

        display = curr["demangled"] or base["demangled"] or func_name
        base_norm, curr_norm = base["norm"], curr["norm"]
        # "Phantom" = the reloc-sensitive ruler moved but the canonical one did
        # not.  Only claim that when BOTH sides actually carry a normalized
        # figure; a missing one is unknown, not unchanged.  With the headline
        # diff now computed normalized-first, a phantom can only reach this
        # list when one of the two sides fell back to fuzzy -- which is exactly
        # the case worth flagging rather than silently mixing rulers.
        phantom = (base_norm is not None and curr_norm is not None
                   and float(base_norm) == float(curr_norm))
        out["changed"].append({
            "unit": unit_name,
            "name": func_name,
            "display": display,
            "base_pct": base["pct"],
            "curr_pct": curr["pct"],
            "diff_pct": diff,
            "base_norm": base_norm,
            "curr_norm": curr_norm,
            "norm_diff": (None if base_norm is None or curr_norm is None
                          else float(curr_norm) - float(base_norm)),
            "phantom": phantom,
            "size": curr["size"],
        })

    # Sort: most regressed first, then most improved.  Full tie-break so two
    # runs over the same pair of reports are byte-identical.
    out["changed"].sort(key=lambda x: (x["diff_pct"], x["unit"], x["name"]))
    for k in ("appeared", "vanished", "only_current", "only_baseline"):
        out[k].sort(key=lambda x: (x["unit"], x["name"]))
    if cov is not None:
        cov.extra("unchanged_within_min_diff", out["unchanged"])
        cov.extra("min_diff", min_diff)
        cov.extra("ruler_used", dict(sorted(out["ruler_used"].items())))
    return out


def print_subsystem_table(results: list, baseline: dict, current: dict):
    """Print subsystem comparison table."""
    total_funcs = sum(r["diff_funcs"] for r in results)
    base_total = baseline.get("measures", {}).get("fuzzy_match_percent", 0)
    curr_total = current.get("measures", {}).get("fuzzy_match_percent", 0)

    print()
    # `measures.fuzzy_match_percent` is objdiff's size-weighted mean of the
    # per-function NORMALIZED percentages, despite the key name.
    print(f"Overall normalized-weighted: {fmt_pct(base_total)} -> {fmt_pct(curr_total)} "
          f"({curr_total-base_total:+.2f}%)")
    n_base_subs = len(aggregate_by_subsystem(baseline.get("units", [])))
    n_curr_subs = len(aggregate_by_subsystem(current.get("units", [])))
    print(f"Subsystems changed: {len(results)} of {n_curr_subs} in current "
          f"({n_base_subs} in baseline), {total_funcs:+d} matched functions")
    print()

    # Calculate column widths
    headers = ["Subsystem", "Baseline", "Current", "Change", "Size"]
    rows = []
    for r in results:
        rows.append([
            r["subsystem"],
            fmt_pct(r["base_pct"]),
            fmt_pct(r["curr_pct"]),
            f"{r['diff_pct']:+.2f}%",
            fmt_bytes_plain(r["total_code"]),
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

    align = [False, True, True, True, True]  # Right-align numeric columns
    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))


NEAR_100 = 99.99   # the band a `%.2f` render collapses onto "100.00"


def count_100pct_units(baseline: dict, current: dict) -> tuple:
    """(at_100, newly_100, near_100, total_with_pct) for the current report.

    `at_100` is now STRICTLY `>= 100.0`.  It used to be `>= 99.99`, which
    counted a unit sitting at 99.995 as complete -- the same rounded-100 shape
    that hid two real bugs in this repo.  The [99.99, 100) band is still
    reported, as `near_100`, because it is a useful worklist; it is just no
    longer added to the "at 100%" total.
    """
    baseline_units = {u["name"]: u for u in baseline.get("units", [])}
    current_units = {u["name"]: u for u in current.get("units", [])}

    at_100 = 0
    newly_100 = 0
    near_100 = 0
    with_pct = 0
    for name in sorted(current_units):
        cu = current_units[name]
        curr_pct, _ruler = unit_match_percent_with_ruler(cu.get("measures", {}))
        if curr_pct is None:
            continue
        with_pct += 1
        if curr_pct >= 100.0:
            at_100 += 1
            if name in baseline_units:
                base_pct = get_unit_match_percent(baseline_units[name].get("measures", {}))
                if base_pct is None or base_pct < 100.0:
                    newly_100 += 1
            else:
                newly_100 += 1  # new unit not in baseline
        elif curr_pct >= NEAR_100:
            near_100 += 1
    return at_100, newly_100, near_100, with_pct


def print_unit_table(results: list, limit: int = 50, baseline: dict = None, current: dict = None):
    """Print detailed unit comparison table."""
    count = min(limit, len(results))
    print()
    print(f"Top {count} Unit Changes")
    print()

    # Show 100% summary from raw reports
    if baseline is not None and current is not None:
        at_100, newly_100, near_100, with_pct = count_100pct_units(baseline, current)
        if at_100 or near_100:
            print(f"  Units at 100% (strictly >= 100.00): {at_100} of {with_pct} scored "
                  f"({newly_100} new since baseline)")
            print(f"  Units in [{NEAR_100}, 100) -- render as 100.0 but are NOT: {near_100}")
            print()

    headers = ["Unit Path", "Baseline", "Current", "Change", "Funcs (base)", "Funcs (curr)"]
    rows = []
    for r in results[:limit]:
        rows.append([
            r["name"].replace("default/", ""),
            fmt_pct(r["base_pct"]),
            fmt_pct(r["curr_pct"]),
            f"{r['diff_pct']:+.2f}%",
            r["base_funcs"],
            r["curr_funcs"],
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

    align = [False, True, True, True, True, True]  # Right-align numeric columns
    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))


def print_function_table(populations: dict, limit: int = 100,
                         regressions_only: bool = False):
    """Print function-level comparison tables for every population.

    The `Regressions (N functions, showing top M)` caption below is the pattern
    the rest of this repo should copy: it prints the FULL count and the
    displayed count as two separate numbers, so a `--limit` can shorten the
    listing without ever shortening the total.
    """
    changed = populations["changed"]
    if regressions_only:
        changed = [r for r in changed if r["diff_pct"] < 0]

    regressions = [r for r in changed if r["diff_pct"] < 0]
    improvements = [r for r in changed if r["diff_pct"] > 0]

    if not changed:
        print("\nNo function-level changes found "
              f"(both sides scored and agreed within min-diff on "
              f"{populations['unchanged']} functions).")

    # State the ruler that was ACTUALLY used, with the per-ruler row counts, so
    # nobody has to infer it from the column header.  The diff below is
    # normalized-first with a fuzzy fallback; a row that fell back is the only
    # way relocation sensitivity can still reach this table, and those get
    # marked so a reader does not take an ICF thunk shuffle for a regression
    # and revert good work chasing it.
    phantom_reg = [r for r in regressions if r.get("phantom")]
    phantom_imp = [r for r in improvements if r.get("phantom")]
    ruler_used = populations.get("ruler_used") or {}
    print()
    print("Ruler: match_percent_normalized (CANONICAL), "
          "falling back to fuzzy_match_percent (relocation-sensitive) "
          "only where the canonical value is absent.")
    if ruler_used:
        print("  rows by ruler: "
              + ", ".join(f"{k}={v}" for k, v in sorted(ruler_used.items())))
    if phantom_reg or phantom_imp:
        print(f"  {len(phantom_reg)} of {len(regressions)} regressions and "
              f"{len(phantom_imp)} of {len(improvements)} improvements moved "
              f"while the canonical ruler did NOT (marked '~'):")
        print("  those necessarily came off the fuzzy fallback. Typically "
              ".text layout / ICF re-folding,")
        print("  not a source change. Confirm against the canonical ruler "
              "before acting on one.")

    if regressions:
        print()
        reg_count = min(limit, len(regressions))
        print(f"Regressions ({len(regressions)} functions, showing top {reg_count}):")
        print()
        _print_func_rows(regressions[:limit])

    if improvements:
        print()
        imp_count = min(limit, len(improvements))
        # Show improvements sorted best-first (full tie-break for determinism)
        imp_sorted = sorted(improvements,
                            key=lambda x: (-x["diff_pct"], x["unit"], x["name"]))
        print(f"Improvements ({len(improvements)} functions, showing top {imp_count}):")
        print()
        _print_func_rows(imp_sorted[:limit])

    # --- The populations that used to be invisible ------------------------- #
    for key, caption, pct_key in (
        ("appeared", "Body APPEARED (no baseline percent -> scored now; NOT an N% improvement)", "curr_pct"),
        ("vanished", "Body VANISHED (scored in baseline -> no percent now; NOT an N% regression)", "base_pct"),
        ("only_current", "Key only in CURRENT (new / renamed / re-ICF'd symbol)", "curr_pct"),
        ("only_baseline", "Key only in BASELINE (deleted / renamed / re-ICF'd symbol)", "base_pct"),
    ):
        rows = populations.get(key) or []
        if not rows or regressions_only and key in ("appeared", "only_current"):
            continue
        shown = min(limit, len(rows))
        print()
        print(f"{caption}: {len(rows)} functions, showing first {shown}")
        print()
        for r in rows[:limit]:
            unit = r["unit"].replace("default/", "")
            print(f"  {fmt_pct(r.get(pct_key)):>9s}  {unit[-34:]:34s}  {r['display'][:60]}")

    # Summary — every population, with its denominator
    total_reg = len(regressions)
    total_imp = len(improvements)
    reg_bytes = sum(r["size"] for r in regressions)
    imp_bytes = sum(r["size"] for r in improvements)
    print()
    print(f"Summary: {total_reg} regressions ({fmt_bytes_plain(reg_bytes)} affected), "
          f"{total_imp} improvements ({fmt_bytes_plain(imp_bytes)} affected), "
          f"{populations['unchanged']} unchanged, "
          f"{len(populations['appeared'])} appeared, "
          f"{len(populations['vanished'])} vanished, "
          f"{len(populations['only_current'])} only-in-current, "
          f"{len(populations['only_baseline'])} only-in-baseline")


def _print_func_rows(results: list):
    """Print rows for function comparison."""
    headers = ["Function", "Unit", "Base", "Curr", "Change", "Norm", "Size"]
    rows = []
    for r in results:
        # Truncate long demangled names
        display = r["display"]
        if len(display) > 60:
            display = display[:57] + "..."
        unit = r["unit"].replace("default/", "")
        # Shorten unit path
        if len(unit) > 30:
            unit = "..." + unit[-27:]
        # Norm column: the same delta on the canonical ruler. "~" means the
        # canonical ruler did not move at all, i.e. the fuzzy change beside it
        # is layout noise rather than a codegen change.
        nd = r.get("norm_diff")
        if r.get("phantom"):
            norm_cell = "~"
        elif nd is None:
            norm_cell = "?"
        else:
            norm_cell = f"{nd:+.1f}%"
        rows.append([
            display,
            unit,
            fmt_pct(r["base_pct"], 1),
            fmt_pct(r["curr_pct"], 1),
            f"{r['diff_pct']:+.1f}%",
            norm_cell,
            str(r["size"]),
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

    align = [False, False, True, True, True, True, True]
    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))


def get_category(subsystem: str) -> str:
    """Categorize a subsystem."""
    if subsystem.startswith("xdk/"):
        return "XDK"
    if subsystem.startswith("lib/"):
        return "Third-Party"
    if subsystem.startswith("lazer/"):
        return "Game Code"
    if subsystem.startswith("default/"):
        return "Standalone"
    return "Milo Engine"


def print_overview(report: dict):
    """Print complete overview grouped by category."""
    agg = aggregate_by_subsystem(report.get("units", []))

    # Build results with category
    results = []
    skipped_no_code = 0
    for sub, stats in sorted(agg.items()):
        if stats["total_code"] <= 0:
            skipped_no_code += 1
        else:
            pct = stats["weighted_fuzzy"] / stats["fuzzy_code"] if stats["fuzzy_code"] > 0 else 0
            results.append({
                "subsystem": sub,
                "category": get_category(sub),
                "total_code": stats["total_code"],
                "percent": pct,
                "matched_funcs": stats["matched_functions"],
                "total_funcs": stats["total_functions"],
            })

    # Overall stats
    measures = report.get("measures", {})
    total_pct = measures.get("fuzzy_match_percent", 0) or 0
    total_code = int(measures.get("total_code", 0) or 0)
    total_funcs_matched = int(measures.get("matched_functions", 0) or 0)
    total_funcs = int(measures.get("total_functions", 0) or 0)

    print()
    print(f"{'='*70}")
    print(f"  DECOMP OVERVIEW: {fmt_pct(total_pct)} normalized-weighted match")
    print(f"  Code: {fmt_bytes_plain(total_code)}")
    print(f"  Functions: {total_funcs_matched:,} / {total_funcs:,}")
    print(f"  Subsystems: {len(results)} of {len(agg)} "
          f"({skipped_no_code} have no code and are not shown)")
    print(f"{'='*70}")

    # Group by category
    categories = ["Game Code", "Milo Engine", "Third-Party", "XDK", "Standalone"]
    shown_cat_total = 0
    for cat in categories:
        cat_results = [r for r in results if r["category"] == cat]
        if not cat_results:
            continue
        shown_cat_total += len(cat_results)

        # Sort by size within category (full tie-break)
        cat_results.sort(key=lambda x: (-x["total_code"], x["subsystem"]))

        # Category totals
        cat_total = sum(r["total_code"] for r in cat_results)
        cat_weighted = sum(r["percent"] * r["total_code"] for r in cat_results)
        cat_pct = cat_weighted / cat_total if cat_total > 0 else 0
        cat_funcs_matched = sum(r["matched_funcs"] for r in cat_results)
        cat_funcs_total = sum(r["total_funcs"] for r in cat_results)

        print()
        print(f"## {cat}: {fmt_pct(cat_pct, 1)} ({fmt_bytes_plain(cat_total)}, {cat_funcs_matched}/{cat_funcs_total} funcs)")
        print()

        headers = ["Subsystem", "Norm %", "Total", "Funcs"]
        rows = []
        for r in cat_results:
            # Trim category prefix for cleaner display
            name = r["subsystem"]
            if name.startswith("system/"):
                name = name[7:]
            elif name.startswith("lazer/"):
                name = name[6:]
            elif name.startswith("xdk/"):
                name = name[4:]
            elif name.startswith("lib/"):
                name = name[4:]
            elif name.startswith("default/"):
                name = name[8:]

            rows.append([
                name,
                fmt_pct(r["percent"], 1),
                fmt_bytes_plain(r["total_code"]),
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
            return "  " + " | ".join(parts)

        align = [False, True, True, True]
        print(fmt_row(headers))
        print("  " + "-+-".join("-" * w for w in widths))
        for row in rows:
            print(fmt_row(row, align))

    print()


def print_snapshot(report: dict, sort_by: str = "percent", show_all: bool = False):
    """Print current snapshot of all subsystems."""
    agg = aggregate_by_subsystem(report.get("units", []))

    # Build results list with filtering.  Every skip is COUNTED: a table that
    # silently drops 60% of its subsystems is a sample presented as a total.
    results = []
    hidden_excluded_prefix = 0
    hidden_too_small = 0
    hidden_no_code = 0
    for sub, stats in sorted(agg.items()):
        if stats["total_code"] <= 0:
            hidden_no_code += 1
            continue
        # Filter out excluded prefixes and small subsystems unless --all
        if not show_all:
            if any(sub.startswith(prefix) for prefix in EXCLUDED_PREFIXES):
                hidden_excluded_prefix += 1
                continue
            if stats["total_code"] < DEFAULT_MIN_SIZE:
                hidden_too_small += 1
                continue

        pct = stats["weighted_fuzzy"] / stats["fuzzy_code"] if stats["fuzzy_code"] > 0 else 0
        results.append({
            "subsystem": sub,
            "total_code": stats["total_code"],
            "percent": pct,
            "matched_funcs": stats["matched_functions"],
            "total_funcs": stats["total_functions"],
        })

    # Sort (full tie-break on subsystem name so repeat runs are identical)
    if sort_by == "percent":
        results.sort(key=lambda x: (-x["percent"], x["subsystem"]))
    elif sort_by == "size":
        results.sort(key=lambda x: (-x["total_code"], x["subsystem"]))
    elif sort_by == "matched":
        results.sort(key=lambda x: (-(x["percent"] * x["total_code"]), x["subsystem"]))
    else:  # name
        results.sort(key=lambda x: x["subsystem"])

    # Overall stats
    measures = report.get("measures", {})
    total_pct = measures.get("fuzzy_match_percent", 0) or 0
    total_code = int(measures.get("total_code", 0) or 0)
    total_funcs_matched = int(measures.get("matched_functions", 0) or 0)
    total_funcs = int(measures.get("total_functions", 0) or 0)

    print()
    print(f"Overall normalized-weighted: {fmt_pct(total_pct)} "
          f"({fmt_bytes_plain(total_code)})")
    print(f"Functions: {total_funcs_matched}/{total_funcs}")
    hidden_total = hidden_excluded_prefix + hidden_too_small + hidden_no_code
    print(f"Subsystems shown: {len(results)} of {len(agg)}"
          + (f"  (hidden: {hidden_excluded_prefix} by EXCLUDED_PREFIXES"
             f"{EXCLUDED_PREFIXES}, {hidden_too_small} below "
             f"DEFAULT_MIN_SIZE={DEFAULT_MIN_SIZE}B, {hidden_no_code} with no code"
             f" -- pass --all to include them)" if hidden_total else ""))
    print()

    headers = ["Subsystem", "Norm %", "Total", "Functions"]
    rows = []
    for r in results:
        rows.append([
            r["subsystem"],
            fmt_pct(r["percent"]),
            fmt_bytes_plain(r["total_code"]),
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

    align = [False, True, True, True]
    print(fmt_row(headers))
    print("|" + "|".join("-" * (w + 2) for w in widths) + "|")
    for row in rows:
        print(fmt_row(row, align))
    print()


def main():
    parser = argparse.ArgumentParser(
        description="Compare decomp progress between two report.json files, or show snapshot",
        epilog="""examples:
  %(prog)s --snapshot                           Show all subsystems
  %(prog)s --snapshot --filter 'system/ui/*'    Show only system/ui subsystems
  %(prog)s --snapshot -g '*/char/*' -g '*/anim/*'  Multiple filters
  %(prog)s -g 'system/synth/*' --functions baseline.json current.json
  %(prog)s --overview --filter 'lazer/*'        Overview filtered to game code
""",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "baseline",
        nargs="?",
        type=Path,
        help="Path to baseline report.json (or report for --snapshot)",
    )
    parser.add_argument(
        "current",
        nargs="?",
        type=Path,
        help="Path to current report.json",
    )
    parser.add_argument(
        "--overview", "-o",
        action="store_true",
        help="Show complete overview grouped by category (Game/Milo/XDK)",
    )
    parser.add_argument(
        "--snapshot", "-s",
        action="store_true",
        help="Show current snapshot of all subsystems (no comparison)",
    )
    parser.add_argument(
        "--sort",
        choices=["name", "percent", "size", "matched"],
        default="percent",
        help="Sort snapshot by: name, percent (default), size, or matched bytes",
    )
    parser.add_argument(
        "--all", "-a",
        action="store_true",
        help="Show all subsystems (including xdk, lib, tiny ones)",
    )
    parser.add_argument(
        "--detailed",
        action="store_true",
        help="Show detailed per-unit breakdown",
    )
    parser.add_argument(
        "--functions", "-f",
        action="store_true",
        help="Show function-level changes (most useful for finding regressions)",
    )
    parser.add_argument(
        "--regressions", "-r",
        action="store_true",
        help="Only show regressions (negative changes) in all views",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=50,
        help="Max items to show in detailed/function view (default: 50)",
    )
    parser.add_argument(
        "--filter", "-g",
        action="append",
        default=[],
        metavar="PATTERN",
        help="Filter units by glob pattern (e.g. 'system/ui/*', '*/char/*'). Can be repeated.",
    )
    parser.add_argument(
        "--map-file",
        type=Path,
        default=DEFAULT_MAP_FILE,
        help=f"Linker map file for resolving merged symbols (default: {DEFAULT_MAP_FILE})",
    )
    parser.add_argument(
        "--no-merged-resolution",
        action="store_true",
        help="Disable merged symbol resolution (skip map file parsing)",
    )
    add_coverage_args(parser)

    args = parser.parse_args()
    cov = CoverageReport("compare_progress", args=args)

    # Overview mode - grouped by category
    if args.overview:
        if args.baseline:
            report_path = args.baseline
        else:
            report_path = Path("build/373307D9/report.json")

        if not report_path.exists():
            print(f"Error: Report not found: {report_path}")
            print("Run 'ninja' first to generate the report.")
            sys.exit(1)

        report = load_report(report_path)
        all_units = report.get("units", [])
        if args.filter:
            kept = filter_units(all_units, args.filter)
            cov.universe(len(all_units), "units in the report")
            cov.drop("filtered-out-by---filter", len(all_units) - len(kept),
                     note=f"patterns={args.filter}")
            cov.examine(len(kept))
            report["units"] = kept
        else:
            cov.universe(len(all_units), "units in the report")
            cov.examine(len(all_units))
        cov.note("unit ruler: measures.fuzzy_match_percent, which objdiff builds "
                 "from per-function match_percent_normalized (canonical)")
        print_overview(report)
        sys.exit(cov.emit())

    # Snapshot mode - show current state without comparison
    if args.snapshot:
        if args.baseline:
            report_path = args.baseline
        else:
            report_path = Path("build/373307D9/report.json")

        if not report_path.exists():
            print(f"Error: Report not found: {report_path}")
            print("Run 'ninja' first to generate the report.")
            sys.exit(1)

        report = load_report(report_path)
        all_units = report.get("units", [])
        cov.universe(len(all_units), "units in the report")
        if args.filter:
            kept = filter_units(all_units, args.filter)
            cov.drop("filtered-out-by---filter", len(all_units) - len(kept),
                     note=f"patterns={args.filter}")
            report["units"] = kept
        else:
            kept = all_units
        cov.examine(len(kept))
        cov.note("unit ruler: measures.fuzzy_match_percent, which objdiff builds "
                 "from per-function match_percent_normalized (canonical)")
        cov.note("--all / DEFAULT_MIN_SIZE additionally hide subsystems from the "
                 "TABLE below; see the 'subsystems hidden' line printed with it")
        print_snapshot(report, sort_by=args.sort, show_all=args.all)
        sys.exit(cov.emit())

    # Comparison mode - need both reports
    if not args.baseline or not args.current:
        parser.error("comparison mode requires both baseline and current reports (or use --snapshot)")

    if not args.baseline.exists():
        print(f"Error: Baseline report not found: {args.baseline}")
        sys.exit(1)
    if not args.current.exists():
        print(f"Error: Current report not found: {args.current}")
        sys.exit(1)

    baseline = load_report(args.baseline)
    current = load_report(args.current)

    if args.filter:
        baseline["units"] = filter_units(baseline.get("units", []), args.filter)
        current["units"] = filter_units(current.get("units", []), args.filter)

    # Set up merged symbol resolver for function-level comparison
    merged_resolver = None
    if not args.no_merged_resolution and args.map_file.exists():
        merged_resolver = MergedSymbolResolver(args.map_file)

    # Always show subsystem summary
    ruler_counts: dict = {}
    aggregate_by_subsystem(current.get("units", []), ruler_counts)
    subsystem_results = compare_subsystems(baseline, current)
    if args.regressions:
        subsystem_results = [r for r in subsystem_results if r["diff_pct"] < 0]
    print_subsystem_table(subsystem_results, baseline, current)
    print(f"  ruler for the subsystem/unit tables: "
          f"{ruler_counts.get(RULER_NORMALIZED, 0)} units scored by the "
          f"normalized-weighted measures.fuzzy_match_percent, "
          f"{ruler_counts.get(RULER_FUZZY, 0)} by the RAW matched_code_percent "
          f"fallback, {ruler_counts.get(RULER_NONE, 0)} unscored")

    # Optionally show detailed unit breakdown
    if args.detailed:
        unit_results = compare_units(baseline, current)
        if args.regressions:
            unit_results = [r for r in unit_results if r["diff_pct"] < 0]
        print_unit_table(unit_results, args.limit, baseline=baseline, current=current)

    # Optionally show function-level breakdown
    if args.functions:
        populations = compare_functions(baseline, current,
                                        merged_resolver=merged_resolver, cov=cov)
        print_function_table(populations, args.limit,
                             regressions_only=args.regressions)
    else:
        # Still declare a denominator for the run we DID do.
        n_units = len(current.get("units", []))
        cov.universe(n_units, "units in the current report (function view not requested)")
        cov.examine(n_units)
        cov.note("pass --functions for the per-function census")

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
