#!/usr/bin/env python3
"""Write the task-#134 verdict re-adjudication to decomp.db.

Two independent populations, derived separately and never merged into one count.

POPULATION A -- callee-divergence findings vs. verdicts (138 functions)
======================================================================
Derived by ``readjudicate_callee_verdicts.py`` (read its docstring for the
method).  Only the buckets below are written; ``UNRESOLVED_anon_ns`` and every
row that carries no verdict are deliberately left alone.

  PRIZE_crosses_row        verdict -> NULL.  Byte-identical except one relocation
  FIXABLE_wrong_callee     naming a function at a DIFFERENT address in the shipped
  FIXABLE_call_order       map.  Open, fixable source work the issuing instrument
                           was structurally unable to see.  floor_cert* columns are
                           NOT cleared -- the unicorn evidence they hold is still
                           true, it just never was a matchability floor.
  UNFIXABLE_merged_stub    verdict KEPT, reason corrected: our side calls a
                           synthesised merged_* stub (#112's refusal class), so
                           AT_LIMIT stands for a real reason that was not recorded.
  FINDING_NOT_INFORMATIVE  verdict KEPT, reason annotated: below 60% the aligner
  RIGHT_icf_fold           pairs bl's arbitrarily / the linker folded the two names
  RIGHT_pairing_artifact   / the enclosing symbol is a byte-signature-guessed EH
                           funclet.  The verdict was right all along; the note
                           exists so the next reader does not re-open it.

POPULATION B -- DB hygiene owed by the ICF fold-survivor rename (e5b1e3ce7)
==========================================================================
That merge rewrote 682 lines of config/373307D9/symbols.txt so dtk assigns the
correct member of each fold class, retiring 341 spellings and introducing 341.

  B1  99 retired spellings still carry AT_LIMIT -- "this function is unfixable"
      attached to a name the splitter no longer emits.  All 99 come back
      objdiff-not_found from a batch sweep, so 0 are scored by report.json.
      -> verdict NULL + excluded=1, prior verdict preserved in the reason.
  B2  328 of the NEW spellings sit at 100.0 normalized carrying NO verdict.  A
      sweep under name_check/4.2.7 gives all 328 zero mismatched instructions AND
      diff_score 0 -- byte identity, not a rounded headline and not "modulo
      register permutation".
      -> verdict COMPLETE.

Usage:
    python3 scripts/analysis/apply_verdict_readjudication.py \
        --adjudicated /tmp/t134-readjudicated.json \
        --popb /tmp/t134-popB2.json \
        --db /home/free/code/milohax/dc3-decomp/decomp.db [--apply]

Dry-run by default.  Run scripts/backup-db.sh first.
"""
from __future__ import annotations

import argparse
import collections
import json
import sqlite3
from datetime import date
from pathlib import Path

TAG = f"t134 {date.today().isoformat()}"

#: bucket -> (new_verdict_or_KEEP, evidence sentence)
ACTIONS = {
    "PRIZE_crosses_row": (
        None,
        "wrong callee is the ONLY defect: zero mismatches under the blind ruler, "
        "one charged relocation under name_check; closing it crosses the row"),
    "FIXABLE_wrong_callee": (
        None,
        "target and base callee sit at DIFFERENT addresses in ham_xbox_r.map "
        "(or the base name is absent from it): we call other code"),
    "FIXABLE_call_order": (
        None,
        "same two callees diverge in BOTH directions: a call-ORDER swap, "
        "fixable source work"),
    "UNFIXABLE_merged_stub": (
        "KEEP",
        "our side calls a synthesised merged_* stub (#112 refusal class); "
        "AT_LIMIT stands, but for this reason and not the one recorded"),
    "FINDING_NOT_INFORMATIVE": (
        "KEEP",
        "below 60% canonical the instruction aligner pairs bl's arbitrarily; "
        "the callee finding convicts nothing either way"),
    "RIGHT_icf_fold": (
        "KEEP",
        "every divergent callee pair shares one address in ham_xbox_r.map -- "
        "/OPT:ICF fold; the gap is scripts/symbol_aliases.json, not the source"),
    "RIGHT_pairing_artifact": (
        "KEEP",
        "enclosing symbol is an MSVC EH funclet objdiff paired by byte signature "
        "(objdiff 4.2.7 co-reports UNVERIFIABLE_PAIRING); the bl is not a call"),
}


def note(bucket: str, evidence: str, row: dict) -> str:
    was = row.get("db_verdict") or "no verdict"
    old = (row.get("db_reason") or "").strip()
    tail = f"; was {was}" + (f" [{old[:90]}]" if old else "")
    return f"{TAG}: {bucket} -- {evidence}{tail}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--adjudicated", required=True)
    ap.add_argument("--popb", required=True)
    ap.add_argument("--db", required=True)
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args(argv)

    rows = json.loads(Path(args.adjudicated).read_text())
    popb = json.loads(Path(args.popb).read_text())
    con = sqlite3.connect(args.db)
    cur = con.cursor()
    plan: collections.Counter = collections.Counter()

    # ---- population A -------------------------------------------------------
    for r in rows:
        b = r["bucket"]
        if b not in ACTIONS:
            plan[f"A skip {b}"] += 1
            continue
        new, evidence = ACTIONS[b]
        if new == "KEEP":
            if r["db_verdict"] is None:          # nothing to defend, nothing to note
                plan[f"A skip {b} (no verdict)"] += 1
                continue
            plan[f"A annotate {b} ({r['db_verdict']})"] += 1
            if args.apply:
                cur.execute("UPDATE functions SET verdict_reason=?, updated_at=CURRENT_TIMESTAMP"
                            " WHERE symbol=?", (note(b, evidence, r), r["symbol"]))
        else:
            if r["db_verdict"] is None:
                plan[f"A skip {b} (already no verdict)"] += 1
                continue
            plan[f"A demote {b} ({r['db_verdict']} -> NULL)"] += 1
            if args.apply:
                cur.execute("UPDATE functions SET verdict=NULL, verdict_reason=?,"
                            " updated_at=CURRENT_TIMESTAMP WHERE symbol=?",
                            (note(b, evidence, r), r["symbol"]))

    # ---- population B1: dead certificates on retired spellings ---------------
    for r in popb["deadcerts"]:
        plan["B1 void AT_LIMIT on retired spelling"] += 1
        reason = (f"{TAG}: symbol spelling retired by the /OPT:ICF fold-survivor rename "
                  f"e5b1e3ce7 (2026-08-21); dtk no longer emits it and a name_check sweep "
                  f"returns objdiff-not_found, so report.json does not score it. "
                  f"Certificate voided; was AT_LIMIT"
                  + (f" [{(r.get('verdict_reason') or '').strip()[:90]}]"
                     if (r.get("verdict_reason") or "").strip() else ""))
        if args.apply:
            cur.execute(
                "UPDATE functions SET verdict=NULL, verdict_reason=?, excluded=1,"
                " exclusion_reason=?, updated_at=CURRENT_TIMESTAMP WHERE symbol=?",
                (reason, "retired symbol spelling (ICF fold-survivor rename e5b1e3ce7)",
                 r["symbol"]))

    # ---- population B2: finished rows with no verdict ------------------------
    for r in popb["nv100"]:
        plan["B2 certify COMPLETE"] += 1
        if args.apply:
            cur.execute(
                "UPDATE functions SET verdict='COMPLETE', verdict_reason=?,"
                " match_percent_normalized=?, updated_at=CURRENT_TIMESTAMP WHERE symbol=?",
                (f"{TAG}: new spelling from the ICF fold-survivor rename e5b1e3ce7; "
                 f"symbol_sweep under functionRelocDiffs=name_check, objdiff-cli 4.2.7 "
                 f"(76c8da87e040) gives ZERO mismatched instructions and diff_score 0 "
                 f"-- byte identity, not a rounded headline",
                 r["norm"], r["symbol"]))

    for k in sorted(plan):
        print(f"  {plan[k]:4}  {k}")
    print(f"  ---- {sum(plan.values())} rows considered")
    if args.apply:
        con.commit()
        print("COMMITTED")
    else:
        print("DRY RUN -- pass --apply to write")
    con.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
