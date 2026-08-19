#!/usr/bin/env python3
"""cap_probe — reproduce and characterise `cap_exhausted_decomp` rows.

For a (unit, symbol) pair, run the unicorn comparison at several instruction
caps and report, per side, whether it hit the cap, where it stopped, and how
many calls it logged. A one-sided cap that DISAPPEARS as the cap is raised is a
budget artifact; one that persists at 10x is a genuine termination asymmetry.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.run import _run_comparison_core, resolve_unit
from scripts.unicorn_runner.comparator import classify_divergence
from scripts.unicorn_runner.memory_map import CODE_BASE, FILL_BYTE


def one(symbol, dcoff, ocoff, cap, fill):
    code, bundle, _, err = _run_comparison_core(
        symbol, dcoff, ocoff, max_insns=cap, fill_pattern=fill)
    if bundle is None:
        return {"cap": cap, "fill": fill, "err": err, "verdict": "SKIP/ERR"}
    r, d, o = bundle.result, bundle.decomp_result, bundle.orig_result
    cls = classify_divergence(r, d, o, bundle.decomp_relocs, bundle.orig_relocs) \
        if r.verdict == "DIVERGENT" else None
    return {
        "cap": cap, "fill": fill, "verdict": r.verdict, "class": cls,
        "reason": r.details.get("reason"),
        "d_cap": d.cap_exhausted, "o_cap": o.cap_exhausted,
        "d_pc": d.final_pc, "o_pc": o.final_pc,
        "d_off": d.final_pc - CODE_BASE, "o_off": o.final_pc - CODE_BASE,
        "d_calls": len(d.call_log), "o_calls": len(o.call_log),
        "d_err": d.error, "o_err": o.error,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--unit", required=True)
    ap.add_argument("--symbol", required=True)
    ap.add_argument("--caps", default="50000,200000,1000000")
    ap.add_argument("--fills", default="none,cd")
    a = ap.parse_args()

    dp, op = resolve_unit(a.unit)
    dcoff, ocoff = COFFParser(dp), COFFParser(op)
    fills = [None if f == "none" else FILL_BYTE for f in a.fills.split(",")]
    print(f"{a.symbol}  [{a.unit}]")
    for fill in fills:
        for cap in [int(c) for c in a.caps.split(",")]:
            r = one(a.symbol, dcoff, ocoff, cap, fill)
            fl = "zero" if fill is None else "0xCD"
            if "err" in r:
                print(f"  fill={fl} cap={cap:>8}  {r['verdict']}  {r['err']}")
                continue
            print(f"  fill={fl} cap={cap:>8}  {r['verdict']:<10} "
                  f"class={str(r['class']):<18} reason={str(r['reason']):<22} "
                  f"dcap={int(r['d_cap'])} ocap={int(r['o_cap'])} "
                  f"d@+0x{r['d_off']:X} o@+0x{r['o_off']:X} "
                  f"calls d={r['d_calls']} o={r['o_calls']}")
            if r["d_err"] or r["o_err"]:
                print(f"      d_err={r['d_err']}  o_err={r['o_err']}")


if __name__ == "__main__":
    main()
