#!/usr/bin/env python3
"""Re-adjudicate decomp.db verdicts against objdiff's LikelyFixable callee findings.

WHY
===
Three instrument defects fixed on 2026-08-21 could each produce a wrong verdict on
a function whose only visible defect is a relocation NAME:

  1. ``sync_objdiff`` ran objdiff with ``functionRelocDiffs=none`` and wrote nine
     relocation-sensitive ``has_*`` columns from that blind measurement on a
     schedule.  Under ``none`` the four callee detectors are STRUCTURALLY unable
     to fire, so the columns were being re-zeroed, not merely stale.
  2. auto-AT_LIMIT's ``PRACTICALLY_UNFIXABLE`` rule reads ``detected_patterns``,
     which under ``none`` cannot contain ``WRONG_CALLEE`` -- so a fixable wrong
     callee could be certified unfixable.
  3. objdiff's detectors never saw the ENCLOSING symbol (fixed in v4.2.7), so all
     25 were free to report findings inside pairs objdiff had GUESSED by byte
     signature.

A verdict contradicted by such a finding is therefore suspect -- but suspect is
not guilty.  This tool refuses to mass-flip on the rule.  It asks the linker that
made the image.

THE ADJUDICATION
================
For every divergent ``(target_symbol, base_symbol)`` pair objdiff reports under
``functionRelocDiffs=name_check``, look BOTH names up in ``orig/373307D9/ham_xbox_r.map``
-- the shipped MSVC linker map for this exact build:

  same address        ->  ``/OPT:ICF`` fold, stated by the linker.  The call is the
                          same bytes to the same code.  The finding is a COVERAGE
                          GAP in ``scripts/symbol_aliases.json``, not a source bug,
                          and the verdict is untouched.
  different addresses ->  REAL: we call other code.  Open, fixable source work.
  base absent         ->  our source names a member the shipped image does not
                          have.  Also real, also fixable.

Rows whose ENCLOSING symbol is a splitter placeholder (``fn_<hex>``/``lbl_``) are
MSVC EH funclets objdiff paired by byte signature; their counterpart routinely
belongs to a different parent function, so the differing ``bl`` is a pairing
artifact.  objdiff 4.2.7 co-reports these as ``UNVERIFIABLE_PAIRING``.

Four second-order screens then run on what survives, because a REAL-looking pair
is not always a wrong callee:

  merged_placeholder          base calls a synthesised ``merged_*`` stub -- the
                              documented #112 refusal class.
  savegpr_helper              a register save/restore helper on either side is
                              instruction-alignment noise, not a call.
  call_order_swap             the same two symbols appear in BOTH directions: we
                              call them in the opposite ORDER.  Real work, but a
                              different repair than "wrong callee".
  low_match_alignment_noise   below 60% the instruction aligner pairs ``bl``s
                              arbitrarily; the finding is not independently
                              informative and convicts nothing.

RULER DISCIPLINE
================
``name_check`` is the ruler -- it is what ``objdiff.json`` sets and what
``report.json`` uses.  ``none`` is blind to this entire class.  ``all`` also
charges addends and is ~99.8% noise.  Every number this tool prints is
``name_check`` unless labelled otherwise; the blind (``none``) mismatch count is
reported alongside because it is the discriminator for "does closing the name
cross the row".

Usage:
    python3 scripts/analysis/readjudicate_callee_verdicts.py \
        --worklist /tmp/worklist.json --project-dir . \
        --db /home/free/code/milohax/dc3-decomp/decomp.db [--apply]

Build ``--worklist`` with ``pattern_census.py`` + ``pattern_worklist.py`` (see
docs/analysis/2026-08-21-pattern-census-4.2.6.md section 7).
"""
from __future__ import annotations

import argparse
import collections
import json
import re
import sqlite3
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "scripts"))

# ONE parser for the shipped linker map, shared with the auto-AT_LIMIT gate that
# reads this tool's adjudication (scripts/orchestrator/callee_gate.py).  Two
# copies of the map regex would be two things to keep in step, and the gate
# issues certificates from it.
from orchestrator.callee_gate import MAP_REL, load_linker_map  # noqa: E402

#: below this canonical percentage objdiff's instruction aligner pairs `bl`s
#: arbitrarily and a callee finding carries no independent information.
ALIGNMENT_NOISE_FLOOR = 60.0


def load_map(project: Path) -> tuple[dict[str, str], dict[str, list[str]]]:
    """(symbol -> address, address -> [symbols]) from the shipped linker map."""
    lmap = load_linker_map(project)
    return lmap.addr, lmap.by_addr


def classify_pair(addr: dict[str, str], target: str, base: str) -> tuple[str, str | None, str | None]:
    at, ab = addr.get(target), addr.get(base)
    if at and ab and at == ab:
        return "ICF_FOLD", at, ab
    if at and ab:
        return "REAL_OTHER_ADDR", at, ab
    if at:
        return "BASE_NOT_IN_MAP", at, ab
    return "TARGET_NOT_IN_MAP", at, ab


def screens(row: dict) -> list[str]:
    tags: list[str] = []
    details = row["callee_detail"]
    if any("merged_" in c["base"] for c in details):
        tags.append("merged_placeholder")
    if any("gprlr" in c["target"] or "gprlr" in c["base"] for c in details):
        tags.append("savegpr_helper")
    pairs = {(c["target"], c["base"]) for c in details}
    if any((b, t) in pairs for t, b in pairs):
        tags.append("call_order_swap")
    if row["norm"] < ALIGNMENT_NOISE_FLOOR:
        tags.append("low_match_alignment_noise")
    return tags


def bucket(row: dict) -> str:
    kind, tags = row["row_kind"], set(row["screens"])
    if kind == "ARTIFACT_PAIRING":
        return "RIGHT_pairing_artifact"
    if kind == "ARTIFACT_ICF":
        return "RIGHT_icf_fold"
    if kind == "UNRESOLVED":
        return "UNRESOLVED_anon_ns"
    if "merged_placeholder" in tags:
        return "UNFIXABLE_merged_stub"
    if "low_match_alignment_noise" in tags:
        return "FINDING_NOT_INFORMATIVE"
    if row.get("blind_mm") == 0:
        return "PRIZE_crosses_row"
    if "call_order_swap" in tags:
        return "FIXABLE_call_order"
    return "FIXABLE_wrong_callee"


def sweep(project: Path, symbols: list[str], ruler: str) -> dict[str, dict]:
    """Batch-diff `symbols` through symbol_sweep (one objdiff process per shard)."""
    tmp = Path("/tmp") / f"readjudicate-syms-{ruler}.txt"
    tmp.write_text("\n".join(symbols) + "\n")
    out = Path("/tmp") / f"readjudicate-sweep-{ruler}.json"
    subprocess.run(
        [sys.executable, str(REPO_ROOT / "scripts/orchestrator/symbol_sweep.py"),
         "--project", str(project), "--kind", "functions",
         "--symbols-file", str(tmp), "--reloc-config", ruler,
         "--workers", "12", "--format", "json", "--out", str(out)],
        check=True, capture_output=True,
    )
    doc = json.loads(out.read_text())
    return {r["symbol"]: r for r in doc["rows"]}, doc["objdiff_version"]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--worklist", required=True,
                    help="pattern_worklist.py --json-out (tier1/tier2/tier3_artifact)")
    ap.add_argument("--project-dir", default=str(REPO_ROOT))
    ap.add_argument("--db", default=str(REPO_ROOT / "decomp.db"),
                    help="the MAIN checkout's decomp.db -- a worktree has only a tripwire")
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--apply", action="store_true", help="write verdict changes to --db")
    args = ap.parse_args(argv)

    project = Path(args.project_dir).resolve()
    addr, byaddr = load_map(project)
    wl = json.loads(Path(args.worklist).read_text())
    rows = []
    for tier in ("tier1", "tier2", "tier3_artifact"):
        for r in wl[tier]:
            rows.append({**r, "tier": tier})

    symbols = [r["symbol"] for r in rows]
    graded, version = sweep(project, symbols, "name_check")
    blind, _ = sweep(project, symbols, "none")

    con = sqlite3.connect(args.db)
    con.row_factory = sqlite3.Row
    q = con.execute(
        "SELECT symbol,verdict,verdict_reason,floor_certificate,floor_cert_at,current_percent"
        "  FROM functions WHERE symbol IN (%s)" % ",".join("?" * len(symbols)), symbols)
    db = {r["symbol"]: dict(r) for r in q}

    def mm(r):
        s = r["instruction_summary"]
        return s["total"] - s["equal"]

    for r in rows:
        detail = []
        for target, base, n in r["callees"]:
            kind, at, ab = classify_pair(addr, target, base)
            detail.append({"target": target, "base": base, "n": n, "verdict": kind,
                           "target_addr": at, "base_addr": ab,
                           "group_size": len(byaddr.get(at, [])) if at else 0})
        kinds = {c["verdict"] for c in detail}
        if r["enclosing_placeholder"]:
            row_kind = "ARTIFACT_PAIRING"
        elif kinds == {"ICF_FOLD"}:
            row_kind = "ARTIFACT_ICF"
        elif kinds & {"REAL_OTHER_ADDR", "BASE_NOT_IN_MAP"}:
            row_kind = "REAL"
        else:
            row_kind = "UNRESOLVED"
        g, b = graded.get(r["symbol"]), blind.get(r["symbol"])
        r.update(callee_detail=detail, row_kind=row_kind,
                 nc_mm=mm(g) if g else None, blind_mm=mm(b) if b else None,
                 nc_score=g["diff_score"]["score"] if g else None,
                 db_verdict=db.get(r["symbol"], {}).get("verdict"),
                 db_reason=db.get(r["symbol"], {}).get("verdict_reason"),
                 floor_certificate=db.get(r["symbol"], {}).get("floor_certificate"))
        r["screens"] = screens(r)
        r["bucket"] = bucket(r)

    print(f"# Callee-divergence verdict re-adjudication -- ruler name_check, {version}")
    print(f"# population: {len(rows)} functions carrying a LikelyFixable callee finding")
    print(f"# adjudicator: {MAP_REL} ({len(addr)} symbol names over {len(byaddr)} addresses)\n")
    tally = collections.Counter((r["bucket"], r["db_verdict"] or "<none>") for r in rows)
    print(f"{'bucket':26} {'verdict':10}    n")
    for k in sorted(tally):
        print(f"  {k[0]:26} {k[1]:10} {tally[k]:4}")

    if args.json_out:
        Path(args.json_out).write_text(json.dumps(rows, indent=1))
        print(f"\nwrote {args.json_out}")
    if args.apply:
        print("\n--apply is intentionally not implemented here: the verdict writes are "
              "recorded in scripts/analysis/apply_verdict_readjudication.py so that "
              "exactly what was written is reviewable in the diff.")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
