#!/usr/bin/env python3
"""Whole-binary census of objdiff's pattern detectors, with the ruler attached.

WHAT THIS REPLACES
==================
`scripts/backfill_reloc_patterns.py` answered one question -- "set these six
booleans" -- and lost everything needed to read the answer afterwards: which
ruler, which objdiff binary, which tree, and what the denominator was.  Three of
the numbers it left in `decomp.db` (`has_linker_merged` 1310,
`has_address_relocation` 680, `detected_patterns` 1500) turned out to be
unreadable for three independent reasons at once, and none of them was a wrong
detector:

  1. **Denominator.** It scanned `WHERE excluded = 0`, which is 31,446 of the
     52,568 rows.  16,922 functions that ARE in `report.json` -- diffable,
     scored, real -- were never looked at.  The number was published as if it
     were about the binary.
  2. **Tree.** It measured a tree three other worktrees were rebuilding, so
     1,310 reproduced as 1,051 on two settled trees.  `patch_guard` now closes
     this and the scan records whether it passed.
  3. **Vocabulary.** objdiff 4.2.6 split the over-broad `detect_linker_merged`
     into five classes; `LINKER_MERGED` now names ~2% of its former population
     and four new strings carry the rest.  An old count cannot be adjusted into
     a new one, because the two are not counting the same predicate.

Only (2) was ever recorded as a hazard.  This tool records all three, per scan,
in the database, so a future reader can tell a real zero from a starved one.

THE RULER IS PART OF THE RESULT
===============================
`functionRelocDiffs=none` -- objdiff's default AND what `sync_objdiff.py` runs --
makes eight detectors structurally unable to fire, because they all key off
`match_type == "diff_arg"` on a `bl` and that mode reports those instructions as
equal.  This tool REFUSES that ruler (`symbol_sweep.RelocBlindPatternError`)
rather than reporting its zeros.  `name_check` is the graded ruler and
`report.json`'s; `all` charges the ~2,992 already-adjudicated /OPT:ICF folds too
and is mostly noise.  Run both to get the artifact rate.

Usage:
    # census only, no writes (safe alongside the build fleet)
    python3 scripts/analysis/pattern_census.py --project-dir . \\
        --ruler name_check --out /tmp/census-name_check.jsonl

    # and record it
    python3 scripts/analysis/pattern_census.py --project-dir . \\
        --ruler name_check --db /path/to/main/decomp.db --apply

Exit codes: 0 ok, 3 TRUNCATED by --limit, 4 unsettled/unpatched build tree.
"""
from __future__ import annotations

import argparse
import collections
import json
import sqlite3
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from scripts.orchestrator import symbol_sweep  # noqa: E402
from scripts.orchestrator.patch_guard import (  # noqa: E402
    UnpatchedTreeError, ensure_patched_tree,
)


def report_universe(project_dir: Path) -> dict[str, dict]:
    """Every function `report.json` scores, with its canonical percentage.

    This -- not `WHERE excluded = 0` -- is the honest universe for a census.
    `excluded` is a WORKLIST predicate (XDK/SDK library code, linker glue, ICF
    placeholders absent from the report); it says nothing about whether objdiff
    can measure the function, and 16,922 excluded rows are scored in
    `report.json` right now.

    `match_percent_normalized` is read from here and ONLY from here: objdiff's
    `--batch` JSONL carries `"match_percent_normalized": null` on every row, and
    a previous triage that read it from the batch classified all 1,310 functions
    as norm=0 and reported the prize slice as empty.
    """
    path = project_dir / "build" / "373307D9" / "report.json"
    doc = json.loads(path.read_text())
    out: dict[str, dict] = {}
    dup = 0

    def rec(node, unit=None):
        nonlocal dup
        u = node.get("name") or unit
        for f in node.get("functions") or []:
            name = f["name"]
            if name in out:
                dup += 1
                continue
            out[name] = {
                "unit": u,
                "size": f.get("size") or 0,
                "norm": f.get("match_percent_normalized"),
                "fuzzy": f.get("fuzzy_match_percent"),
            }
        for c in node.get("units") or []:
            rec(c, u)

    rec(doc)
    prov = doc.get("provenance") or {}
    return {
        "functions": out,
        "duplicate_names": dup,
        "tool_version": prov.get("tool_version"),
        "tool_commit": prov.get("tool_commit"),
        "diff_config": prov.get("diff_config"),
    }


def git_rev(project_dir: Path) -> str | None:
    try:
        p = subprocess.run(["git", "-C", str(project_dir), "rev-parse", "--short", "HEAD"],
                           capture_output=True, text=True, timeout=30)
        return p.stdout.strip() or None
    except Exception:  # noqa: BLE001
        return None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--ruler", default="name_check",
                    choices=[r for r in sorted(symbol_sweep.RELOC_RULERS)],
                    help="functionRelocDiffs. `none` is accepted by the parser "
                         "only so the refusal explains itself.")
    ap.add_argument("--db", default=None,
                    help="path to the REAL decomp.db. A worktree has a tripwire "
                         "file there on purpose; pass the main checkout's path.")
    ap.add_argument("--apply", action="store_true", help="write the scan to --db")
    ap.add_argument("--out", default=None, help="write per-function JSONL here")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None)
    ap.add_argument("--skip-patch-check", action="store_true",
                    help="do not build/verify the tree first. The scan is then "
                         "about a moment, and is recorded with tree_verified=0.")
    ap.add_argument("--notes", default=None)
    args = ap.parse_args()

    project_dir = Path(args.project_dir).resolve()
    started = datetime.now(timezone.utc).isoformat()

    tree_verified = 0
    if args.skip_patch_check:
        print("WARNING: --skip-patch-check. The objects behind this census may "
              "be raw compiler output: unpatched anon-namespace hashes, atexit "
              "scope counters and static guards all read as relocation-NAME "
              "divergences, which is exactly what these detectors count.",
              file=sys.stderr)
    else:
        try:
            note = ensure_patched_tree(project_dir, build=True)
            tree_verified = 1
            print(f"tree: {note}")
        except UnpatchedTreeError as e:
            print(f"\n{e}\n", file=sys.stderr)
            return 4

    uni = report_universe(project_dir)
    fns = uni["functions"]
    symbols = sorted(fns)
    universe = len(symbols)
    truncated = bool(args.limit) and args.limit < universe
    if args.limit:
        symbols = symbols[:args.limit]

    print(f"{'TRUNCATED: ' if truncated else ''}{len(symbols)} of {universe} "
          f"report.json functions ({uni['duplicate_names']} duplicate names "
          f"collapsed), functionRelocDiffs={args.ruler}")

    try:
        res = symbol_sweep.sweep_functions(
            project_dir, symbols, include_patterns=True,
            reloc_config=args.ruler, jobs=args.jobs, timeout=7200,
            scanner_name=f"pattern_census.{args.ruler}")
    except symbol_sweep.RelocBlindPatternError as e:
        print(f"\n{e}\n", file=sys.stderr)
        return 4

    print()
    print(res["_coverage_render"])

    # --- populations, by DISTINCT FUNCTION, with the denominator -------------
    examined = res["_coverage"]["examined"]
    by_pattern: dict[str, list[str]] = collections.defaultdict(list)
    rows_out = []
    for r in res["rows"]:
        sym = r["symbol"]
        pats = (r.get("analysis") or {}).get("patterns") or []
        meta = fns.get(sym, {})
        norm = meta.get("norm")
        canon = []
        for p in pats:
            name = symbol_sweep.canonical_pattern_name(p.get("pattern", ""))
            by_pattern[name].append(sym)
            canon.append({
                "pattern": name,
                "confidence": p.get("confidence"),
                "fixability": p.get("fixability"),
                "instruction_count": p.get("instruction_count"),
                "details": p.get("details"),
            })
        rows_out.append({
            "symbol": sym,
            "unit": meta.get("unit") or r.get("unit"),
            "demangled": r.get("demangled"),
            "size": meta.get("size") or r.get("target_size") or 0,
            # From report.json. NEVER from the batch row -- it is null there.
            "norm": norm,
            "fuzzy": r.get("fuzzy_match_percent"),
            "canonical": r.get("canonical_match_percent"),
            "patterns": canon,
        })

    print()
    print(f"# Pattern populations -- functionRelocDiffs={args.ruler}, "
          f"{res['objdiff_version']}")
    print(f"# denominator: {examined} functions examined of {universe} in "
          f"report.json ({universe - examined} dropped, see COVERAGE above)")
    print(f"{'pattern':38s} {'functions':>9s} {'% of examined':>14s}")
    for name, syms in sorted(by_pattern.items(), key=lambda kv: -len(set(kv[1]))):
        n = len(set(syms))
        print(f"{name:38s} {n:9d} {100.0 * n / max(examined, 1):13.3f}%")
    silent = sorted(set(res.get("patterns_checked") or []) - set(by_pattern))
    if silent:
        print(f"\n{len(silent)} of {len(res.get('patterns_checked') or [])} "
              f"checked patterns fired on ZERO functions under this ruler "
              f"(measured zero, not an unmeasured one): {', '.join(silent)}")

    if args.out:
        with open(args.out, "w") as fh:
            fh.write(json.dumps({
                "_scan": {
                    "ruler": args.ruler,
                    "tool_version": res["objdiff_version"],
                    "project_dir": str(project_dir),
                    "build_rev": git_rev(project_dir),
                    "tree_verified": tree_verified,
                    "universe": universe,
                    "examined": examined,
                    "truncated": truncated,
                    "coverage": res["_coverage"],
                    "patterns_checked": res.get("patterns_checked"),
                    "report_provenance": {k: uni[k] for k in
                                          ("tool_version", "tool_commit", "diff_config")},
                    "started_at": started,
                }}) + "\n")
            for r in rows_out:
                fh.write(json.dumps(r) + "\n")
        print(f"\nwrote {len(rows_out)} rows -> {args.out}")

    if args.apply:
        if not args.db:
            print("--apply needs --db (the MAIN checkout's decomp.db; a worktree "
                  "has a deliberate tripwire at that path)", file=sys.stderr)
            return 2
        write_scan(Path(args.db), args, res, rows_out, uni, universe, examined,
                   tree_verified, project_dir, started)

    return 3 if truncated else 0


def write_scan(db_path, args, res, rows_out, uni, universe, examined,
               tree_verified, project_dir, started) -> None:
    """Record the scan. Every finding is a row; the ruler is on the scan."""
    sys.path.insert(0, str(REPO_ROOT / "scripts"))
    from orchestrator import database as db_mod

    conn = sqlite3.connect(str(db_path))
    ver = conn.execute("SELECT version FROM schema_version").fetchone()[0]
    if ver < 17:
        conn.close()
        # Route through the project's own migrator rather than issuing DDL here:
        # two places that both know the schema is how they drift.
        db_mod.init_database(str(db_path))
        conn = sqlite3.connect(str(db_path))

    ids = {r[0]: r[1] for r in conn.execute("SELECT symbol, id FROM functions")}

    cur = conn.execute(
        "INSERT INTO pattern_scans (ruler, tool_version, project_dir, build_rev,"
        " tree_verified, universe, examined, coverage_json, patterns_checked,"
        " notes, started_at) VALUES (?,?,?,?,?,?,?,?,?,?,?)",
        (args.ruler, res["objdiff_version"], str(project_dir), git_rev(project_dir),
         tree_verified, universe, examined,
         json.dumps(res["_coverage"]), json.dumps(res.get("patterns_checked") or []),
         args.notes, started))
    scan_id = cur.lastrowid

    examined_rows, pattern_rows, unmatched = [], [], 0
    for r in rows_out:
        fid = ids.get(r["symbol"])
        if fid is None:
            unmatched += 1
            continue
        examined_rows.append((scan_id, fid))
        for p in r["patterns"]:
            pattern_rows.append((
                scan_id, fid, p["pattern"], p.get("confidence"),
                p.get("fixability"), p.get("instruction_count"),
                json.dumps(p.get("details")) if p.get("details") is not None else None))

    conn.executemany("INSERT OR REPLACE INTO pattern_scan_examined VALUES (?,?)",
                     examined_rows)
    conn.executemany("INSERT OR REPLACE INTO function_patterns VALUES (?,?,?,?,?,?,?)",
                     pattern_rows)
    conn.commit()
    print(f"\nrecorded scan id={scan_id} ruler={args.ruler}: "
          f"{len(examined_rows)} examined rows, {len(pattern_rows)} pattern rows"
          + (f", {unmatched} symbols had no decomp.db row" if unmatched else ""))
    conn.close()


if __name__ == "__main__":
    sys.exit(main())
