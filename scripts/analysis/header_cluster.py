#!/usr/bin/env python3
"""Automated header-level cluster detection from report.json.

Identifies groups of non-100% functions that share the same match percentage
across multiple translation units — the signature of a shared header-level
root cause (template bug, wrong inlining, struct layout error).

Analysis modes:
  match-pct    — Cluster by exact match% across TUs
  template     — Cluster by template/class name patterns in demangled symbols
  combined     — Cross-reference both (default)
  opportunities — Actionable clusters ranked by fixability × impact

Usage:
    python3 scripts/analysis/header_cluster.py
    python3 scripts/analysis/header_cluster.py --mode opportunities --min-cluster 5
    python3 scripts/analysis/header_cluster.py --mode template --pattern "ObjPtrVec"
    python3 scripts/analysis/header_cluster.py --json  # machine-readable output

COVERAGE / HONESTY NOTES  (see scripts/analysis/coverage.py)
------------------------------------------------------------
This scanner used to print `Loaded 2241 non-complete functions (50–100% range)`
against a report.json holding 48,344 function rows.  Three separate filters ate
the difference and none of them was mentioned:

  * `func.get("fuzzy_match_percent", 0)` defaulted a MISSING key to 0, and the
    next line said `if ... or pct <= 0: continue`.  objdiff only emits
    `fuzzy_match_percent` for functions we actually DEFINE, so all 16,920 rows
    with no body at all (35.0% of the report) were silently discarded by a guard
    written for "true zero" rows — of which there are exactly ZERO.
  * `pct >= 99.95` discarded 29,151 rows as "complete".  99.96 is NOT 100%.
  * `--min-pct` (default 50) discarded a further 32.

Every one of those is now routed through `CoverageReport.drop()` and named in
the COVERAGE block on stderr.  The filters themselves are UNCHANGED — this is a
denominator fix, not a heuristic change — except for the one thing that makes
the 16,920 countable at all: we now fall back to `match_percent_normalized`
when `fuzzy_match_percent` is absent (see `_row_pct`).

BIN WIDTH: the "exact match% cluster" key is `round(pct, 1)`, i.e. a 0.1-WIDE
BIN.  99.94 and 100.0 land in the same bucket.  The binning is deliberately left
as it was; it is now LABELLED everywhere it is printed or serialised.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402


# The cluster key is round(pct, 1): a 0.1-wide bin, NOT an exact percentage.
_PCT_BIN_WIDTH = 0.1
_BIN_LABEL = f"{_PCT_BIN_WIDTH:.1f}pp-wide bin (key = round(pct, 1))"

# `pct >= _COMPLETE_THRESHOLD` is treated as complete and dropped.  99.96 is not
# 100%; this threshold is retained unchanged, but it is now counted and named.
_COMPLETE_THRESHOLD = 99.95


def _fmt_pct(p: float, width: int = 0) -> str:
    """Render a percentage WITHOUT letting a sub-100 value print as `100.0`.

    Every percentage surface in this project rounds, and two real bugs have
    already hidden under a rendered `100.0` that was really 99.97.  Anything
    strictly below 100 that would round up renders as `<100` instead.
    """
    s = f"{p:.1f}"
    if p < 100.0 and float(s) >= 100.0:
        s = "<100"
    return s.rjust(width) if width else s


# Template/class patterns to search for in demangled names.
# Ordered roughly by historical impact.
_TEMPLATE_PATTERNS = [
    # STL containers and algorithms
    ("vector", r"\bvector\b"),
    ("_M_fill_insert", r"_M_fill_insert"),
    ("_M_insert_overflow", r"_M_insert_overflow"),
    ("_M_insert_aux", r"_M_insert_aux"),
    ("push_heap", r"push_heap|__push_heap"),
    ("sort", r"\bsort\b.*<"),
    ("copy", r"\bcopy\b.*<"),
    # Milo engine ObjPtr
    ("ObjPtrVec", r"ObjPtrVec"),
    ("ObjPtrList", r"ObjPtrList"),
    ("ObjRef", r"ObjRef"),
    ("ObjPtr::operator", r"ObjPtr.*operator"),
    # Milo core
    ("ObjectDir::Find", r"ObjectDir::Find"),
    ("DataNode", r"DataNode"),
    ("DataArray", r"DataArray"),
    ("Symbol", r"\bSymbol\b"),
    ("iterator", r"\biterator\b"),
    ("operator=", r"operator="),
    ("operator+", r"operator\+"),
    ("operator==", r"operator=="),
    ("operator!=", r"operator!="),
    # Common inlined functions
    ("Save", r"::Save\b"),
    ("Load", r"::Load\b"),
    ("Copy", r"::Copy\b"),
    ("Poll", r"::Poll\b"),
    ("Draw", r"::Draw\b"),
    ("Enter", r"::Enter\b"),
    ("Init", r"::Init\b"),
    ("Handle", r"::Handle\b"),
]

# Known unfixable patterns (from AT_LIMIT analysis)
_UNFIXABLE_PATTERNS = {
    "operator+": "ObjPtrVec::iterator::operator+ copy-semantic (header-driven, 68 known regressions)",
    "_M_fill_insert": "May be fixed (CopyRef operator= fix applied)",
}


@dataclass
class FuncInfo:
    """Lightweight function record from report.json."""
    name: str
    demangled: str
    unit: str
    pct: float
    size: int
    address: str = ""
    # Which ruler `pct` came from: "fuzzy" (fuzzy_match_percent, present only
    # for functions we define) or "normalized" (match_percent_normalized,
    # present for all 48,344 rows).  Carried so a consumer can never mistake a
    # no-body row for a scored one.
    ruler: str = "fuzzy"

    @property
    def sort_key(self) -> tuple:
        """Full tie-breaking key — never sort FuncInfo on pct alone."""
        return (-self.pct, self.unit, self.demangled, self.name)


@dataclass
class MatchCluster:
    """A group of functions sharing the same match percentage."""
    pct: float
    functions: list[FuncInfo] = field(default_factory=list)

    @property
    def count(self) -> int:
        return len(self.functions)

    @property
    def unit_count(self) -> int:
        return len(set(f.unit for f in self.functions))

    @property
    def units(self) -> set[str]:
        return set(f.unit for f in self.functions)

    @property
    def total_size(self) -> int:
        return sum(f.size for f in self.functions)

    @property
    def sorted_functions(self) -> list[FuncInfo]:
        """Deterministic order for printing/serialising — never rely on report order."""
        return sorted(self.functions, key=lambda f: f.sort_key)


@dataclass
class TemplateCluster:
    """A group of functions matching a template/class pattern."""
    pattern_name: str
    functions: list[FuncInfo] = field(default_factory=list)

    @property
    def count(self) -> int:
        return len(self.functions)

    @property
    def unit_count(self) -> int:
        return len(set(f.unit for f in self.functions))

    @property
    def pct_distribution(self) -> dict[float, int]:
        # NOTE: round(pct, 1) is a 0.1pp-wide BIN, not an exact percentage.
        c = Counter(round(f.pct, 1) for f in self.functions)
        return dict(sorted(c.items()))

    @property
    def sorted_functions(self) -> list[FuncInfo]:
        return sorted(self.functions, key=lambda f: f.sort_key)

    @property
    def median_pct(self) -> float:
        pcts = sorted(f.pct for f in self.functions)
        n = len(pcts)
        if n == 0:
            return 0.0
        mid = n // 2
        if n % 2 == 0:
            return (pcts[mid - 1] + pcts[mid]) / 2
        return pcts[mid]


@dataclass
class Opportunity:
    """A ranked, actionable cluster with estimated impact."""
    pattern_name: str
    match_pct: Optional[float]  # None if spread across multiple %s
    function_count: int
    unit_count: int
    total_code_bytes: int
    median_pct: float
    fixability: str  # "likely_fixable", "maybe_fixable", "likely_unfixable"
    reason: str
    sample_functions: list[str]  # demangled names — a SAMPLE, see *_total below
    sample_units: list[str]
    # How many there really are, so a truncated sample can never be read as a total.
    sample_functions_total: int = 0
    sample_units_total: int = 0

    @property
    def impact_score(self) -> float:
        """Higher = more impactful to fix. Combines count, spread, and proximity to 100%."""
        fix_weight = {"likely_fixable": 1.0, "maybe_fixable": 0.5, "likely_unfixable": 0.1}
        w = fix_weight.get(self.fixability, 0.3)
        # Weight by: function count × unit spread × closeness to 100%
        closeness = max(0, (self.median_pct - 50) / 50)  # 0..1 scale
        return self.function_count * (1 + self.unit_count * 0.2) * closeness * w


def _row_pct(func: dict) -> tuple[float, str]:
    """Return ``(pct, ruler)`` for one report.json function row.

    ***DENOMINATOR FIX — read this before "simplifying" it.***
    objdiff emits `fuzzy_match_percent` ONLY for functions we have actually
    defined.  On this tree that key is absent from 16,920 of 48,344 rows (35.0%)
    — the entire "we wrote no body at all" tier.  The old code did
    ``func.get("fuzzy_match_percent", 0)`` and then dropped everything ``<= 0``,
    so that whole tier was invisible: not "examined and rejected", but never
    counted, never in any denominator, and therefore repeatedly declared
    exhausted.  `match_percent_normalized` is present on ALL 48,344 rows, so
    falling back to it is what makes those rows COUNTABLE.  They still get
    filtered out by the same `<= 0` / `--min-pct` guards as before — but now
    they appear in the COVERAGE block as a named drop instead of vanishing.
    """
    p = func.get("fuzzy_match_percent")
    if p is not None:
        return float(p), "fuzzy"
    n = func.get("match_percent_normalized")
    if n is not None:
        return float(n), "normalized"
    return 0.0, "absent"


def load_report(report_path: str | Path,
                cov: Optional[CoverageReport] = None,
                min_pct: float = 0.0,
                max_pct: float = 100.0) -> list[FuncInfo]:
    """Load the non-complete functions from report.json, counting every discard.

    `cov` is optional so existing library callers keep working, but a call
    without it cannot report its own denominator — pass one.

    The percentage window (`min_pct` <= pct < `max_pct`) is applied HERE rather
    than by the caller so that the rows it removes land in the coverage block
    instead of disappearing between two function calls.
    """
    with open(report_path) as f:
        report = json.load(f)

    rows = [(unit["name"], func)
            for unit in report.get("units", [])
            for func in unit.get("functions", [])]

    if cov is not None:
        cov.universe(len(rows), "function rows in report.json (ALL units)")
        cov.note(f"cluster key is a {_BIN_LABEL} — an 'exact match%' cluster is a "
                 f"{_PCT_BIN_WIDTH:.1f}pp band, so 99.94 and 100.0 share a bucket")
        cov.note(f"rows at pct >= {_COMPLETE_THRESHOLD} are dropped as complete; "
                 f"99.96 is NOT 100% and this threshold is unchanged, only counted")

    functions: list[FuncInfo] = []
    ruler_counts: Counter[str] = Counter()

    for unit_name, func in rows:
        pct, ruler = _row_pct(func)
        ruler_counts[ruler] += 1

        if pct >= _COMPLETE_THRESHOLD:
            if cov is not None:
                cov.drop("complete-at-or-above-99.95", note=(
                    f"pct >= {_COMPLETE_THRESHOLD} treated as complete "
                    f"(99.96 is not 100%)"))
            continue
        if pct <= 0:
            # TODO(heuristic): this guard was written for "true zero" rows and
            # there are NONE on the fuzzy ruler.  On the normalized ruler it is
            # the no-body tier.  Left as-is deliberately (widening what this
            # scanner FINDS is a separate change); it is now counted.
            if cov is not None:
                cov.drop("zero-pct-no-body", note=(
                    "pct <= 0; on this tree these are the rows objdiff scored "
                    "without a fuzzy_match_percent key at all (no body emitted)"))
            continue
        if pct < min_pct:
            if cov is not None:
                cov.drop("below---min-pct", note=f"pct < {min_pct} (deliberate filter)")
            continue
        if pct >= max_pct:
            if cov is not None:
                cov.drop("at-or-above---max-pct", note=f"pct >= {max_pct} (deliberate filter)")
            continue

        demangled = func.get("metadata", {}).get("demangled_name", func["name"])
        functions.append(FuncInfo(
            name=func["name"],
            demangled=demangled,
            unit=unit_name,
            pct=pct,
            size=int(func.get("size", 0)),
            address=func.get("address", ""),
            ruler=ruler,
        ))
        if cov is not None:
            cov.examine()

    if cov is not None:
        for ruler in sorted(ruler_counts):
            cov.extra(f"rows_on_{ruler}_ruler", ruler_counts[ruler])
        cov.note(
            "ruler census over the whole universe: "
            + ", ".join(f"{r}={ruler_counts[r]}" for r in sorted(ruler_counts))
            + "  (`normalized` rows have no fuzzy_match_percent key)")
    return functions


def cluster_by_match_pct(functions: list[FuncInfo],
                          min_cluster: int = 3,
                          min_units: int = 2,
                          pct_tolerance: float = 0.0) -> list[MatchCluster]:
    """Group functions by exact match percentage.

    Args:
        functions: Non-100% functions from report.
        min_cluster: Minimum functions to form a cluster.
        min_units: Minimum distinct TUs to count as cross-unit.
        pct_tolerance: If > 0, merge adjacent percentages within tolerance.
    """
    by_pct: dict[float, list[FuncInfo]] = defaultdict(list)
    for f in functions:
        # 0.1pp-wide BIN, not an exact match%. Binning intentionally unchanged.
        key = round(f.pct, 1)
        by_pct[key].append(f)

    if pct_tolerance > 0:
        # Merge adjacent bins
        merged: dict[float, list[FuncInfo]] = {}
        sorted_keys = sorted(by_pct.keys())
        i = 0
        while i < len(sorted_keys):
            base = sorted_keys[i]
            group = list(by_pct[base])
            j = i + 1
            while j < len(sorted_keys) and sorted_keys[j] - base <= pct_tolerance:
                group.extend(by_pct[sorted_keys[j]])
                j += 1
            # Use median as representative
            median_pct = round(sorted(f.pct for f in group)[len(group) // 2], 1)
            merged[median_pct] = group
            i = j
        by_pct = merged

    clusters = []
    for pct in sorted(by_pct):
        funcs = by_pct[pct]
        units = set(f.unit for f in funcs)
        if len(funcs) >= min_cluster and len(units) >= min_units:
            clusters.append(MatchCluster(pct=pct, functions=funcs))

    # Full tie-breaking key: count, then bin, then the lexically-first unit —
    # two runs over the same report must produce byte-identical output.
    clusters.sort(key=lambda c: (-c.count, -c.pct,
                                 min(c.units) if c.units else "",
                                 -c.total_size))
    return clusters


def cluster_by_template(functions: list[FuncInfo],
                         min_cluster: int = 3,
                         pattern_filter: Optional[str] = None) -> list[TemplateCluster]:
    """Group functions by template/class name pattern in demangled names."""
    patterns = _TEMPLATE_PATTERNS
    if pattern_filter:
        patterns = [(n, r) for n, r in patterns if pattern_filter.lower() in n.lower()]

    clusters: dict[str, TemplateCluster] = {}
    for name, regex in patterns:
        compiled = re.compile(regex)
        matches = [f for f in functions if compiled.search(f.demangled)]
        if len(matches) >= min_cluster:
            clusters[name] = TemplateCluster(pattern_name=name, functions=matches)

    # Full tie-breaking key (pattern_name) — Counter/dict insertion order is not
    # a sort key, and two runs must agree byte-for-byte.
    result = sorted(clusters.values(), key=lambda c: (-c.count, c.pattern_name))
    return result


def find_opportunities(functions: list[FuncInfo],
                        min_cluster: int = 3) -> list[Opportunity]:
    """Cross-reference match% clusters with template patterns to find actionable fixes."""
    match_clusters = cluster_by_match_pct(functions, min_cluster=min_cluster)
    template_clusters = cluster_by_template(functions, min_cluster=min_cluster)

    opportunities: list[Opportunity] = []

    # 1. Template clusters that concentrate at specific match percentages
    for tc in template_clusters:
        pct_dist = tc.pct_distribution
        # Find if there's a dominant percentage (>40% of functions)
        total = tc.count
        for pct, count in pct_dist.items():
            if count < min_cluster:
                continue
            concentration = count / total
            if concentration < 0.3:
                continue

            subset = sorted((f for f in tc.functions if abs(round(f.pct, 1) - pct) < 0.1),
                            key=lambda f: f.sort_key)
            units = set(f.unit for f in subset)

            # Determine fixability
            fixability = "maybe_fixable"
            reason = f"{tc.pattern_name} template at {pct}% across {len(units)} TUs"

            if tc.pattern_name in _UNFIXABLE_PATTERNS:
                fixability = "likely_unfixable"
                reason = _UNFIXABLE_PATTERNS[tc.pattern_name]
            elif len(units) >= 5 and pct > 80:
                fixability = "likely_fixable"
                reason = f"High concentration ({count}/{total}) at {pct}% across {len(units)} TUs — likely shared header template"
            elif pct < 60:
                fixability = "maybe_fixable"
                reason = f"Low match% suggests deep structural issue"

            opportunities.append(Opportunity(
                pattern_name=tc.pattern_name,
                match_pct=pct,
                function_count=count,
                unit_count=len(units),
                total_code_bytes=sum(f.size for f in subset),
                median_pct=pct,
                fixability=fixability,
                reason=reason,
                sample_functions=[f.demangled[:80] for f in subset[:5]],
                sample_units=sorted(units)[:5],
                sample_functions_total=len(subset),
                sample_units_total=len(units),
            ))

    # 2. Large match% clusters without a known template pattern
    known_in_templates = set()
    for tc in template_clusters:
        for f in tc.functions:
            known_in_templates.add(f.name)

    for mc in match_clusters:
        # Filter to functions NOT already in a template cluster
        orphans = sorted((f for f in mc.functions if f.name not in known_in_templates),
                         key=lambda f: f.sort_key)
        units = set(f.unit for f in orphans)
        if len(orphans) < min_cluster or len(units) < 2:
            continue

        # Try to find a common class/namespace
        common = _find_common_prefix(orphans)
        pattern_name = common if common else f"unknown@{mc.pct}%"

        opportunities.append(Opportunity(
            pattern_name=pattern_name,
            match_pct=mc.pct,
            function_count=len(orphans),
            unit_count=len(units),
            total_code_bytes=sum(f.size for f in orphans),
            median_pct=mc.pct,
            fixability="maybe_fixable",
            reason=(f"{len(orphans)} functions in the {mc.pct}% {_BIN_LABEL} "
                    f"across {len(units)} TUs — unknown shared cause"),
            sample_functions=[f.demangled[:80] for f in orphans[:5]],
            sample_units=sorted(units)[:5],
            sample_functions_total=len(orphans),
            sample_units_total=len(units),
        ))

    # Sort by impact score descending, with a full tie-break so two runs agree.
    opportunities.sort(key=lambda o: (-o.impact_score, o.pattern_name,
                                      -(o.match_pct or 0.0), -o.function_count))

    # Deduplicate overlapping opportunities
    seen_patterns: set[tuple[str, Optional[float]]] = set()
    deduped = []
    for o in opportunities:
        key = (o.pattern_name, o.match_pct)
        if key not in seen_patterns:
            seen_patterns.add(key)
            deduped.append(o)
    return deduped


def _find_common_prefix(functions: list[FuncInfo]) -> str:
    """Find common class/namespace prefix in demangled names."""
    # Extract class::method patterns
    classes: Counter[str] = Counter()
    for f in functions:
        m = re.match(r"(?:virtual\s+)?(?:\w+\s+)?(\w[\w:]*)::", f.demangled)
        if m:
            classes[m.group(1)] += 1

    if not classes:
        return ""

    # Return the most common class if it covers >40% of functions
    top, count = classes.most_common(1)[0]
    if count / len(functions) > 0.4:
        return top
    return ""


# ---------------------------------------------------------------------------
# Output formatting
# ---------------------------------------------------------------------------

def print_match_clusters(clusters: list[MatchCluster], limit: int = 30) -> None:
    print("\n" + "=" * 75)
    print("MATCH PERCENTAGE CLUSTERS")
    print(f"Functions sharing a match% BIN across multiple TUs — {_BIN_LABEL}")
    print("=" * 75)

    if not clusters:
        print("  No significant match% clusters found.")
        return

    shown = clusters[:limit]
    print(f"  showing {len(shown)} of {len(clusters)} clusters "
          f"({sum(c.count for c in shown)} of "
          f"{sum(c.count for c in clusters)} clustered functions)")

    for i, c in enumerate(shown):
        print(f"\n  {_fmt_pct(c.pct, 5)}% bin: {c.count} functions across {c.unit_count} TUs "
              f"({c.total_size:,} bytes)")
        # Show unit distribution — sorted with a full tie-break, not most_common
        unit_counts = Counter(f.unit for f in c.functions)
        ordered = sorted(unit_counts.items(), key=lambda kv: (-kv[1], kv[0]))
        for unit, cnt in ordered[:5]:
            # Extract just the filename
            short = Path(unit).stem if "/" in unit else unit
            print(f"    {cnt:3d} in {short}")
        if len(unit_counts) > 5:
            print(f"    ... and {len(unit_counts) - 5} more TUs")
        # Show sample demangled names
        seen = set()
        ordered_funcs = c.sorted_functions
        for f in ordered_funcs[:4]:
            if f.demangled not in seen:
                seen.add(f.demangled)
                print(f"    → {f.demangled[:72]}")
        if len(ordered_funcs) > 4:
            print(f"    ... and {len(ordered_funcs) - 4} more functions in this bin")

    if len(clusters) > limit:
        print(f"\n  ... and {len(clusters) - limit} more clusters not shown "
              f"({sum(c.count for c in clusters[limit:])} further functions)")


def print_template_clusters(clusters: list[TemplateCluster], limit: int = 20) -> None:
    print("\n" + "=" * 75)
    print("TEMPLATE / CLASS PATTERN CLUSTERS")
    print("Functions grouped by shared template or class name")
    print("=" * 75)

    if not clusters:
        print("  No significant template clusters found.")
        return

    shown = clusters[:limit]
    print(f"  showing {len(shown)} of {len(clusters)} template clusters")

    for tc in shown:
        pct_dist = tc.pct_distribution
        pct_range = f"{_fmt_pct(min(pct_dist.keys()))}–{_fmt_pct(max(pct_dist.keys()))}%"
        print(f"\n  {tc.pattern_name}: {tc.count} functions across "
              f"{tc.unit_count} TUs ({pct_range}, {_BIN_LABEL})")

        # Show percentage distribution (bins), full tie-break on the sort
        ordered_pcts = sorted(pct_dist.items(), key=lambda x: (-x[1], -x[0]))
        dist_str = ", ".join(f"{_fmt_pct(p)}%×{n}" for p, n in ordered_pcts[:5])
        more = f"  (+{len(ordered_pcts) - 5} more bins)" if len(ordered_pcts) > 5 else ""
        print(f"    Distribution: {dist_str}{more}")

        if tc.pattern_name in _UNFIXABLE_PATTERNS:
            print(f"    ⚠ Known: {_UNFIXABLE_PATTERNS[tc.pattern_name]}")

        # Show sample functions
        ordered_funcs = tc.sorted_functions
        for f in ordered_funcs[:3]:
            print(f"    → {f.demangled[:72]}  [{_fmt_pct(f.pct)}%]")
        if len(ordered_funcs) > 3:
            print(f"    ... and {len(ordered_funcs) - 3} more functions in this cluster")

    if len(clusters) > limit:
        print(f"\n  ... and {len(clusters) - limit} more template clusters not shown")


def print_opportunities(opportunities: list[Opportunity], limit: int = 20) -> None:
    print("\n" + "=" * 75)
    print("ACTIONABLE OPPORTUNITIES (ranked by impact)")
    print("=" * 75)

    if not opportunities:
        print("  No actionable opportunities found.")
        return

    shown = opportunities[:limit]
    print(f"  showing {len(shown)} of {len(opportunities)} opportunities")

    for i, o in enumerate(shown, 1):
        fix_icon = {"likely_fixable": "+", "maybe_fixable": "?", "likely_unfixable": "-"}
        icon = fix_icon.get(o.fixability, "?")
        pct_str = f"{_fmt_pct(o.match_pct)}% bin" if o.match_pct else "mixed"
        print(f"\n  [{icon}] #{i}: {o.pattern_name} @ {pct_str}")
        print(f"      {o.function_count} functions across {o.unit_count} TUs "
              f"({o.total_code_bytes:,} bytes)")
        print(f"      Impact score: {o.impact_score:.1f}")
        print(f"      {o.reason}")
        for fn in o.sample_functions[:3]:
            print(f"      → {fn}")
        extra_fn = max(0, o.sample_functions_total - min(3, len(o.sample_functions)))
        if extra_fn:
            print(f"      ... and {extra_fn} more functions (sample of "
                  f"{o.sample_functions_total})")
        if o.sample_units:
            print(f"      TUs: {', '.join(Path(u).stem for u in o.sample_units[:4])}")
            extra_u = max(0, o.sample_units_total - min(4, len(o.sample_units)))
            if extra_u:
                print(f"      ... and {extra_u} more TUs (sample of {o.sample_units_total})")

    if len(opportunities) > limit:
        print(f"\n  ... and {len(opportunities) - limit} more opportunities not shown")


_JSON_MATCH_CLUSTER_LIMIT = 50
_JSON_TEMPLATE_CLUSTER_LIMIT = 30
_JSON_OPPORTUNITY_LIMIT = 30
_JSON_SAMPLE_LIMIT = 10


def output_json(functions: list[FuncInfo], min_cluster: int,
                cov: Optional[CoverageReport] = None) -> None:
    """Machine-readable JSON output for downstream tooling.

    Every truncated list carries its `*_total` alongside, so a consumer can
    never read a 30-element sample as a population.
    """
    match_clusters = cluster_by_match_pct(functions, min_cluster=min_cluster)
    template_clusters = cluster_by_template(functions, min_cluster=min_cluster)
    opportunities = find_opportunities(functions, min_cluster=min_cluster)

    result = {
        "summary": {
            # NB: "non complete" here means "survived every filter in
            # load_report()", NOT "every function below 100%". The real
            # denominator is in `_coverage` below — read that, not this.
            "total_examined": len(functions),
            "total_non_complete": len(functions),   # back-compat alias
            "match_clusters": len(match_clusters),
            "match_clusters_emitted": min(len(match_clusters), _JSON_MATCH_CLUSTER_LIMIT),
            "template_clusters": len(template_clusters),
            "template_clusters_emitted": min(len(template_clusters),
                                             _JSON_TEMPLATE_CLUSTER_LIMIT),
            "opportunities": len(opportunities),
            "opportunities_emitted": min(len(opportunities), _JSON_OPPORTUNITY_LIMIT),
            "pct_bin_width": _PCT_BIN_WIDTH,
            "pct_bin_note": (
                f"cluster keys are {_BIN_LABEL}; an 'exact match%' cluster is a "
                f"{_PCT_BIN_WIDTH:.1f}pp band"),
            "complete_threshold": _COMPLETE_THRESHOLD,
        },
        "match_clusters": [
            {
                "pct_bin": c.pct,
                "pct": c.pct,                       # back-compat alias
                "pct_bin_width": _PCT_BIN_WIDTH,
                "count": c.count,
                "unit_count": c.unit_count,
                "units": sorted(c.units),
                "sample_functions": [f.demangled
                                     for f in c.sorted_functions[:_JSON_SAMPLE_LIMIT]],
                "sample_functions_total": c.count,
            }
            for c in match_clusters[:_JSON_MATCH_CLUSTER_LIMIT]
        ],
        "template_clusters": [
            {
                "pattern": tc.pattern_name,
                "count": tc.count,
                "unit_count": tc.unit_count,
                "pct_distribution": tc.pct_distribution,
                "pct_distribution_is_binned": True,
                "median_pct": tc.median_pct,
            }
            for tc in template_clusters[:_JSON_TEMPLATE_CLUSTER_LIMIT]
        ],
        "opportunities": [
            {
                "pattern": o.pattern_name,
                "match_pct": o.match_pct,
                "match_pct_is_bin": True,
                "count": o.function_count,
                "unit_count": o.unit_count,
                "code_bytes": o.total_code_bytes,
                "median_pct": o.median_pct,
                "fixability": o.fixability,
                "impact_score": round(o.impact_score, 1),
                "reason": o.reason,
                "sample_functions": o.sample_functions,
                "sample_functions_total": o.sample_functions_total,
                "sample_units": o.sample_units,
                "sample_units_total": o.sample_units_total,
            }
            for o in opportunities[:_JSON_OPPORTUNITY_LIMIT]
        ],
    }
    if cov is not None:
        result["_coverage"] = cov.as_dict()
    json.dump(result, sys.stdout, indent=2)
    print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--report", default="build/373307D9/report.json",
        help="Path to report.json (default: build/373307D9/report.json)",
    )
    parser.add_argument(
        "--mode", default="combined",
        choices=["match-pct", "template", "combined", "opportunities"],
        help="Analysis mode (default: combined)",
    )
    parser.add_argument(
        "--min-cluster", type=int, default=3,
        help="Minimum cluster size to report (default: 3)",
    )
    parser.add_argument(
        "--min-pct", type=float, default=50,
        help="Minimum match%% to include (default: 50)",
    )
    parser.add_argument(
        "--max-pct", type=float, default=100,
        help="Maximum match%% to include (default: 100)",
    )
    parser.add_argument(
        "--pattern", type=str, default=None,
        help="Filter template clusters to this pattern name",
    )
    parser.add_argument(
        "--tolerance", type=float, default=0.0,
        help="Match%% tolerance for merging adjacent bins (default: 0.0 = exact)",
    )
    parser.add_argument(
        "--json", action="store_true",
        help="Output machine-readable JSON instead of human-readable text",
    )
    add_coverage_args(parser)
    args = parser.parse_args()

    report_path = Path(args.report)
    if not report_path.exists():
        print(f"Error: {report_path} not found. Run ninja first.", file=sys.stderr)
        sys.exit(1)

    cov = CoverageReport("header_cluster", args=args)
    cov.extra("report_path", str(report_path))
    # The percentage window is applied INSIDE load_report so its removals are
    # counted; it used to be a bare list comprehension out here, which is how
    # 32 rows left the population without a trace.
    functions = load_report(report_path, cov=cov,
                            min_pct=args.min_pct, max_pct=args.max_pct)

    # The old line here was `Loaded {n} non-complete functions ({min}–{max}%
    # range)` — a range label with no denominator, printed above a number that
    # was 2,241 out of 48,344.  State both, and point at the COVERAGE block.
    info = (f"Loaded {len(functions)} functions in the "
            f"{args.min_pct}–{args.max_pct}% window "
            f"out of {cov.as_dict()['universe']} rows in {report_path} "
            f"({cov.dropped_total} dropped — see the COVERAGE block on stderr "
            f"for the breakdown)")

    if args.json:
        print(info, file=sys.stderr)
        output_json(functions, args.min_cluster, cov=cov)
        return cov.emit()

    print(info)

    if args.mode in ("match-pct", "combined"):
        clusters = cluster_by_match_pct(
            functions, min_cluster=args.min_cluster,
            pct_tolerance=args.tolerance,
        )
        print_match_clusters(clusters)

    if args.mode in ("template", "combined"):
        clusters = cluster_by_template(
            functions, min_cluster=args.min_cluster,
            pattern_filter=args.pattern,
        )
        print_template_clusters(clusters)

    if args.mode in ("opportunities", "combined"):
        opps = find_opportunities(functions, min_cluster=args.min_cluster)
        print_opportunities(opps)

    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
