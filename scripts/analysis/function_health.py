#!/usr/bin/env python3
"""Unified function health report.

Combines multiple analysis signals into a single diagnostic:
- Match% and build status (objdiff)
- Mismatch breakdown (regswaps, encoding, scheduling, etc.)
- Theoretical ceiling (max achievable match%)
- Pattern suggestions (which permuter patterns to try)
- Fixability verdict (worth attempting vs AT_LIMIT)

Usage:
    # By unit + match range (batch mode)
    python scripts/analysis/function_health.py --unit "system/rndobj/*" --min 90 --max 99.9

    # Top N most workable functions
    python scripts/analysis/function_health.py --top 20

    # Single function -- BROKEN, see TODO(repair) in _run_objdiff below.
    # objdiff-cli takes the symbol POSITIONALLY (`objdiff-cli diff [<symbol>]`),
    # so `--symbol` exits 1 and every single-function report comes back
    # `verdict=error`. It is left unrepaired deliberately: fixing the call
    # changes what this tool FINDS, which does not belong in an honesty pass.
    # The failure now names itself instead of vanishing. Use the orchestrator
    # MCP tools (run_objdiff / run_analyze_function) for a single symbol.
    python scripts/analysis/function_health.py --symbol "..." --json   # exits with verdict=error

COVERAGE / HONESTY NOTES  (see scripts/analysis/coverage.py)
------------------------------------------------------------
**Batch mode was answering "no work exists" to every query.**  The SQL was

    SELECT symbol, demangled, unit, source_path, match_percent FROM functions ...

and `decomp.db`'s `functions` table has NEITHER a `source_path` NOR a
`match_percent` column — they are `current_percent` and
`match_percent_normalized`.  Every execution raised `sqlite3.OperationalError`,
`except Exception: return []` swallowed it, and the caller printed
`No functions found matching criteria.` and exited 1.  A schema error and an
empty result were indistinguishable, so the tool reported an empty pool for
every unit anyone asked about.  Verified before the fix:
`--unit 'default/system/rndobj/*' --min 90` -> `No functions found matching
criteria.`; after the fix the same query returns 255 rows.

The database PATH was wrong too: it pointed at `build/373307D9/decomp.db`,
which is a zero-byte placeholder.  The populated database is `decomp.db` at the
repo root.  A zero-byte file passes `.exists()` and `sqlite3.connect()` happily,
so this failed as "no such table" — swallowed by the same handler.  Path
resolution is now explicit, overridable with `--db`, and a database with no
`functions` table is a LOUD error.

Other changes: the pool size is measured with `COUNT(*)` before `--limit` is
applied, so a truncated run knows and prints its own denominator; the batch
`ORDER BY` gained a `symbol` tie-break (without it, *which* 50 rows the cap kept
was undefined); and match percentages render with enough precision that 99.97
can no longer print as `100.0`.
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import subprocess
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path
from typing import Optional

PROJECT_DIR = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(PROJECT_DIR))

from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402


# Columns that actually exist on `functions`.  `match_percent` and
# `source_path` DO NOT — see the module docstring.
PERCENT_COLUMNS = ("match_percent_normalized", "current_percent")
DEFAULT_PERCENT_COLUMN = "match_percent_normalized"

# Candidate database locations, in the order they are tried.  The first entry
# is the real one; the second is the path the old code hardcoded and is kept
# only so a stale tree still resolves.
DB_CANDIDATES = ("decomp.db", "build/373307D9/decomp.db")


class DatabaseUnavailableError(RuntimeError):
    """No usable decomp.db.  Raised instead of returning an empty pool.

    "The database is missing/empty" and "there is no work in this unit" used to
    print identically (`No functions found matching criteria.`), which is how
    this tool spent its life reporting an exhausted pool it had never queried.
    """


class QueryFailedError(RuntimeError):
    """The batch SQL raised.  Never swallowed into an empty result."""


def _fmt_pct(p: float | None, width: int = 0) -> str:
    """Render a percentage without letting a sub-100 value print as `100.0`.

    Every percentage surface in this project rounds; two real bugs have already
    hidden under a rendered `100.0` that was really 99.97 (see
    docs/decomp/patterns/rounded-100-hides-real-bugs.md).  Anything strictly
    below 100 that would round up renders as `<100` instead.
    """
    if p is None:
        return "n/a".rjust(width) if width else "n/a"
    s = f"{p:.3f}"
    if p < 100.0 and float(s) >= 100.0:
        s = "<100"
    return s.rjust(width) if width else s


@dataclass
class MismatchCategory:
    """Counts for a mismatch category.

    `fixable` is a THREE-state claim wearing a two-state type, which is how
    `insert_delete` came to be filed as a hard floor.  `contested` marks the
    classes ceiling_calculator.py reports two ways precisely because the
    project does not agree they are floors -- see its "TWO CEILINGS, NOT ONE"
    header.  A category can be `fixable=False, contested=True`: not known
    fixable, not known unfixable.
    """
    name: str
    count: int
    fixable: bool
    description: str
    contested: bool = False


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
    #: Included in `unfixable_mismatches`, and NOT a floor. See
    #: MismatchCategory.contested and ceiling_calculator's two ceilings.
    contested_mismatches: int = 0

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

    # Anything that went wrong while building this report.  Previously these
    # were `except ...: pass` / `return None`, so a failed objdiff and a clean
    # function looked the same in JSON.
    errors: list[str] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Analysis helpers
# ---------------------------------------------------------------------------


def _run_objdiff(symbol: str) -> tuple[dict | None, str]:
    """Run objdiff and return ``(data, error)``.

    Returns the reason alongside the None so that "objdiff is not built",
    "objdiff timed out" and "this symbol has no diff" stop being the same
    result.  The old signature returned a bare None for all three.
    """
    # REPAIRED 2026-08-20 (frontier lane).  This invocation passed the symbol as
    # `--symbol <sym>`; objdiff-cli takes it POSITIONALLY and has no such flag,
    # so every call exited 1 with "Unrecognized argument: --symbol" and this
    # function has never once returned data.  The 2026-08-19 honesty pass
    # diagnosed it exactly and left it standing, on the reasoning that repairing
    # the call changes what the tool FINDS.  That scoping was right for an
    # honesty pass and wrong to leave permanently: batch mode goes through the
    # same call, so a full-band run returned 2,705 rows of which 2,705 were
    # `verdict=error` -- and because the error path returns a
    # default-constructed HealthReport, each one serialised as
    # `ceiling_percent: 100.0, headroom: 0.0, total_mismatches: 0` with exit 0.
    # A consumer filtering on `headroom > 0` got nothing, which reads as
    # "this class is exhausted".  Fixing the call is what makes the tool a
    # measurement instead of 2,705 confident wrong records.
    #
    # Negative control for anyone re-testing:
    #   bin/objdiff-cli diff --symbol '?Poll@BlockMgr@@QAAXXZ' --format json
    #     -> rc 1, empty stdout, "Unrecognized argument: --symbol"
    #   bin/objdiff-cli diff '?Poll@BlockMgr@@QAAXXZ' --format json
    #     -> rc 0, {"symbol":...,"normalized_match_percent":99.98214,...}
    cli = PROJECT_DIR / "bin" / "objdiff-cli"
    if not cli.exists():
        return None, f"objdiff-cli not found at {cli}"
    try:
        result = subprocess.run(
            [str(cli), "diff", symbol,
             "--format", "json", "--include-instructions"],
            capture_output=True, text=True, timeout=60,
            cwd=str(PROJECT_DIR),
        )
    except subprocess.TimeoutExpired:
        return None, "objdiff-cli timed out after 60s"
    except OSError as exc:
        return None, f"could not launch objdiff-cli: {exc}"
    if result.returncode != 0:
        tail = (result.stderr or "").strip().splitlines()
        return None, (f"objdiff-cli exited {result.returncode}"
                      + (f": {tail[-1][:200]}" if tail else ""))
    try:
        return json.loads(result.stdout), ""
    except json.JSONDecodeError as exc:
        return None, f"objdiff-cli emitted unparseable JSON: {exc}"


_MISMATCH_DESCS = {
    "regswap": ("Register swap", False, "Callee-saved register allocation difference"),
    "merged": ("Merged symbol", False, "ICF-merged call target difference"),
    "relocation": ("Relocation", False, "Address/symbol relocation difference"),
    "scheduling": ("Scheduling", False, "Instruction reorder (same opcodes)"),
    "save_restore": ("Save/Restore", False, "Prologue/epilogue stub difference"),
    "encoding": ("Encoding", True, "Fixable opcode encoding (extrwi/rlwinm)"),
    "fma": ("FMA", True, "FMA instruction form difference"),
    # CONTESTED, not unfixable.  ceiling_calculator.py calls insert_delete
    # "the single most FIXABLE class" and excludes it from its optimistic
    # floor; this file went on counting it as a hard floor, and at line
    # ~408 that fed straight into `Only unfixable mismatches remain` ->
    # verdict at_limit.  The lane fixed the classification in
    # ceiling_calculator and left it standing in this sibling, which it
    # edited in the same branch.  Now neither tool silently picks a side.
    "insert_delete": ("Insert/Delete", False,
                      "Extra/missing instructions — CONTESTED: "
                      "ceiling_calculator treats this as reachable, not a floor",
                      True),
    "other": ("Other", False,
              "Unclassified opcode difference — CONTESTED: unclassified is "
              "not the same as unfixable", True),
}


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
    descs = _MISMATCH_DESCS

    for key, count in counts.items():
        if count > 0:
            name, fixable, desc, *rest = descs[key]
            categories.append(MismatchCategory(
                name=name, count=count, fixable=fixable, description=desc,
                contested=bool(rest[0]) if rest else False,
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
    # Of the "unfixable" total, how much is actually CONTESTED rather than a
    # known floor.  Counting contested instructions as a floor is what let this
    # tool hand out `at_limit` on the strength of insert_delete alone.
    contested = sum(c.count for c in categories if c.contested)
    hard_unfixable = unfixable - contested

    if match_pct >= 100.0:
        return "complete", "Already at 100%", 0.0

    # THE CEILING AND THE MEASUREMENT ARE ON DIFFERENT RULERS.  `ceiling` is
    #     100 - 100 * unfixable_mismatches / total_instructions
    # -- an UNWEIGHTED instruction-count ratio.  `match_pct` is objdiff's
    # `normalized_match_percent`, a SCORE-WEIGHTED f32 that gives near-full
    # credit to a partially-matching instruction (a register-only difference
    # costs almost nothing).  Subtracting one from the other is meaningless, and
    # it reads low: measured on this tree 2026-08-20, 21 of 25 functions in
    # `default/system/math/*` came back with a ceiling BELOW their own measured
    # percent -- `?OnSide@BSPFace@@` 89.8 % ceiling at 99.9 % measured,
    # `Multiply(Vector3, Quat)` 16.0 % ceiling at 84.8 % measured.
    #
    # This is also the mechanism behind ceiling_calculator.py's clamp: it
    # reports `ceilings_clamped_up_to_current: 1172 / 1568 (74.7 %)`, and that
    # rate is a property of the SCALE MISMATCH, not of the functions.  A clamp
    # hides it; here it would surface as a confident `at_limit` on a
    # near-perfect function, which is worse.
    #
    # So: refuse.  Do not manufacture an at_limit verdict out of a comparison
    # that cannot be made.  Fixing this properly means computing the ceiling on
    # objdiff's own weighted score, which is a real change to what the tool
    # FINDS and needs its own validation -- not a guard.
    if headroom < 0.0:
        return ("ceiling_unusable",
                f"Ceiling model unusable here: it reports {ceiling:.1f} % for a "
                f"function measured at {match_pct:.2f} %. The ceiling is an "
                f"unweighted instruction-count ratio and the measurement is "
                f"objdiff's score-weighted normalized percent; they are not "
                f"comparable. NOT an at_limit certificate.", 0.0)

    if headroom <= 0.5 and ceiling < 100.0:
        return "at_limit", f"Ceiling {ceiling:.1f}% — no room to improve", 0.0

    if fixable == 0 and unfixable > 0:
        if contested > 0:
            # Do not certify a floor out of a class the project reports two
            # ways.  Say what is known and what is not, and let the reader
            # decide -- the same refusal ceiling_calculator makes.
            return ("contested",
                    f"No KNOWN-fixable mismatches, but {contested} of "
                    f"{unfixable} remaining instructions are in contested "
                    f"classes (insert/delete, unclassified) that "
                    f"ceiling_calculator treats as reachable; "
                    f"{hard_unfixable} are hard floors. Not an at_limit "
                    f"certificate.", 0.0)
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
    elif contested > 0:
        return ("contested",
                f"Low known fixability ({fixable}/{total_mm}), but {contested} "
                f"instructions are in contested classes — not an at_limit "
                f"certificate", score)
    else:
        return "at_limit", f"Low fixability ({fixable}/{total_mm})", score


def analyze_function(symbol: str) -> HealthReport:
    """Generate a full health report for a function."""
    # Look up function info from DB
    demangled = ""
    unit = ""
    source_path = ""
    lookup_error = ""
    try:
        from scripts.orchestrator.db_helpers import resolve_symbol_info
        info = resolve_symbol_info(symbol)
        demangled = info.get("demangled", "")
        unit = info.get("unit", "")
        source_path = info.get("source_path", "")
    except Exception as exc:
        # Was a bare `pass`: a broken DB lookup and a symbol with no metadata
        # produced identical output.  Recorded, not swallowed.
        lookup_error = f"resolve_symbol_info failed: {exc.__class__.__name__}: {exc}"

    report = HealthReport(
        symbol=symbol,
        demangled=demangled,
        unit=unit,
        source_path=source_path,
        match_percent=0.0,
        total_instructions=0,
    )
    if lookup_error:
        report.errors.append(lookup_error)

    # Run objdiff
    data, objdiff_error = _run_objdiff(symbol)
    if data is None:
        report.verdict = "error"
        report.verdict_reason = f"objdiff failed: {objdiff_error}"
        report.errors.append(objdiff_error)
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
    report.contested_mismatches = sum(c.count for c in categories if c.contested)

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


def _has_functions_table(path: Path) -> bool:
    """True iff `path` is a database that actually carries a `functions` table.

    A zero-byte file passes `Path.exists()` and `sqlite3.connect()` without
    complaint — which is precisely how the old hardcoded
    `build/373307D9/decomp.db` (0 bytes on every tree checked) turned into
    `No functions found matching criteria.`
    """
    try:
        conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    except sqlite3.Error:
        return False
    try:
        row = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='functions'"
        ).fetchone()
        return row is not None
    except sqlite3.Error:
        return False
    finally:
        conn.close()


def resolve_db_path(explicit: str | Path | None = None) -> Path:
    """Find a decomp.db that has a `functions` table, or raise loudly.

    Never returns a path it has not proven usable, and never degrades into an
    empty result set.
    """
    if explicit:
        p = Path(explicit)
        if not p.exists():
            raise DatabaseUnavailableError(f"--db {p} does not exist")
        if not _has_functions_table(p):
            raise DatabaseUnavailableError(
                f"--db {p} exists ({p.stat().st_size} bytes) but has no `functions` "
                f"table — an empty/placeholder database, not an empty pool")
        return p

    tried: list[str] = []
    for rel in DB_CANDIDATES:
        p = PROJECT_DIR / rel
        if not p.exists():
            tried.append(f"{p}: missing")
            continue
        size = p.stat().st_size
        if not _has_functions_table(p):
            tried.append(f"{p}: {size} bytes, no `functions` table")
            continue
        return p

    raise DatabaseUnavailableError(
        "no usable decomp.db found (a database with a `functions` table). Tried:\n  "
        + "\n  ".join(tried)
        + "\nPass --db PATH. NOTE: this is a MISSING DATABASE, not an empty "
          "result — do not read it as 'no work exists'.")


def _query_functions(unit_pattern: str | None, min_pct: float, max_pct: float,
                     limit: int, *,
                     cov: Optional[CoverageReport] = None,
                     db_path: str | Path | None = None,
                     percent_column: str = DEFAULT_PERCENT_COLUMN) -> list[dict]:
    """Query functions from the orchestrator DB.

    ***REPAIR — see the module docstring.***  The previous query named two
    columns that do not exist on `functions` (`source_path`, `match_percent`),
    so it raised `OperationalError` on every invocation; the exception was
    swallowed into `return []` and the caller printed "No functions found
    matching criteria."  The real columns are `current_percent` and
    `match_percent_normalized`.

    `match_percent_normalized` is the default ruler because it is the one the
    single-function path already uses (`normalized_match_percent` from
    objdiff).  `--percent-column current_percent` selects the older, drifting
    work-selection index instead.  Rows where the chosen column is NULL are
    COUNTED as a named drop, never silently skipped.
    """
    if percent_column not in PERCENT_COLUMNS:
        raise QueryFailedError(
            f"unknown percent column {percent_column!r}; choose from {PERCENT_COLUMNS}")

    path = resolve_db_path(db_path)
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row
    col = percent_column          # validated against a literal allowlist above

    def count(where: str, params: list) -> int:
        sql = f"SELECT COUNT(*) FROM functions WHERE {where}"
        try:
            return conn.execute(sql, params).fetchone()[0]
        except sqlite3.Error as exc:
            # LOUD.  A schema mismatch must never look like an empty pool again.
            raise QueryFailedError(f"{exc.__class__.__name__}: {exc}\n  SQL: {sql}\n"
                                   f"  DB : {path}") from exc

    try:
        # The denominator is measured BEFORE --limit, so a capped run still
        # knows how big the pool it sampled from was.
        total = count("1", [])
        n_null = count(f"{col} IS NULL", [])
        n_below = count(f"{col} IS NOT NULL AND {col} < ?", [min_pct])
        n_above = count(f"{col} IS NOT NULL AND {col} >= ?", [max_pct])
        window = f"{col} IS NOT NULL AND {col} >= ? AND {col} < ?"
        n_window = count(window, [min_pct, max_pct])

        where = window
        params: list = [min_pct, max_pct]
        if unit_pattern:
            where += " AND unit GLOB ?"
            params = params + [unit_pattern]
        n_pool = count(where, params)

        # `ORDER BY <pct> DESC` alone left ties in an undefined order, so WHICH
        # rows a --limit kept was not reproducible.  Tie-break on symbol.
        sql = (f"SELECT symbol, demangled, unit, "
               f"       {col} AS match_percent, "
               f"       current_percent, match_percent_normalized "
               f"FROM functions WHERE {where} "
               f"ORDER BY {col} DESC, symbol ASC")
        row_params = list(params)
        if limit and limit > 0:
            sql += " LIMIT ?"
            row_params.append(limit)
        try:
            rows = [dict(r) for r in conn.execute(sql, row_params).fetchall()]
        except sqlite3.Error as exc:
            raise QueryFailedError(f"{exc.__class__.__name__}: {exc}\n  SQL: {sql}\n"
                                   f"  DB : {path}") from exc
    finally:
        conn.close()

    if cov is not None:
        cov.universe(total, f"rows in `functions` of {path}")
        cov.extra("db_path", str(path))
        cov.extra("percent_column", col)
        cov.extra("pool_size_before_limit", n_pool)
        cov.note(f"ruler = `{col}`; `--percent-column` switches it "
                 f"(the two disagree — they are different measurements)")
        cov.drop("percent-is-null", n_null, note=(
            f"{col} IS NULL — never measured on this ruler; NOT 'zero percent'"))
        cov.drop("below---min", n_below, note=f"{col} < {min_pct} (deliberate filter)")
        cov.drop("at-or-above---max", n_above, note=f"{col} >= {max_pct} (deliberate filter)")
        if unit_pattern:
            cov.drop("unit-glob-not-matched", n_window - n_pool,
                     note=f"unit does not match GLOB {unit_pattern!r}")
        cov.cap("--limit", limit, before=n_pool, after=len(rows),
                note="rows in the pool that were never analyzed")
        cov.examine(len(rows))
    return rows


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
    # 3 decimals + a `<100` guard: a rendered `100.0` has already hidden two
    # real bugs in this project (99.97 rounding up).
    lines.append(f"  Match:      {_fmt_pct(report.match_percent)}%")
    lines.append(f"  Ceiling:    {_fmt_pct(report.ceiling_percent)}%")
    lines.append(f"  Headroom:   {report.headroom:.3f}%")
    lines.append(f"  Verdict:    {report.verdict} — {report.verdict_reason}")
    if report.workability_score > 0:
        lines.append(f"  Score:      {report.workability_score:.1f}")
    lines.append(f"  Instructions: {report.total_instructions}")
    if report.errors:
        for e in report.errors:
            lines.append(f"  !! error:   {e}")
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
    lines.append(f"{'Match%':>8} {'Ceiling':>8} {'Room':>7} {'Fix':>4} {'Unfix':>5} "
                 f"{'Score':>6} {'Verdict':10} Symbol")
    lines.append(f"{'─' * 8} {'─' * 8} {'─' * 7} {'─' * 4} {'─' * 5} "
                 f"{'─' * 6} {'─' * 10} {'─' * 50}")
    for r in reports:
        symbol_short = r.demangled[:50] if r.demangled else r.symbol[:50]
        # `{:6.1f}` rendered 99.97 as `100.0` here — the exact surface named in
        # docs/decomp/patterns/rounded-100-hides-real-bugs.md.
        lines.append(
            f"{_fmt_pct(r.match_percent, 7)}% {_fmt_pct(r.ceiling_percent, 7)}% "
            f"{r.headroom:6.3f}% {r.fixable_mismatches:4d} {r.unfixable_mismatches:5d} "
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
    parser.add_argument(
        "--limit", type=int, default=50,
        help="Max functions to ANALYZE (default: 50, unchanged). This truncates "
             "the analysis, not just the printout: --top N ranks the N best of "
             "these 50, not of the pool. The pool size is now always reported and "
             "a capped run exits 3 (TRUNCATED). Use --limit 0 for a full census, "
             "or --allow-truncation to accept the sample.")
    parser.add_argument("--top", type=int, help="Show top N most workable functions")
    parser.add_argument("--json", action="store_true", help="JSON output")
    parser.add_argument("--db", default=None,
                        help="Path to decomp.db. Default: the first of "
                             + ", ".join(DB_CANDIDATES)
                             + " that actually contains a `functions` table.")
    parser.add_argument("--percent-column", default=DEFAULT_PERCENT_COLUMN,
                        choices=list(PERCENT_COLUMNS),
                        help=f"Which DB column is the ruler (default: "
                             f"{DEFAULT_PERCENT_COLUMN}). The old code named a "
                             f"`match_percent` column that does not exist, so "
                             f"every batch query raised and returned nothing.")
    add_coverage_args(parser)
    args = parser.parse_args()

    if args.symbol:
        # Single function mode
        report = analyze_function(args.symbol)
        if args.json:
            print(json.dumps(asdict(report), indent=2))
        else:
            print(_format_report(report))
        sys.exit(2 if report.verdict == "error" else 0)
    else:
        # Batch mode
        cov = CoverageReport("function_health.batch", args=args)
        try:
            functions = _query_functions(
                args.unit, args.min, args.max, args.limit,
                cov=cov, db_path=args.db, percent_column=args.percent_column,
            )
        except (DatabaseUnavailableError, QueryFailedError) as exc:
            # LOUD.  This is the whole point: the old handler turned exactly
            # this into "No functions found matching criteria." + exit 1.
            print(f"error: batch query could not run — this is NOT an empty pool:\n"
                  f"{exc}", file=sys.stderr)
            sys.exit(2)

        pool = cov.as_dict().get("pool_size_before_limit", len(functions))
        if not functions:
            print(f"No functions matched the criteria "
                  f"(pool={pool}, universe={cov.as_dict()['universe']} rows in "
                  f"`functions`). The query RAN — see the COVERAGE block for what "
                  f"was filtered.", file=sys.stderr)
            cov.emit()
            sys.exit(1)

        print(f"Analyzing {len(functions)} of {pool} functions in the pool "
              f"({cov.as_dict()['universe']} rows in the DB)...", file=sys.stderr)

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

        # Sort by workability score, with a full tie-break so two runs over the
        # same DB emit the same ordering.
        reports.sort(key=lambda r: (-r.workability_score, -r.match_percent, r.symbol))

        all_reports = list(reports)      # the full analyzed set, never sliced
        analyzed_total = len(all_reports)
        top_hidden = 0
        if args.top:
            # This truncates a DISPLAY of an already-analyzed set, so it is a
            # note rather than a cap — but it must still print its remainder.
            top_hidden = max(0, len(reports) - args.top)
            reports = reports[:args.top]
            if top_hidden:
                cov.note(f"--top {args.top} hides {top_hidden} of {analyzed_total} "
                         f"ANALYZED functions from the output (they were analyzed, "
                         f"just not printed)")

        if args.json:
            payload = {
                "reports": [asdict(r) for r in reports],
                "analyzed_total": analyzed_total,
                "reports_emitted": len(reports),
                "pool_size_before_limit": pool,
                "_coverage": cov.as_dict(),
            }
            # Back-compat: the old JSON was a bare list. Emit the list on
            # stdout and the accounting on stderr.
            print(json.dumps(payload["reports"], indent=2))
            print(json.dumps({k: v for k, v in payload.items() if k != "reports"},
                             indent=2), file=sys.stderr)
        else:
            # Summary stats over EVERY analyzed report, not the --top slice.
            # Counting them over `reports` (already truncated by --top) printed
            # `Analyzed: 3` above `Errors: 2` for three all-error functions —
            # the same "sample under a total's label" shape this pass exists to
            # remove.
            workable = [r for r in all_reports if r.verdict == "workable"]
            marginal = [r for r in all_reports if r.verdict == "marginal"]
            at_limit = [r for r in all_reports if r.verdict == "at_limit"]
            complete = [r for r in all_reports if r.verdict == "complete"]
            errors = [r for r in all_reports if r.verdict == "error"]
            other = [r for r in all_reports
                     if r.verdict not in ("workable", "marginal", "at_limit",
                                          "complete", "error")]

            print(f"\nFunction Health Summary")
            print(f"{'=' * 60}")
            # `Analyzed: {len(reports)}` used to be the --limit (50) with no
            # denominator: a sample presented as a total.
            print(f"  Analyzed:   {analyzed_total} of {pool} in the pool "
                  f"(DB holds {cov.as_dict()['universe']} rows)")
            if top_hidden:
                print(f"  Shown:      {len(reports)}  "
                      f"(--top {args.top}; {top_hidden} analyzed but not printed)")
            if pool > analyzed_total:
                print(f"  !! NOT ANALYZED: {pool - analyzed_total} pool rows were "
                      f"never looked at (--limit {args.limit}). Do NOT read this "
                      f"summary as a census of the pool.")
            print(f"  Workable:   {len(workable)}")
            print(f"  Marginal:   {len(marginal)}")
            print(f"  AT_LIMIT:   {len(at_limit)}")
            print(f"  Complete:   {len(complete)}")
            if errors:
                print(f"  Errors:     {len(errors)}")
            if other:
                print(f"  Unclassified: {len(other)}  "
                      f"(verdicts: {sorted({r.verdict for r in other})})")
            bucketed = (len(workable) + len(marginal) + len(at_limit)
                        + len(complete) + len(errors) + len(other))
            if bucketed != analyzed_total:
                print(f"  !! {analyzed_total - bucketed} analyzed functions fell into "
                      f"NO bucket — the summary does not add up")
            print()

            if workable or marginal:
                candidates = workable + marginal
                show = candidates[:args.top or 30]
                print(_format_batch_table(show))
                if len(candidates) > len(show):
                    print(f"  ... and {len(candidates) - len(show)} more "
                          f"workable/marginal functions not shown "
                          f"(showing {len(show)} of {len(candidates)})")

        sys.exit(cov.emit())


if __name__ == "__main__":
    main()
