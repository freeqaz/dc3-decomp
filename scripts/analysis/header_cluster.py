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
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional


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
        c = Counter(round(f.pct, 1) for f in self.functions)
        return dict(sorted(c.items()))

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
    sample_functions: list[str]  # demangled names
    sample_units: list[str]

    @property
    def impact_score(self) -> float:
        """Higher = more impactful to fix. Combines count, spread, and proximity to 100%."""
        fix_weight = {"likely_fixable": 1.0, "maybe_fixable": 0.5, "likely_unfixable": 0.1}
        w = fix_weight.get(self.fixability, 0.3)
        # Weight by: function count × unit spread × closeness to 100%
        closeness = max(0, (self.median_pct - 50) / 50)  # 0..1 scale
        return self.function_count * (1 + self.unit_count * 0.2) * closeness * w


def load_report(report_path: str | Path) -> list[FuncInfo]:
    """Load all non-100% functions from report.json."""
    with open(report_path) as f:
        report = json.load(f)

    functions = []
    for unit in report.get("units", []):
        unit_name = unit["name"]
        for func in unit.get("functions", []):
            pct = func.get("fuzzy_match_percent", 0)
            if pct >= 99.95 or pct <= 0:
                continue
            demangled = func.get("metadata", {}).get("demangled_name", func["name"])
            functions.append(FuncInfo(
                name=func["name"],
                demangled=demangled,
                unit=unit_name,
                pct=pct,
                size=int(func.get("size", 0)),
                address=func.get("address", ""),
            ))
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
    for pct, funcs in by_pct.items():
        units = set(f.unit for f in funcs)
        if len(funcs) >= min_cluster and len(units) >= min_units:
            clusters.append(MatchCluster(pct=pct, functions=funcs))

    clusters.sort(key=lambda c: (-c.count, -c.pct))
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

    result = sorted(clusters.values(), key=lambda c: -c.count)
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

            subset = [f for f in tc.functions if abs(round(f.pct, 1) - pct) < 0.1]
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
            ))

    # 2. Large match% clusters without a known template pattern
    known_in_templates = set()
    for tc in template_clusters:
        for f in tc.functions:
            known_in_templates.add(f.name)

    for mc in match_clusters:
        # Filter to functions NOT already in a template cluster
        orphans = [f for f in mc.functions if f.name not in known_in_templates]
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
            reason=f"{len(orphans)} functions at exactly {mc.pct}% across {len(units)} TUs — unknown shared cause",
            sample_functions=[f.demangled[:80] for f in orphans[:5]],
            sample_units=sorted(units)[:5],
        ))

    # Sort by impact score descending
    opportunities.sort(key=lambda o: -o.impact_score)

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
    print("Functions sharing the same match% across multiple TUs")
    print("=" * 75)

    if not clusters:
        print("  No significant match% clusters found.")
        return

    for i, c in enumerate(clusters[:limit]):
        print(f"\n  {c.pct:5.1f}%: {c.count} functions across {c.unit_count} TUs "
              f"({c.total_size:,} bytes)")
        # Show unit distribution
        unit_counts = Counter(f.unit for f in c.functions)
        top_units = unit_counts.most_common(5)
        for unit, cnt in top_units:
            # Extract just the filename
            short = Path(unit).stem if "/" in unit else unit
            print(f"    {cnt:3d} in {short}")
        if len(unit_counts) > 5:
            print(f"    ... and {len(unit_counts) - 5} more TUs")
        # Show sample demangled names
        seen = set()
        for f in c.functions[:4]:
            if f.demangled not in seen:
                seen.add(f.demangled)
                print(f"    → {f.demangled[:72]}")


def print_template_clusters(clusters: list[TemplateCluster], limit: int = 20) -> None:
    print("\n" + "=" * 75)
    print("TEMPLATE / CLASS PATTERN CLUSTERS")
    print("Functions grouped by shared template or class name")
    print("=" * 75)

    if not clusters:
        print("  No significant template clusters found.")
        return

    for tc in clusters[:limit]:
        pct_dist = tc.pct_distribution
        pct_range = f"{min(pct_dist.keys()):.1f}–{max(pct_dist.keys()):.1f}%"
        print(f"\n  {tc.pattern_name}: {tc.count} functions across "
              f"{tc.unit_count} TUs ({pct_range})")

        # Show percentage distribution
        top_pcts = sorted(pct_dist.items(), key=lambda x: -x[1])[:5]
        dist_str = ", ".join(f"{p:.1f}%×{n}" for p, n in top_pcts)
        print(f"    Distribution: {dist_str}")

        if tc.pattern_name in _UNFIXABLE_PATTERNS:
            print(f"    ⚠ Known: {_UNFIXABLE_PATTERNS[tc.pattern_name]}")

        # Show sample functions
        for f in tc.functions[:3]:
            print(f"    → {f.demangled[:72]}  [{f.pct:.1f}%]")


def print_opportunities(opportunities: list[Opportunity], limit: int = 20) -> None:
    print("\n" + "=" * 75)
    print("ACTIONABLE OPPORTUNITIES (ranked by impact)")
    print("=" * 75)

    if not opportunities:
        print("  No actionable opportunities found.")
        return

    for i, o in enumerate(opportunities[:limit], 1):
        fix_icon = {"likely_fixable": "+", "maybe_fixable": "?", "likely_unfixable": "-"}
        icon = fix_icon.get(o.fixability, "?")
        pct_str = f"{o.match_pct:.1f}%" if o.match_pct else "mixed"
        print(f"\n  [{icon}] #{i}: {o.pattern_name} @ {pct_str}")
        print(f"      {o.function_count} functions across {o.unit_count} TUs "
              f"({o.total_code_bytes:,} bytes)")
        print(f"      Impact score: {o.impact_score:.1f}")
        print(f"      {o.reason}")
        for fn in o.sample_functions[:3]:
            print(f"      → {fn}")
        if o.sample_units:
            print(f"      TUs: {', '.join(Path(u).stem for u in o.sample_units[:4])}")


def output_json(functions: list[FuncInfo], min_cluster: int) -> None:
    """Machine-readable JSON output for downstream tooling."""
    match_clusters = cluster_by_match_pct(functions, min_cluster=min_cluster)
    template_clusters = cluster_by_template(functions, min_cluster=min_cluster)
    opportunities = find_opportunities(functions, min_cluster=min_cluster)

    result = {
        "summary": {
            "total_non_complete": len(functions),
            "match_clusters": len(match_clusters),
            "template_clusters": len(template_clusters),
            "opportunities": len(opportunities),
        },
        "match_clusters": [
            {
                "pct": c.pct,
                "count": c.count,
                "unit_count": c.unit_count,
                "units": sorted(c.units),
                "sample_functions": [f.demangled for f in c.functions[:10]],
            }
            for c in match_clusters[:50]
        ],
        "template_clusters": [
            {
                "pattern": tc.pattern_name,
                "count": tc.count,
                "unit_count": tc.unit_count,
                "pct_distribution": tc.pct_distribution,
                "median_pct": tc.median_pct,
            }
            for tc in template_clusters[:30]
        ],
        "opportunities": [
            {
                "pattern": o.pattern_name,
                "match_pct": o.match_pct,
                "count": o.function_count,
                "unit_count": o.unit_count,
                "code_bytes": o.total_code_bytes,
                "median_pct": o.median_pct,
                "fixability": o.fixability,
                "impact_score": round(o.impact_score, 1),
                "reason": o.reason,
                "sample_functions": o.sample_functions,
                "sample_units": o.sample_units,
            }
            for o in opportunities[:30]
        ],
    }
    json.dump(result, sys.stdout, indent=2)
    print()


def main() -> None:
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
    args = parser.parse_args()

    report_path = Path(args.report)
    if not report_path.exists():
        print(f"Error: {report_path} not found. Run ninja first.", file=sys.stderr)
        sys.exit(1)

    functions = load_report(report_path)
    # Apply percentage filter
    functions = [f for f in functions if args.min_pct <= f.pct < args.max_pct]

    info = (f"Loaded {len(functions)} non-complete functions "
            f"({args.min_pct}–{args.max_pct}% range)")

    if args.json:
        print(info, file=sys.stderr)
        output_json(functions, args.min_cluster)
        return

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


if __name__ == "__main__":
    main()
