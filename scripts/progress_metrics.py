#!/usr/bin/env python3
"""Authorable-denominator progress metrics for DC3 decomp.

Reads ``build/373307D9/report.json`` (produced by ``ninja``) and computes:

  (a) XEX total bytes/functions matched % (the XDK-diluted headline).
  (b) Authorable bytes/functions matched % — excludes ``default/xdk/*`` and
      ``default/lib/binkxenon/*`` (the honest game-code headline).
  (c) Remaining authorable bytes (normalized < 100 %).
  (d) Complete-unit counts (authorable units where every function is
      normalized == 100 %).

The "authorable normalized %" is the canonical metric: it uses the
``match_percent_normalized`` per-function field (which does not forgive wrong
constants, offsets, or vtable-slot values — only register permutation, branch
layout, and benign relocation-addend diffs are normalized away).

``--markdown`` mode regenerates ``docs/PROGRESS_METRICS.md``.

Usage:
    python3 scripts/progress_metrics.py
    python3 scripts/progress_metrics.py --report path/to/report.json
    python3 scripts/progress_metrics.py --markdown
    python3 scripts/progress_metrics.py --markdown --out docs/PROGRESS_METRICS.md
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

# Shared exclusion-prefix definition.
from authorable import SDK_UNIT_PREFIXES, is_authorable  # type: ignore[import]

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_REPORT = REPO_ROOT / "build" / "373307D9" / "report.json"
DEFAULT_MD = REPO_ROOT / "docs" / "PROGRESS_METRICS.md"


# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------

@dataclass
class UnitStats:
    """Per-unit aggregated statistics."""
    name: str
    total_code: int = 0
    matched_code: int = 0   # raw (fuzzy==100) bytes
    total_fns: int = 0
    matched_fns_fuzzy: int = 0    # fuzzy_match_percent == 100
    matched_fns_norm: int = 0     # match_percent_normalized == 100
    remaining_fns: int = 0        # normalized < 100
    remaining_bytes: int = 0      # sum of sizes for normalized < 100 fns


@dataclass
class Metrics:
    """Aggregated metrics across a subset of units."""
    # Code bytes
    total_code: int = 0
    matched_code: int = 0          # raw (matched_code from report)

    # Function counts
    total_fns: int = 0
    matched_fns_fuzzy: int = 0    # fuzzy_match_percent == 100
    matched_fns_norm: int = 0     # match_percent_normalized == 100

    # Remaining work (normalized < 100)
    remaining_fns: int = 0
    remaining_bytes: int = 0

    # Units
    total_units: int = 0
    complete_units: int = 0       # all fns normalized == 100

    @property
    def matched_code_pct(self) -> float:
        if self.total_code == 0:
            return 0.0
        return 100.0 * self.matched_code / self.total_code

    @property
    def matched_fns_fuzzy_pct(self) -> float:
        if self.total_fns == 0:
            return 0.0
        return 100.0 * self.matched_fns_fuzzy / self.total_fns

    @property
    def matched_fns_norm_pct(self) -> float:
        if self.total_fns == 0:
            return 0.0
        return 100.0 * self.matched_fns_norm / self.total_fns

    @property
    def complete_units_pct(self) -> float:
        if self.total_units == 0:
            return 0.0
        return 100.0 * self.complete_units / self.total_units


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def parse_report(report_path: Path) -> tuple[Metrics, Metrics]:
    """Parse report.json; return (all_metrics, authorable_metrics)."""
    with open(report_path) as f:
        data = json.load(f)

    all_m = Metrics()
    auth_m = Metrics()

    for unit in data.get("units", []):
        unit_name = unit.get("name", "")
        fns = unit.get("functions", [])
        measures = unit.get("measures", {})

        unit_total_code = int(measures.get("total_code", 0))
        unit_matched_code = int(measures.get("matched_code", 0))

        # Per-unit function stats
        unit_fns_fuzzy_matched = 0
        unit_fns_norm_matched = 0
        unit_remaining_fns = 0
        unit_remaining_bytes = 0

        for fn in fns:
            fuzzy = fn.get("fuzzy_match_percent") or 0.0
            norm = fn.get("match_percent_normalized") or 0.0
            sz = int(fn.get("size", 0))

            if fuzzy >= 100.0:
                unit_fns_fuzzy_matched += 1
            if norm >= 100.0:
                unit_fns_norm_matched += 1
            else:
                unit_remaining_fns += 1
                unit_remaining_bytes += sz

        unit_complete = bool(fns) and unit_fns_norm_matched == len(fns)

        # Accumulate into all_m
        all_m.total_code += unit_total_code
        all_m.matched_code += unit_matched_code
        all_m.total_fns += len(fns)
        all_m.matched_fns_fuzzy += unit_fns_fuzzy_matched
        all_m.matched_fns_norm += unit_fns_norm_matched
        all_m.remaining_fns += unit_remaining_fns
        all_m.remaining_bytes += unit_remaining_bytes
        if fns:
            all_m.total_units += 1
            if unit_complete:
                all_m.complete_units += 1

        # Accumulate into auth_m (skip SDK units)
        if not is_authorable(unit_name):
            continue

        auth_m.total_code += unit_total_code
        auth_m.matched_code += unit_matched_code
        auth_m.total_fns += len(fns)
        auth_m.matched_fns_fuzzy += unit_fns_fuzzy_matched
        auth_m.matched_fns_norm += unit_fns_norm_matched
        auth_m.remaining_fns += unit_remaining_fns
        auth_m.remaining_bytes += unit_remaining_bytes
        if fns:
            auth_m.total_units += 1
            if unit_complete:
                auth_m.complete_units += 1

    return all_m, auth_m


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------

def fmt_bytes(n: int) -> str:
    if n >= 1_000_000:
        return f"{n / 1_000_000:.2f} MB ({n:,} bytes)"
    if n >= 1_000:
        return f"{n / 1_000:.1f} KB ({n:,} bytes)"
    return f"{n} bytes"


def print_metrics(all_m: Metrics, auth_m: Metrics) -> None:
    """Print a human-readable summary to stdout."""
    xdk_bytes = all_m.total_code - auth_m.total_code
    xdk_pct = 100.0 * xdk_bytes / all_m.total_code if all_m.total_code else 0.0

    print("=" * 60)
    print("DC3 Decomp — Progress Metrics")
    print("=" * 60)
    print()
    print("  Excluded prefixes (SDK/vendor, not authorable):")
    for p in SDK_UNIT_PREFIXES:
        print(f"    {p}")
    print(f"  SDK/vendor bytes excluded: {fmt_bytes(xdk_bytes)} ({xdk_pct:.1f}% of XEX)")
    print()
    print(f"  [XEX total — XDK-diluted headline]")
    print(f"    Matched code (raw bytes):  {all_m.matched_code_pct:.2f}%"
          f"  ({all_m.matched_code:,} / {all_m.total_code:,} bytes)")
    print(f"    Matched fns  (fuzzy==100): {all_m.matched_fns_fuzzy_pct:.2f}%"
          f"  ({all_m.matched_fns_fuzzy:,} / {all_m.total_fns:,})")
    print(f"    Matched fns  (norm==100):  {all_m.matched_fns_norm_pct:.2f}%"
          f"  ({all_m.matched_fns_norm:,} / {all_m.total_fns:,})")
    print(f"    Complete units:            {all_m.complete_units_pct:.2f}%"
          f"  ({all_m.complete_units} / {all_m.total_units})")
    print()
    print(f"  [Authorable — CANONICAL HEADLINE]")
    print(f"    Matched code (raw bytes):  {auth_m.matched_code_pct:.2f}%"
          f"  ({auth_m.matched_code:,} / {auth_m.total_code:,} bytes)")
    print(f"    Matched fns  (fuzzy==100): {auth_m.matched_fns_fuzzy_pct:.2f}%"
          f"  ({auth_m.matched_fns_fuzzy:,} / {auth_m.total_fns:,})")
    print(f" ** Matched fns  (norm==100):  {auth_m.matched_fns_norm_pct:.2f}%"
          f"  ({auth_m.matched_fns_norm:,} / {auth_m.total_fns:,})  <-- CANONICAL")
    print(f"    Complete units:            {auth_m.complete_units_pct:.2f}%"
          f"  ({auth_m.complete_units} / {auth_m.total_units})")
    print()
    print(f"  [Remaining authorable work  (normalized < 100 %)]")
    print(f"    Functions:  {auth_m.remaining_fns:,}")
    print(f"    Bytes:      {fmt_bytes(auth_m.remaining_bytes)}")
    print()


def generate_markdown(all_m: Metrics, auth_m: Metrics, report_path: Path) -> str:
    """Return the PROGRESS_METRICS.md content as a string."""
    xdk_bytes = all_m.total_code - auth_m.total_code
    xdk_pct = 100.0 * xdk_bytes / all_m.total_code if all_m.total_code else 0.0
    prefixes_md = "\n".join(f"- `{p}`" for p in SDK_UNIT_PREFIXES)

    lines = [
        "# DC3 Decomp — Progress Metrics",
        "",
        "> **Auto-generated** by `scripts/progress_metrics.py`.  Do not edit manually.",
        "> Re-generate with `python3 scripts/progress_metrics.py --markdown`.",
        "",
        "## Why there are four headline numbers",
        "",
        "The DC3 binary contains two disjoint code populations:",
        "",
        "1. **Authorable game code** — C++ source lives in `src/`; this is the code",
        "   the decomp project is actually writing.  (~6.35 MB)",
        "2. **Vendor / SDK code** — Microsoft Xbox Dev Kit (XDK), RAD Bink, etc.",
        "   No source exists in the repo; these units will never be authored.",
        f"   (~{xdk_bytes/1_000_000:.2f} MB, {xdk_pct:.1f} % of the binary)",
        "",
        "Excluded prefixes:",
        "",
        prefixes_md,
        "",
        "Any metric that counts vendor bytes in the denominator is permanently capped",
        f"near {100-xdk_pct:.0f} % — the project looks half-done when game code is",
        "actually three-quarters matched.  This document names the four numbers so",
        "they cannot be confused.",
        "",
        "## The four coexisting headlines",
        "",
        "| Metric | Value | Notes |",
        "|--------|-------|-------|",
        f"| **XDK-diluted fuzzy** | {all_m.matched_code_pct:.2f} % | `matched_code_percent` in report.json measures root node; counts vendor bytes in denominator |",
        f"| **Authorable fuzzy** | {auth_m.matched_code_pct:.2f} % | Raw matched-code bytes over authorable-only total; best apples-to-apples byte signal |",
        f"| **Authorable normalized %** ✅ | **{auth_m.matched_fns_norm_pct:.2f} %** | **CANONICAL.** Functions where `match_percent_normalized == 100` over authorable total. Forgives register permutation / benign reloc-addend, but NOT wrong constants, offsets, or vtable slots |",
        f"| Strict reloc (pending Lane C) | TBD | Would use name-only reloc mode; expected to differ only for benign addend diffs |",
        "",
        "## Relocation-mode caveat",
        "",
        "The `match_percent_normalized` field (and therefore this metric) is computed",
        "with `functionRelocDiffs = None`, which forgives relocation-target differences.",
        "In practice this means a `bl wrong_function` would still score 100 % if the",
        "wrong callee has the same relocation flags.  Lane C's strict-reloc recertification",
        "will quantify the genuine false-100 % exposure; preliminary analysis (doc 02)",
        "found only ~2 non-boilerplate functions dropping >10 % under strict mode, so the",
        "risk is bounded but currently unquantified.",
        "",
        "## Current numbers",
        "",
        f"*(report: `{report_path.name}`, {auth_m.total_fns:,} authorable functions)*",
        "",
        "### Authorable code (canonical)",
        "",
        f"| | Value |",
        f"|---|---|",
        f"| Total authorable code | {auth_m.total_code:,} bytes ({auth_m.total_code/1_000_000:.2f} MB) |",
        f"| Matched code (raw bytes) | {auth_m.matched_code:,} bytes → **{auth_m.matched_code_pct:.2f} %** |",
        f"| Matched fns (fuzzy == 100) | {auth_m.matched_fns_fuzzy:,} / {auth_m.total_fns:,} → {auth_m.matched_fns_fuzzy_pct:.2f} % |",
        f"| **Matched fns (normalized == 100)** | **{auth_m.matched_fns_norm:,} / {auth_m.total_fns:,} → {auth_m.matched_fns_norm_pct:.2f} %** |",
        f"| Complete units (all fns norm==100) | {auth_m.complete_units} / {auth_m.total_units} → {auth_m.complete_units_pct:.2f} % |",
        f"| Remaining fns (norm < 100) | {auth_m.remaining_fns:,} |",
        f"| Remaining bytes (norm < 100) | {auth_m.remaining_bytes:,} bytes ({auth_m.remaining_bytes/1_000_000:.2f} MB) |",
        "",
        "### Full XEX (XDK-diluted, for reference only)",
        "",
        f"| | Value |",
        f"|---|---|",
        f"| Total code | {all_m.total_code:,} bytes ({all_m.total_code/1_000_000:.2f} MB) |",
        f"| Matched code (raw bytes) | {all_m.matched_code:,} bytes → {all_m.matched_code_pct:.2f} % |",
        f"| Matched fns (fuzzy == 100) | {all_m.matched_fns_fuzzy:,} / {all_m.total_fns:,} → {all_m.matched_fns_fuzzy_pct:.2f} % |",
        f"| Matched fns (normalized == 100) | {all_m.matched_fns_norm:,} / {all_m.total_fns:,} → {all_m.matched_fns_norm_pct:.2f} % |",
        f"| Complete units | {all_m.complete_units} / {all_m.total_units} → {all_m.complete_units_pct:.2f} % |",
        "",
        "## How to re-compute",
        "",
        "```bash",
        "ninja build/373307D9/report.json     # refresh objdiff report",
        "python3 scripts/progress_metrics.py  # print to stdout",
        "python3 scripts/progress_metrics.py --markdown  # regenerate this file",
        "```",
        "",
        "Or via `measure_progress.sh`:",
        "",
        "```bash",
        "scripts/measure_progress.sh --authorable",
        "```",
        "",
    ]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Print authorable-denominator decomp progress metrics."
    )
    p.add_argument(
        "--report",
        type=Path,
        default=DEFAULT_REPORT,
        help=f"Path to report.json (default: {DEFAULT_REPORT})",
    )
    p.add_argument(
        "--markdown",
        action="store_true",
        help="Generate docs/PROGRESS_METRICS.md instead of printing to stdout",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_MD,
        help=f"Output path for --markdown (default: {DEFAULT_MD})",
    )
    return p.parse_args()


def main() -> int:
    args = parse_args()

    if not args.report.exists():
        print(f"Error: report.json not found: {args.report}", file=sys.stderr)
        print("Run 'ninja build/373307D9/report.json' first.", file=sys.stderr)
        return 1

    all_m, auth_m = parse_report(args.report)

    if args.markdown:
        md = generate_markdown(all_m, auth_m, args.report)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(md, encoding="utf-8")
        print(f"Generated: {args.out}")
        print_metrics(all_m, auth_m)
    else:
        print_metrics(all_m, auth_m)

    return 0


if __name__ == "__main__":
    sys.exit(main())
