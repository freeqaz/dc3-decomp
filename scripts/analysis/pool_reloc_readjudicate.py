#!/usr/bin/env python3
"""Re-adjudicate decomp.db verdicts over the pool-reloc phantom-row population.

Every verdict on this population was decided while `objdiff-cli diff` charged
mismatch rows that `report generate` did not -- rows synthesized by
`ppc.calculatePoolRelocations`, which reconstructs "fake" relocations from each
object's own symbol table and therefore disagrees between a dtk-carved target
obj and our per-TU COMDAT obj.  `sync_objdiff.py`'s "auto: all mismatches
unfixable" walked exactly those rows.

This script decides each row on the CORRECTED evidence and writes the verdict.
Three buckets, and the discriminator is the ROW SET, not the percentage:

  evidence_intact   rows_phantom == 0.  The config change did not alter which
                    rows exist; only the score moved, because the synthesized
                    annotation lands in arg_diff_score without creating a row.
                    Whatever the verdict rested on, it was not this defect.
                    NOT rewritten -- there is nothing here to correct, and
                    stamping 158 rows with a fresh timestamp would launder an
                    old judgement as a new one.

  evidence_shrank   rows_phantom > 0 and rows_now > 0.  The verdict cited a row
                    set that partly did not exist.  AT_LIMIT is cleared to NULL
                    (workable) with the exact before/after row counts recorded,
                    unless --floor names the symbol as independently confirmed.

  matched           rows_now == 0 AND report.json says 100.0.  COMPLETE.

Requires a report.json rebuilt AFTER any source change, and refuses to run
against one older than the newest object it can see.
"""

from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from datetime import datetime, timezone
from pathlib import Path

# Symbols re-adjudicated BY HAND this lane, with the codegen evidence that keeps
# them at a floor.  A name here keeps AT_LIMIT even though its evidence set
# shrank -- but only because someone looked, and the reason says what they saw.
CONFIRMED_FLOORS: dict[str, str] = {
    "yylex":
        "flex DFA table divergence: target `cmpwi cr6, r7, 0x7d` vs our 0x7b -- "
        "our generated scanner has 2 fewer DFA states, so every yy_accept/yy_ec/"
        "yy_base/yy_nxt table is a different size and every table base carries a "
        "different displacement (0x10/0x18/0x110/...). Not reachable by source "
        "spelling; needs the original .l grammar regenerated.",
    "yy_get_previous_state":
        "same flex DFA table divergence as yylex (target `cmpwi cr6, r7, 0x7d` vs "
        "0x7b, 2 fewer DFA states in our generated scanner).",
}


# Symbols whose real rows were read BY HAND this lane.  These get the specific
# lead instead of the generic "not re-triaged" note, because someone looked.
HAND_ADJUDICATED: dict[str, str] = {
    "?MemPushTemp@@YAXXZ":
        "Real bug found and fixed this lane: the guard was `gNumHeaps != 0 && "
        "gNumHeaps > 0` (a tautology) where the target reads gInitted at "
        "&gNumHeaps-0x13. 83.33 -> 91.46. REMAINING cause is known and specific: "
        "the target forms &gNumHeaps ONCE and reaches gInitted at -0x13 off it; "
        "our gInitted is not adjacent in .data so MSVC emits a second lis/lbz. "
        "Closing it needs MemMgr.cpp's four packed bools moved ahead of the ints, "
        "which that file's header comment says was tuned to preserve the "
        "+0xbd4/+0xbd8 displacements ThreadMemStack depends on.",
    "?MemPopTemp@@YAXXZ":
        "Same gInitted guard bug as MemPushTemp, fixed this lane: 91.11 -> 95.44. "
        "Same remaining cause (gInitted not adjacent to gNumHeaps in our .data).",
    "?DrawMeterScale@Synth@@QAAXAAM@Z":
        "Real rows read this lane. Two causes, neither a phantom: (1) the target "
        "loads its 0.2f from a pooled data label (lbl_820B4590) where we emit our "
        "own __real@3e4ccccd COMDAT; (2) a 3-instruction scheduling permutation of "
        "the constant setup around the TheRnd load. Not adjudicated as a floor -- "
        "unverified, but no longer resting on phantom rows.",
}


def find_report(project: Path) -> Path:
    cands = sorted((project / "build").glob("*/report.json"))
    if not cands:
        raise SystemExit(f"no build/<title>/report.json under {project} -- run ninja")
    return max(cands, key=lambda p: p.stat().st_mtime)


def assert_report_fresh(project: Path, report: Path) -> None:
    """Refuse a report older than the newest object it claims to describe."""
    rt = report.stat().st_mtime
    newest, newest_p = 0.0, None
    for obj in (project / "build").rglob("src/**/*.obj"):
        m = obj.stat().st_mtime
        if m > newest:
            newest, newest_p = m, obj
    if newest > rt + 1.0:
        raise SystemExit(
            f"STALE REPORT: {report} is older than {newest_p}\n"
            f"  report {datetime.fromtimestamp(rt)} < obj {datetime.fromtimestamp(newest)}\n"
            "Run a full `ninja` before re-adjudicating."
        )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--db", required=True)
    ap.add_argument("--rows", required=True, help="pool_reloc_rows.py --out JSON")
    ap.add_argument("--apply", action="store_true", help="write (default: dry run)")
    args = ap.parse_args()

    repo = Path(args.repo).resolve()
    report = find_report(repo)
    assert_report_fresh(repo, report)

    with report.open() as fh:
        rep = json.load(fh)
    canon: dict[str, float] = {}
    for u in rep["units"]:
        for f in u.get("functions", []):
            canon[f["name"]] = f["match_percent_normalized"]

    rows = json.load(open(args.rows))
    stamp = datetime.now(timezone.utc).strftime("%Y-%m-%d")

    plan = []
    buckets = {"matched": 0, "evidence_shrank": 0, "evidence_intact": 0,
               "confirmed_floor": 0, "no_change": 0}

    for r in rows:
        sym, v = r["symbol"], r["verdict"]
        mpn = canon.get(sym)
        if mpn is None:
            continue
        n, o, ph = r["rows_now"], r["rows_old"], r["rows_phantom"]

        if n == 0 and mpn >= 100.0:
            buckets["matched"] += 1
            if v != "COMPLETE":
                plan.append((sym, "COMPLETE",
                             f"re-adjudicated {stamp}: 0 real mismatch rows "
                             f"({o} charged under the pre-fix ruler, all phantom); "
                             f"report.json match_percent_normalized={mpn:.6f}. "
                             f"Complete modulo register permutation -- the canonical "
                             f"ruler forgives regalloc, so this is a zero-mismatch "
                             f"count, not a byte-identity claim.", mpn))
            else:
                buckets["no_change"] += 1
            continue

        if sym in CONFIRMED_FLOORS:
            buckets["confirmed_floor"] += 1
            plan.append((sym, "AT_LIMIT",
                         f"re-adjudicated {stamp} on the corrected ruler: {o} charged "
                         f"rows -> {n} real ({ph} phantom pool-relocation rows). "
                         f"Still at a floor. {CONFIRMED_FLOORS[sym]}", mpn))
            continue

        if ph > 0:
            buckets["evidence_shrank"] += 1
            if v == "AT_LIMIT":
                plan.append((sym, None,
                             f"AT_LIMIT cleared {stamp}: the verdict was computed over "
                             f"{o} mismatch rows, {ph} of which did not exist -- "
                             f"synthesized ppc.calculatePoolRelocations rows charged by "
                             f"`objdiff-cli diff` but not by `report generate` "
                             f"(fixed a2debafdb). {n} real rows remain, "
                             f"match_percent_normalized={mpn:.6f}. "
                             + HAND_ADJUDICATED.get(
                                 sym,
                                 "NOT re-triaged; workable pending a look at the "
                                 "real rows."), mpn))
            else:
                buckets["no_change"] += 1
        else:
            buckets["evidence_intact"] += 1
            buckets["no_change"] += 1

    print(f"# report: {report}  (fresh)")
    print(f"# rows examined: {len(rows)}")
    for k, v in buckets.items():
        print(f"#   {k:18s} {v}")
    print(f"# verdict writes planned: {len(plan)}")
    print()
    for sym, verdict, reason, mpn in plan:
        print(f"  {str(verdict):9s} {mpn:9.5f}  {sym[:70]}")

    if not args.apply:
        print("\nDRY RUN -- pass --apply to write.")
        return 0

    con = sqlite3.connect(args.db)
    cur = con.cursor()
    written = 0
    for sym, verdict, reason, mpn in plan:
        cur.execute(
            "UPDATE functions SET verdict = ?, verdict_reason = ?, "
            "match_percent_normalized = ?, updated_at = CURRENT_TIMESTAMP "
            "WHERE symbol = ?",
            (verdict, reason, mpn, sym))
        written += cur.rowcount
    con.commit()
    con.close()
    print(f"\nwrote {written} rows to {args.db}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
