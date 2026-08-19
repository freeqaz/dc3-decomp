#!/usr/bin/env python3
"""cap_triage — batch-classify the `cap_exhausted_decomp` cohort.

The DB class `cap_exhausted_decomp` is supposed to mean "our side executed
dramatically more instructions than the original with identical inputs".
The engine sets `cap_exhausted` only when, at the moment the instruction cap
fires, PC lies inside the ROOT function's byte range (engine.py:479). With
co-loading, a side that is spinning just as hard but happens to be inside a
trampoline (0x80010000+) or a co-loaded callee at that instant is instead
recorded `terminated_normally=True`. So the class can fire on a purely
SYMMETRIC infinite loop, decided by loop-body phase alignment.

This tool separates the two by re-running at 1x and 10x the cap and asking:
did the ORIGINAL actually reach the return sentinel (0xDEAD0000)?

Verdicts:
  ARTIFACT_SYMMETRIC  orig never returned either (PC not at sentinel, and its
                      work scales with the cap) -> phase-alignment artifact.
  REAL_ASYMMETRY      orig returned (or errored out promptly) while decomp
                      still spins -> genuine termination asymmetry, adjudicate.
  ORIG_FAULT          orig died on a fixture fault (unmapped/exception) while
                      decomp kept running -> fixture artifact, not a loop bug.
  NOT_REPRODUCED      no cap_exhausted_decomp under either cap.
"""
import argparse
import json
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.run import _run_comparison_core, resolve_unit
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.memory_map import CODE_BASE, TRAMPOLINE_BASE, FILL_BYTE

SENTINEL = 0xDEAD0000


def helper_asymmetry(symbol, dcoff, ocoff):
    """Do the two sides disagree about using the MSVC register-save helpers?

    If they do, the harness's `li r3,0; blr` stub zeroes `this` at entry on the
    helper-using side and never restores LR on its tail-branch epilogue, so any
    verdict for that function is fixture noise. Returns "decomp"/"orig"/"both"/"".
    """
    from scripts.unicorn_runner import extractor
    from scripts.cap_helpers import uses_helpers
    # install() replaces the module-level extractors; use the stashed originals
    # so this probe always sees the real, un-neutralized relocation set.
    _d = getattr(extractor, "_cap_orig_decomp", extractor.extract_from_decomp)
    _o = getattr(extractor, "_cap_orig_original", extractor.extract_from_original)
    try:
        _, dr = _d(dcoff, symbol)
        _, orl = _o(ocoff, symbol)
    except Exception:
        return "?"
    du, ou = uses_helpers(dr), uses_helpers(orl)
    if du and ou:
        return "both"
    if du:
        return "decomp"
    if ou:
        return "orig"
    return ""


def probe(symbol, dcoff, ocoff, cap, fill=None):
    code, bundle, _, err = _run_comparison_core(
        symbol, dcoff, ocoff, max_insns=cap, fill_pattern=fill)
    if bundle is None:
        return {"skip": err}
    r, d, o = bundle.result, bundle.decomp_result, bundle.orig_result
    cls = classify_divergence(r, d, o, bundle.decomp_relocs,
                              bundle.orig_relocs) if r.verdict == "DIVERGENT" else None
    return {
        "verdict": r.verdict, "class": cls, "reason": r.details.get("reason"),
        "d_cap": bool(d.cap_exhausted), "o_cap": bool(o.cap_exhausted),
        "d_pc": d.final_pc, "o_pc": o.final_pc,
        "d_calls": len(d.call_log), "o_calls": len(o.call_log),
        "d_err": d.error, "o_err": o.error,
        "d_term": bool(d.terminated_normally), "o_term": bool(o.terminated_normally),
    }


def region(pc):
    if pc == SENTINEL:
        return "sentinel"
    if TRAMPOLINE_BASE <= pc < TRAMPOLINE_BASE + 0x10000:
        return "trampoline"
    if CODE_BASE <= pc < TRAMPOLINE_BASE:
        return "code"
    return f"0x{pc:08X}"


def adjudicate(a, b):
    """a = 1x cap result, b = 10x cap result."""
    if "skip" in a:
        return "SKIPPED", a["skip"]
    if not (a.get("d_cap") or b.get("d_cap")):
        return "NOT_REPRODUCED", f"1x={a['reason']} 10x={b['reason']}"
    # decomp hit the cap on at least one run. What did orig do?
    o_ret = a["o_pc"] == SENTINEL and b["o_pc"] == SENTINEL
    o_grew = b["o_calls"] > a["o_calls"] * 1.5
    if a.get("o_err") or b.get("o_err"):
        return "ORIG_FAULT", f"orig error: {a.get('o_err') or b.get('o_err')}"
    if o_grew or a["o_cap"] or b["o_cap"]:
        return "ARTIFACT_SYMMETRIC", (
            f"orig work scales with cap ({a['o_calls']}->{b['o_calls']} calls), "
            f"orig PC at cap in {region(a['o_pc'])}")
    if o_ret:
        return "REAL_ASYMMETRY", (
            f"orig returned to sentinel with {a['o_calls']} calls; "
            f"decomp still inside root at +0x{a['d_pc'] - CODE_BASE:X} "
            f"after {b['d_calls']} calls at 10x cap")
    return "UNCLEAR", (f"orig PC {region(a['o_pc'])} calls {a['o_calls']}->{b['o_calls']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", default="decomp.db")
    ap.add_argument("--cap", type=int, default=50000)
    ap.add_argument("--out", default="cap_triage.json")
    ap.add_argument("--only", default=None, help="substring filter on symbol")
    ap.add_argument("--neutralize-helpers", action="store_true",
                    help="REMOVED — the harness now emulates __savegprlr_N and "
                         "friends in production (unicorn_runner/save_helpers.py). "
                         "Passing this flag is an error.")
    a = ap.parse_args()

    if a.neutralize_helpers:
        # Do NOT quietly ignore it. The flag used to mean "measure a harness
        # better than production"; production overtook it, so honouring it now
        # measures a WORSE harness (the open-coded rewrite drops the r14-r31 /
        # f14-f31 spill) while the operator believes the opposite.
        ap.error("--neutralize-helpers is removed: the harness emulates the "
                 "register save/restore helpers in production. Re-run without "
                 "the flag; results are already helper-correct.")

    conn = sqlite3.connect(a.db)
    rows = conn.execute(
        "SELECT symbol, unit, round(current_percent,1) FROM functions "
        "WHERE excluded=0 AND unicorn_verdict='DIVERGENT' "
        "AND unicorn_tested_at>='2026-08-18' "
        "AND unicorn_class='cap_exhausted_decomp' ORDER BY current_percent DESC"
    ).fetchall()
    conn.close()
    if a.only:
        rows = [r for r in rows if a.only in r[0]]

    coff_cache = {}
    out = []
    for i, (sym, unit, pct) in enumerate(rows):
        short = unit.split("/", 1)[-1] if "/" in unit else unit
        short = "/".join(short.split("/")[-2:])
        try:
            if short not in coff_cache:
                dp, op = resolve_unit(short)
                coff_cache[short] = (COFFParser(dp), COFFParser(op))
            dcoff, ocoff = coff_cache[short]
            helper_skew = helper_asymmetry(sym, dcoff, ocoff)
            r1 = probe(sym, dcoff, ocoff, a.cap)
            r10 = probe(sym, dcoff, ocoff, a.cap * 10)
            verdict, why = adjudicate(r1, r10)
        except Exception as e:
            verdict, why, r1, r10, helper_skew = "ERROR", f"{type(e).__name__}: {e}", {}, {}, "?"
        out.append({"symbol": sym, "unit": unit, "pct": pct,
                    "triage": verdict, "why": why, "helper_skew": helper_skew,
                    "r1x": r1, "r10x": r10})
        hs = f" [helper-skew:{helper_skew}]" if helper_skew not in ("", "both") else ""
        print(f"[{i+1}/{len(rows)}] {verdict:<19} {pct:>5}  {sym[:70]}{hs}", flush=True)
        print(f"      {why}", flush=True)

    with open(a.out, "w") as f:
        json.dump(out, f, indent=1)
    dist = {}
    for r in out:
        dist[r["triage"]] = dist.get(r["triage"], 0) + 1
    print("\n== triage distribution ==")
    for k, v in sorted(dist.items(), key=lambda x: -x[1]):
        print(f"  {k:<19} {v}")


if __name__ == "__main__":
    main()
