#!/usr/bin/env python3
"""Unified function health report.

Combines multiple analysis signals into a single diagnostic:
- Match% and build status (objdiff)
- Mismatch breakdown (regswaps, encoding, scheduling, etc.)
- Theoretical ceiling (max achievable match%)
- Pattern suggestions (which permuter patterns to try)
- Fixability verdict (worth attempting vs AT_LIMIT)

Usage:
    # Single function
    python scripts/analysis/function_health.py --symbol "?Poll@LabelNumberTicker@@UAAXXZ"

    # By unit + match range (batch mode)
    python scripts/analysis/function_health.py --unit "system/rndobj/*" --min 90 --max 99.9

    # Top N most workable functions
    python scripts/analysis/function_health.py --top 20

    # JSON output
    python scripts/analysis/function_health.py --symbol "..." --json
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(PROJECT_DIR))


@dataclass
class MismatchCategory:
    """Counts for a mismatch category."""
    name: str
    count: int
    fixable: bool
    description: str


@dataclass
class PatternSuggestion:
    """Suggested permuter pattern."""
    pattern: str
    reason: str
    confidence: float  # 0.0 to 1.0


@dataclass
class HealthReport:
    """Full health report for a function."""
    symbol: str
    demangled: str
    unit: str
    source_path: str
    match_percent: float
    total_instructions: int

    # Mismatch breakdown
    categories: list[MismatchCategory] = field(default_factory=list)
    total_mismatches: int = 0
    fixable_mismatches: int = 0
    unfixable_mismatches: int = 0

    # Ceiling
    ceiling_percent: float = 100.0
    headroom: float = 0.0  # ceiling - current

    # Suggestions
    suggestions: list[PatternSuggestion] = field(default_factory=list)

    # Verdict
    verdict: str = ""  # "workable", "marginal", "at_limit", "error"
    verdict_reason: str = ""

    # Workability score (higher = more worth working on)
    workability_score: float = 0.0


# ---------------------------------------------------------------------------
# Analysis helpers
# ---------------------------------------------------------------------------


def _run_objdiff(symbol: str) -> dict | None:
    """Run objdiff and return parsed JSON data."""
    cli = PROJECT_DIR / "bin" / "objdiff-cli"
    if not cli.exists():
        return None
    try:
        result = subprocess.run(
            [str(cli), "diff", "--symbol", symbol,
             "--format", "json", "--include-instructions"],
            capture_output=True, text=True, timeout=60,
            cwd=str(PROJECT_DIR),
        )
        if result.returncode != 0:
            return None
        return json.loads(result.stdout)
    except Exception:
        return None


def _classify_mismatches(instructions: list[dict]) -> tuple[list[MismatchCategory], int]:
    """Classify instruction mismatches into categories."""
    counts = {
        "regswap": 0,
        "merged": 0,
        "relocation": 0,
        "scheduling": 0,
        "save_restore": 0,
        "encoding": 0,
        "fma": 0,
        "insert_delete": 0,
        "other": 0,
    }
    total = len(instructions)

    for instr in instructions:
        match_type = instr.get("match_type", "")

        if match_type in ("full_match", "none"):
            continue

        if match_type == "diff_arg":
            # Check if it's a register swap
            t_args = instr.get("target", {}).get("typed_args", [])
            b_args = instr.get("base", {}).get("typed_args", [])
            is_regswap = False
            for ta, ba in zip(t_args, b_args):
                if ta.get("type") == "Register" and ba.get("type") == "Register":
                    if ta.get("value") != ba.get("value"):
                        is_regswap = True
                        break

            if is_regswap:
                counts["regswap"] += 1
            else:
                # Check for relocation/merged symbol diffs
                t_text = instr.get("target", {}).get("text", "")
                b_text = instr.get("base", {}).get("text", "")
                if "merged_" in t_text or "merged_" in b_text:
                    counts["merged"] += 1
                else:
                    counts["relocation"] += 1

        elif match_type in ("insert", "delete"):
            text = instr.get("target", {}).get("text", "") or instr.get("base", {}).get("text", "")
            opcode = instr.get("target", {}).get("opcode", "") or instr.get("base", {}).get("opcode", "")
            if "merged_" in text:
                counts["merged"] += 1
            elif opcode and ("savegprlr" in opcode or "restgprlr" in opcode
                            or "savefpr" in opcode or "restfpr" in opcode
                            or "__save" in text or "__rest" in text):
                counts["save_restore"] += 1
            else:
                counts["insert_delete"] += 1

        elif match_type == "replace":
            t_opcode = instr.get("target", {}).get("opcode", "")
            b_opcode = instr.get("base", {}).get("opcode", "")
            if t_opcode == b_opcode:
                counts["scheduling"] += 1
            else:
                # Check for fixable encoding patterns
                pair = tuple(sorted([t_opcode, b_opcode]))
                if pair in (("extrwi", "rlwinm"), ("rlwinm", "extrwi")):
                    counts["encoding"] += 1
                elif ("fmadds" in pair or "fmsubs" in pair or "fnmsubs" in pair):
                    counts["fma"] += 1
                else:
                    counts["other"] += 1

    categories = []
    descs = {
        "regswap": ("Register swap", False, "Callee-saved register allocation difference"),
        "merged": ("Merged symbol", False, "ICF-merged call target difference"),
        "relocation": ("Relocation", False, "Address/symbol relocation difference"),
        "scheduling": ("Scheduling", False, "Instruction reorder (same opcodes)"),
        "save_restore": ("Save/Restore", False, "Prologue/epilogue stub difference"),
        "encoding": ("Encoding", True, "Fixable opcode encoding (extrwi/rlwinm)"),
        "fma": ("FMA", True, "FMA instruction form difference"),
        "insert_delete": ("Insert/Delete", False, "Extra/missing instructions"),
        "other": ("Other", False, "Unclassified opcode difference"),
    }

    for key, count in counts.items():
        if count > 0:
            name, fixable, desc = descs[key]
            categories.append(MismatchCategory(
                name=name, count=count, fixable=fixable, description=desc,
            ))

    return categories, total


def _suggest_patterns(categories: list[MismatchCategory], match_pct: float) -> list[PatternSuggestion]:
    """Suggest permuter patterns based on mismatch categories."""
    suggestions = []

    cat_names = {c.name: c.count for c in categories}

    # Register swap → declaration reorder
    if cat_names.get("Register swap", 0) > 0:
        suggestions.append(PatternSuggestion(
            pattern="declaration_reorder",
            reason=f"{cat_names['Register swap']} register swap instructions — "
                   f"try reordering variable declarations",
            confidence=0.3 if cat_names.get("Register swap", 0) <= 4 else 0.1,
        ))

    # Encoding → variable extraction / bool materialize
    if cat_names.get("Encoding", 0) > 0:
        suggestions.append(PatternSuggestion(
            pattern="variable_extraction",
            reason=f"{cat_names['Encoding']} fixable encoding mismatches — "
                   f"try extracting subexpressions",
            confidence=0.5,
        ))
        suggestions.append(PatternSuggestion(
            pattern="bool_materialize",
            reason="Encoding mismatches may involve boolean materialization",
            confidence=0.4,
        ))

    # FMA → fma_reorder
    if cat_names.get("FMA", 0) > 0:
        suggestions.append(PatternSuggestion(
            pattern="fma_reorder",
            reason=f"{cat_names['FMA']} FMA instruction mismatches",
            confidence=0.6,
        ))

    # Scheduling → statement_reorder / tail_call_reorder
    if cat_names.get("Scheduling", 0) > 0:
        suggestions.append(PatternSuggestion(
            pattern="statement_reorder",
            reason=f"{cat_names['Scheduling']} scheduling mismatches — "
                   f"may respond to statement reordering",
            confidence=0.2,
        ))
        suggestions.append(PatternSuggestion(
            pattern="tail_call_reorder",
            reason="Scheduling may involve tail call ordering",
            confidence=0.2,
        ))

    # Insert/Delete → branch_polarity / early_return_merge
    if cat_names.get("Insert/Delete", 0) > 0:
        suggestions.append(PatternSuggestion(
            pattern="branch_polarity",
            reason=f"{cat_names['Insert/Delete']} insert/delete mismatches — "
                   f"branch direction may differ",
            confidence=0.3,
        ))
        suggestions.append(PatternSuggestion(
            pattern="early_return_merge",
            reason="Insert/delete clusters may indicate guard merging opportunity",
            confidence=0.2,
        ))

    # High match% → smaller targeted patterns
    if match_pct >= 95.0:
        suggestions.append(PatternSuggestion(
            pattern="signed_unsigned",
            reason="Near-perfect match — signed/unsigned cast may close the gap",
            confidence=0.4,
        ))
        suggestions.append(PatternSuggestion(
            pattern="comparison_equivalence",
            reason="Near-perfect match — comparison form may differ",
            confidence=0.3,
        ))

    # Sort by confidence
    suggestions.sort(key=lambda s: -s.confidence)
    return suggestions


def _compute_verdict(
    match_pct: float,
    categories: list[MismatchCategory],
    ceiling: float,
    headroom: float,
) -> tuple[str, str, float]:
    """Compute workability verdict and score."""
    fixable = sum(c.count for c in categories if c.fixable)
    unfixable = sum(c.count for c in categories if not c.fixable)

    if match_pct >= 100.0:
        return "complete", "Already at 100%", 0.0

    if headroom <= 0.5 and ceiling < 100.0:
        return "at_limit", f"Ceiling {ceiling:.1f}% — no room to improve", 0.0

    if fixable == 0 and unfixable > 0:
        return "at_limit", f"Only unfixable mismatches remain ({unfixable} instructions)", 0.0

    # Compute workability score
    # Higher = more worth working on
    score = 0.0

    # Headroom bonus (more room to improve = better)
    score += min(headroom, 10.0) * 2.0

    # Fixable mismatch bonus
    score += min(fixable, 10) * 3.0

    # Penalty for large unfixable portion
    total_mm = fixable + unfixable
    if total_mm > 0:
        fixable_ratio = fixable / total_mm
        score *= (0.3 + 0.7 * fixable_ratio)

    # High match% bonus (closer to 100% = more impactful)
    if match_pct >= 95.0:
        score *= 1.5
    elif match_pct >= 90.0:
        score *= 1.2

    if score > 10.0:
        return "workable", f"{fixable} fixable mismatches, ceiling {ceiling:.1f}%", score
    elif score > 3.0:
        return "marginal", f"Some potential ({fixable} fixable), but limited headroom", score
    else:
        return "at_limit", f"Low fixability ({fixable}/{total_mm})", score


def analyze_function(symbol: str) -> HealthReport:
    """Generate a full health report for a function."""
    # Look up function info from DB
    demangled = ""
    unit = ""
    source_path = ""
    try:
        from scripts.orchestrator.db_helpers import resolve_symbol_info
        info = resolve_symbol_info(symbol)
        demangled = info.get("demangled", "")
        unit = info.get("unit", "")
        source_path = info.get("source_path", "")
    except Exception:
        pass

    report = HealthReport(
        symbol=symbol,
        demangled=demangled,
        unit=unit,
        source_path=source_path,
        match_percent=0.0,
        total_instructions=0,
    )

    # Run objdiff
    data = _run_objdiff(symbol)
    if data is None:
        report.verdict = "error"
        report.verdict_reason = "objdiff failed"
        return report

    report.match_percent = data.get("normalized_match_percent", 0.0)
    instructions = data.get("instructions", [])
    report.total_instructions = len(instructions)

    if report.match_percent >= 100.0:
        report.verdict = "complete"
        report.verdict_reason = "Already at 100%"
        return report

    # Classify mismatches
    categories, total = _classify_mismatches(instructions)
    report.categories = categories
    report.total_mismatches = sum(c.count for c in categories)
    report.fixable_mismatches = sum(c.count for c in categories if c.fixable)
    report.unfixable_mismatches = sum(c.count for c in categories if not c.fixable)

    # Compute ceiling
    if total > 0:
        unfixable_pct = (report.unfixable_mismatches / total) * 100.0
        report.ceiling_percent = min(100.0, 100.0 - unfixable_pct)
    report.headroom = report.ceiling_percent - report.match_percent

    # Suggest patterns
    report.suggestions = _suggest_patterns(categories, report.match_percent)

    # Compute verdict
    report.verdict, report.verdict_reason, report.workability_score = _compute_verdict(
        report.match_percent, categories, report.ceiling_percent, report.headroom,
    )

    return report


# ---------------------------------------------------------------------------
# Batch mode
# ---------------------------------------------------------------------------


def _query_functions(unit_pattern: str | None, min_pct: float, max_pct: float,
                     limit: int) -> list[dict]:
    """Query functions from the orchestrator DB."""
    import sqlite3
    db_path = PROJECT_DIR / "build" / "373307D9" / "decomp.db"
    if not db_path.exists():
        return []

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    cursor = conn.cursor()

    query = """
        SELECT symbol, demangled, unit, source_path, match_percent
        FROM functions
        WHERE match_percent >= ? AND match_percent < ?
    """
    params: list = [min_pct, max_pct]

    if unit_pattern:
        query += " AND unit GLOB ?"
        params.append(unit_pattern)

    query += " ORDER BY match_percent DESC LIMIT ?"
    params.append(limit)

    try:
        cursor.execute(query, params)
        return [dict(row) for row in cursor.fetchall()]
    except Exception:
        return []
    finally:
        conn.close()


# ---------------------------------------------------------------------------
# Display
# ---------------------------------------------------------------------------


def _format_report(report: HealthReport) -> str:
    """Format a health report for human reading."""
    lines = []
    lines.append(f"Function Health Report")
    lines.append(f"{'=' * 60}")
    lines.append(f"  Symbol:     {report.symbol}")
    if report.demangled:
        lines.append(f"  Demangled:  {report.demangled}")
    if report.unit:
        lines.append(f"  Unit:       {report.unit}")
    if report.source_path:
        lines.append(f"  Source:     {report.source_path}")
    lines.append(f"  Match:      {report.match_percent:.2f}%")
    lines.append(f"  Ceiling:    {report.ceiling_percent:.1f}%")
    lines.append(f"  Headroom:   {report.headroom:.1f}%")
    lines.append(f"  Verdict:    {report.verdict} — {report.verdict_reason}")
    if report.workability_score > 0:
        lines.append(f"  Score:      {report.workability_score:.1f}")
    lines.append(f"  Instructions: {report.total_instructions}")
    lines.append("")

    if report.categories:
        lines.append(f"Mismatch Breakdown:")
        for cat in sorted(report.categories, key=lambda c: -c.count):
            fix = "FIXABLE" if cat.fixable else "unfixable"
            lines.append(f"  {cat.count:4d}  {cat.name:15s}  [{fix}]  {cat.description}")
        lines.append(
            f"  Total: {report.total_mismatches} "
            f"({report.fixable_mismatches} fixable, "
            f"{report.unfixable_mismatches} unfixable)"
        )
        lines.append("")

    if report.suggestions:
        lines.append(f"Pattern Suggestions:")
        for s in report.suggestions[:5]:
            lines.append(f"  {s.pattern:25s}  (conf={s.confidence:.1f})  {s.reason}")
        lines.append("")

    return "\n".join(lines)


def _format_batch_table(reports: list[HealthReport]) -> str:
    """Format batch results as a compact table."""
    lines = []
    lines.append(f"{'Match%':>7} {'Ceiling':>8} {'Room':>5} {'Fix':>4} {'Unfix':>5} "
                 f"{'Score':>6} {'Verdict':10} Symbol")
    lines.append(f"{'─' * 7} {'─' * 8} {'─' * 5} {'─' * 4} {'─' * 5} "
                 f"{'─' * 6} {'─' * 10} {'─' * 50}")
    for r in reports:
        symbol_short = r.demangled[:50] if r.demangled else r.symbol[:50]
        lines.append(
            f"{r.match_percent:6.1f}% {r.ceiling_percent:7.1f}% "
            f"{r.headroom:4.1f}% {r.fixable_mismatches:4d} {r.unfixable_mismatches:5d} "
            f"{r.workability_score:6.1f} {r.verdict:10s} {symbol_short}"
        )
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--symbol", help="Single function symbol")
    parser.add_argument("--unit", help="Unit glob pattern for batch mode")
    parser.add_argument("--min", type=float, default=90.0, help="Min match%% (default: 90)")
    parser.add_argument("--max", type=float, default=99.99, help="Max match%% (default: 99.99)")
    parser.add_argument("--limit", type=int, default=50, help="Max functions to analyze (default: 50)")
    parser.add_argument("--top", type=int, help="Show top N most workable functions")
    parser.add_argument("--json", action="store_true", help="JSON output")
    args = parser.parse_args()

    if args.symbol:
        # Single function mode
        report = analyze_function(args.symbol)
        if args.json:
            print(json.dumps(asdict(report), indent=2))
        else:
            print(_format_report(report))
    else:
        # Batch mode
        functions = _query_functions(
            args.unit, args.min, args.max, args.limit,
        )
        if not functions:
            print("No functions found matching criteria.", file=sys.stderr)
            sys.exit(1)

        print(f"Analyzing {len(functions)} functions...", file=sys.stderr)

        reports = []
        for i, func in enumerate(functions):
            symbol = func["symbol"]
            print(
                f"  [{i + 1}/{len(functions)}] {symbol[:60]}...",
                file=sys.stderr, end="\r",
            )
            report = analyze_function(symbol)
            # Fill in from DB if objdiff didn't provide
            if not report.demangled:
                report.demangled = func.get("demangled", "")
            if not report.unit:
                report.unit = func.get("unit", "")
            if not report.source_path:
                report.source_path = func.get("source_path", "")
            reports.append(report)

        print(f"\nDone analyzing {len(reports)} functions.", file=sys.stderr)

        # Sort by workability score
        reports.sort(key=lambda r: -r.workability_score)

        if args.top:
            reports = reports[:args.top]

        if args.json:
            print(json.dumps([asdict(r) for r in reports], indent=2))
        else:
            # Summary stats
            workable = [r for r in reports if r.verdict == "workable"]
            marginal = [r for r in reports if r.verdict == "marginal"]
            at_limit = [r for r in reports if r.verdict == "at_limit"]
            complete = [r for r in reports if r.verdict == "complete"]
            errors = [r for r in reports if r.verdict == "error"]

            print(f"\nFunction Health Summary")
            print(f"{'=' * 60}")
            print(f"  Analyzed:   {len(reports)}")
            print(f"  Workable:   {len(workable)}")
            print(f"  Marginal:   {len(marginal)}")
            print(f"  AT_LIMIT:   {len(at_limit)}")
            print(f"  Complete:   {len(complete)}")
            if errors:
                print(f"  Errors:     {len(errors)}")
            print()

            if workable or marginal:
                show = (workable + marginal)[:args.top or 30]
                print(_format_batch_table(show))


if __name__ == "__main__":
    main()
