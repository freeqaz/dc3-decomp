#!/usr/bin/env python3
"""Batch-check all untracked functions in a unit.

Runs objdiff on each, auto-reports 100% matches as COMPLETE.
Returns summary with counts and partial-match details.

WRITES: this script updates `functions.current_percent`, `verdict` and
`is_stub`.  `--dry-run` makes it read-only.

Usage:
    python3 scripts/batch_check.py 'system/char/*'
    python3 scripts/batch_check.py 'system/rndobj/Text' --dry-run
    python3 scripts/batch_check.py 'system/*' --skip-boilerplate
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

# Add project root to path
PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from orchestrator.database import (
    get_connection,
    normalize_unit_pattern,
    update_function_status,
    BOILERPLATE_SYMBOL_PREFIXES,
    DEFAULT_EXCLUDE_PATTERNS,
)
from orchestrator.mcp_server import _demangle_itanium_to_qualified
from scripts.analysis.coverage import CoverageReport, add_coverage_args

DB_PATH = str(PROJECT_ROOT / "decomp.db")
OBJDIFF_CLI = PROJECT_ROOT / "bin" / "objdiff-cli"


def _round_pct(v: float | None) -> float | None:
    """Two decimals, except that rounding may never REACH 100 from below.

    Copied from scripts/sync_match_percent.py's `_round_pct`, for the same
    reason: the stored value is a GATE, not just a display -- `--promote` and
    `--demote` compare it against 100.  `round(99.9967, 2)` is `100.0`, and
    nine functions once carried a COMPLETE cert while measurably not matching.
    """
    if v is None:
        return None
    r = round(v, 2)
    if r >= 100.0 and v < 100.0:
        return 99.99
    return r


def fmt_pct(v: float | None, decimals: int = 1) -> str:
    """Render a percentage that can never round up across the 100 boundary."""
    if v is None:
        return "n/a"
    r = round(v, decimals)
    if r >= 100.0 and v < 100.0:
        r = 100.0 - 10.0 ** (-decimals)
    return f"{r:.{decimals}f}"


def match_percent_from_diff(data: dict) -> tuple[float | None, str]:
    """(percent, ruler) from one `objdiff-cli diff -f json` payload.

    RULER.  `objdiff-cli diff` emits THREE numbers and they mean different
    things:

      normalized_match_percent  the canonical scorer (objdiff-cli
                                diff.rs:1263).  Note that the `diff` command
                                also copies this value into the key literally
                                named `fuzzy_match_percent` (diff.rs:1262), so
                                on THIS payload the two agree -- unlike
                                report.json, where `fuzzy_match_percent` is the
                                RAW scorer and a separate
                                `match_percent_normalized` key carries the
                                canonical one.  Same key name, two rulers,
                                two files.
      raw_match_percent         relocation-sensitive.
      instruction_summary.equal_percent
                                the fraction of instructions that compared
                                EQUAL.  It is not a match percent at all: it
                                counts a `diff_arg` instruction as unequal
                                while the scorer charges it partially.  On
                                ObjectDir::Save the two read 99.67 vs 99.98.

    This script used to prefer `equal_percent` over everything, so the number
    it wrote into `current_percent` -- and gated COMPLETE on -- was a third
    ruler, agreeing with neither sync_objdiff.py nor sync_match_percent.py.
    """
    n = data.get("normalized_match_percent")
    if n is not None:
        return float(n), "normalized"
    f = data.get("fuzzy_match_percent")
    if f is not None:
        # On a `diff` payload this key already holds the normalized value.
        return float(f), "normalized-via-fuzzy-key"
    return None, "none"


BUCKETS = ("stub", "complete", "partial", "zero")


def bucket_for(match_pct: float | None, classification: str, is_stub: bool) -> str:
    """Which bucket a checked function belongs in.  TOTAL over its domain.

    The previous chain ended at `elif match_pct > 0:`, so a checked, non-stub
    function at EXACTLY 0% -- "we compiled a body and it matches nothing" --
    landed in no bucket at all while still incrementing `checked`.  The buckets
    therefore never summed to `checked`, and the tier was invisible.  This
    function is total by construction: every input returns one of BUCKETS.
    """
    if is_stub:
        return "stub"
    pct = 0.0 if match_pct is None else float(match_pct)
    if pct >= 100.0 or classification == "COMPLETE":
        return "complete"
    if pct > 0:
        return "partial"
    return "zero"


def batch_check(unit_pattern: str, dry_run: bool = False, skip_boilerplate: bool = False,
                cov: CoverageReport = None, db_path: str = None) -> str:
    """Run batch check and return formatted results."""
    db_path = db_path or DB_PATH
    conn = get_connection(db_path)
    norm_pattern = normalize_unit_pattern(unit_pattern)

    query = """
        SELECT id, symbol, demangled, unit, current_percent
        FROM functions
        WHERE unit GLOB ?
          AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
          AND symbol NOT LIKE 'merged~_%' ESCAPE '~'
    """
    params: list = [norm_pattern]

    for ep in DEFAULT_EXCLUDE_PATTERNS:
        norm_ep = normalize_unit_pattern(ep)
        query += " AND unit NOT GLOB ?"
        params.append(norm_ep)

    boilerplate_skipped_note = ""
    if skip_boilerplate:
        # ESCAPE the '_' in boilerplate prefixes (??__F, ??__E, ??_9, ??_E,
        # ??_G): SQL LIKE treats '_' as a single-char wildcard, so an
        # unescaped '??__E%' over-matches operator overloads (??Y..., ??A...)
        # and hid 146 real authorable rows here (wave-9 measurement bug).
        # Mirror orchestrator.database.query_functions's escaped form.
        #
        # This escaping is CORRECT as written and must stay: `\_` is a literal
        # underscore under `ESCAPE '\'`, and the `merged~_%` filter above uses
        # the same trick with `~`.  It is the one SQL LIKE in this file and it
        # does not have the certify_floor defect.
        for prefix in BOILERPLATE_SYMBOL_PREFIXES:
            escaped = prefix.replace("_", r"\_")
            query += f" AND symbol NOT LIKE '{escaped}%' ESCAPE '\\'"
        boilerplate_skipped_note = " (--skip-boilerplate active)"

    # Deterministic order: without ORDER BY, SQLite is free to return rows in
    # any order it likes, so two runs could print two different listings.
    query += " ORDER BY unit ASC, symbol ASC, id ASC"

    rows = conn.execute(query, params).fetchall()
    functions = [dict(row) for row in rows]

    # The DENOMINATOR of this scan: how many rows the pattern selected, and how
    # many rows the pattern would have selected without the exclusion filters.
    total_in_pattern = conn.execute(
        "SELECT count(*) FROM functions WHERE unit GLOB ?", [norm_pattern]
    ).fetchone()[0]
    if cov is not None:
        cov.universe(total_in_pattern,
                     f"rows whose unit matches {unit_pattern!r} (before any filter)")
        cov.drop("already-COMPLETE-or-AT_LIMIT-or-excluded-or-merged-or-boilerplate",
                 total_in_pattern - len(functions),
                 note="removed by the WHERE clause: verdict filter, "
                      "DEFAULT_EXCLUDE_PATTERNS, merged_ symbols"
                      + boilerplate_skipped_note)

    if not functions:
        if cov is not None:
            cov.note("no unchecked functions matched; nothing was run")
        return (f"No unchecked functions found for pattern: {unit_pattern}\n"
                f"(the pattern itself matches {total_in_pattern} rows; all of them "
                f"were removed by the verdict/exclusion filters)")

    if not OBJDIFF_CLI.exists():
        if cov is not None:
            cov.drop("objdiff-cli-missing", len(functions),
                     note=f"binary not found at {OBJDIFF_CLI}")
        return f"Error: objdiff-cli not found at {OBJDIFF_CLI}"

    checked = 0
    newly_complete = 0
    unimplemented = 0
    partial = []
    zero_percent = []          # the tier that used to fall through every branch
    failed = []
    errors = []
    complete_by_classification_only = []   # verdict=COMPLETE granted with pct < 100
    ruler_counts: dict[str, int] = {}
    equal_pct_disagreements = 0

    for func in functions:
        symbol = func["symbol"]

        if symbol.startswith("merged_"):
            # Belt-and-braces: the SQL already excludes these.  Counted rather
            # than silently skipped so the arithmetic still balances if the
            # query ever stops filtering them.
            if cov is not None:
                cov.drop("merged-symbol", note="ICF-folded name, not a real function")
            continue

        lookup_symbol = symbol
        demangled = _demangle_itanium_to_qualified(symbol)
        if demangled is not None:
            lookup_symbol = demangled

        counted = False   # has this row been examine()d yet?
        try:
            result = subprocess.run(
                [str(OBJDIFF_CLI), "diff", "-p", str(PROJECT_ROOT),
                 lookup_symbol, "--build", "--verdict", "-f", "json"],
                capture_output=True, text=True, timeout=90,
                cwd=str(PROJECT_ROOT),
            )

            if result.returncode != 0 or "Symbol not found" in result.stdout:
                failed.append(symbol)
                if cov is not None:
                    cov.drop("objdiff-symbol-not-found",
                             note="objdiff-cli returned non-zero or 'Symbol not found'")
                continue

            stdout = result.stdout
            json_start = stdout.find("{")
            if json_start > 0:
                stdout = stdout[json_start:]

            data = json.loads(stdout)
            checked += 1
            counted = True
            if cov is not None:
                cov.examine()

            # RULER: canonical normalized percent, NOT instruction-equality%.
            # See `match_percent_from_diff` for why the old `equal_percent`
            # preference was a third, incompatible ruler in the same column.
            instr_summary = data.get("instruction_summary", {})
            equal_pct = instr_summary.get("equal_percent")
            match_pct, ruler = match_percent_from_diff(data)
            ruler_counts[ruler] = ruler_counts.get(ruler, 0) + 1
            if match_pct is None:
                match_pct = 0.0
            if equal_pct is not None and abs(float(equal_pct) - match_pct) > 0.5:
                equal_pct_disagreements += 1

            verdict_data = data.get("verdict", {})
            classification = verdict_data.get("classification", "")

            base_size = data.get("base_size", 0)
            target_size = data.get("target_size", 0)
            diff_score = data.get("diff_score", {})
            max_score = diff_score.get("max_score", 0) if diff_score else 0
            is_stub = (classification == "STUB" or (base_size == 0 and target_size > 0)) and max_score == 0

            bucket = bucket_for(match_pct, classification, is_stub)
            if bucket == "stub":
                unimplemented += 1
                if not dry_run:
                    conn.execute(
                        "UPDATE functions SET is_stub = 1, updated_at = CURRENT_TIMESTAMP WHERE id = ?",
                        (func["id"],),
                    )
                    conn.commit()
            elif bucket == "complete":
                newly_complete += 1
                if match_pct < 100.0:
                    # COMPLETE granted by objdiff's `classification` alone, with
                    # a measured percent BELOW 100.  The verdict decision is
                    # unchanged (that is a behaviour question, not an honesty
                    # one), but it is now counted -- and the percent written is
                    # the MEASURED one, not a hardcoded 100.0 that made the row
                    # indistinguishable from a real match forever after.
                    complete_by_classification_only.append({
                        "symbol": symbol,
                        "demangled": func.get("demangled", ""),
                        "percent": match_pct,
                    })
                if not dry_run:
                    update_function_status(
                        function_id=func["id"],
                        current_percent=_round_pct(match_pct),
                        verdict="COMPLETE",
                        db_path=db_path,
                    )
            elif bucket == "partial":
                partial.append({
                    "symbol": symbol,
                    "demangled": func.get("demangled", ""),
                    "percent": match_pct,
                })
                if not dry_run:
                    update_function_status(
                        function_id=func["id"],
                        current_percent=_round_pct(match_pct),
                        db_path=db_path,
                    )
            else:
                # match_pct == 0 and not a stub: a function we DID compile whose
                # body scores nothing against the target.  `elif match_pct > 0`
                # left this row in no bucket at all -- not complete, not
                # partial, not unimplemented, not failed -- while still counting
                # it in `checked`, so the buckets never summed to the total and
                # the "we wrote a body but it matches nothing" tier was
                # invisible.  It is its own bucket now.  No DB write is added
                # here: that would be a behaviour change, and the reporting is
                # the honesty fix.
                zero_percent.append({
                    "symbol": symbol,
                    "demangled": func.get("demangled", ""),
                    "percent": match_pct,
                    "classification": classification,
                })

        except subprocess.TimeoutExpired:
            errors.append(f"{symbol}: timeout")
            if cov is not None and not counted:
                cov.drop("objdiff-timeout")
        except json.JSONDecodeError:
            errors.append(f"{symbol}: invalid JSON output")
            if cov is not None and not counted:
                cov.drop("objdiff-invalid-json")
        except Exception as e:
            errors.append(f"{symbol}: {e}")
            if cov is not None and not counted:
                cov.drop("scanner-exception", note=str(type(e).__name__))

    # Format summary.  Every bucket, and they SUM to `checked` -- which is the
    # arithmetic the missing zero-percent branch used to break.
    bucket_sum = newly_complete + len(partial) + unimplemented + len(zero_percent)
    mode = " (DRY RUN)" if dry_run else ""
    output = f"## Batch Check Results{mode}\n\n"
    output += f"**Pattern:** `{unit_pattern}`\n"
    output += (f"**Selected:** {len(functions)} of {total_in_pattern} rows matching the "
               f"pattern ({total_in_pattern - len(functions)} removed by the "
               f"verdict/exclusion filters)\n")
    output += (f"**Checked:** {checked} | **Newly COMPLETE:** {newly_complete} | "
               f"**Partial:** {len(partial)} | **Zero%:** {len(zero_percent)} | "
               f"**Unimplemented:** {unimplemented} | **Failed:** {len(failed)} | "
               f"**Errors:** {len(errors)}\n")
    output += (f"**Bucket sum:** {bucket_sum} of {checked} checked"
               + ("" if bucket_sum == checked
                  else f"  <-- MISMATCH: {checked - bucket_sum} checked rows are in no bucket")
               + "\n")
    if ruler_counts:
        output += (f"**Ruler:** " + ", ".join(f"{k}={v}" for k, v in sorted(ruler_counts.items()))
                   + f"; instruction equal_percent disagreed with it by >0.5pp on "
                     f"{equal_pct_disagreements} of {checked} rows\n")

    if complete_by_classification_only:
        output += (f"\n### COMPLETE granted by objdiff classification alone "
                   f"({len(complete_by_classification_only)} of {newly_complete})\n\n")
        output += ("These scored BELOW 100 but objdiff's verdict said COMPLETE. The "
                   "verdict is unchanged; the percent written is now the measured one, "
                   "not a hardcoded 100.0.\n\n")
        for p in sorted(complete_by_classification_only,
                        key=lambda x: (-x["percent"], x["symbol"])):
            output += f"- `{p['symbol']}` ({p['demangled']}) — {fmt_pct(p['percent'])}%\n"

    if partial:
        output += f"\n### Partial Matches ({len(partial)})\n\n"
        partial.sort(key=lambda x: (-x["percent"], x["symbol"]))
        for p in partial:
            output += f"- `{p['symbol']}` ({p['demangled']}) — {fmt_pct(p['percent'])}%\n"

    if zero_percent:
        output += (f"\n### Zero-percent, non-stub ({len(zero_percent)})\n\n"
                   "A body exists but scores 0 against the target. Previously these "
                   "fell through every branch and were counted only in `Checked`.\n\n")
        for p in sorted(zero_percent, key=lambda x: x["symbol"]):
            output += (f"- `{p['symbol']}` ({p['demangled']}) — "
                       f"{fmt_pct(p['percent'])}% [{p['classification'] or 'no classification'}]\n")

    if failed and len(failed) <= 20:
        output += f"\n### Not Found ({len(failed)})\n\n"
        for f in sorted(failed):
            output += f"- `{f}`\n"
    elif failed:
        output += (f"\n### Not Found: {len(failed)} symbols "
                   f"(over the 20-symbol listing threshold; the COUNT above is complete)\n")

    if errors:
        output += f"\n### Errors ({len(errors)}, showing first {min(10, len(errors))})\n\n"
        for e in sorted(errors)[:10]:
            output += f"- {e}\n"

    if cov is not None:
        cov.extra("checked", checked)
        cov.extra("bucket_sum", bucket_sum)
        cov.extra("newly_complete", newly_complete)
        cov.extra("complete_by_classification_only",
                  len(complete_by_classification_only))
        cov.extra("zero_percent_non_stub", len(zero_percent))
        cov.extra("ruler_counts", dict(sorted(ruler_counts.items())))
        cov.extra("equal_percent_disagreements", equal_pct_disagreements)
        cov.note("ruler: normalized_match_percent from objdiff-cli diff "
                 "(NOT instruction_summary.equal_percent, which this script "
                 "used to prefer)")

    return output


def main():
    parser = argparse.ArgumentParser(description="Batch-check functions in a unit")
    parser.add_argument("unit_pattern", help="Unit glob pattern (e.g., 'system/char/*')")
    parser.add_argument("--dry-run", action="store_true", help="Check but don't update DB")
    parser.add_argument("--skip-boilerplate", action="store_true", help="Skip atexit/MakeString/thunks")
    parser.add_argument("--db", default=DB_PATH,
                        help=f"decomp.db to read/write (default: {DB_PATH})")
    add_coverage_args(parser)
    args = parser.parse_args()

    cov = CoverageReport("batch_check", args=args)
    result = batch_check(args.unit_pattern, dry_run=args.dry_run,
                         skip_boilerplate=args.skip_boilerplate, cov=cov,
                         db_path=args.db)
    print(result)
    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
