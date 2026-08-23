#!/usr/bin/env python3
"""Split reloc_name_gate's rows into HOIST-ORDER noise and genuine IDENTITY leads.

WHY THIS EXISTS
===============
`reloc_name_gate.py` pairs relocations **by instruction position**.  That is the
only pairing available from a positional diff, and it is correct whenever the
two sides emit their address computations in the same order.  They frequently do
not: MSVC hoists a block of `lis/addi` pairs to the top of a basic block and is
free to schedule them in any order, so N constants hoisted in a different order
produce N charged pairs in which **both sides reference exactly the same N
symbols**.  There is no naming content in such a row at all.

Measured on this tree 2026-08-23 (331 standing rows, 727 charged pairs):

    class               pairs   ORDER(same set)   IDENTITY
    reg_save_helper       432                 0        432
    named_symbol          117                 0        117
    float_pool             80                32         48
    string_literal         37                26         11
    cross:named+string     23                 4         19
    rtti                   10                10          0
    cross:named+rsh        10                 0         10
    cross:named+vtable      5                 3          2
    cross:float+named       4                 0          4
    cross:rtti+string       4                 4          0
    vtable                  2                 2          0
    ...
    TOTAL                 727                83        644

**Every RTTI charge in the population (10/10) and every vtable charge (2/2) is
pure hoist order.**  70% of string-literal charges and 40% of float-pool charges
are too.  A lane that reads `??_R0?AVObject@Hmx@@@8` vs `??_R0?AVUIScreen@@@8`
as "the dynamic_cast names the wrong type" is chasing a scheduling artifact --
`UIManager::GotoFirstScreen` charges exactly that pair in **both directions**,
which is the signature.

Conversely **0 of 117 named-symbol charges are order**: when the charged pair is
two ordinary named symbols, the base really does reference something the target
never mentions.  Those are the leads.

HOW
===
For each charged pair, restrict BOTH sides' whole-function relocation multiset
to the symbol CLASS(es) the pair straddles, and compare.  Equal multiset =>
ORDER.  Restricting by class matters: a function can hoist its floats in a
different order *and* save a different register count, and a whole-function
comparison would let the register-save difference mask the float ordering.

    ORDER      the same symbols, scheduled differently -- no naming content
    IDENTITY   one side names a symbol the other never does -- a real lead

WHAT IT IS NOT
==============
IDENTITY is **not** a bug verdict.  `__savegprlr_28` vs `__savegprlr_29` is
IDENTITY and is regalloc; objdiff folds that class so it costs zero canonical
points (see `--headroom`).  IDENTITY means "the two sides disagree about which
symbol, not merely about when" -- it removes the scheduling explanation, nothing
more.

USAGE
=====
    python3 scripts/analysis/reloc_name_gate.py --project . --json-out rows.json
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --project .
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --leads-out leads.json
    python3 scripts/analysis/reloc_order_vs_identity.py --selftest
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis import ruler as ruler_mod  # noqa: E402


def symbol_class(name: str) -> str:
    """Coarse class of a relocation target name.

    The classes exist so an ordering comparison is made against the pool the
    compiler actually scheduled from.  Float constants, string literals, RTTI
    descriptors and vtables each live in their own COMDAT pool.
    """
    n = str(name)
    if re.match(r"^__(save|rest)(gpr|fpr|vmx)", n):
        return "reg_save_helper"
    if n.startswith("__real@"):
        return "float_pool"
    if n.startswith("??_C@"):
        return "string_literal"
    if n.startswith("??_R"):
        return "rtti"
    if n.startswith("??_7"):
        return "vtable"
    return "named_symbol"


def pair_class(target: str, base: str) -> tuple[str, set]:
    """Label for a charged pair, and the set of classes to compare within."""
    ct, cb = symbol_class(target), symbol_class(base)
    if ct == cb:
        return ct, {ct}
    return f"cross:{'+'.join(sorted({ct, cb}))}", {ct, cb}


def relocation_multisets(cli, project, ruler, unit, syms, timeout=600):
    """(unit, sym) -> (target Counter, base Counter) of relocation target names."""
    cmd = [cli, "diff", "-p", str(project), "-u", unit, "--batch",
           "-f", "json", "-o", "-", "--include-instructions"] + ruler.args
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           input="\n".join(syms) + "\n", timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"  ! timeout on {unit}", file=sys.stderr)
        return {}
    txt = r.stdout.strip()
    if not txt:
        print(f"  ! empty diff for {unit}", file=sys.stderr)
        return {}
    try:
        j = json.loads(txt)
        recs = j if isinstance(j, list) else [j]
    except json.JSONDecodeError:
        recs = [json.loads(x) for x in txt.splitlines() if x.strip()]
    out = {}
    for rec in recs:
        sym = rec.get("symbol") or rec.get("name") or ""
        if sym not in syms:
            continue
        t, b = collections.Counter(), collections.Counter()
        for ins in rec.get("instructions", []) or []:
            for side, bag in (("target", t), ("base", b)):
                for a in ((ins.get(side) or {}).get("typed_args") or []):
                    if a.get("type") == "Symbol" and isinstance(a.get("value"), str):
                        bag[a["value"]] += 1
        out[(unit, sym)] = (t, b)
    return out


def adjudicate(row, tset, bset):
    """-> (row_verdict, [(pair_label, verdict, target_only, base_only), ...])"""
    details, any_identity = [], False
    for p in row["pairs"]:
        label, classes = pair_class(p["target"], p["base"])
        tc = collections.Counter({k: v for k, v in tset.items()
                                  if symbol_class(k) in classes})
        bc = collections.Counter({k: v for k, v in bset.items()
                                  if symbol_class(k) in classes})
        only_t, only_b = tc - bc, bc - tc
        if not only_t and not only_b:
            details.append((label, "ORDER", [], []))
        else:
            any_identity = True
            details.append((label, "IDENTITY", sorted(only_t), sorted(only_b)))
    return ("IDENTITY" if any_identity else "ORDER_ONLY"), details


# ── Negative control ─────────────────────────────────────────────────────────
# A discriminator that answered ORDER for everything would look like a great
# noise filter and would hide every real lead, so the selftest grades BOTH
# directions on fixtures taken verbatim from this tree.
def _selftest() -> int:
    ok = True

    def check(label, cond, detail=""):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}"
              f"{(' - ' + detail) if detail else ''}")
        ok = ok and cond

    # UIManager::GotoFirstScreen -- two RTTI descriptors, charged both ways.
    # Same two symbols on both sides: ORDER.
    row = {"pairs": [
        {"target": "??_R0?AVObject@Hmx@@@8", "base": "??_R0?AVUIScreen@@@8"},
        {"target": "??_R0?AVUIScreen@@@8", "base": "??_R0?AVObject@Hmx@@@8"},
    ]}
    t = collections.Counter({"??_R0?AVObject@Hmx@@@8": 2, "??_R0?AVUIScreen@@@8": 2})
    v, _ = adjudicate(row, t, collections.Counter(t))
    check("RTTI swap adjudicates ORDER_ONLY", v == "ORDER_ONLY", v)

    # MemFindAddrHeap -- base names gNumHeaps, target never does.  IDENTITY.
    row = {"pairs": [{"target": "?gHeaps@@3PAVMemHeap@@A", "base": "?gNumHeaps@@3HA"}]}
    t = collections.Counter({"?gHeaps@@3PAVMemHeap@@A": 2})
    b = collections.Counter({"?gNumHeaps@@3HA": 2, "?gHeaps@@3PAVMemHeap@@A": 2})
    v, d = adjudicate(row, t, b)
    check("named-symbol disagreement adjudicates IDENTITY", v == "IDENTITY", v)
    check("  ...and names the base-only symbol",
          d[0][3] == ["?gNumHeaps@@3HA"], str(d[0][3]))

    # The class restriction must actually restrict.  A float hoist swap in a
    # function that ALSO saves a different register count must still read ORDER
    # for the float pair -- a whole-function comparison would call it IDENTITY.
    row = {"pairs": [{"target": "__real@3f800000", "base": "__real@00000000"}]}
    t = collections.Counter({"__real@3f800000": 2, "__real@00000000": 2,
                             "__savegprlr_28": 1})
    b = collections.Counter({"__real@00000000": 2, "__real@3f800000": 2,
                             "__savegprlr_29": 1})
    v, d = adjudicate(row, t, b)
    check("class restriction survives a co-occurring reg-save difference",
          d[0][1] == "ORDER", d[0][1])

    # ...but the reg-save pair in the SAME function must still read IDENTITY.
    row = {"pairs": [{"target": "__savegprlr_28", "base": "__savegprlr_29"}]}
    v, d = adjudicate(row, t, b)
    check("reg-save helper still adjudicates IDENTITY", d[0][1] == "IDENTITY",
          d[0][1])

    # A real wrong float VALUE (target uses a constant the base never does)
    # must not be laundered as ordering.
    row = {"pairs": [{"target": "__real@469c4000", "base": "__real@41c80000"}]}
    t = collections.Counter({"__real@469c4000": 2})
    b = collections.Counter({"__real@41c80000": 2})
    v, d = adjudicate(row, t, b)
    check("wrong float VALUE adjudicates IDENTITY", d[0][1] == "IDENTITY", d[0][1])

    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rows", help="JSON written by reloc_name_gate.py --json-out")
    ap.add_argument("--project", default=".")
    ap.add_argument("--cli", default="bin/objdiff-cli")
    ap.add_argument("--leads-out", help="write the IDENTITY rows here")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()
    if not args.rows:
        ap.error("--rows is required (or --selftest)")

    rows = [r for r in json.load(open(args.rows)) if r.get("pairs")]
    ruler = ruler_mod.resolve_ruler(args.project, ruler_mod.RULER_GRADED)
    print(f"ruler: {' '.join(ruler.args)}")
    print(f"standing rows: {len(rows)}  bytes: {sum(r['size'] for r in rows)}")

    by_unit = collections.defaultdict(list)
    for r in rows:
        by_unit[r["unit"]].append(r["symbol"])

    sets = {}
    for unit, syms in sorted(by_unit.items()):
        sets.update(relocation_multisets(args.cli, args.project, ruler, unit, syms))

    pair_tot, pair_order = collections.Counter(), collections.Counter()
    verdicts, leads, no_data = collections.Counter(), [], 0
    for r in rows:
        k = (r["unit"], r["symbol"])
        if k not in sets:
            no_data += 1
            continue
        t, b = sets[k]
        v, details = adjudicate(r, t, b)
        verdicts[v] += 1
        for label, pv, only_t, only_b in details:
            pair_tot[label] += 1
            if pv == "ORDER":
                pair_order[label] += 1
        if v == "IDENTITY":
            leads.append({"unit": r["unit"], "symbol": r["symbol"],
                          "size": r["size"], "other_charges": r["other_charges"],
                          "pairs": [{"class": c, "verdict": pv,
                                     "target_only": ot, "base_only": ob}
                                    for c, pv, ot, ob in details]})

    print("\n=== per-charged-pair adjudication, by symbol class ===")
    print(f"{'class':36s} {'pairs':>6s} {'ORDER':>7s} {'IDENTITY':>9s}")
    for c in sorted(pair_tot, key=lambda c: -pair_tot[c]):
        print(f"{c:36s} {pair_tot[c]:6d} {pair_order[c]:7d} "
              f"{pair_tot[c] - pair_order[c]:9d}")
    tot, to = sum(pair_tot.values()), sum(pair_order.values())
    print(f"{'TOTAL':36s} {tot:6d} {to:7d} {tot - to:9d}")

    print(f"\n=== rows === {dict(verdicts)}   (no diff data: {no_data})")
    ob = sum(r["size"] for r in rows
             if (r["unit"], r["symbol"]) in sets
             and adjudicate(r, *sets[(r["unit"], r["symbol"])])[0] == "ORDER_ONLY")
    print(f"ORDER_ONLY bytes: {ob}")

    if args.leads_out:
        json.dump(leads, open(args.leads_out, "w"), indent=1)
        print(f"wrote {len(leads)} IDENTITY leads -> {args.leads_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
