#!/usr/bin/env python3
"""Populate the pattern flags that a reloc-blind objdiff pass cannot see.

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

Usage:
    python3 scripts/backfill_reloc_patterns.py --db <path>              # dry run
    python3 scripts/backfill_reloc_patterns.py --db <path> --apply
    python3 scripts/backfill_reloc_patterns.py --db <path> --histogram  # survey only
"""

import argparse
import collections
import sqlite3
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
    args = ap.parse_args()

    conn = sqlite3.connect(args.db)
    rows = conn.execute(
        "SELECT id, symbol FROM functions WHERE excluded = 0 ORDER BY id"
    ).fetchall()
    if args.limit:
        rows = rows[:args.limit]
    print(f"Scanning {len(rows)} functions with "
          f"functionRelocDiffs={args.reloc_config} ...")

    results = run_batch([(i, s) for i, s in rows], args.project_dir,
                        jobs=args.jobs, reloc_config=args.reloc_config)

    hist = collections.Counter()
    for r in results:
        for p in (r.detected_patterns or []):
            hist[p] += 1
    print(f"\nPattern histogram ({len(results)} results):")
    for k, v in hist.most_common():
        mark = "  <- reloc-sensitive" if k in RELOC_SENSITIVE else ""
        print(f"  {k:34s} {v:6d}{mark}")

    if args.histogram:
        return 0

    updates = []
    for r in results:
        pats = set(r.detected_patterns or [])
        for pattern, column in RELOC_SENSITIVE.items():
            updates.append((1 if pattern in pats else 0, r.db_id, column))

    by_col = collections.Counter()
    for value, _db_id, column in updates:
        if value:
            by_col[column] += 1
    print("\nWould set:")
    for column in RELOC_SENSITIVE.values():
        before = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {column} = 1").fetchone()[0]
        print(f"  {column}: {before} -> {by_col[column]}")

    if not args.apply:
        print("\n(dry run) Re-run with --apply to write.")
        return 0

    for column in RELOC_SENSITIVE.values():
        payload = [(v, i) for v, i, c in updates if c == column]
        conn.executemany(
            f"UPDATE functions SET {column} = ?, updated_at = CURRENT_TIMESTAMP "
            f"WHERE id = ?", payload)
    conn.commit()
    for column in RELOC_SENSITIVE.values():
        n = conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {column} = 1").fetchone()[0]
        print(f"\nApplied: {column} = 1 on {n} rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
