#!/usr/bin/env python3
"""One-time reset of false COMPLETE functions caused by the base_size=0 objdiff bug.

748 functions were falsely marked COMPLETE during initial DB population
(Jan 2026) when objdiff reported 100% match for functions where the original
binary's section had 0 bytes.  This script resets them to workable state.

⚠ IT ATE ITS OWN OUTPUT.  READ THIS BEFORE RE-ENABLING ANYTHING
================================================================
The selector was

    WHERE verdict_reason LIKE '%base\\_size=0%' AND verdict = 'COMPLETE'

and the repair wrote

    verdict_reason = 'reset: was false COMPLETE from base_size=0 objdiff bug'

which CONTAINS the substring the selector matches on.  The only thing keeping
the script from re-firing on its own output was `AND verdict = 'COMPLETE'` --
a condition the same statement had just cleared.  So the script was a LATCH:
the moment any other writer legitimately re-promoted one of those rows
(`sync_match_percent.py --promote`, `sync_objdiff.py`, `batch_check.py` -- none
of which touch `verdict_reason`), the row matched again, and the next run
silently demoted a genuinely-matched function AND NULLED ITS current_percent.

Measured on the live decomp.db, 2026-08-22, before any edit:

    would demote right now                                  385
      ...carrying THIS SCRIPT'S OWN reset marker            367
      ...with match_percent_normalized >= 100 (matched!)    379
      ...with current_percent >= 100                        383

e.g. `?MakeRotMatrixX@@YAXMAAVMatrix3@Hmx@@@Z` at norm 100.0, unit
default/system/char/CharBonesMeshes.  The old script would have destroyed 385
measurements and printed one line: "Reset 385 functions to workable state."
No --dry-run existed, no --db existed (DB_PATH was hardcoded, so it could not
even be pointed at a copy), and the report was indistinguishable from the
intended one-time repair.

Nothing in the tree writes `base_size=0` into `verdict_reason` any more (the
only writers are batch_promote.py, find_hidden_work.py, sync_objdiff.py,
unicorn/classify_at_limit.py and this file), so every remaining match is a
FOSSIL of the original repair, not a fresh false COMPLETE.

Three changes make it unable to do that again:

  * `--apply` is REQUIRED.  The default is a report.
  * the reason it writes no longer contains its own selector, and rows already
    carrying a reset marker are refused outright (`SELF_MARKER_RE`).
  * a row with a real measurement at 100 is PROTECTED and counted, never
    demoted.  "This row was re-matched" and "this row is a Jan-2026 fossil" are
    different facts and the old code could not tell them apart.

And it reports its denominator, because "No false COMPLETE functions found.
Already reset?" collapsed four states -- already reset / never populated /
wrong DB / the marker convention was abandoned -- into one reassuring line.
"""
import argparse
import sqlite3
import sys
from collections import Counter
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: Rows the ORIGINAL bug left behind.  `\_` because `_` is a LIKE wildcard.
BUG_MARKER_SQL = r"verdict_reason LIKE '%base\_size=0%' ESCAPE '\'"

#: Rows THIS SCRIPT wrote.  Historically these also match BUG_MARKER_SQL --
#: that is the latch.  They are subtracted explicitly rather than relied on to
#: fail the verdict test, because the verdict test is the part that stopped
#: holding.
SELF_MARKER_SQL = "verdict_reason LIKE 'reset:%'"

#: Deliberately contains NO substring matched by BUG_MARKER_SQL.
NEW_REASON = ("reset: false COMPLETE from the objdiff zero-length-target-"
              "section bug (Jan 2026); re-measure before re-promoting")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--db", default=str(REPO / "decomp.db"),
                    help="database to operate on (default: this repo's). In a "
                         "worktree, pass the main repo's path -- the local "
                         "decomp.db is a deliberate tripwire.")
    ap.add_argument("--apply", action="store_true",
                    help="actually write. Without it this only reports.")
    ap.add_argument("--include-unmeasured", action="store_true",
                    help="also reset rows whose match_percent_normalized is "
                         "NULL. 'never measured' is a third state, not a "
                         "synonym for 'measured below 100', so it is opt-in.")
    a = ap.parse_args()

    db = Path(a.db)
    if not db.exists():
        print(f"Error: Database not found: {db}", file=sys.stderr)
        return 1

    conn = sqlite3.connect(str(db))
    conn.row_factory = sqlite3.Row

    def count(where: str) -> int:
        return conn.execute(
            f"SELECT COUNT(*) FROM functions WHERE {where}").fetchone()[0]

    # ---- denominator, always, whatever the answer turns out to be ----------
    total = count("1=1")
    marked = count(BUG_MARKER_SQL)
    marked_complete = count(f"{BUG_MARKER_SQL} AND verdict = 'COMPLETE'")
    self_marked = count(f"{BUG_MARKER_SQL} AND {SELF_MARKER_SQL}")
    # ⚠ The evidence is `match_percent_normalized`, NEVER `current_percent`.
    # current_percent is the column the base_size=0 bug FALSIFIED -- protecting
    # on it would protect the entire original population and make this script a
    # permanent no-op. Caught by the test: a fossil row (norm 43.2,
    # current_percent 100.0) was shielded by the first draft of this guard.
    remaining = f"{BUG_MARKER_SQL} AND verdict = 'COMPLETE' AND NOT ({SELF_MARKER_SQL})"
    protected = count(f"{remaining} AND match_percent_normalized >= 100")
    unmeasured = count(f"{remaining} AND match_percent_normalized IS NULL")

    print(f"database          : {db}")
    print(f"functions rows    : {total:,}")
    # Print the CONSTANT, never a hand-copied paraphrase of it.  This line used
    # to spell the selector out as `verdict_reason LIKE '%base_size=0%'`, which
    # is a DIFFERENT query from the one above it -- unescaped, `_` is a
    # single-char wildcard, so the printed form also matches `base-size=0`,
    # `base size=0`, ... A reader auditing "which rows did it count?" against
    # that line would have been auditing a query the script never ran.
    # (honesty_lint E1 flagged the literal; the fix is to stop having two.)
    print(f"carry the marker  : {marked:,}   ({BUG_MARKER_SQL})")
    print(f"  ...and COMPLETE : {marked_complete:,}")
    print(f"  ...SELF-MARKED  : {self_marked:,}   <- written by THIS script; "
          f"the latch. Never eligible.")
    print(f"  ...PROTECTED    : {protected:,}   <- match_percent_normalized "
          f">= 100: re-matched since, not a fossil. Never eligible.")
    print(f"  ...NEVER MEASURED: {unmeasured:,}  <- match_percent_normalized "
          f"IS NULL. Not the same as 'measured below 100'; needs "
          f"--include-unmeasured.")

    eligible = (f"{remaining} AND match_percent_normalized IS NOT NULL "
                f"AND match_percent_normalized < 100")
    if a.include_unmeasured:
        eligible = (f"{remaining} AND (match_percent_normalized IS NULL OR "
                    f"match_percent_normalized < 100)")
    rows = conn.execute(
        f"SELECT id, symbol, unit, current_percent, match_percent_normalized "
        f"FROM functions WHERE {eligible}"
    ).fetchall()

    print(f"ELIGIBLE TO RESET : {len(rows):,}"
          f"{'  (including never-measured)' if a.include_unmeasured else ''}")

    if not rows:
        print("\nNothing eligible. Note this is NOT the same statement as "
              "'already reset': the counts above say which of the four states "
              "you are in (fossils present but protected / already reset / the "
              "marker convention was never populated in this DB / wrong DB).")
        return 0

    unit_counts = Counter(r["unit"] for r in rows)
    print("\nTop affected units:")
    for unit, c in unit_counts.most_common(15):
        print(f"  {c:4d}  {unit}")

    if not a.apply:
        print(f"\nDRY RUN -- pass --apply to write. {len(rows)} row(s) would "
              f"have verdict cleared and current_percent NULLed.")
        return 0

    ids = [r["id"] for r in rows]
    cur = conn.executemany(
        "UPDATE functions SET verdict = NULL, verdict_reason = ?, "
        "current_percent = NULL WHERE id = ?",
        [(NEW_REASON, i) for i in ids])
    conn.commit()
    # The rowcount is READ, not assumed. An UPDATE that matched nothing used to
    # be reported as a success with the planned count.
    changed = cur.rowcount
    conn.close()
    if changed != len(ids):
        print(f"\nWARNING: planned {len(ids)} updates, sqlite reports "
              f"{changed} rows changed.", file=sys.stderr)
        return 1
    print(f"\nReset {changed} functions to workable state.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
