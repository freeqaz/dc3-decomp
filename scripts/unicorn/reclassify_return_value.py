#!/usr/bin/env python3
"""Re-classify `unicorn_class='return_value'` rows that are blind to return type.

The comparator used to diff `r3` without knowing whether `r3` was the function's
return register, and `classify_divergence()` labelled any difference
`return_value` -- documented as "integer return value mismatch (real bug)".

On the Xenon PPC ABI a **void** function has no return register at all and a
**float/double** function returns in **f1**. Their `r3` is scratch: two correct
compilations may leave different values there for free.

Measured on decomp.db, 2026-08-19 -- of the 10 `return_value` rows, **5 were
mislabelled** (4 void + 1 float), and 4 of those 5 are the entire sub-100% band
where the class could have changed anyone's mind:

    void   55.69  ?Transform@CSHA1@@AAAXPAIPBE@Z
    float  80.57  ?CompareSkeletonJointDisplacement@FreestyleMoveRecorder@@...
    void   81.47  ?getMasher@KeyChain@@YAXPAE@Z
    void   83.92  ?Reset@EQEffect@@QAAXXZ
    void  100.00  ?getKeyImpl@@YAXPAEPAD0@Z

They become `scratch_return_reg` -- still DIVERGENT, still visible, just not
filed as a return-value bug. The three `rijndael_*` rows are plain C symbols
with no encoded return type; they are deliberately left alone rather than
guessed at.

Usage:
    python3 scripts/unicorn/reclassify_return_value.py --db <path>            # dry run
    python3 scripts/unicorn/reclassify_return_value.py --db <path> --apply
"""

import argparse
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from scripts.unicorn_runner.rettype import return_type_class  # noqa: E402

NEW_CLASS = "scratch_return_reg"
REASON_TAG = "reclassified: r3 is not the return register"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--db", default="decomp.db", help="Path to decomp.db")
    ap.add_argument("--apply", action="store_true",
                    help="Write the changes (default: dry run)")
    args = ap.parse_args()

    conn = sqlite3.connect(args.db)
    conn.row_factory = sqlite3.Row
    rows = conn.execute(
        "SELECT id, symbol, demangled, current_percent, unicorn_reason "
        "FROM functions WHERE unicorn_class = 'return_value' "
        "ORDER BY current_percent"
    ).fetchall()

    if not rows:
        print("No rows with unicorn_class='return_value'. Nothing to do.")
        return 0

    updates, kept, unknown = [], [], []
    for r in rows:
        rc = return_type_class(r["symbol"], r["demangled"])
        if rc in ("void", "float"):
            updates.append((r, rc))
        elif rc is None:
            unknown.append(r)
        else:
            kept.append((r, rc))

    def show(title, items, get_rc):
        print(f"\n{title} ({len(items)}):")
        for item in items:
            row = item[0] if isinstance(item, tuple) else item
            pct = row["current_percent"]
            print(f"  {str(get_rc(item)):7s} {pct if pct is None else f'{pct:7.2f}'}"
                  f"  {row['symbol'][:78]}")

    show("RE-CLASSIFY -> scratch_return_reg", updates, lambda i: i[1])
    show("KEPT as return_value (r3 IS the return register)", kept, lambda i: i[1])
    show("LEFT ALONE (return type not encoded in the symbol)", unknown,
         lambda i: "?")

    if not args.apply:
        print(f"\n(dry run) {len(updates)} row(s) would change. "
              f"Re-run with --apply.")
        return 0

    for row, rc in updates:
        reason = row["unicorn_reason"]
        note = f"{REASON_TAG} (return type: {rc})"
        conn.execute(
            "UPDATE functions SET unicorn_class = ?, unicorn_reason = ? "
            "WHERE id = ?",
            (NEW_CLASS, note if not reason else f"{reason}; {note}", row["id"]),
        )
    conn.commit()
    print(f"\nApplied: {len(updates)} row(s) -> unicorn_class='{NEW_CLASS}'")
    return 0


if __name__ == "__main__":
    sys.exit(main())
