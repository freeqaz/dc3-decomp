#!/usr/bin/env python3
"""Find hidden decomp work: stale verdicts and missing implementations.

Identifies functions that need work but are hidden behind stale COMPLETE/AT_LIMIT
verdicts, plus functions in the target binary with no decomp implementation at all.

Usage:
    python3 scripts/find_hidden_work.py                  # report only (no writes)
    python3 scripts/find_hidden_work.py --demote 80      # demote COMPLETE < 80% to workable
    python3 scripts/find_hidden_work.py --demote 80 --dry-run  # preview demotion

WRITES: `--demote` sets `verdict = NULL`.  Everything else is read-only.

CHANGED DEFAULT (2026-08-19): the band and demotion gates read
`COALESCE(match_percent_normalized, current_percent)` instead of
`current_percent`.  `current_percent` is the relocation-sensitive fuzzy scorer,
so ICF / atexit-thunk churn could phantom-regress a row straight into demotion;
374 rows currently sit at COMPLETE with current_percent in [96.46, 100) and
match_percent_normalized >= 100.  Pass `--ruler fuzzy` for the old behaviour.
"""
from __future__ import annotations

import argparse
import json
import os
import sqlite3
import sys
from collections import Counter
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

DEFAULT_REPORT = REPO_ROOT / "build" / "373307D9" / "report.json"
DEFAULT_DB = REPO_ROOT / "decomp.db"

# Units that are third-party / SDK code (not our decomp targets)
SDK_UNIT_PATTERNS = [
    "%bink%", "%xdk/%", "%xnet/%", "%xapilib%", "%libcmt%", "%libcpmt%",
    "%d3d/%", "%xaudio/%", "%tomcrypt/%", "%zlib/%", "%oggvorbis/%",
    "%nui_core%", "%auto_%", "%sapi/%", "%srdrv%", "%x2voice%", "%x2engine%",
    "%c30sw%", "%femanager%", "%textnorm%", "%filter%", "%shader%",
    "%cfglexicon%", "%phone/%", "%msasrx%", "%rtresults%", "%ccodec%",
    "%xspeech%", "%spphrase%", "%ctransducer%", "%crtvmx%", "%crtgpr%",
    "%crtfpr%", "%cconstant%", "%irinst%", "%import/%", "%constreg%",
    "%srrecomaster%", "%keygen%", "%xonline%", "%curl/%",
]
SDK_TOKENS = tuple(p.strip("%") for p in SDK_UNIT_PATTERNS)


def is_sdk_unit(unit_name: str, mode: str = "substring") -> bool:
    """Check if a unit is SDK/third-party code.

    HEURISTIC WARNING -- read before trusting a "nothing hidden here" result.

    `mode="substring"` is the historical behaviour and remains the DEFAULT: a
    token matches ANYWHERE in the unit path.  That is not a path test, and it
    over-fires.  Measured against the current report.json (2,224 units):

        * 1,356 of 2,224 units are classed SDK; 132 of those are outside
          `default/xdk/`, and 114 outside `xdk/` + `lib/binkxenon/`.
        * the token 'bink' swallows `default/system/moviebink/BinkMovie{Impl,Sys}`
          and their `_Xbox` siblings -- 4 units with real, authorable sources
          under `src/system/moviebink/`, hiding 6 no-percent rows.
        * 'keygen' swallows `default/keygen_xbox`.
        * 'filter' also matches `default/system/synth/filterdesign`, 'auto_'
          matches the 13 `default/auto_NN_*` data blobs, and 'shader' /
          'femanager' happen to hit only `xdk/` units today -- but nothing in
          the heuristic makes that a property rather than a coincidence.

    `mode="segment"` requires the token to be a path SEGMENT (or a segment
    prefix ending in '/'), which is what the patterns were clearly meant to
    say.  It is OPT-IN because turning it on CHANGES WHAT THIS SCANNER FINDS.
    """
    if mode == "substring":
        return any(t in unit_name for t in SDK_TOKENS)
    if mode == "segment":
        segs = unit_name.split("/")
        for t in SDK_TOKENS:
            if t.endswith("/"):
                if t.rstrip("/") in segs:
                    return True
            elif t in segs:
                return True
        return False
    raise ValueError(f"unknown sdk-match mode: {mode!r}")


def sdk_classification_delta(unit_names: list[str]) -> dict:
    """How the two `is_sdk_unit` modes disagree, as counts. Read-only."""
    sub = {u for u in unit_names if is_sdk_unit(u, "substring")}
    seg = {u for u in unit_names if is_sdk_unit(u, "segment")}
    return {
        "units_total": len(unit_names),
        "sdk_substring": len(sub),
        "sdk_segment": len(seg),
        "sdk_only_under_substring": sorted(sub - seg),
        "sdk_only_under_segment": sorted(seg - sub),
    }


# --------------------------------------------------------------------------- #
# THE RULER.  `current_percent` is the RAW/fuzzy scorer, relocation-sensitive,
# so ICF and atexit-thunk churn move it with no source change.  This script
# DEMOTES on that column -- i.e. a phantom regression used to be able to strip
# a COMPLETE verdict.  `match_percent_normalized` is the canonical scorer
# (it is what sync_match_percent.py:419 promotes on).  In the current DB, 374
# rows carry verdict=COMPLETE with current_percent in [96.46, 100) and
# match_percent_normalized >= 100: under the fuzzy ruler those are all
# demotion candidates and under the normalized ruler none of them are.
#
# Default is now `normalized`; pass --ruler fuzzy for the pre-2026-08-19
# behaviour.
# --------------------------------------------------------------------------- #

RULER_SQL = {
    "normalized": "COALESCE(match_percent_normalized, current_percent)",
    "fuzzy": "current_percent",
}


def fmt_pct(v: float | None, decimals: int = 1) -> str:
    """Render a percentage that can never round up across the 100 boundary.

    99.9967 must not print as `100.0` inside a list captioned "marked COMPLETE
    but far from 100%".  Same rule as sync_match_percent.py's `_round_pct`.
    """
    if v is None:
        return "  n/a"
    r = round(v, decimals)
    if r >= 100.0 and v < 100.0:
        r = 100.0 - 10.0 ** (-decimals)
    return f"{r:.{decimals}f}"


def find_stale_verdicts(conn: sqlite3.Connection, threshold: float = 80.0,
                        ruler: str = "normalized") -> list[dict]:
    """Find functions marked COMPLETE but below threshold match%.

    Rows whose gate percentage is NULL -- COMPLETE but NEVER MEASURED, the most
    suspicious "stale COMPLETE" population there is -- are NOT returned here
    (demoting on an absent measurement would be guessing), but they are no
    longer invisible either: `count_never_measured` reports them.
    """
    gate = RULER_SQL[ruler]
    rows = conn.execute(f"""
        SELECT id, symbol, demangled, current_percent, match_percent_normalized,
               {gate} AS gate_percent, unit, size, verdict
        FROM functions
        WHERE verdict = 'COMPLETE' AND excluded = 0
        AND {gate} IS NOT NULL AND {gate} < ?
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        ORDER BY gate_percent ASC, unit ASC, symbol ASC
    """, (threshold,)).fetchall()
    return [dict(r) for r in rows]


def find_stale_at_limit(conn: sqlite3.Connection, threshold: float = 60.0,
                        ruler: str = "normalized") -> list[dict]:
    """Find functions marked AT_LIMIT but below threshold (likely gave up too early)."""
    gate = RULER_SQL[ruler]
    rows = conn.execute(f"""
        SELECT id, symbol, demangled, current_percent, match_percent_normalized,
               {gate} AS gate_percent, unit, size, verdict
        FROM functions
        WHERE verdict = 'AT_LIMIT' AND excluded = 0
        AND {gate} IS NOT NULL AND {gate} < ?
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        ORDER BY gate_percent ASC, unit ASC, symbol ASC
    """, (threshold,)).fetchall()
    return [dict(r) for r in rows]


def count_never_measured(conn: sqlite3.Connection, verdict: str,
                         ruler: str = "normalized") -> int:
    """COUNT of rows with this verdict whose gate percentage is NULL.

    This is the population `AND current_percent IS NOT NULL` used to drop from
    every band query and from --demote without ever printing a number.
    """
    gate = RULER_SQL[ruler]
    return conn.execute(f"""
        SELECT count(*) FROM functions
        WHERE verdict = ? AND excluded = 0
        AND {gate} IS NULL
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
    """, (verdict,)).fetchone()[0]


def count_verdict_pool(conn: sqlite3.Connection, verdict: str) -> int:
    """Denominator: every non-excluded, non-merged row carrying this verdict."""
    return conn.execute("""
        SELECT count(*) FROM functions
        WHERE verdict = ? AND excluded = 0
        AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
    """, (verdict,)).fetchone()[0]


def find_missing_implementations(report_path: Path, sdk_mode: str = "substring",
                                 cov: CoverageReport = None) -> list[dict]:
    """Find functions in target .obj with no decomp implementation.

    These show up in report.json with `fuzzy_match_percent == null` -- objdiff
    omits the key for functions we never defined.  That is the RIGHT key for
    this particular question (it is a presence test, not a score comparison),
    which is why it is left alone; `match_percent_normalized` is present on
    every row and would find nothing.
    """
    with open(report_path) as f:
        data = json.load(f)

    units = data.get("units", [])
    total_rows = sum(len(u.get("functions", [])) for u in units)
    if cov is not None:
        cov.universe(total_rows, "function rows in report.json")

    missing = []
    for unit in sorted(units, key=lambda u: u["name"]):
        unit_name = unit["name"]
        fns = unit.get("functions", [])
        if is_sdk_unit(unit_name, sdk_mode):
            if cov is not None:
                cov.drop(f"sdk-unit-{sdk_mode}-heuristic", len(fns),
                         note="unit classed third-party by SDK_UNIT_PATTERNS; "
                              "see is_sdk_unit's docstring for the over-fire list")
            continue

        for fn in fns:
            if cov is not None:
                cov.examine()
            mp = fn.get("fuzzy_match_percent")
            if mp is None:
                missing.append({
                    "unit": unit_name,          # FULL path: two units can share a
                    "unit_leaf": unit_name.split("/")[-1],   # leaf name and did
                    "unit_full": unit_name,
                    "symbol": fn["name"],
                    "demangled": fn.get("metadata", {}).get("demangled_name", fn["name"]),
                    "size": int(fn.get("size", "0")),
                })

    return missing


def demote_functions(conn: sqlite3.Connection, func_ids: list[int], dry_run: bool) -> int:
    """Reset verdict from COMPLETE to NULL (workable) for given function IDs."""
    if dry_run or not func_ids:
        return len(func_ids)

    conn.executemany(
        """UPDATE functions
           SET verdict = NULL, verdict_reason = 'demoted: stale COMPLETE below threshold',
               updated_at = CURRENT_TIMESTAMP
           WHERE id = ?""",
        [(fid,) for fid in func_ids],
    )
    conn.commit()
    return len(func_ids)


def main():
    p = argparse.ArgumentParser(description="Find hidden decomp work")
    p.add_argument("--db", type=Path, default=DEFAULT_DB)
    p.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    p.add_argument("--demote", type=float, default=None, metavar="PCT",
                   help="Demote COMPLETE functions below this %% to workable")
    p.add_argument("--dry-run", action="store_true",
                   help="Preview changes without writing")
    p.add_argument("--ruler", choices=sorted(RULER_SQL), default="normalized",
                   help="Percentage column the band/demotion gates read. "
                        "'normalized' = COALESCE(match_percent_normalized, "
                        "current_percent), the canonical scorer (DEFAULT since "
                        "2026-08-19). 'fuzzy' = current_percent only, which is "
                        "relocation-sensitive and WAS the previous hardcoded "
                        "behaviour.")
    p.add_argument("--sdk-match", choices=("substring", "segment"), default="substring",
                   help="How SDK_UNIT_PATTERNS match a unit path. 'substring' "
                        "(DEFAULT, unchanged) matches a token anywhere -- 'bink' "
                        "swallows src/system/moviebink/*. 'segment' requires a "
                        "whole path segment; it CHANGES WHAT THIS SCANNER FINDS, "
                        "so it is opt-in.")
    p.add_argument("--verbose", "-v", action="store_true")
    add_coverage_args(p)
    args = p.parse_args()

    cov = CoverageReport("find_hidden_work", args=args)

    conn = sqlite3.connect(str(args.db))
    conn.row_factory = sqlite3.Row
    ruler = args.ruler
    gate = RULER_SQL[ruler]

    print(f"  ruler: {ruler}  ->  gate column = {gate}")
    print(f"  sdk-match: {args.sdk_match}")

    # === Part 1: Stale COMPLETE verdicts ===
    print("=" * 70)
    print("  STALE COMPLETE VERDICTS (marked COMPLETE but far from 100%)")
    print("=" * 70)

    complete_pool = count_verdict_pool(conn, "COMPLETE")
    complete_unmeasured = count_never_measured(conn, "COMPLETE", ruler)
    print(f"  Pool: {complete_pool} rows are verdict=COMPLETE, excluded=0, non-merged")
    print(f"  Of those, {complete_unmeasured} have NO {ruler} percentage at all "
          f"(COMPLETE but never measured -- excluded from every band below and "
          f"from --demote, as they always were, but no longer silently)")

    ranges = [(0, 10), (10, 30), (30, 50), (50, 70), (70, 80), (80, 90), (90, 95)]
    banded = 0
    for lo, hi in ranges:
        cnt = conn.execute(f"""
            SELECT count(*) FROM functions
            WHERE verdict = 'COMPLETE' AND excluded = 0
            AND {gate} >= ? AND {gate} < ?
            AND symbol NOT LIKE 'merged\\_%' ESCAPE '\\'
        """, (lo, hi)).fetchone()[0]
        banded += cnt
        if cnt > 0:
            print(f"  {lo:3d}-{hi:3d}%: {cnt:5d} functions")
    print(f"  Banded 0-95%: {banded} of {complete_pool}  "
          f"({complete_pool - banded - complete_unmeasured} are >= 95%, "
          f"{complete_unmeasured} unmeasured)")

    # Show the worst ones
    stale = find_stale_verdicts(conn, threshold=50.0, ruler=ruler)
    game_stale = [s for s in stale if not is_sdk_unit(s["unit"] or "", args.sdk_match)]
    sdk_stale = len(stale) - len(game_stale)
    print(f"\n  COMPLETE < 50%: {len(stale)} total, {len(game_stale)} game code, "
          f"{sdk_stale} in SDK-classed units")
    if game_stale:
        shown = min(20, len(game_stale))
        print(f"  Worst offenders (COMPLETE < 50%, game code): "
              f"{len(game_stale)}, showing first {shown}")
        for s in game_stale[:20]:
            name = (s["demangled"] or s["symbol"])[:55]
            unit = s["unit"] or ""
            print(f"    {fmt_pct(s['gate_percent'])}% | {unit[-34:]:34s} | {name}")
        if len(game_stale) > 20:
            print(f"    ... and {len(game_stale) - 20} more")

    # === Part 2: Stale AT_LIMIT verdicts ===
    print()
    print("=" * 70)
    print("  STALE AT_LIMIT VERDICTS (gave up too early?)")
    print("=" * 70)
    at_limit_pool = count_verdict_pool(conn, "AT_LIMIT")
    at_limit_unmeasured = count_never_measured(conn, "AT_LIMIT", ruler)
    stale_al = find_stale_at_limit(conn, threshold=60.0, ruler=ruler)
    game_stale_al = [s for s in stale_al
                     if not is_sdk_unit(s["unit"] or "", args.sdk_match)]
    print(f"  Pool: {at_limit_pool} rows are verdict=AT_LIMIT, excluded=0, non-merged")
    print(f"  Of those, {at_limit_unmeasured} have NO {ruler} percentage at all "
          f"(AT_LIMIT but never measured)")
    print(f"  AT_LIMIT < 60%: {len(stale_al)} total, {len(game_stale_al)} game code")
    shown_al = min(15, len(game_stale_al))
    if game_stale_al:
        print(f"  showing first {shown_al}")
    for s in game_stale_al[:15]:
        name = (s["demangled"] or s["symbol"])[:55]
        unit = s["unit"] or ""
        print(f"    {fmt_pct(s['gate_percent'])}% | {unit[-34:]:34s} | {name}")

    # === Part 3: Missing implementations ===
    missing = []
    if args.report.exists():
        print()
        print("=" * 70)
        print("  MISSING IMPLEMENTATIONS (in target, no decomp source)")
        print("=" * 70)

        missing = find_missing_implementations(args.report, args.sdk_match, cov)

        # How much the SDK heuristic is costing, as a count -- not a fix.
        with open(args.report) as f:
            unit_names = [u["name"] for u in json.load(f).get("units", [])]
        delta = sdk_classification_delta(unit_names)
        print(f"  SDK filter ({args.sdk_match}): {delta['sdk_' + args.sdk_match]} of "
              f"{delta['units_total']} units classed third-party and skipped.")
        print(f"  A path-SEGMENT rule would class {delta['sdk_segment']} instead; "
              f"{len(delta['sdk_only_under_substring'])} units are SDK only because "
              f"a token matched mid-path.")
        if delta["sdk_only_under_substring"] and args.verbose:
            for u in delta["sdk_only_under_substring"][:20]:
                print(f"      substring-only SDK: {u}")

        if missing:
            # Group by FULL unit path.  The old `unit_name.split('/')[-1]` merged
            # units that share a leaf name -- 64 leaf names cover 138 units in the
            # current report, including every system/synth/FxSend* against its
            # system/synth_xbox/FxSend* twin, so their byte totals cross-contaminated.
            by_unit = Counter(m["unit_full"] for m in missing)
            leaf_names = {m["unit_leaf"] for m in missing}
            print(f"  {len(missing)} functions across {len(by_unit)} units "
                  f"({len(leaf_names)} distinct leaf names -- "
                  f"{len(by_unit) - len(leaf_names)} would have been merged by a "
                  f"leaf-name aggregation)")
            print()
            shown = min(20, len(by_unit))
            print(f"  Top {shown} of {len(by_unit)} units:")
            size_by_unit: dict[str, int] = {}
            for m in missing:
                size_by_unit[m["unit_full"]] = size_by_unit.get(m["unit_full"], 0) + m["size"]
            for unit, cnt in sorted(by_unit.items(), key=lambda kv: (-kv[1], kv[0]))[:20]:
                print(f"    {cnt:4d} | {size_by_unit[unit]:7d}B | {unit}")

            # Show individual missing functions
            if args.verbose:
                print()
                non_trivial = [m for m in missing if m["size"] > 20
                               and "??_9" not in m["symbol"]
                               and "??_G" not in m["symbol"]]
                shown_nt = min(30, len(non_trivial))
                print(f"  Non-trivial missing ({len(non_trivial)} of {len(missing)}, "
                      f"showing top {shown_nt} by size):")
                for m in sorted(non_trivial,
                                key=lambda x: (-x["size"], x["unit_full"], x["symbol"]))[:30]:
                    print(f"    {m['size']:5d}B | {m['unit_full'][-30:]:30s} | "
                          f"{m['demangled'][:55]}")
        else:
            print("  No missing implementations found!")
    else:
        print(f"\n  Warning: report.json not found at {args.report}")
        print("  Run: ninja build/373307D9/report.json")
        cov.note(f"report.json absent at {args.report}: the missing-implementation "
                 f"census did not run and has NO denominator")

    # === Part 4: Demote if requested ===
    demoted = 0
    if args.demote is not None:
        print()
        print("=" * 70)
        print(f"  DEMOTION: COMPLETE < {args.demote}% -> workable  (ruler={ruler})")
        print("=" * 70)

        to_demote = find_stale_verdicts(conn, threshold=args.demote, ruler=ruler)
        game_demote = [s for s in to_demote
                       if not is_sdk_unit(s["unit"] or "", args.sdk_match)]
        print(f"  Candidates: {len(to_demote)} of {complete_pool} COMPLETE rows below "
              f"{args.demote}%; {len(game_demote)} after the SDK filter "
              f"({len(to_demote) - len(game_demote)} skipped as SDK); "
              f"{complete_unmeasured} more are unmeasured and never considered")

        if game_demote:
            func_ids = sorted(s["id"] for s in game_demote)
            mode = " (DRY RUN -- nothing written)" if args.dry_run else ""
            demoted = demote_functions(conn, func_ids, args.dry_run)
            print(f"  Demoted {demoted} functions{mode}")
        else:
            print("  Nothing to demote.")

    conn.close()

    # === Overall summary — every number above with its denominator ========== #
    print()
    print("=" * 70)
    print("  SUMMARY (counts with denominators)")
    print("=" * 70)
    print(f"  COMPLETE pool          : {complete_pool}  "
          f"(unmeasured: {complete_unmeasured})")
    print(f"  COMPLETE < 50%         : {len(stale)}  "
          f"(game code: {len(game_stale)})")
    print(f"  AT_LIMIT pool          : {at_limit_pool}  "
          f"(unmeasured: {at_limit_unmeasured})")
    print(f"  AT_LIMIT < 60%         : {len(stale_al)}  "
          f"(game code: {len(game_stale_al)})")
    print(f"  Missing implementations: {len(missing)}")
    print(f"  Demoted                : {demoted}"
          + ("  (DRY RUN)" if args.dry_run else ""))

    cov.extra("ruler", ruler)
    cov.extra("sdk_match", args.sdk_match)
    cov.extra("complete_pool", complete_pool)
    cov.extra("complete_never_measured", complete_unmeasured)
    cov.extra("at_limit_pool", at_limit_pool)
    cov.extra("at_limit_never_measured", at_limit_unmeasured)
    cov.note(f"DB gates use {gate}; the missing-implementation census below is a "
             f"presence test on report.json's fuzzy_match_percent KEY, which is "
             f"the right test for 'we never wrote a body'")
    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
