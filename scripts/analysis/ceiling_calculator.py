#!/usr/bin/env python3
"""Automated at-limit ceiling calculator.

For each AT_LIMIT function, categorizes all remaining instruction mismatches
and computes a theoretical maximum match%. This enables tracking "effective
completion" vs raw match%.

Mismatch categories:
  REGSWAP     - diff_arg with only register differences (unfixable, but patcher helps)
  MERGED      - insert/delete involving merged_XXXX branch targets (unfixable)
  SCHEDULING  - replace where opcodes match but order differs (unfixable)
  SAVE_RESTORE- insert/delete of __savegprlr/__restgprlr stubs (unfixable)
  ENCODING    - fixable encoding patterns (extrwi/rlwinm, bool_mask, cmp_encoding)
  FMA         - fmadds vs fmuls+fadds (partially fixable)
  OTHER       - unclassified mismatches

Ceiling = 100% - (unfixable_instructions / total_instructions * 100)

Usage:
    python scripts/analysis/ceiling_calculator.py                    # All AT_LIMIT functions
    python scripts/analysis/ceiling_calculator.py --unit 'system/*'  # Filter by unit
    python scripts/analysis/ceiling_calculator.py --min 90 --max 99  # Filter by match%
    python scripts/analysis/ceiling_calculator.py --json             # JSON output
    python scripts/analysis/ceiling_calculator.py --find-fixable     # Show functions with fixable issues
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
OBJDIFF_CLI = PROJECT_DIR / "bin" / "objdiff-cli"

sys.path.insert(0, str(PROJECT_DIR))

# Import pattern detection from batch_pattern_scan
from scripts.analysis.batch_pattern_scan import detect_patterns


@dataclass
class MismatchBreakdown:
    """Categorized mismatch counts for a function."""
    regswap: int = 0          # diff_arg, register-only diffs
    merged: int = 0           # insert/delete involving merged symbols
    relocation: int = 0       # diff_arg with symbol/address differences
    immediate: int = 0        # diff_arg with signed/unsigned/branch immediate diffs
    scheduling: int = 0       # replace with same opcode (reordering)
    save_restore: int = 0     # __savegprlr / __restgprlr stubs
    insert_delete: int = 0    # insert/delete not matching other patterns
    encoding_fixable: int = 0 # extrwi/rlwinm, bool_mask, cmp_encoding
    fma_fixable: int = 0      # fmadds vs fmuls+fadds
    other: int = 0            # truly unclassified (replace with diff opcode)

    @property
    def total_unfixable(self) -> int:
        return (self.regswap + self.merged + self.relocation + self.immediate +
                self.scheduling + self.save_restore + self.insert_delete)

    @property
    def total_fixable(self) -> int:
        return self.encoding_fixable + self.fma_fixable

    @property
    def total_mismatches(self) -> int:
        return (self.regswap + self.merged + self.relocation + self.immediate +
                self.scheduling + self.save_restore + self.insert_delete +
                self.encoding_fixable + self.fma_fixable + self.other)

    def to_dict(self) -> dict:
        return {
            "regswap": self.regswap,
            "merged": self.merged,
            "relocation": self.relocation,
            "immediate": self.immediate,
            "scheduling": self.scheduling,
            "save_restore": self.save_restore,
            "insert_delete": self.insert_delete,
            "encoding_fixable": self.encoding_fixable,
            "fma_fixable": self.fma_fixable,
            "other": self.other,
            "total_unfixable": self.total_unfixable,
            "total_fixable": self.total_fixable,
            "total": self.total_mismatches,
        }


@dataclass
class FunctionCeiling:
    """Ceiling analysis for a single function."""
    symbol: str
    demangled: str
    unit: str
    current_percent: float
    total_instructions: int
    matched_instructions: int
    breakdown: MismatchBreakdown
    ceiling_percent: float       # theoretical max (100% - unfixable%)
    fixable_potential: float     # how much % could improve with source fixes
    error: Optional[str] = None


def run_objdiff_json(symbol: str) -> Optional[dict]:
    """Run objdiff-cli and return JSON instruction diff."""
    cmd = [
        str(OBJDIFF_CLI), "diff",
        "-p", str(PROJECT_DIR),
        symbol,
        "--include-instructions",
        "-f", "json",
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            return None
        return json.loads(result.stdout)
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError):
        return None


def classify_instruction(instr: dict) -> str:
    """Classify a single mismatched instruction into a category.

    Returns one of: 'regswap', 'merged', 'relocation', 'immediate',
    'scheduling', 'save_restore', 'insert_delete', 'other'.
    Encoding/FMA patterns are detected separately via batch_pattern_scan.
    """
    match_type = instr.get("match_type", "")

    if match_type in ("matched", "equal"):
        return "matched"

    target = instr.get("target", {})
    base = instr.get("base", {})
    t_args_str = (target.get("args") or "").strip()
    b_args_str = (base.get("args") or "").strip()

    # Check for merged symbols anywhere
    if "merged_" in t_args_str or "merged_" in b_args_str:
        return "merged"

    # diff_arg: classify by what kind of arguments differ
    if match_type == "diff_arg":
        t_args = target.get("typed_args", [])
        b_args = base.get("typed_args", [])

        # If opcodes differ, it's not a pure regswap
        if target.get("opcode") != base.get("opcode"):
            return "other"

        # Categorize all differences
        has_reg_diff = False
        has_sym_diff = False
        has_imm_diff = False  # Signed, Unsigned, BranchDest
        has_other_diff = False
        has_any_diff = False

        for i in range(min(len(t_args), len(b_args))):
            ta, ba = t_args[i], b_args[i]
            if ta.get("value") != ba.get("value"):
                has_any_diff = True
                t_type = ta.get("type", "")
                b_type = ba.get("type", "")
                if t_type == "Register" and b_type == "Register":
                    has_reg_diff = True
                elif t_type == "Symbol" or b_type == "Symbol":
                    has_sym_diff = True
                elif t_type in ("Signed", "Unsigned", "BranchDest", "Other"):
                    has_imm_diff = True
                else:
                    has_other_diff = True

        if not has_any_diff:
            # typed_args identical but objdiff flagged as diff_arg
            # This is relocation noise (HA16/LO16 address splits)
            return "relocation"

        if has_reg_diff and not has_sym_diff and not has_imm_diff and not has_other_diff:
            return "regswap"
        if has_sym_diff:
            return "relocation"
        if has_imm_diff and not has_reg_diff:
            # Pure immediate diff: stack offsets, branch targets, constants
            return "immediate"
        if has_imm_diff and has_reg_diff:
            # Register + immediate diff: scheduling with different base register
            return "immediate"
        return "other"

    # insert/delete: check for save/restore stubs
    if match_type in ("insert", "delete"):
        side = base if match_type == "insert" else target
        args = (side.get("args") or "").strip()

        if any(stub in args for stub in ("__savegprlr", "__restgprlr",
                                          "__savefpr", "__restfpr",
                                          "__savevmx", "__restvmx")):
            return "save_restore"

        return "insert_delete"

    # replace: check if same opcode (scheduling/reordering)
    if match_type == "replace":
        t_op = (target.get("opcode") or "").strip()
        b_op = (base.get("opcode") or "").strip()

        # Same opcode with different args → likely scheduling reorder
        if t_op == b_op:
            return "scheduling"

        return "other"

    return "other"


def analyze_function(symbol: str, unit: str, demangled: str = "",
                     current_pct: float = 0.0) -> FunctionCeiling:
    """Run full ceiling analysis on a single function."""
    result = FunctionCeiling(
        symbol=symbol, demangled=demangled or symbol, unit=unit,
        current_percent=current_pct, total_instructions=0,
        matched_instructions=0, breakdown=MismatchBreakdown(),
        ceiling_percent=0.0, fixable_potential=0.0,
    )

    data = run_objdiff_json(symbol)
    if data is None:
        result.error = "objdiff failed"
        return result

    instructions = data.get("instructions", [])
    result.total_instructions = len(instructions)
    result.current_percent = data.get("fuzzy_match_percent", current_pct)

    if not instructions:
        result.error = "no instructions"
        return result

    # Phase 1: Detect fixable patterns (encoding, FMA) using batch_pattern_scan
    fixable_patterns = detect_patterns(instructions)
    fixable_indices = set()
    fma_indices = set()
    encoding_indices = set()
    for pattern in fixable_patterns:
        for idx in pattern.indices:
            if pattern.pattern_type == "fma_mismatch":
                fma_indices.add(idx)
            else:
                encoding_indices.add(idx)
            fixable_indices.add(idx)

    # Phase 2: Classify every instruction
    matched = 0
    for instr in instructions:
        idx = instr.get("index", -1)
        mt = instr.get("match_type", "")

        if mt in ("matched", "equal"):
            matched += 1
            continue

        # Check if this instruction was claimed by a fixable pattern
        if idx in encoding_indices:
            result.breakdown.encoding_fixable += 1
            continue
        if idx in fma_indices:
            result.breakdown.fma_fixable += 1
            continue

        # Classify by instruction-level analysis
        category = classify_instruction(instr)
        if category == "matched":
            matched += 1
        elif category == "regswap":
            result.breakdown.regswap += 1
        elif category == "merged":
            result.breakdown.merged += 1
        elif category == "relocation":
            result.breakdown.relocation += 1
        elif category == "immediate":
            result.breakdown.immediate += 1
        elif category == "scheduling":
            result.breakdown.scheduling += 1
        elif category == "save_restore":
            result.breakdown.save_restore += 1
        elif category == "insert_delete":
            result.breakdown.insert_delete += 1
        else:
            result.breakdown.other += 1

    result.matched_instructions = matched

    # Compute ceiling: if we fixed all fixable issues, how high could we go?
    # Ceiling = (matched + fixable + regswap_patchable) / total * 100
    # Note: regswap is "unfixable from source" but IS patchable via obj_regswap_patcher
    total = result.total_instructions
    if total > 0:
        unfixable_frac = result.breakdown.total_unfixable / total
        fixable_frac = result.breakdown.total_fixable / total
        other_frac = result.breakdown.other / total

        # Conservative ceiling: assume "other" is unfixable
        result.ceiling_percent = 100.0 * (1.0 - (unfixable_frac + other_frac))
        # But the fixable_potential tells how much source fixes could help
        result.fixable_potential = fixable_frac * 100.0

        # Clamp
        result.ceiling_percent = max(result.current_percent, min(100.0, result.ceiling_percent))
    else:
        result.ceiling_percent = result.current_percent

    return result


def load_at_limit_functions(db_path: str, min_pct: float = 0.0,
                            max_pct: float = 100.0,
                            unit_filter: str = "") -> list[dict]:
    """Load AT_LIMIT functions from the orchestrator database."""
    import sqlite3
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    query = """
        SELECT symbol, demangled, unit, current_percent
        FROM functions
        WHERE verdict = 'AT_LIMIT'
          AND excluded = 0
          AND current_percent IS NOT NULL
          AND symbol NOT LIKE 'merged_%'
    """
    params = []

    if min_pct > 0:
        query += " AND current_percent >= ?"
        params.append(min_pct)
    if max_pct < 100:
        query += " AND current_percent <= ?"
        params.append(max_pct)
    if unit_filter:
        query += " AND unit LIKE ?"
        params.append(f"%{unit_filter}%")

    query += " ORDER BY current_percent DESC"

    rows = conn.execute(query, params).fetchall()
    conn.close()

    return [dict(r) for r in rows]


def load_all_verdicts(db_path: str) -> dict:
    """Load verdict distribution for aggregate stats."""
    import sqlite3
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    stats = {}
    # Total non-excluded
    row = conn.execute(
        "SELECT COUNT(*) as cnt FROM functions WHERE excluded = 0"
    ).fetchone()
    stats["total"] = row["cnt"]

    # Complete
    row = conn.execute(
        "SELECT COUNT(*) as cnt, COALESCE(SUM(size), 0) as bytes FROM functions "
        "WHERE verdict = 'COMPLETE' AND excluded = 0"
    ).fetchone()
    stats["complete"] = row["cnt"]
    stats["complete_bytes"] = row["bytes"]

    # AT_LIMIT
    row = conn.execute(
        "SELECT COUNT(*) as cnt, COALESCE(SUM(size), 0) as bytes, "
        "COALESCE(AVG(current_percent), 0) as avg_pct "
        "FROM functions WHERE verdict = 'AT_LIMIT' AND excluded = 0"
    ).fetchone()
    stats["at_limit"] = row["cnt"]
    stats["at_limit_bytes"] = row["bytes"]
    stats["at_limit_avg_pct"] = row["avg_pct"]

    # Remaining (not COMPLETE, not AT_LIMIT)
    row = conn.execute(
        "SELECT COUNT(*) as cnt FROM functions "
        "WHERE excluded = 0 AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"
    ).fetchone()
    stats["remaining"] = row["cnt"]

    conn.close()
    return stats


def print_summary(results: list[FunctionCeiling], db_stats: dict):
    """Print aggregate summary."""
    total_funcs = len(results)
    if total_funcs == 0:
        print("No functions analyzed.")
        return

    # Aggregate mismatch breakdown
    agg = MismatchBreakdown()
    total_instr = 0
    total_matched = 0
    ceilings = []

    for r in results:
        if r.error:
            continue
        agg.regswap += r.breakdown.regswap
        agg.merged += r.breakdown.merged
        agg.relocation += r.breakdown.relocation
        agg.immediate += r.breakdown.immediate
        agg.scheduling += r.breakdown.scheduling
        agg.save_restore += r.breakdown.save_restore
        agg.insert_delete += r.breakdown.insert_delete
        agg.encoding_fixable += r.breakdown.encoding_fixable
        agg.fma_fixable += r.breakdown.fma_fixable
        agg.other += r.breakdown.other
        total_instr += r.total_instructions
        total_matched += r.matched_instructions
        ceilings.append(r.ceiling_percent)

    print(f"\n{'=' * 72}")
    print(f"AT-LIMIT CEILING ANALYSIS")
    print(f"{'=' * 72}")
    print(f"\nFunctions analyzed: {total_funcs}")
    print(f"Total instructions: {total_instr:,}")
    print(f"Matched instructions: {total_matched:,} ({total_matched/total_instr*100:.1f}%)" if total_instr else "")

    print(f"\n--- Mismatch Breakdown (across all AT_LIMIT functions) ---")
    print(f"  Register swaps:       {agg.regswap:6d}  (unfixable from source, patcher helps)")
    print(f"  Merged symbols:       {agg.merged:6d}  (unfixable - linker ICF)")
    print(f"  Relocation noise:     {agg.relocation:6d}  (unfixable - address layout)")
    print(f"  Immediate diffs:      {agg.immediate:6d}  (unfixable - stack/branch offsets)")
    print(f"  Scheduling/reorder:   {agg.scheduling:6d}  (unfixable - compiler heuristic)")
    print(f"  Save/restore stubs:   {agg.save_restore:6d}  (unfixable - prologue/epilogue)")
    print(f"  Insert/delete:        {agg.insert_delete:6d}  (code structure differences)")
    print(f"  Encoding (fixable):   {agg.encoding_fixable:6d}  (bool_mask, extrwi, cmp_encoding)")
    print(f"  FMA (fixable):        {agg.fma_fixable:6d}  (fmadds vs fmuls+fadds)")
    print(f"  Other (unclassified): {agg.other:6d}")
    print(f"  {'─' * 40}")
    print(f"  Total mismatches:     {agg.total_mismatches:6d}")
    total_unfixable = agg.total_unfixable + agg.other  # conservative: "other" counted as unfixable
    total_fixable = agg.total_fixable
    if agg.total_mismatches > 0:
        print(f"\n  Unfixable:  {total_unfixable:6d} ({total_unfixable/agg.total_mismatches*100:.1f}%)")
        print(f"  Fixable:    {total_fixable:6d} ({total_fixable/agg.total_mismatches*100:.1f}%)")

    # Ceiling distribution
    if ceilings:
        avg_ceiling = sum(ceilings) / len(ceilings)
        print(f"\n--- Ceiling Distribution ---")
        print(f"  Average ceiling:  {avg_ceiling:.1f}%")
        print(f"  At 100%:          {sum(1 for c in ceilings if c >= 99.99)}")
        print(f"  99-100%:          {sum(1 for c in ceilings if 99 <= c < 99.99)}")
        print(f"  95-99%:           {sum(1 for c in ceilings if 95 <= c < 99)}")
        print(f"  90-95%:           {sum(1 for c in ceilings if 90 <= c < 95)}")
        print(f"  <90%:             {sum(1 for c in ceilings if c < 90)}")

    # Effective completion
    if db_stats:
        total = db_stats["total"]
        complete = db_stats["complete"]
        at_limit = db_stats["at_limit"]
        remaining = db_stats["remaining"]

        print(f"\n--- Effective Completion ---")
        print(f"  Total non-excluded functions: {total}")
        print(f"  COMPLETE (100%):              {complete} ({complete/total*100:.1f}%)")
        print(f"  AT_LIMIT:                     {at_limit} (avg {db_stats['at_limit_avg_pct']:.1f}%)")
        print(f"  Remaining:                    {remaining}")

        # "Done" = COMPLETE + AT_LIMIT (both represent closure)
        done = complete + at_limit
        print(f"\n  Closure (COMPLETE + AT_LIMIT): {done}/{total} ({done/total*100:.1f}%)")

        # Weighted effective %: COMPLETE counts as 100%, AT_LIMIT counts as their ceiling
        if ceilings:
            at_limit_weighted = sum(c for c in ceilings) / 100.0  # each ceiling / 100 = fraction
            effective_complete = complete + at_limit_weighted
            print(f"  Effective completion:          {effective_complete:.1f}/{total} "
                  f"({effective_complete/total*100:.1f}%)")


def print_fixable_functions(results: list[FunctionCeiling]):
    """Print functions with fixable patterns (candidates for source improvements)."""
    fixable = [r for r in results if not r.error and r.breakdown.total_fixable > 0]
    if not fixable:
        print("\nNo functions with fixable encoding patterns found.")
        return

    fixable.sort(key=lambda r: -r.breakdown.total_fixable)

    print(f"\n{'=' * 72}")
    print(f"FUNCTIONS WITH FIXABLE PATTERNS ({len(fixable)} functions)")
    print(f"{'=' * 72}")

    for r in fixable:
        enc = r.breakdown.encoding_fixable
        fma = r.breakdown.fma_fixable
        parts = []
        if enc: parts.append(f"{enc} encoding")
        if fma: parts.append(f"{fma} FMA")
        fix_desc = ", ".join(parts)

        print(f"  {r.current_percent:5.1f}% -> ceiling {r.ceiling_percent:5.1f}%  "
              f"[{fix_desc}]  {r.demangled[:55]}")
        print(f"         Unit: {r.unit}")


def print_worst_other(results: list[FunctionCeiling], limit: int = 20):
    """Print functions with highest 'other' (unclassified) mismatch count."""
    with_other = [r for r in results if not r.error and r.breakdown.other > 0]
    if not with_other:
        return

    with_other.sort(key=lambda r: -r.breakdown.other)

    print(f"\n--- Top {min(limit, len(with_other))} Functions with Unclassified Mismatches ---")
    for r in with_other[:limit]:
        b = r.breakdown
        parts = []
        if b.regswap: parts.append(f"reg:{b.regswap}")
        if b.merged: parts.append(f"merged:{b.merged}")
        if b.relocation: parts.append(f"reloc:{b.relocation}")
        if b.immediate: parts.append(f"imm:{b.immediate}")
        if b.scheduling: parts.append(f"sched:{b.scheduling}")
        if b.insert_delete: parts.append(f"ins/del:{b.insert_delete}")
        if b.other: parts.append(f"other:{b.other}")
        desc = " ".join(parts)
        print(f"  {r.current_percent:5.1f}%  [{desc}]  {r.demangled[:55]}")


def main():
    parser = argparse.ArgumentParser(description="AT_LIMIT ceiling calculator")
    parser.add_argument("--db", type=str, default=str(PROJECT_DIR / "decomp.db"),
                        help="Path to orchestrator database")
    parser.add_argument("--min", type=float, default=0.0, help="Minimum match%%")
    parser.add_argument("--max", type=float, default=100.0, help="Maximum match%%")
    parser.add_argument("--unit", type=str, default="", help="Filter by unit substring")
    parser.add_argument("--limit", type=int, default=0, help="Max functions to analyze (0=all)")
    parser.add_argument("--json", action="store_true", help="JSON output")
    parser.add_argument("--find-fixable", action="store_true",
                        help="Show functions with fixable patterns")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show progress")
    args = parser.parse_args()

    # Load functions from DB
    functions = load_at_limit_functions(args.db, args.min, args.max, args.unit)
    if args.limit:
        functions = functions[:args.limit]

    if not functions:
        print("No AT_LIMIT functions found matching criteria.", file=sys.stderr)
        sys.exit(1)

    if args.verbose:
        print(f"Analyzing {len(functions)} AT_LIMIT functions...", file=sys.stderr)

    # Analyze each function
    results: list[FunctionCeiling] = []
    for i, func in enumerate(functions):
        if args.verbose and (i + 1) % 50 == 0:
            print(f"  Progress: {i+1}/{len(functions)}", file=sys.stderr)

        ceiling = analyze_function(
            symbol=func["symbol"],
            unit=func["unit"],
            demangled=func.get("demangled", func["symbol"]),
            current_pct=func.get("current_percent", 0.0),
        )
        results.append(ceiling)

    errors = sum(1 for r in results if r.error)
    if args.verbose:
        print(f"  Done. {len(results) - errors} analyzed, {errors} errors.", file=sys.stderr)

    # Output
    if args.json:
        output = []
        for r in results:
            entry = {
                "symbol": r.symbol,
                "demangled": r.demangled,
                "unit": r.unit,
                "current_percent": round(r.current_percent, 2),
                "ceiling_percent": round(r.ceiling_percent, 2),
                "fixable_potential": round(r.fixable_potential, 2),
                "total_instructions": r.total_instructions,
                "matched_instructions": r.matched_instructions,
                "breakdown": r.breakdown.to_dict(),
            }
            if r.error:
                entry["error"] = r.error
            output.append(entry)
        print(json.dumps(output, indent=2))
    else:
        db_stats = load_all_verdicts(args.db)
        print_summary(results, db_stats)
        if args.find_fixable:
            print_fixable_functions(results)
        print_worst_other(results)

    # Return exit code based on findings
    fixable_count = sum(1 for r in results if not r.error and r.breakdown.total_fixable > 0)
    if fixable_count > 0:
        print(f"\n{fixable_count} function(s) have fixable patterns. "
              f"Use --find-fixable for details.", file=sys.stderr)


if __name__ == "__main__":
    main()
