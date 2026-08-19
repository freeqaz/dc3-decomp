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

TWO CEILINGS, NOT ONE
=====================
`ceiling = 100% - unfixable%` is only as honest as the word "unfixable". The
original `total_unfixable` folded in `insert_delete` — which this very file
labels "code structure differences" when it prints it, i.e. the single most
FIXABLE class — and `immediate`, which is stack/branch offsets. Counting those
unfixable makes the ceiling systematically UNDERSTATE what is reachable, and a
ceiling that reads below the current match% is how a function acquires an
"at limit" verdict it never earned.

So this tool now reports TWO ceilings and never silently picks one:

  CONSERVATIVE  everything except the encoding/FMA patterns is a floor
                (== the historical number, unchanged)
  OPTIMISTIC    only the hard-floor classes are a floor — regswap, merged,
                relocation, scheduling, save_restore. `immediate`,
                `insert_delete` and `other` are treated as reachable.

The truth is between them. Quote both or quote neither.

Usage:
    python scripts/analysis/ceiling_calculator.py                    # All AT_LIMIT functions
    python scripts/analysis/ceiling_calculator.py --unit 'system/*'  # Filter by unit
    python scripts/analysis/ceiling_calculator.py --min 90 --max 99  # Filter by match%
    python scripts/analysis/ceiling_calculator.py --json             # JSON output
    python scripts/analysis/ceiling_calculator.py --find-fixable     # Show functions with fixable issues
    python scripts/analysis/ceiling_calculator.py --include-null-percent  # + the 1,231 NULL rows
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
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
from scripts.analysis.coverage import CoverageReport, add_coverage_args, like_escape
from scripts.analysis.ruler import graded_ruler


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

    # ── RESULT-CHANGING BLOCK (b) ────────────────────────────────────────────
    # `total_unfixable` is LEFT EXACTLY AS IT WAS so the conservative ceiling is
    # bit-for-bit the historical number.  What changes is that it is no longer
    # the ONLY answer: `hard_unfixable` excludes the two classes that were
    # miscategorised (`insert_delete`, printed by this file as "code structure
    # differences", and `immediate`, i.e. stack/branch offsets), and feeds an
    # OPTIMISTIC ceiling reported alongside.
    @property
    def total_unfixable(self) -> int:
        """CONSERVATIVE floor — unchanged from the original definition."""
        return (self.regswap + self.merged + self.relocation + self.immediate +
                self.scheduling + self.save_restore + self.insert_delete)

    @property
    def hard_unfixable(self) -> int:
        """OPTIMISTIC floor: only classes with no known source lever.

        Deliberately excludes `immediate` (stack/branch offsets move with frame
        layout and branch structure, both source-reachable) and `insert_delete`
        (code structure — the class this project fixes most often).
        """
        return (self.regswap + self.merged + self.relocation +
                self.scheduling + self.save_restore)

    @property
    def soft_unfixable(self) -> int:
        """The difference between the two ceilings: immediate + insert_delete."""
        return self.immediate + self.insert_delete

    @property
    def total_fixable(self) -> int:
        return self.encoding_fixable + self.fma_fixable
    # ── end RESULT-CHANGING BLOCK (b) ────────────────────────────────────────

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
            "hard_unfixable": self.hard_unfixable,
            "soft_unfixable": self.soft_unfixable,
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
    ceiling_percent: float       # CONSERVATIVE theoretical max (100% - unfixable%)
    fixable_potential: float     # how much % could improve with source fixes
    ceiling_percent_optimistic: float = 0.0   # 100% - hard_unfixable%
    ceiling_percent_raw: float = 0.0          # conservative ceiling BEFORE clamping
    clamped_to_current: bool = False          # raw ceiling was BELOW current_percent
    error: Optional[str] = None


def run_objdiff_json(symbol: str, extra_args: Optional[list] = None) -> tuple:
    """Run objdiff-cli and return (json_dict|None, error_detail|None).

    The old signature returned only `Optional[dict]`, so a timeout, a missing
    binary, a non-zero exit and unparseable stdout all collapsed into the same
    `error="objdiff failed"` string — and the count of them was printed only
    under `--verbose` and omitted from `--json` entirely. The reason is now
    carried out so the summary can say WHICH failure, and how many.
    """
    cmd = [
        str(OBJDIFF_CLI), "diff",
        "-p", str(PROJECT_DIR),
        symbol,
        "--include-instructions",
        "-f", "json",
    ]
    if extra_args:
        cmd += list(extra_args)
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        if result.returncode != 0:
            tail = (result.stderr or "").strip().splitlines()
            detail = tail[-1][:160] if tail else "(no stderr)"
            return None, f"objdiff exit {result.returncode}: {detail}"
        return json.loads(result.stdout), None
    except subprocess.TimeoutExpired:
        return None, "objdiff timeout (30s)"
    except json.JSONDecodeError as exc:
        return None, f"objdiff stdout was not JSON: {exc}"
    except FileNotFoundError:
        return None, f"objdiff-cli not found at {OBJDIFF_CLI}"


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
                     current_pct: float = 0.0,
                     objdiff_args: Optional[list] = None) -> FunctionCeiling:
    """Run full ceiling analysis on a single function."""
    result = FunctionCeiling(
        symbol=symbol, demangled=demangled or symbol, unit=unit,
        current_percent=current_pct or 0.0, total_instructions=0,
        matched_instructions=0, breakdown=MismatchBreakdown(),
        ceiling_percent=0.0, fixable_potential=0.0,
    )

    data, detail = run_objdiff_json(symbol, objdiff_args)
    if data is None:
        result.error = detail or "objdiff failed"
        return result

    instructions = data.get("instructions", [])
    result.total_instructions = len(instructions)
    # NB: this OVERWRITES the DB's (drifting) current_percent with objdiff's
    # live number, but only for rows that reached here — the --min/--max band
    # that selected them still came from the DB column.
    live_pct = data.get("fuzzy_match_percent")
    result.current_percent = live_pct if live_pct is not None else (current_pct or 0.0)

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
        hard_frac = result.breakdown.hard_unfixable / total

        # Conservative ceiling: assume "other" is unfixable
        result.ceiling_percent = 100.0 * (1.0 - (unfixable_frac + other_frac))
        # Optimistic ceiling: only the hard-floor classes are a floor.
        result.ceiling_percent_optimistic = 100.0 * (1.0 - hard_frac)
        # But the fixable_potential tells how much source fixes could help
        result.fixable_potential = fixable_frac * 100.0

        # Clamp.  A ceiling BELOW the current match% means the classifier and
        # the grader disagree about this function — clamping it up to `current`
        # makes that disagreement invisible, which is exactly how a bogus "at
        # limit" reads as consistent.  The clamp is kept (removing it would
        # change every aggregate) but the raw value and the fact of the clamp
        # are now recorded and counted.
        result.ceiling_percent_raw = result.ceiling_percent
        result.clamped_to_current = result.ceiling_percent < result.current_percent
        result.ceiling_percent = max(result.current_percent, min(100.0, result.ceiling_percent))
        result.ceiling_percent_optimistic = max(
            result.ceiling_percent, min(100.0, result.ceiling_percent_optimistic))
    else:
        result.ceiling_percent = result.current_percent
        result.ceiling_percent_optimistic = result.current_percent
        result.ceiling_percent_raw = result.current_percent

    return result


def _connect_ro(db_path: str):
    """Open decomp.db READ-ONLY. This tool only ever SELECTs.

    Concurrent agents share this database; an accidental write here would be
    invisible and unattributable. `mode=ro` makes that impossible rather than
    merely unlikely.
    """
    return sqlite3.connect(f"file:{Path(db_path).resolve()}?mode=ro", uri=True)


def load_at_limit_functions(db_path: str, min_pct: float = 0.0,
                            max_pct: float = 100.0,
                            unit_filter: str = "",
                            include_null_percent: bool = False,
                            cov: Optional[CoverageReport] = None) -> list[dict]:
    """Load AT_LIMIT functions from the orchestrator database.

    Every WHERE clause below removes rows, and the tool used to report only the
    survivors.  The filtering is therefore done in Python against the FULL
    AT_LIMIT population so each removal can be counted — the SQL result is
    identical, the difference is that the denominator is now knowable.

    Notably `current_percent IS NOT NULL` silently removed 1,231 of the 3,796
    AT_LIMIT rows on this tree.  A SQL NULL comparison yields NULL, so those
    rows would have vanished at the --min/--max band even without the explicit
    clause.  `--include-null-percent` puts them back (they sort last).
    """
    conn = _connect_ro(db_path)
    conn.row_factory = sqlite3.Row

    # `merged\_%` ESCAPE '\' is CORRECTLY escaped: without the ESCAPE, `_` is a
    # single-character wildcard and `merged_%` would also exclude e.g. `mergedX…`.
    # (This is the certify_floor.py defect; this line does not have it.)
    rows = conn.execute("""
        SELECT symbol, demangled, unit, current_percent, excluded
        FROM functions
        WHERE verdict = 'AT_LIMIT'
        ORDER BY symbol ASC
    """).fetchall()
    conn.close()

    if cov is not None:
        cov.universe(len(rows), "rows with verdict='AT_LIMIT' in decomp.db")
        # Drop buckets below are mutually exclusive (first match wins) so the
        # arithmetic balances; the OVERLAP between them is the interesting part,
        # so measure it here rather than leaving it to be inferred.
        n_null_all = sum(1 for r in rows if r["current_percent"] is None)
        n_null_excl = sum(1 for r in rows
                          if r["current_percent"] is None and r["excluded"])
        cov.extra("at_limit_null_current_percent_total", n_null_all)
        cov.extra("at_limit_null_current_percent_also_excluded", n_null_excl)
        if n_null_all and n_null_all == n_null_excl:
            cov.note(f"all {n_null_all} AT_LIMIT rows with NULL current_percent are ALSO "
                     f"excluded=1, so `AND current_percent IS NOT NULL` removed nothing "
                     f"this run that `excluded = 0` had not already removed. That is a "
                     f"property of TODAY's DB, not of the query — the clause is still a "
                     f"silent NULL trap.")

    # `unit LIKE ?` with `f"%{unit_filter}%"` was UNESCAPED: a `_` in the filter
    # matched any character, so the filter could only ever OVER-match (extra
    # units, never missing ones).  The filter is now a literal substring test —
    # `like_escape` shows what the equivalent escaped SQL pattern would be.
    unit_like_equivalent = f"%{like_escape(unit_filter)}%" if unit_filter else ""

    out = []
    n_null = 0
    for r in rows:
        d = dict(r)
        sym = d["symbol"] or ""
        if d.get("excluded"):
            if cov is not None:
                cov.drop("excluded-row", note="functions.excluded = 1")
            continue
        if sym.startswith("merged_"):
            if cov is not None:
                cov.drop("merged-symbol", note="ICF-folded alias, not a real function")
            continue
        cp = d["current_percent"]
        if cp is None:
            n_null += 1
            if not include_null_percent:
                if cov is not None:
                    cov.drop("null-current-percent",
                             note="DB column is NULL; the SQL band silently dropped these. "
                                  "Pass --include-null-percent to analyze them")
                continue
        else:
            if min_pct > 0 and cp < min_pct:
                if cov is not None:
                    cov.drop("below--min")
                continue
            if max_pct < 100 and cp > max_pct:
                if cov is not None:
                    cov.drop("above--max")
                continue
        if unit_filter:
            # SQLite LIKE is ASCII-case-INSENSITIVE by default; matched here so
            # moving the filter out of SQL does not quietly change the result.
            if unit_filter.lower() not in (d.get("unit") or "").lower():
                if cov is not None:
                    cov.drop("unit-filter-excluded")
                continue
        out.append(d)

    if cov is not None:
        cov.extra("at_limit_rows_with_null_current_percent", n_null)
        cov.extra("unit_filter", unit_filter)
        cov.extra("unit_like_escaped_equivalent", unit_like_equivalent)
        cov.note("the --min/--max band and the sort key are `current_percent`, a column "
                 "the ninja sync deliberately does NOT write — it DRIFTS. Treat the band "
                 "as a rough selector, never as a measurement.")

    # Deterministic total order: percent DESC (NULLs last), then symbol, then unit.
    out.sort(key=lambda d: (d["current_percent"] is None,
                            -(d["current_percent"] or 0.0),
                            d["symbol"] or "",
                            d.get("unit") or ""))
    return out


def load_all_verdicts(db_path: str) -> dict:
    """Load verdict distribution for aggregate stats."""
    conn = _connect_ro(db_path)
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


def print_summary(results: list[FunctionCeiling], db_stats: dict,
                  cov: Optional[CoverageReport] = None, ruler_banner: str = ""):
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
    ceilings_opt = []
    clamped = 0

    for r in results:
        if r.error:
            continue
        if r.clamped_to_current:
            clamped += 1
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
        ceilings_opt.append(r.ceiling_percent_optimistic)

    errors = [r for r in results if r.error]
    from collections import Counter as _Counter
    err_kinds = _Counter((r.error or "").split(":")[0] for r in errors)

    print(f"\n{'=' * 72}")
    print(f"AT-LIMIT CEILING ANALYSIS")
    print(f"{'=' * 72}")
    if ruler_banner:
        for line in ruler_banner.splitlines():
            print(f"  {line}")
    # `Functions analyzed` used to print the SELECTED count while every
    # aggregate below skipped the errored rows — errors deflated the numerator
    # against a full denominator.  Both numbers now print.
    print(f"\nFunctions selected:  {total_funcs}")
    print(f"Functions analyzed:  {total_funcs - len(errors)}   "
          f"(every aggregate below covers only these)")
    print(f"Functions errored:   {len(errors)}"
          + (f"   {dict(sorted(err_kinds.items()))}" if errors else ""))
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
    hard_unfixable = agg.hard_unfixable
    total_fixable = agg.total_fixable
    if agg.total_mismatches > 0:
        m = agg.total_mismatches
        print(f"\n  Unfixable (CONSERVATIVE): {total_unfixable:6d} "
              f"({total_unfixable/m*100:.1f}%)   incl. immediate + insert/delete + other")
        print(f"  Unfixable (OPTIMISTIC):   {hard_unfixable:6d} "
              f"({hard_unfixable/m*100:.1f}%)   regswap/merged/reloc/sched/save-restore only")
        # PRINT THE PERCENTAGE. This line used to give the gap as a bare count
        # of three terms and leave the reader to divide -- and commit 547b459f3
        # duly did it by hand and got it wrong, publishing
        # "35,039 + 12,424 + 3,203 = 47.3% of ALL mismatches" when the sum is
        # 68.4%; 47.3% is insert_delete ALONE. A tool that computes a
        # denominator and then withholds the ratio is inviting exactly that.
        gap = agg.soft_unfixable + agg.other
        print(f"    ↳ the gap between them: {gap} mismatches = {gap/m*100:.1f}% "
              f"of all {m}, made up of "
              f"{agg.immediate} immediate ({agg.immediate/m*100:.1f}%), "
              f"{agg.insert_delete} insert/delete ({agg.insert_delete/m*100:.1f}%), "
              f"{agg.other} other ({agg.other/m*100:.1f}%).")
        print(f"      Those are shares of the SAME denominator, so they add to "
              f"the gap -- do not quote one of them as the sum. This is a "
              f"JUDGEMENT, not a measurement.")
        print(f"  Fixable (pattern-matched): {total_fixable:6d} ({total_fixable/m*100:.1f}%)")

    # Ceiling distribution
    if ceilings:
        avg_ceiling = sum(ceilings) / len(ceilings)
        avg_opt = sum(ceilings_opt) / len(ceilings_opt)
        print(f"\n--- Ceiling Distribution (n={len(ceilings)} analyzed) ---")
        print(f"  Average ceiling:  {avg_ceiling:.1f}% conservative "
              f"/ {avg_opt:.1f}% optimistic")
        print(f"                     conservative   optimistic")
        for label, lo, hi in (("At 100%", 99.99, 1e9), ("99-100%", 99.0, 99.99),
                              ("95-99%", 95.0, 99.0), ("90-95%", 90.0, 95.0),
                              ("<90%", -1e9, 90.0)):
            c1 = sum(1 for c in ceilings if lo <= c < hi)
            c2 = sum(1 for c in ceilings_opt if lo <= c < hi)
            print(f"  {label:<16s} {c1:12d} {c2:12d}")
        # A ceiling clamped UP to `current` is the classifier disagreeing with
        # the grader.  Hiding it makes an unearned "at limit" read as consistent.
        pct = clamped / len(ceilings) * 100.0
        print(f"\n  Ceilings clamped UP to current%: {clamped} of {len(ceilings)} "
              f"({pct:.1f}%) — the raw ceiling was BELOW the measured match% for "
              f"these, i.e. the classifier and the grader disagree. See "
              f"`ceiling_percent_raw` in --json.")

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
            # The numerator covers only the ceilings we actually computed; the
            # denominator is every non-excluded function in the DB.  Say so.
            print(f"    ⚠ numerator uses ceilings for {len(ceilings)} of {at_limit} "
                  f"AT_LIMIT rows ({at_limit - len(ceilings)} not analyzed here: "
                  f"errored, filtered, or outside --min/--max). The remaining "
                  f"AT_LIMIT rows contribute 0 to this figure, so it is a LOWER "
                  f"bound whenever that gap is non-zero.")

    if cov is not None:
        cov.extra("errors", len(errors))
        cov.extra("error_kinds", dict(sorted(err_kinds.items())))
        cov.extra("ceilings_clamped_up_to_current", clamped)


def print_fixable_functions(results: list[FunctionCeiling]):
    """Print functions with fixable patterns (candidates for source improvements)."""
    fixable = [r for r in results if not r.error and r.breakdown.total_fixable > 0]
    if not fixable:
        print("\nNo functions with fixable encoding patterns found.")
        return

    # Full tie-break: two runs must produce byte-identical listings.
    fixable.sort(key=lambda r: (-r.breakdown.total_fixable, r.symbol, r.unit))

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

    with_other.sort(key=lambda r: (-r.breakdown.other, r.symbol, r.unit))

    print(f"\n--- Top {min(limit, len(with_other))} of {len(with_other)} Functions with "
          f"Unclassified Mismatches ---")
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
    if len(with_other) > limit:
        rest = with_other[limit:]
        print(f"  ... +{len(rest)} more, {sum(r.breakdown.other for r in rest)} further "
              f"unclassified mismatches (display cut at --worst-other-limit)")


def ruler_disclosure() -> str:
    """What ruler this tool's percentages are ACTUALLY on, versus the grader's.

    `objdiff-cli diff` (what run_objdiff_json shells out to) has its OWN base
    config — `functionRelocDiffs=DataValue`, `pool=true`, `combine*=false` —
    which is NOT `report generate`'s and NOT the project's graded config.  A
    percentage without its ruler is not a measurement; `ruler.py` measured pool
    relocations alone as worth up to 14.75 percentage points.
    """
    lines = ["ruler: `objdiff-cli diff` BASE config (functionRelocDiffs=data_value, "
             "ppc.calculatePoolRelocations=true, combineData/TextSections=false)",
             "  ⚠ this is NOT the graded ruler and NOT report.json's. Pass "
             "--use-graded-ruler to score on the grader's config instead."]
    try:
        g = graded_ruler(PROJECT_DIR)
        lines.append(f"  grader for comparison: {g.label()}")
    except Exception as exc:                       # never let disclosure crash the tool
        lines.append(f"  grader ruler unresolvable: {exc}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="AT_LIMIT ceiling calculator")
    parser.add_argument("--db", type=str, default=str(PROJECT_DIR / "decomp.db"),
                        help="Path to orchestrator database")
    parser.add_argument("--min", type=float, default=0.0, help="Minimum match%%")
    parser.add_argument("--max", type=float, default=100.0, help="Maximum match%%")
    parser.add_argument("--unit", type=str, default="", help="Filter by unit substring")
    parser.add_argument("--limit", type=int, default=0,
                        help="Max functions to ANALYZE (0=all). This truncates the "
                             "analysis, not a printout: every aggregate below is then "
                             "computed over the sample and the run exits 3.")
    parser.add_argument("--worst-other-limit", type=int, default=20,
                        help="Rows in the unclassified-mismatch listing (default: 20). "
                             "Display only — the residual is printed.")
    parser.add_argument("--include-null-percent", action="store_true",
                        help="Also analyze AT_LIMIT rows whose current_percent is NULL "
                             "(1,231 of 3,796 on this tree). Previously these were "
                             "dropped silently by the SQL band.")
    parser.add_argument("--use-graded-ruler", action="store_true",
                        help="Score with report.json's graded diff config instead of "
                             "`objdiff-cli diff`'s own base config (the historical, "
                             "undisclosed default). Changes every percentage.")
    parser.add_argument("--json", action="store_true", help="JSON output")
    parser.add_argument("--find-fixable", action="store_true",
                        help="Show functions with fixable patterns")
    parser.add_argument("--verbose", "-v", action="store_true", help="Show progress")
    add_coverage_args(parser)
    args = parser.parse_args()

    cov = CoverageReport("ceiling_calculator", args=args)
    cov.extra("db_path", str(args.db))

    objdiff_args = None
    if args.use_graded_ruler:
        g = graded_ruler(PROJECT_DIR)
        objdiff_args = g.args
        cov.note(f"scored on the GRADED ruler: {g.label()}")
        ruler_banner = g.banner()
    else:
        ruler_banner = ruler_disclosure()
        cov.note("scored on `objdiff-cli diff`'s BASE config "
                 "(functionRelocDiffs=data_value, pool=true) — NOT the graded ruler. "
                 "Pass --use-graded-ruler to match report.json.")

    # Load functions from DB
    functions = load_at_limit_functions(args.db, args.min, args.max, args.unit,
                                        include_null_percent=args.include_null_percent,
                                        cov=cov)
    if args.limit:
        cov.cap("--limit", args.limit, before=len(functions),
                after=min(args.limit, len(functions)),
                note="these rows were NEVER analyzed; every aggregate below is a sample")
        functions = functions[:args.limit]

    if not functions:
        print("No AT_LIMIT functions found matching criteria.", file=sys.stderr)
        cov.emit()
        sys.exit(1)

    cov.examine(len(functions))

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
            current_pct=func.get("current_percent") or 0.0,
            objdiff_args=objdiff_args,
        )
        results.append(ceiling)

    errors = sum(1 for r in results if r.error)
    clamped = sum(1 for r in results if not r.error and r.clamped_to_current)
    # Was printed only under --verbose and omitted from --json entirely.
    print(f"  {len(results) - errors} analyzed, {errors} objdiff errors "
          f"(errored rows are excluded from every aggregate).", file=sys.stderr)

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
                "ceiling_percent_conservative": round(r.ceiling_percent, 2),
                "ceiling_percent_optimistic": round(r.ceiling_percent_optimistic, 2),
                "ceiling_percent_raw": round(r.ceiling_percent_raw, 2),
                "clamped_to_current": r.clamped_to_current,
                "fixable_potential": round(r.fixable_potential, 2),
                "total_instructions": r.total_instructions,
                "matched_instructions": r.matched_instructions,
                "breakdown": r.breakdown.to_dict(),
            }
            if r.error:
                entry["error"] = r.error
            output.append(entry)
        cov.extra("errors", errors)
        cov.extra("ceilings_clamped_up_to_current", clamped)
        print(json.dumps({
            "functions": output,
            "errors": errors,
            "ceilings_clamped_up_to_current": clamped,
            "ruler": ruler_banner,
            "_coverage": cov.as_dict(),
        }, indent=2))
    else:
        db_stats = load_all_verdicts(args.db)
        print_summary(results, db_stats, cov=cov, ruler_banner=ruler_banner)
        if args.find_fixable:
            print_fixable_functions(results)
        print_worst_other(results, limit=args.worst_other_limit)

    # Return exit code based on findings
    fixable_count = sum(1 for r in results if not r.error and r.breakdown.total_fixable > 0)
    if fixable_count > 0:
        print(f"\n{fixable_count} function(s) have fixable patterns. "
              f"Use --find-fixable for details.", file=sys.stderr)

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
