"""Bulk reclassification of AT_LIMIT functions.

Scans AT_LIMIT functions, runs objdiff + diagnosis, classifies fixable vs
unfixable patterns, and updates DB verdicts so query_functions surfaces
fixable ones as workable targets.

THIS TOOL WRITES TO decomp.db. Read the funnel before you believe its numbers.
==============================================================================
It used to print exactly one count — `1517 candidates` — for a funnel that
started at 3,796 AT_LIMIT rows:

    3,796 AT_LIMIT
      → 1,701  after the SQL WHERE (a NULL-comparison band silently ate 1,231
               rows whose current_percent IS NULL; `--max-pct 99.9` ate the 27
               rows above 99.9 — precisely the rounded-100 population this
               project's own docs say hides real bugs)
      → 1,517  after three uncounted Python `continue`s: no source_path (184),
               source file missing, and DEMANGLER PARSE FAILURE — the last of
               which is the accidental-blindness class, since a symbol we
               cannot demangle is exactly the symbol nobody has looked at.

Every one of those is now counted and printed (see `scripts/analysis/coverage.py`).

Usage:
    python -m scripts.analysis.reclassify_at_limit                         # Dry run, all AT_LIMIT
    python -m scripts.analysis.reclassify_at_limit --apply                 # Actually update DB
    python -m scripts.analysis.reclassify_at_limit --unit 'system/char/*'  # Filter by unit
    python -m scripts.analysis.reclassify_at_limit --min-pct 90            # Filter by match %
    python -m scripts.analysis.reclassify_at_limit --json -o report.json   # JSON output
    python -m scripts.analysis.reclassify_at_limit --include-null-percent  # + the 1,231 NULL rows
    python -m scripts.analysis.reclassify_at_limit --skip-excluded         # - the 2,178 excluded=1 rows
"""

from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

from decomp_synth.diagnosis import diagnose_baseline, is_all_noise
from decomp_synth.types import extract_qualified_name
from decomp_synth.batch_triage import (
    classify,
    build_object,
    run_objdiff,
    load_unit_source_map,
)

# Repo root
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DECOMP_DB = REPO_ROOT / "decomp.db"

sys.path.insert(0, str(REPO_ROOT))
from scripts.analysis.coverage import CoverageReport, add_coverage_args, like_escape  # noqa: E402


def provenance_lines() -> list[str]:
    """Name the DB we WRITE and the tree we MEASURE. They can be different repos.

    `DECOMP_DB` is derived from this file's location, but `batch_triage` builds
    and diffs against `decomp_synth`'s own repo-root resolution (CWD walk /
    $DECOMP_SYNTH_REPO). Run this file from a worktree with $DECOMP_SYNTH_REPO
    pointing elsewhere and it will measure one tree and write verdicts into
    another's database, with nothing in the output to say so.
    """
    lines = [f"DB WRITTEN    : {DECOMP_DB}",
             f"              (derived from {Path(__file__).resolve()})"]
    try:
        from decomp_synth.project import get_project_config
        measured = Path(get_project_config().repo_root).resolve()
    except Exception as exc:                       # never let disclosure crash the tool
        lines.append(f"TREE MEASURED : UNRESOLVED ({exc})")
        return lines
    lines.append(f"TREE MEASURED : {measured}   (decomp_synth repo_root — "
                 f"builds + objdiff run here)")
    env = os.environ.get("DECOMP_SYNTH_REPO")
    if env:
        lines.append(f"              $DECOMP_SYNTH_REPO={env}")
    if measured != REPO_ROOT.resolve():
        lines.append("  ⚠⚠ THESE ARE DIFFERENT REPOS. Verdicts measured in one tree "
                     "would be written into another's decomp.db. Refusing is not this "
                     "tool's job, but do not --apply until you know why.")
    return lines


def ruler_lines() -> list[str]:
    """Which ruler the verdicts written to `verdict_reason` were measured on.

    `batch_triage` → `project._RELOC_DIFF_MODE_DEFAULT = "none"`, the LOOSEST
    ruler there is, while the grader uses `name_check`. Per `ruler.py`: a `none`
    percentage is an UPPER BOUND — "a row it calls 100% can still be withholding
    its bytes". These verdicts get persisted, so the ruler goes in the row.
    """
    try:
        from decomp_synth.project import reloc_diff_mode
        mode = reloc_diff_mode()
    except Exception:
        mode = "unknown"
    out = [f"RULER         : functionRelocDiffs={mode}  (decomp_synth default; "
           f"$DECOMP_SYNTH_RELOC_DIFF_MODE overrides)"]
    try:
        from scripts.analysis.ruler import graded_ruler
        g = graded_ruler(REPO_ROOT)
        out.append(f"              grader uses: {g.reloc_mode} ({g.source})")
        if mode != g.reloc_mode:
            out.append(f"  ⚠ verdicts below are NOT on the graded ruler. `{mode}` ignores "
                       f"relocation NAMES, so it is an UPPER BOUND on the graded score: a "
                       f"row it calls 100% can still be withholding its bytes.")
    except Exception:
        pass
    return out


def ruler_tag() -> str:
    """Short ruler token embedded in every persisted `verdict_reason`."""
    try:
        from decomp_synth.project import reloc_diff_mode
        return f"reloc={reloc_diff_mode()}"
    except Exception:
        return "reloc=unknown"


@dataclass
class ReclassifyResult:
    """Result of reclassifying a single function."""

    symbol: str
    demangled: str
    unit: str
    current_percent: float
    category: str  # NOISE_ONLY, REGSWAP_ONLY, REGSWAP_PLUS, STRUCTURAL, UNFIXABLE, MIXED, ERROR
    action: str  # REOPEN, KEEP, ERROR
    verdict_reason: str
    diff_op_count: int = 0
    cluster_count: int = 0
    gpr_swap_count: int = 0
    total_instructions: int = 0
    error: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reclassify AT_LIMIT functions: find fixable ones and reopen them.",
    )
    parser.add_argument(
        "--apply", action="store_true",
        help="Actually update the database (default: dry run)",
    )
    parser.add_argument(
        "--unit", type=str, default=None,
        help="Filter by unit glob pattern (e.g. 'system/char/*')",
    )
    parser.add_argument(
        "--min-pct", type=float, default=0,
        help="Minimum match percentage (default: 0)",
    )
    parser.add_argument(
        "--max-pct", type=float, default=99.9,
        help="Maximum match percentage (default: 99.9 — UNCHANGED. This drops the 27 "
             "AT_LIMIT rows above 99.9, i.e. exactly the rounded-100 band that "
             "docs/decomp/patterns/rounded-100-hides-real-bugs.md says hides real "
             "bugs. Pass --max-pct 100 to include them.)",
    )
    parser.add_argument(
        "--include-null-percent", action="store_true",
        help="Also process AT_LIMIT rows whose current_percent IS NULL (1,231 of 3,796 "
             "on this tree). A SQL NULL comparison is NULL, so the --min-pct/--max-pct "
             "band used to drop these without counting them.",
    )
    parser.add_argument(
        "--skip-excluded", action="store_true",
        help="Drop rows with excluded=1 (2,178 of 3,796 AT_LIMIT rows). DEFAULT IS OFF, "
             "i.e. the historical behaviour: excluded rows ARE eligible for --apply.",
    )
    parser.add_argument(
        "--limit", type=int, default=0,
        help="Max functions to process (0 = unlimited). Truncates the ANALYSIS: capped "
             "candidates are never diagnosed and never reclassified.",
    )
    parser.add_argument(
        "-o", "--output", type=Path, default=None,
        help="Output JSON file (default: none)",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output",
        help="Output JSON to stdout",
    )
    add_coverage_args(parser)
    return parser.parse_args()


def query_at_limit_functions(
    unit_source_map: dict[str, str],
    unit_pattern: str | None,
    min_pct: float,
    max_pct: float,
    limit: int,
    cov: CoverageReport | None = None,
    include_null_percent: bool = False,
    skip_excluded: bool = False,
) -> list[dict]:
    """Query decomp.db for AT_LIMIT functions matching filters.

    Every filter that used to live in the SQL WHERE now runs in Python against
    the FULL AT_LIMIT population, so each one can be counted. The surviving set
    is identical; what changes is that the denominator is knowable.

    The three prefix exclusions the SQL carried (`merged\\_%`, `fn\\_%`,
    `%stlpmtx\\_std::%`) WERE correctly escaped with ``ESCAPE '\\'`` — that is the
    right spelling, and it is preserved here as literal prefix/substring tests.
    The `unit LIKE ?` glob was NOT escaped; it is escaped below.
    """
    conn = sqlite3.connect(f"file:{Path(DECOMP_DB).resolve()}?mode=ro", uri=True)
    conn.row_factory = sqlite3.Row

    rows = conn.execute("""
        SELECT symbol, demangled, unit, current_percent, verdict, excluded
        FROM functions
        WHERE verdict = 'AT_LIMIT'
        ORDER BY symbol ASC
    """).fetchall()
    conn.close()

    if cov is not None:
        cov.universe(len(rows), "rows with verdict='AT_LIMIT' in decomp.db")
        n_excl = sum(1 for r in rows if r["excluded"])
        cov.extra("at_limit_rows_excluded_1", n_excl)
        cov.extra("at_limit_rows_null_current_percent",
                  sum(1 for r in rows if r["current_percent"] is None))
        if n_excl and not skip_excluded:
            cov.note(f"{n_excl} of {len(rows)} AT_LIMIT rows have excluded=1 and are "
                     f"STILL ELIGIBLE FOR UPDATE (this tool has no `excluded = 0` filter, "
                     f"unlike ceiling_calculator.py). Pass --skip-excluded to drop them.")

    # `unit LIKE ?` had no ESCAPE, so a `_` in the pattern matched any character
    # (over-match only, never under-match).  Escape the LITERAL first, then turn
    # the caller's `*` glob into `%` — order matters or the glob gets escaped too.
    like_pattern = ""
    like_alt = ""
    if unit_pattern:
        like_pattern = like_escape(unit_pattern).replace("*", "%")
        if not unit_pattern.startswith("default/"):
            like_alt = like_escape("default/") + like_pattern

    def _unit_matches(unit: str) -> bool:
        if not like_pattern:
            return True
        for pat in (p for p in (like_pattern, like_alt) if p):
            if _LIKE_DB.execute("SELECT ? LIKE ? ESCAPE '\\'", (unit, pat)).fetchone()[0]:
                return True
        return False

    candidates = []
    for row in rows:
        row_dict = dict(row)
        unit = row_dict["unit"] or ""
        symbol = row_dict["symbol"] or ""
        demangled = row_dict.get("demangled") or ""

        if skip_excluded and row_dict.get("excluded"):
            if cov is not None:
                cov.drop("excluded-row", note="functions.excluded = 1 (--skip-excluded)")
            continue

        cp = row_dict["current_percent"]
        if cp is None:
            # SQL `current_percent >= ? AND current_percent <= ?` yields NULL for
            # these, so they vanished from the band without ever being counted.
            if not include_null_percent:
                if cov is not None:
                    cov.drop("null-current-percent",
                             note="NULL fails the SQL band silently; "
                                  "--include-null-percent keeps them")
                continue
        else:
            if cp < min_pct:
                if cov is not None:
                    cov.drop("below--min-pct")
                continue
            if cp > max_pct:
                if cov is not None:
                    cov.drop("above--max-pct",
                             note=f"--max-pct {max_pct}; at the 99.9 default this is the "
                                  f"ROUNDED-100 band that docs/decomp/patterns/"
                                  f"rounded-100-hides-real-bugs.md is about")
                continue

        if symbol.startswith("merged_"):
            if cov is not None:
                cov.drop("merged-symbol", note="ICF-folded alias")
            continue
        if symbol.startswith("fn_"):
            if cov is not None:
                cov.drop("fn_-shape", note="unnamed/EH-funclet shape")
            continue
        if "stlpmtx_std::" in demangled:
            if cov is not None:
                cov.drop("stlpmtx-std", note="STLport internals")
            continue

        if not _unit_matches(unit):
            if cov is not None:
                cov.drop("unit-pattern-excluded")
            continue

        source_path = unit_source_map.get(unit)
        if not source_path:
            if cov is not None:
                cov.drop("no-source-path",
                         note="unit absent from objdiff.json's unit→source map")
            continue

        if not Path(REPO_ROOT / source_path).exists():
            if cov is not None:
                cov.drop("source-file-missing",
                         note="objdiff.json names a source_path that is not on disk")
            continue

        qualified_name = extract_qualified_name(demangled or "")
        if not qualified_name:
            # THE ACCIDENTAL-BLINDNESS CLASS.  A symbol whose demangled name the
            # parser cannot handle is, almost by definition, a symbol nobody has
            # looked at.  Dropping it uncounted is how a pool gets called
            # exhausted while its hardest rows were never in it.
            if cov is not None:
                cov.drop("demangler-parse-failure",
                         note="extract_qualified_name() returned nothing — these are "
                              "NOT known-good rows, they are unparsed ones")
            continue

        row_dict["source_path"] = source_path
        row_dict["qualified_name"] = qualified_name
        candidates.append(row_dict)

    # Deterministic total order: percent DESC (NULLs last), symbol, unit.
    candidates.sort(key=lambda d: (d["current_percent"] is None,
                                   -(d["current_percent"] or 0.0),
                                   d["symbol"] or "",
                                   d.get("unit") or ""))

    if limit > 0 and len(candidates) > limit:
        if cov is not None:
            cov.cap("--limit", limit, before=len(candidates), after=limit,
                    note="these candidates were NEVER diagnosed and never reclassified")
        candidates = candidates[:limit]
    elif cov is not None:
        cov.cap("--limit", limit or 0, before=len(candidates), after=len(candidates))

    if cov is not None:
        cov.examine(len(candidates))

    return candidates


# A throwaway in-memory connection used only to evaluate SQL LIKE with SQLite's
# own semantics (ASCII-case-insensitive, `_`/`%` wildcards, ESCAPE), so moving
# the unit filter out of the main query cannot quietly change what it matches.
_LIKE_DB = sqlite3.connect(":memory:")


# Reclassification rules: which categories get reopened
REOPEN_CATEGORIES = {"STRUCTURAL", "REGSWAP_PLUS", "MIXED"}
KEEP_CATEGORIES = {"NOISE_ONLY", "REGSWAP_ONLY", "UNFIXABLE"}


def reclassify_function(candidate: dict) -> ReclassifyResult:
    """Diagnose and reclassify a single AT_LIMIT function."""
    symbol = candidate["symbol"]
    source_path = candidate["source_path"]

    base = dict(
        symbol=symbol,
        demangled=candidate.get("demangled", ""),
        unit=candidate["unit"],
        current_percent=candidate["current_percent"],
    )

    # Build
    if not build_object(source_path):
        return ReclassifyResult(
            **base,
            category="ERROR",
            action="ERROR",
            verdict_reason="build_failed",
            error="build failed",
        )

    # Run objdiff
    match_pct, objdiff_data = run_objdiff(symbol)

    if not objdiff_data or not objdiff_data.get("instructions"):
        return ReclassifyResult(
            **base,
            category="ERROR",
            action="ERROR",
            verdict_reason="no_objdiff_data",
            error="no instruction data from objdiff",
        )

    # Diagnose
    diagnosis = diagnose_baseline(objdiff_data)
    category = classify(diagnosis)

    # Count GPR swaps
    gpr_count = sum(
        1 for (r0, r1) in diagnosis.reg_swap_pairs
        if r0.startswith("r") or r1.startswith("r")
    )

    # Decide action.  The ruler is appended to every persisted verdict_reason:
    # these verdicts outlive this run, and a verdict without its ruler is not a
    # measurement (the decomp_synth default is `none`, the LOOSEST ruler, while
    # the grader uses `name_check`).
    tag = ruler_tag()
    if category in REOPEN_CATEGORIES:
        action = "REOPEN"
        verdict_reason = f"has_fixable_{category.lower()} [{tag}]"
    elif category in KEEP_CATEGORIES:
        action = "KEEP"
        verdict_reason = f"{category.lower()} [{tag}]"
    else:
        action = "KEEP"
        verdict_reason = f"{category.lower()} [{tag}]"

    return ReclassifyResult(
        **base,
        category=category,
        action=action,
        verdict_reason=verdict_reason,
        diff_op_count=len(diagnosis.diff_ops),
        cluster_count=len(diagnosis.clusters),
        gpr_swap_count=gpr_count,
        total_instructions=diagnosis.total_instructions,
    )


def apply_reclassification(result: ReclassifyResult) -> int:
    """Update the DB verdict for a reclassified function. Returns rows changed.

    ── RESULT-CHANGING FIX (c) ───────────────────────────────────────────────
    Both UPDATEs matched `WHERE symbol = ?` with NO unit qualifier. The
    candidate was selected as a (symbol, unit) pair but rewritten by symbol
    alone, so one symbol present in several units would have every one of its
    rows rewritten from a diagnosis of just one of them.

    Today's schema declares `symbol TEXT NOT NULL UNIQUE`, so the blast radius
    is currently one row and the bug is LATENT, not active — measured against
    the live DB on 2026-08-19, zero symbols appear in more than one unit. That
    makes the qualifier free to add and correct to add: the constraint is a
    property of one schema revision, not of the query, and a per-unit-keyed DB
    (or ICF alias rows) would make it fire silently.

    The returned rowcount is checked by the caller: an UPDATE that matched 0 or
    >1 rows is reported instead of being assumed to have worked.
    """
    conn = sqlite3.connect(str(DECOMP_DB))

    if result.action == "REOPEN":
        # Clear verdict to NULL so query_functions surfaces it as workable
        cur = conn.execute(
            "UPDATE functions SET verdict = NULL, verdict_reason = ?, "
            "updated_at = CURRENT_TIMESTAMP WHERE symbol = ? AND unit IS ?",
            (result.verdict_reason, result.symbol, result.unit),
        )
    elif result.action == "KEEP":
        # Confirm AT_LIMIT and record reason
        cur = conn.execute(
            "UPDATE functions SET verdict_reason = ?, "
            "updated_at = CURRENT_TIMESTAMP WHERE symbol = ? AND unit IS ?",
            (result.verdict_reason, result.symbol, result.unit),
        )
    else:
        conn.close()
        return 0

    n = cur.rowcount
    conn.commit()
    conn.close()
    return n
    # ── end RESULT-CHANGING FIX (c) ───────────────────────────────────────────


def main():
    args = parse_args()

    mode = "APPLY" if args.apply else "DRY RUN"
    print(f"=== Reclassify AT_LIMIT Functions [{mode}] ===", file=sys.stderr)
    for line in provenance_lines() + ruler_lines():
        print(f"  {line}", file=sys.stderr)

    cov = CoverageReport("reclassify_at_limit", args=args)
    cov.extra("mode", mode)
    cov.extra("decomp_db", str(DECOMP_DB))
    for line in provenance_lines() + ruler_lines():
        cov.note(line.strip())

    print("Loading objdiff.json...", file=sys.stderr)
    unit_source_map = load_unit_source_map()
    print(f"  {len(unit_source_map)} units with source paths", file=sys.stderr)
    cov.extra("units_with_source_paths", len(unit_source_map))

    print(
        f"Querying AT_LIMIT functions "
        f"({args.min_pct}-{args.max_pct}%, unit={args.unit or '*'})...",
        file=sys.stderr,
    )
    candidates = query_at_limit_functions(
        unit_source_map, args.unit, args.min_pct, args.max_pct, args.limit,
        cov=cov, include_null_percent=args.include_null_percent,
        skip_excluded=args.skip_excluded,
    )
    # The funnel, not just its last term. "1517 candidates" on its own is a
    # sample presented as a total.
    print(f"  {len(candidates)} candidates "
          f"(of {cov.as_dict()['universe']} AT_LIMIT rows; "
          f"{cov.dropped_total} dropped — see the COVERAGE block at exit)",
          file=sys.stderr)

    if not candidates:
        print("No candidates found.", file=sys.stderr)
        sys.exit(cov.emit())

    # Process each function
    results: list[ReclassifyResult] = []
    start_time = time.time()
    reopen_count = 0
    keep_count = 0
    error_count = 0
    update_rows_written = 0
    update_anomalies: list[str] = []

    for i, candidate in enumerate(candidates):
        func_name = candidate["qualified_name"]
        pct = candidate["current_percent"]
        # `current_percent` is NULL for 1,231 AT_LIMIT rows; with
        # --include-null-percent they reach here and must not crash the format.
        pct_s = "NULL" if pct is None else f"{pct:.1f}%"
        print(
            f"[{i + 1}/{len(candidates)}] {func_name} ({pct_s}) ... ",
            end="", flush=True, file=sys.stderr,
        )

        result = reclassify_function(candidate)
        results.append(result)

        if result.error:
            error_count += 1
            print(f"ERROR: {result.error}", file=sys.stderr)
        elif result.action == "REOPEN":
            reopen_count += 1
            print(
                f"REOPEN [{result.category}] "
                f"(diff_ops={result.diff_op_count}, clusters={result.cluster_count}, gpr_swaps={result.gpr_swap_count})",
                file=sys.stderr,
            )
            if args.apply:
                n = apply_reclassification(result)
                update_rows_written += n
                if n != 1:
                    update_anomalies.append(f"{result.symbol} @ {result.unit}: {n} rows")
        else:
            keep_count += 1
            print(f"KEEP [{result.category}]", file=sys.stderr)
            if args.apply:
                n = apply_reclassification(result)
                update_rows_written += n
                if n != 1:
                    update_anomalies.append(f"{result.symbol} @ {result.unit}: {n} rows")

    elapsed = time.time() - start_time

    # Summary
    print(f"\n{'=' * 60}", file=sys.stderr)
    print("RECLASSIFICATION SUMMARY", file=sys.stderr)
    print(f"{'=' * 60}", file=sys.stderr)
    d = cov.as_dict()
    print(f"  FUNNEL: {d['universe']} AT_LIMIT rows "
          f"-> {d['examined']} candidates -> {len(results)} processed", file=sys.stderr)
    for reason, n in sorted(d["dropped"].items(), key=lambda kv: (-kv[1], kv[0])):
        print(f"      dropped {n:5d}  {reason}", file=sys.stderr)
    print(f"  Total processed: {len(results)}", file=sys.stderr)
    print(f"  REOPEN (fixable): {reopen_count}", file=sys.stderr)
    print(f"  KEEP (unfixable): {keep_count}", file=sys.stderr)
    print(f"  ERROR:            {error_count}", file=sys.stderr)
    print(f"  Elapsed:          {elapsed:.1f}s", file=sys.stderr)
    if args.apply:
        print(f"\n  Database updated: {reopen_count} functions reopened, "
              f"{update_rows_written} DB rows written.", file=sys.stderr)
        if update_anomalies:
            print(f"  ⚠ {len(update_anomalies)} UPDATE(s) did not match exactly one row "
                  f"— the (symbol, unit) key is not unique here:", file=sys.stderr)
            for a in sorted(update_anomalies)[:20]:
                print(f"      {a}", file=sys.stderr)
    else:
        print(f"\n  DRY RUN — no database changes. Use --apply to update.", file=sys.stderr)
    cov.extra("reopen", reopen_count)
    cov.extra("keep", keep_count)
    cov.extra("errors", error_count)
    cov.extra("db_rows_written", update_rows_written)
    cov.extra("update_rowcount_anomalies", len(update_anomalies))

    # Category breakdown.  Percentages are against the PROCESSED set, which the
    # funnel above shows is a fraction of the AT_LIMIT population — they are not
    # percentages of AT_LIMIT.
    from collections import Counter
    cats = Counter(r.category for r in results if not r.error)
    if cats:
        print(f"\n  Category breakdown (% of the {len(results)} PROCESSED, "
              f"not of {d['universe']} AT_LIMIT):", file=sys.stderr)
        for cat, count in sorted(cats.most_common(), key=lambda kv: (-kv[1], kv[0])):
            pct = 100 * count / len(results)
            pct_all = 100 * count / d["universe"] if d["universe"] else 0.0
            print(f"    {cat:20s}: {count:4d} ({pct:.1f}% of processed, "
                  f"{pct_all:.1f}% of AT_LIMIT)", file=sys.stderr)

    # JSON output
    if args.output or args.json_output:
        report = {
            "metadata": {
                "mode": mode,
                "unit_pattern": args.unit,
                "min_pct": args.min_pct,
                "max_pct": args.max_pct,
                "total_candidates": len(candidates),
                "elapsed_seconds": round(elapsed, 1),
                "provenance": [ln.strip() for ln in provenance_lines()],
                "ruler": [ln.strip() for ln in ruler_lines()],
            },
            "summary": {
                "at_limit_rows": d["universe"],
                "candidates": d["examined"],
                "total": len(results),
                "reopen": reopen_count,
                "keep": keep_count,
                "error": error_count,
                "db_rows_written": update_rows_written,
                "by_category": {
                    cat: count for cat, count in sorted(cats.most_common(),
                                                        key=lambda kv: (-kv[1], kv[0]))
                },
            },
            "_coverage": cov.as_dict(),
            "results": [asdict(r) for r in results],
        }
        output_text = json.dumps(report, indent=2)

        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(output_text)
            print(f"\nReport written to: {args.output}", file=sys.stderr)
        else:
            print(output_text)

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
