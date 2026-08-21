#!/usr/bin/env python3
"""SUPERSEDED by scripts/analysis/pattern_census.py (2026-08-21, task #127).

`--apply` now REFUSES.  `--histogram` still works and is still a fine quick
survey.

Why it was retired rather than repaired:

* It writes its flags under `functionRelocDiffs=all`, which charges the ~2,992
  `/OPT:ICF` folds this project has already adjudicated.  Measured whole-binary:
  58.1% of `WRONG_CALLEE` and 95.5% of `TEMPLATE_INSTANTIATION_MISMATCH` under
  `all` are folds the graded `name_check` ruler forgives.  The graded ruler is
  the one `report.json` uses and the one a population should be quoted under.
* It scans `WHERE excluded = 0`, which is 31,446 of 52,568 rows.  16,922
  functions that `report.json` scores right now were never in its denominator.
* It writes BOOLEANS, which cannot distinguish "measured and absent" from "the
  ruler could not see it" from "never measured".  All three read 0, and that is
  the mechanism behind every dead `has_*` column in this database.
* objdiff 4.2.6 split `detect_linker_merged` into five classes, so the
  `RELOC_SENSITIVE` map below names a vocabulary the binary no longer speaks:
  `LINKER_MERGED` now selects 4 functions where this script recorded 1,310.

`pattern_census.py` fixes all four: graded ruler by default with the ruler
recorded NOT NULL on every scan, `report.json` as the universe, one row per
finding with its payload, and `patch_guard.ensure_patched_tree()` as a
precondition.  See docs/analysis/2026-08-21-pattern-census-4.2.6.md.

---

Original docstring follows.

Populate the pattern flags that a reloc-blind objdiff pass cannot see.

The problem
-----------
`has_linker_merged` reads **0 on all 52,547 rows** of decomp.db while
`verdict_reason` on **708** of them literally says `LINKER_MERGED`. The flag
column and the text on the same row contradict each other.

It is not a missing detector. `sync_objdiff.py` runs objdiff-cli with
`-c functionRelocDiffs=none` -- the project's canonical ruler -- and
`detect_linker_merged` only inspects `bl`/`b` instructions whose `match_type`
is `diff_arg`, i.e. **calls whose relocation targets differ**. Under `none`
those instructions are reported as equal, so the detector is structurally
starved. Proven 2026-08-19 on one symbol, three configs:

    ?Handle@StorePanel@@UAA?AVDataNode@@PAVDataArray@@_N@Z
      -c functionRelocDiffs=none       patterns = []
      -c functionRelocDiffs=name_only  patterns = [ADDRESS_RELOCATION_NOISE]
      -c functionRelocDiffs=all        patterns = [LINKER_MERGED,
                                                   ADDRESS_RELOCATION_NOISE]

The fix
-------
Keep the canonical ruler where it belongs -- on the percentages -- and run a
SECOND, reloc-visible pass whose only job is the reloc-sensitive flags. This
script is that pass. `sync_objdiff.py` no longer writes `has_linker_merged` at
all, so the reloc-blind pass can no longer wipe what this one establishes.

Nothing here touches `current_percent`, `verdict`, or any percentage.

The precondition
----------------
Every flag here is measured from `--project-dir`'s object tree, so it is only
as settled as that tree. This script now runs
`verify_objs_patched.py --verify-manifest` first and exits 4 rather than
recording a tree that a concurrent `ninja` is rewriting -- see
`require_settled_tree` for the run that motivated it.

Usage:
    python3 scripts/backfill_reloc_patterns.py --db <path>              # dry run
    python3 scripts/backfill_reloc_patterns.py --db <path> --apply
    python3 scripts/backfill_reloc_patterns.py --db <path> --histogram  # survey only

Exit codes: 0 ok, 3 truncated by --limit, 4 unsettled build tree.
"""

import argparse
import collections
import sqlite3
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sync_objdiff import run_batch  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent

# Patterns whose detectors read relocation differences and are therefore
# invisible under the canonical functionRelocDiffs=none ruler.
# Measured 2026-08-19 over the SAME 3,000 functions, two configs:
#
#   pattern                          none (canonical)   all
#   LINKER_MERGED                            0          119
#   PROLOGUE_MISMATCH                        0           20
#   ANONYMOUS_NAMESPACE_HASH                 0           11
#   SCOPE_COUNTER_MISMATCH                   0            9
#   MAKE_STRING_TEMPLATE_MISMATCH            0            7
#   STATIC_GUARD_COUNTER                     0            2
#   ADDRESS_RELOCATION_NOISE                46         1108
#
# Four of the "identically 0" columns were never dead detectors -- they were
# reloc-starved. (ANONYMOUS_NAMESPACE_HASH and STATIC_GUARD_COUNTER already had
# nonzero counts DB-wide, so they are not part of the dead-column set; they are
# listed here only to show the effect is not specific to the dead four.)
#
# The MakeString pattern additionally has a SPELLING bug: `patterns[].pattern`
# in the JSON is serde-derived from the Rust enum under
# rename_all="SCREAMING_SNAKE_CASE", which splits the internal capital in
# "String" and emits MAKE_STRING_TEMPLATE_MISMATCH, while PatternType::to_str
# (used for patterns_checked and the human output) returns
# MAKESTRING_TEMPLATE_MISMATCH. sync_objdiff compared against the second and
# so could never set has_makestring_mismatch on any row. It now canonicalises
# both spellings onto the to_str one, which is what the keys below use.
RELOC_SENSITIVE = {
    "LINKER_MERGED": "has_linker_merged",
    "PROLOGUE_MISMATCH": "has_prologue_mismatch",
    "SCOPE_COUNTER_MISMATCH": "has_scope_counter_mismatch",
    "MAKESTRING_TEMPLATE_MISMATCH": "has_makestring_mismatch",
    "ALLOCA_MISMATCH": "has_alloca_mismatch",
    "DYNAMIC_CAST_MISMATCH": "has_dynamic_cast_mismatch",
}


def require_settled_tree(project_dir: str, skip: bool) -> None:
    """Refuse to measure a build tree that is being rewritten under us.

    2026-08-19: this script recorded 1,310 `has_linker_merged` rows where a
    settled clean worktree gives 1,052 and main's tree 1,069 -- and the two
    settled trees agree with each other on 1,051. The flags carry `updated_at`
    between 09:11:03 and 09:11:36, against main commits at 09:12/09:16/09:17/
    09:18 with at least two other worktrees mid-`ninja`. The only bucket that
    reproduced exactly (MAKESTRING) is the one whose detector does not read
    patched symbol names. Nothing was wrong with the scan: it measured a moving
    tree and wrote the result into a column that reads like a fact about the
    build. See docs/analysis/2026-08-19-reloc-pattern-flag-triage.md finding 2.

    `verify_objs_patched.py --verify-manifest` is the cheap outside check: it
    recomputes `build/<version>/patch_state.json` and reports drift with no
    toolchain and no COFF parsing. Exit 0 = settled, 1 = drifted, 2 = never
    verified (no manifest).
    """
    if skip:
        print(f"WARNING: --skip-verify -- writing flags measured from "
              f"{project_dir} WITHOUT checking that the tree is settled. Any "
              f"row this run writes is a fact about a moment, not the build.",
              file=sys.stderr)
        return
    verifier = Path(__file__).resolve().parent / "verify_objs_patched.py"
    proc = subprocess.run(
        [sys.executable, str(verifier), "--repo", project_dir,
         "--verify-manifest", "--quiet"])
    if proc.returncode == 0:
        return
    why = {1: "the tree DRIFTED since it was last verified patched",
           2: "the tree has NEVER been verified patched (no manifest)"}.get(
               proc.returncode, f"the verifier exited {proc.returncode}")
    print(f"\nREFUSING to backfill reloc pattern flags: {why}.\n"
          f"  tree: {project_dir}\n"
          f"These flags are read back as facts about the build, so measuring a "
          f"tree that another `ninja` is rewriting silently records a moment "
          f"instead (2026-08-19: 1,310 LINKER_MERGED rows where two settled "
          f"trees agree on 1,051).\n"
          f"Fix: run a full `ninja` in that tree, wait for concurrent builds to "
          f"finish, and re-run. To record a survey anyway (never with --apply "
          f"unless you mean it), pass --skip-verify.", file=sys.stderr)
    sys.exit(4)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"))
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--reloc-config", default="all",
                    help="functionRelocDiffs value for the pattern pass "
                         "(default: all)")
    ap.add_argument("-j", "--jobs", type=int, default=8)
    ap.add_argument("--limit", type=int, default=None,
                    help="Only scan the first N functions (for a quick probe)")
    ap.add_argument("--apply", action="store_true",
                    help="Write the flags (default: dry run)")
    ap.add_argument("--histogram", action="store_true",
                    help="Print the full pattern histogram under this config "
                         "and exit without writing")
    ap.add_argument("--skip-verify", "--force", dest="skip_verify",
                    action="store_true",
                    help="Skip the verify_objs_patched.py --verify-manifest "
                         "precondition on --project-dir. The flags then "
                         "describe whatever the tree was at that minute; see "
                         "the 2026-08-19 triage. Exit 4 == unsettled tree.")
    args = ap.parse_args()

    if args.apply:
        print(
            "REFUSING --apply: this script is superseded by\n"
            "    python3 scripts/analysis/pattern_census.py --ruler name_check --apply\n\n"
            "It measures under functionRelocDiffs=all (charges the adjudicated "
            "/OPT:ICF folds), scans only WHERE excluded = 0 (omitting 16,922 "
            "functions report.json scores), writes booleans that cannot "
            "distinguish an absent pattern from an unmeasurable one, and its "
            "RELOC_SENSITIVE map names objdiff's pre-4.2.6 vocabulary. "
            "--histogram still works. "
            "See docs/analysis/2026-08-21-pattern-census-4.2.6.md.",
            file=sys.stderr)
        return 2

    require_settled_tree(args.project_dir, args.skip_verify)

    conn = sqlite3.connect(args.db)
    rows = conn.execute(
        "SELECT id, symbol FROM functions WHERE excluded = 0 ORDER BY id"
    ).fetchall()
    # `rows = rows[:args.limit]` ran BEFORE the count below, so `--limit`
    # silently rewrote the denominator this tool then wrote back to the DB.
    universe = len(rows)
    truncated = bool(args.limit) and args.limit < universe
    if args.limit:
        rows = rows[:args.limit]
    if truncated:
        print(f"TRUNCATED by --limit: scanning {len(rows)} of {universe} "
              f"functions with functionRelocDiffs={args.reloc_config} -- "
              f"the histogram below is a SAMPLE, and only the scanned rows "
              f"are updated")
    else:
        print(f"Scanning {len(rows)} of {universe} functions with "
              f"functionRelocDiffs={args.reloc_config} ...")

    # run_batch now returns (results, line_stats); the stats count the JSONL
    # lines the workers discarded, which used to be two bare `continue`s.
    results, line_stats = run_batch([(i, s) for i, s in rows], args.project_dir,
                                    jobs=args.jobs, reloc_config=args.reloc_config)
    if line_stats:
        print("  objdiff JSONL lines: "
              + ", ".join(f"{k}={v}" for k, v in sorted(line_stats.items())),
              file=sys.stderr)

    hist = collections.Counter()
    for r in results:
        for p in (r.detected_patterns or []):
            hist[p] += 1
    print(f"\nPattern histogram ({len(results)} results of {universe} "
          f"functions in the DB):")
    for k, v in hist.most_common():
        mark = "  <- reloc-sensitive" if k in RELOC_SENSITIVE else ""
        print(f"  {k:34s} {v:6d}{mark}")

    if args.histogram:
        return 3 if truncated else 0

    updates = []
    for r in results:
        pats = set(r.detected_patterns or [])
        for pattern, column in RELOC_SENSITIVE.items():
            updates.append((1 if pattern in pats else 0, r.db_id, column))

    by_col = collections.Counter()
    for value, _db_id, column in updates:
        if value:
            by_col[column] += 1
    # `before` counts the WHOLE table; `by_col` counts only what was scanned.
    # Under --limit those are different denominators, so say which is which
    # rather than rendering them as a before/after pair.
    print("\nWould set:")
    for column in RELOC_SENSITIVE.values():
        before = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {column} = 1").fetchone()[0]
        if truncated:
            print(f"  {column}: {before} set across all {universe} rows "
                  f"-> {by_col[column]} within the {len(results)} scanned")
        else:
            print(f"  {column}: {before} -> {by_col[column]}")

    if not args.apply:
        print("\n(dry run) Re-run with --apply to write.")
        return 3 if truncated else 0

    for column in RELOC_SENSITIVE.values():
        payload = [(v, i) for v, i, c in updates if c == column]
        conn.executemany(
            f"UPDATE functions SET {column} = ?, updated_at = CURRENT_TIMESTAMP "
            f"WHERE id = ?", payload)
    conn.commit()
    for column in RELOC_SENSITIVE.values():
        n = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {column} = 1").fetchone()[0]
        print(f"\nApplied: {column} = 1 on {n} rows "
              f"({len(results)} of {universe} functions were rescanned)")
    # Exit 3 == TRUNCATED, matching scripts/analysis/coverage.py.
    return 3 if truncated else 0


if __name__ == "__main__":
    sys.exit(main())
