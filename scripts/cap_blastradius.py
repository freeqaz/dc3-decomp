#!/usr/bin/env python3
"""How far does the stubbed-register-save-helper defect reach?

Static pass: for every function carrying a verdict from the 2026-08-18 unicorn
sweep, ask whether either side's relocations reference __savegprlr_N /
__restgprlr_N / __savefpr_N / __restfpr_N. Any function that does has a broken
epilogue under emulation (the `li r3,0; blr` stub never reloads LR from
-0x8(r1), so the tail-branch epilogue returns to a stale in-function LR) and
also loses r3 at the second instruction of its prologue.

Cross-tabulate against unicorn_class to see which classes are contaminated.
"""
import collections
import os
import sqlite3
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from scripts.unicorn_runner.coff import COFFParser
from scripts.unicorn_runner.run import resolve_unit
from scripts.unicorn_runner.extractor import extract_from_decomp, extract_from_original
from scripts.cap_helpers import uses_helpers

DB = sys.argv[1] if len(sys.argv) > 1 else "decomp.db"

conn = sqlite3.connect(DB)
rows = conn.execute(
    "SELECT symbol, unit, unicorn_verdict, unicorn_class FROM functions "
    "WHERE excluded=0 AND unicorn_tested_at>='2026-08-18' AND unit IS NOT NULL"
).fetchall()
conn.close()

by_unit = collections.defaultdict(list)
for sym, unit, v, c in rows:
    by_unit[unit].append((sym, v, c))

tab = collections.defaultdict(lambda: [0, 0])  # class -> [helper, total]
errors = 0
for unit, syms in sorted(by_unit.items()):
    short = "/".join(unit.split("/")[-2:])
    try:
        dp, op = resolve_unit(short)
        d, o = COFFParser(dp), COFFParser(op)
    except Exception:
        errors += len(syms)
        continue
    for sym, v, c in syms:
        key = c or (v or "?")
        try:
            _, dr = extract_from_decomp(d, sym)
            _, orl = extract_from_original(o, sym)
        except Exception:
            errors += 1
            continue
        hit = uses_helpers(dr) or uses_helpers(orl)
        tab[key][1] += 1
        if hit:
            tab[key][0] += 1

print(f"{'unicorn_class':<26} {'w/ helper':>10} {'total':>7} {'%':>7}")
th = tt = 0
for k, (h, t) in sorted(tab.items(), key=lambda x: -x[1][1]):
    th += h
    tt += t
    print(f"{k:<26} {h:>10} {t:>7} {100.0*h/t if t else 0:>6.1f}%")
print(f"{'ALL':<26} {th:>10} {tt:>7} {100.0*th/tt if tt else 0:>6.1f}%   (errors {errors})")
