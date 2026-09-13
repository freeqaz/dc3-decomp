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

    ORDER                the same symbols, scheduled differently -- no naming content
    ANCHOR_DISPLACEMENT  one lis/addi anchor, the other side's global reached by
                         a displacement on the load/store -- same byte of memory
    IDENTITY             one side names a symbol the other never reaches -- a lead

ANCHOR_DISPLACEMENT (added 2026-09-11)
======================================
The IDENTITY verdict used to fire on a `named_symbol` pair whenever the target
relocation named one global and ours named a different one.  But MSVC
materialises ONE `lis/addi` anchor for a pair of same-section globals and
reaches the neighbour by a compile-time displacement on the load/store; the
relocation names only the anchor, and the anchor choice does not follow layout
or any source lever.  Two rows handed to a lane as real wrong-variable bugs were
both this:

    MemPushTemp    target  lis/addi ?gNumHeaps@@3HA ; lbz r10, -0x13(r11)
                   0x830E56EC - 0x13 = 0x830E56D9 = gInitted (internal linkage,
                   present only as lbl_830E56D9 in symbols.txt, never in the map)
                   control: MemPushHeap in the same object anchors the other way
    PreInitSystem  target anchors on gUsingCD (0x82F652E8) and reads
                   gSystemConfig at -0x8(r30); ours anchors on gSystemConfig

The gate now records, per charged pair, the displacements the anchor's
consumers apply on each side (`target_disps` / `base_disps`, from
`reloc_name_gate.anchor_displacements`).  With at least one non-zero
displacement in play, `anchor + displacement` is resolved in
`config/373307D9/symbols.txt` (and the linker map for named globals): if the
two sides reach a common address, or the resolvable side's displaced address
lands exactly on an unnamed `lbl_` where the other side's name is
unresolvable, the pair is ANCHOR_DISPLACEMENT.  A displacement resolving to a
NAMED third symbol, or to nothing, stays IDENTITY; so does a pair whose "name"
on one side is not a symbol at all (repr of an int: that instruction has NO
relocation, the loudest form of the bug).  Measured on this tree 2026-09-11
(269 standing rows, 571 pairs): named_symbol 47 pairs = 11 ORDER / 6
ANCHOR_DISPLACEMENT / 30 IDENTITY (was 36); rows 239 IDENTITY / 6
ANCHOR_DISPLACEMENT (1,624 B) / 24 ORDER_ONLY.  The six are exactly the
MemMgr/System family; no other class moved.  Table and the surviving list:
docs/decomp/patterns/relocation-names-are-unmetered.md section 4.

WHAT IT IS NOT
==============
IDENTITY is **not** a bug verdict.  `__savegprlr_28` vs `__savegprlr_29` is
IDENTITY and is regalloc; objdiff folds that class so it costs zero canonical
points (see `--headroom`).  IDENTITY means "the two sides disagree about which
symbol, not merely about when or via which anchor" -- it removes the scheduling
and the anchor explanations, nothing more.

USAGE
=====
    python3 scripts/analysis/reloc_name_gate.py --project . --json-out rows.json
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --project .
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --leads-out leads.json
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --list-identity --anchor-out anchors.json
    python3 scripts/analysis/reloc_order_vs_identity.py --rows rows.json --no-anchor   # pre-2026-09-11 verdicts
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

from scripts.analysis import reloc_name_gate as gate_mod  # noqa: E402
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


def pair_displacements(instrs):
    """(target, base) -> (target_disps, base_disps) for every charged Symbol pair.

    Fallback for a rows.json written before the gate carried `target_disps` /
    `base_disps`; the same walk the gate does, over the diff this tool already
    fetches for the multisets.
    """
    out: dict[tuple, tuple[set, set]] = collections.defaultdict(lambda: (set(), set()))
    for i, ins in enumerate(instrs):
        if ins.get("match_type") != "diff_arg":
            continue
        t, b = ins.get("target") or {}, ins.get("base") or {}
        kinds, sp = set(), None
        for x, y in zip(t.get("typed_args", []) or [], b.get("typed_args", []) or []):
            if x.get("value") != y.get("value"):
                kinds.add(x.get("type"))
                if x.get("type") == "Symbol":
                    sp = (x.get("value"), y.get("value"))
        if kinds == {"Symbol"} and sp:
            td, bd = out[sp]
            td.update(gate_mod.anchor_displacements(instrs, "target", i))
            bd.update(gate_mod.anchor_displacements(instrs, "base", i))
    return {p: (sorted(td), sorted(bd)) for p, (td, bd) in out.items()}


def relocation_multisets(cli, project, ruler, unit, syms, timeout=600):
    """(unit, sym) -> (target Counter, base Counter, pair_displacements)."""
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
        instrs = rec.get("instructions", []) or []
        for ins in instrs:
            for side, bag in (("target", t), ("base", b)):
                for a in ((ins.get(side) or {}).get("typed_args") or []):
                    if a.get("type") == "Symbol" and isinstance(a.get("value"), str):
                        bag[a["value"]] += 1
        out[(unit, sym)] = (t, b, pair_displacements(instrs))
    return out


class AddressIndex:
    """name -> address and address -> [names], from symbols.txt (+ linker map).

    `symbols.txt` is the primary source because it is the only one that lists
    the file-static globals this class is about (`ham_xbox_r.map` omits .data
    statics entirely); the map fills in named globals the config lacks.
    """

    def __init__(self, by_name: dict[str, int], map_idx: dict | None = None):
        self.by_name = dict(by_name)
        self.by_addr: dict[int, list[str]] = collections.defaultdict(list)
        for n, a in self.by_name.items():
            self.by_addr[a].append(n)
        self.map_idx = map_idx or {}

    def resolve(self, name: str) -> int | None:
        a = self.by_name.get(name)
        if a is None and name in self.map_idx:
            addrs = self.map_idx[name]
            a = int(addrs[0], 16) if len(addrs) == 1 else None
        return a

    def names_at(self, addr: int) -> list[str]:
        return sorted(self.by_addr.get(addr, []))


_NOT_A_SYMBOL = re.compile(r"^(-?\d+|None|True|False)$")


def _is_symbol_name(name) -> bool:
    return isinstance(name, str) and bool(name) and not _NOT_A_SYMBOL.match(name)


def anchor_displacement_note(p: dict, addr: AddressIndex | None) -> str | None:
    """Explain a pair as one anchor reaching the other side's global, or None.

    MSVC materialises ONE lis/addi anchor for a pair of same-section globals
    and reaches the neighbour by a compile-time displacement on the load or
    store; the relocation names only the anchor, and the anchor choice does
    not follow layout or any source lever.  So a positional pair of two
    different names can address the same byte:

        target  lis/addi r11, ?gNumHeaps@@3HA ; lbz r10, -0x13(r11)   (gInitted)
        ours    lis r11, gInitted@ha           ; lbz r11, gInitted@l(r11)

    The pair is ANCHOR_DISPLACEMENT when, with at least one NON-ZERO
    displacement in play, the set of addresses the target's anchor reaches
    intersects the set ours reaches -- or, when one side's name is unresolvable
    (an internal-linkage static dtk could only call `lbl_<addr>`), when the
    resolvable side's displaced address lands exactly on an `lbl_` in
    symbols.txt.  A displaced address that resolves to a NAMED third symbol,
    or to nothing, is left alone: that is a real disagreement.

    `target_disps`/`base_disps` come from `reloc_name_gate.anchor_displacements`
    via the gate's `--json-out`.  With no address index, or no displacement
    data on the pair, nothing is reclassified.
    """
    if addr is None:
        return None
    # A pair whose "name" on one side is not a symbol at all (the gate stores
    # repr() of a non-string typed_arg value, e.g. "328" or "None") means that
    # side has NO relocation on that instruction.  That is the loudest form of
    # the wrong-symbol bug, not an anchor choice: never reclassify it.
    if not all(_is_symbol_name(p.get(k)) for k in ("target", "base")):
        return None
    td, bd = p.get("target_disps"), p.get("base_disps")
    if td is None and bd is None:
        return None
    td, bd = list(td or []), list(bd or [])
    if not any(d != 0 for d in td + bd):
        return None
    at, ab = addr.resolve(p["target"]), addr.resolve(p["base"])
    reach_t = {at + d for d in (td or [0])} if at is not None else set()
    reach_b = {ab + d for d in (bd or [0])} if ab is not None else set()
    if at is not None and ab is not None:
        common = reach_t & reach_b
        if not common:
            return None
        a = min(common)
        return (f"both anchors reach 0x{a:08X} ({', '.join(addr.names_at(a)) or '?'}): "
                f"target {p['target']}{td} / ours {p['base']}{bd}")
    # One side unresolvable (an internal-linkage static the splitter could only
    # call lbl_).  If the two anchors reach one address, the unresolvable
    # anchor sits at (reached address - its own displacement); that candidate
    # must land EXACTLY on an lbl_ in symbols.txt.  Symmetric in which side
    # carries the displacement: MemPushTemp is `gNumHeaps - 0x13 = lbl_` with
    # the resolvable side displaced; the mirror is ours anchoring the static
    # and reading the named global at +d, so the static is at (named - d).
    if at is not None:
        known, known_name, unk_disps, unk_name = reach_t, p["target"], bd, p["base"]
    else:
        known, known_name, unk_disps, unk_name = reach_b, p["base"], td, p["target"]
    for r in sorted(known):
        for d in (unk_disps or [0]):
            cand = r - d
            if cand == (at if at is not None else ab) and d == 0:
                continue  # the resolvable anchor itself, which is named
            names = addr.names_at(cand)
            if names and all(n.startswith("lbl_") for n in names):
                return (f"{unk_name} is unresolvable (internal linkage); "
                        f"{known_name} reaches 0x{r:08X} and {unk_name}{unk_disps} "
                        f"puts it at 0x{cand:08X} = {'/'.join(names)}")
    return None


def adjudicate(row, tset, bset, addr: AddressIndex | None = None):
    """-> (row_verdict, [(pair_label, verdict, target_only, base_only, note), ...])

    Pair verdicts, in the order they are tried:
        ORDER                same symbol multiset (per class) -- scheduling
        ANCHOR_DISPLACEMENT  one anchor, the other side's global by displacement
        IDENTITY             one side names a symbol the other never reaches
    Row verdict is the strongest pair verdict: IDENTITY > ANCHOR_DISPLACEMENT
    > ORDER_ONLY.
    """
    details, any_identity, any_anchor = [], False, False
    for p in row["pairs"]:
        label, classes = pair_class(p["target"], p["base"])
        tc = collections.Counter({k: v for k, v in tset.items()
                                  if symbol_class(k) in classes})
        bc = collections.Counter({k: v for k, v in bset.items()
                                  if symbol_class(k) in classes})
        only_t, only_b = tc - bc, bc - tc
        if not only_t and not only_b:
            details.append((label, "ORDER", [], [], ""))
            continue
        note = anchor_displacement_note(p, addr)
        if note is not None:
            any_anchor = True
            details.append((label, "ANCHOR_DISPLACEMENT",
                            sorted(only_t), sorted(only_b), note))
        else:
            any_identity = True
            details.append((label, "IDENTITY", sorted(only_t), sorted(only_b), ""))
    verdict = ("IDENTITY" if any_identity
               else "ANCHOR_DISPLACEMENT" if any_anchor else "ORDER_ONLY")
    return verdict, details


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

    # ── ANCHOR_DISPLACEMENT ──────────────────────────────────────────────────
    # Addresses are the live ones from config/373307D9/symbols.txt.  gInitted
    # is internal-linkage and present there only as lbl_830E56D9; gUsingCD and
    # gSystemConfig are both named.
    addr = AddressIndex({
        "?gNumHeaps@@3HA": 0x830E56EC,
        "lbl_830E56D9": 0x830E56D9,
        "lbl_830E56D8": 0x830E56D8,
        "gUsingCD": 0x82F652E8,
        "gSystemTitles": 0x82F652E4,
        "gSystemConfig": 0x82F652E0,
        "?gHeaps@@3PAVMemHeap@@A": 0x830E5458,
        "?gElsewhere@@3HA": 0x830F0000,
    })

    # MemPushTemp: target `lis/addi ?gNumHeaps@@3HA; lbz r10, -0x13(r11)`
    # = 0x830E56D9 = gInitted, which the target could only name lbl_.  Ours
    # anchors on gInitted directly.  Same byte of memory: ANCHOR_DISPLACEMENT.
    row = {"pairs": [{"target": "?gNumHeaps@@3HA", "base": "gInitted",
                      "target_disps": [-19, 0], "base_disps": [0]}]}
    t = collections.Counter({"?gNumHeaps@@3HA": 2})
    b = collections.Counter({"gInitted": 2, "?gNumHeaps@@3HA": 2})
    v, d = adjudicate(row, t, b, addr)
    check("MemPushTemp shape (displacement -> lbl_ at the other side's static) "
          "adjudicates ANCHOR_DISPLACEMENT",
          d[0][1] == "ANCHOR_DISPLACEMENT", d[0][1])
    check("  ...and the row verdict is ANCHOR_DISPLACEMENT, not IDENTITY",
          v == "ANCHOR_DISPLACEMENT", v)

    # PreInitSystem/InitSystem: target anchors on gUsingCD and reads
    # gSystemConfig at -0x8(r30); ours anchors on gSystemConfig.  The
    # displaced address IS the symbol the other side names.
    row = {"pairs": [{"target": "gUsingCD", "base": "gSystemConfig",
                      "target_disps": [-8, 0], "base_disps": [0, 8]}]}
    t = collections.Counter({"gUsingCD": 2})
    b = collections.Counter({"gSystemConfig": 2})
    v, d = adjudicate(row, t, b, addr)
    check("PreInitSystem shape (displacement -> the other side's named global) "
          "adjudicates ANCHOR_DISPLACEMENT",
          d[0][1] == "ANCHOR_DISPLACEMENT", d[0][1])
    # ...and it must be symmetric: OUR side displaced, target direct.
    row = {"pairs": [{"target": "gSystemConfig", "base": "gUsingCD",
                      "target_disps": [0], "base_disps": [-8]}]}
    v, d = adjudicate(row, b, t, addr)
    check("  ...symmetrically when OUR side carries the displacement",
          d[0][1] == "ANCHOR_DISPLACEMENT", d[0][1])

    # The mirror of MemPushTemp: OURS anchors the unresolvable static and
    # reads the named global at +0x13; the target anchors the named global.
    # The static then sits at gNumHeaps - 0x13, which is lbl_830E56D9.
    row = {"pairs": [{"target": "?gNumHeaps@@3HA", "base": "gInitted",
                      "target_disps": [0], "base_disps": [0, 19]}]}
    t = collections.Counter({"?gNumHeaps@@3HA": 2})
    b = collections.Counter({"gInitted": 2})
    v, d = adjudicate(row, t, b, addr)
    check("mirror: unresolvable side carries the displacement, static lands on lbl_",
          d[0][1] == "ANCHOR_DISPLACEMENT", d[0][1])
    # ...but NOT when that candidate address holds a NAMED symbol.  Live row:
    # ?BlurShadowRT@NgLight@@ -- ours anchors kWeights (function-local static,
    # unresolvable) and reads -4 (`kWeights - 1` pointer arithmetic); the
    # target anchors __real@3f800000 @ 0x82001210 at +0.  0x82001214 is a
    # named string literal in the target, so kWeights is not there: IDENTITY.
    addr5 = AddressIndex({"__real@3f800000": 0x82001210,
                          "??_C@_0CP@BLHNOOJH@e?3?2lazer_build_gmc1@": 0x82001214})
    row = {"pairs": [{"target": "__real@3f800000",
                      "base": "?kWeights@?1??BlurShadowRT@NgLight@@MAAXXZ@4QBMB",
                      "target_disps": [0], "base_disps": [-4]}]}
    t = collections.Counter({"__real@3f800000": 2})
    b = collections.Counter({"?kWeights@?1??BlurShadowRT@NgLight@@MAAXXZ@4QBMB": 2})
    v, d = adjudicate(row, t, b, addr5)
    check("BlurShadowRT shape (candidate address holds a NAMED literal) stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])

    # One side has NO relocation: the gate stores repr() of the non-string
    # typed_arg, so our "name" is "328".  Live row ?DisplayObject@CharDebug@@:
    # target anchors ?mesh@?8??DisplayObject... @ 0x82F5E3A8 and reads +0x148;
    # 0x82F5E4F0 is an lbl_ in symbols.txt, which is exactly how the first
    # cut of this filter laundered the row.  A missing relocation is the
    # loudest form of the bug and must stay IDENTITY whatever the displacement
    # lands on.
    addr6 = AddressIndex({"?mesh@?8??DisplayObject@CharDebug@@AAAXPAVObject@Hmx@@@Z@4PAVRndMesh@@A": 0x82F5E3A8,
                          "lbl_82F5E4F0": 0x82F5E4F0})
    row = {"pairs": [{"target": "?mesh@?8??DisplayObject@CharDebug@@AAAXPAVObject@Hmx@@@Z@4PAVRndMesh@@A",
                      "base": "328", "target_disps": [328], "base_disps": [256, 272]}]}
    t = collections.Counter({"?mesh@?8??DisplayObject@CharDebug@@AAAXPAVObject@Hmx@@@Z@4PAVRndMesh@@A": 2})
    b = collections.Counter()
    v, d = adjudicate(row, t, b, addr6)
    check("one side has NO relocation (name is repr of an int) stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])
    # The verbatim row above is also rejected by the candidate arithmetic
    # (0x82F5E3A8+0x148-0x100 and -0x110 hold nothing), so it does not prove
    # the guard is live.  This variant DOES land the candidate on the lbl_
    # (ours consumed at +0): only the no-relocation guard keeps it IDENTITY.
    # Sabotage `_is_symbol_name` to always-True and this line must FAIL.
    row = {"pairs": [{"target": "?mesh@?8??DisplayObject@CharDebug@@AAAXPAVObject@Hmx@@@Z@4PAVRndMesh@@A",
                      "base": "328", "target_disps": [328], "base_disps": [0]}]}
    v, d = adjudicate(row, t, b, addr6)
    check("  ...even when the displacement lands exactly on an lbl_ (guard is live)",
          d[0][1] == "IDENTITY", d[0][1])

    # ── MakeBSPTree: the two-register lis/addi anchor ────────────────────────
    # `?MakeBSPTree@@…` (src/system/math/Geo.cpp), rows 18-22 of the live diff,
    # objdiff 4.2.8 / name_check, transcribed verbatim.  The target anchors
    # ?gBSPDirTol@@3MA (0x82F0F694) in r11, MOVES it to r19 with
    # `addi r19, r11, @l`, and reads `0x4(r19)` = 0x82F0F698 = ?gBSPMaxDepth@@3HA
    # -- which is exactly what OUR side anchors.  Same byte of memory, a
    # different anchor: ANCHOR_DISPLACEMENT, the section-3 shape.
    #
    # Driven through `pair_displacements()` rather than hand-written disps on
    # purpose: the defect this case was written for is in the anchor WALK, not
    # in the adjudicator.  `anchor_displacements` only recognised
    # `addi reg, reg, sym@l` (same destination register), so the r11 -> r19
    # move was dropped, the target side reported NO displacements at all, and
    # the pair never reached the address arithmetic.  A hand-written
    # `target_disps=[4]` would have passed against the broken walker.
    R = lambda v: {"type": "Register", "value": v}   # noqa: E731
    S = lambda v: {"type": "Symbol", "value": v}     # noqa: E731
    I = lambda v: {"type": "Signed", "value": v}     # noqa: E731
    _bsp = [
        {"target": {"opcode": "lis", "typed_args": [R("r11"), S("?gBSPDirTol@@3MA")]},
         "base": {"opcode": "lis", "typed_args": [R("r11"), S("?gBSPMaxDepth@@3HA")]},
         "match_type": "diff_arg"},
        {"target": {"opcode": "addi", "typed_args": [R("r16"), R("r5"), I(1)]},
         "base": {"opcode": "addi", "typed_args": [R("r15"), R("r5"), I(1)]},
         "match_type": "diff_arg"},
        {"target": {"opcode": "addi",
                    "typed_args": [R("r19"), R("r11"), S("?gBSPDirTol@@3MA")]},
         "match_type": "delete"},
        {"target": {"opcode": "lwz", "typed_args": [R("r11"), I(4), R("r19")]},
         "base": {"opcode": "lwz",
                  "typed_args": [R("r11"), S("?gBSPMaxDepth@@3HA"), R("r11")]},
         "match_type": "diff_arg"},
        {"target": {"opcode": "cmpw", "typed_args": [R("cr6"), R("r16"), R("r11")]},
         "base": {"opcode": "cmpw", "typed_args": [R("cr6"), R("r15"), R("r11")]},
         "match_type": "diff_arg"},
    ]
    addr_bsp = AddressIndex({"?gBSPDirTol@@3MA": 0x82F0F694,
                             "?gBSPMaxDepth@@3HA": 0x82F0F698})
    _bsp_pair = ("?gBSPDirTol@@3MA", "?gBSPMaxDepth@@3HA")
    _td, _bd = pair_displacements(_bsp).get(_bsp_pair, ([], []))
    check("MakeBSPTree: the walk follows `addi rD, rA, sym@l` into rD",
          _td == [4], f"target_disps={_td}")
    row = {"pairs": [{"target": _bsp_pair[0], "base": _bsp_pair[1],
                      "target_disps": _td, "base_disps": _bd}]}
    t = collections.Counter({"?gBSPDirTol@@3MA": 2})
    b = collections.Counter({"?gBSPMaxDepth@@3HA": 2})
    v, d = adjudicate(row, t, b, addr_bsp)
    check("MakeBSPTree shape (two-register anchor, +4 onto the OTHER side's "
          "global) adjudicates ANCHOR_DISPLACEMENT",
          d[0][1] == "ANCHOR_DISPLACEMENT", f"{d[0][1]} td={_td} bd={_bd}")
    # ...and it must survive our side recording no +0 consumer of its own: an
    # anchor's OWN address is reached by the side that materialises it,
    # whatever the bounded walk happened to capture downstream.
    row = {"pairs": [{"target": _bsp_pair[0], "base": _bsp_pair[1],
                      "target_disps": [4], "base_disps": [-4]}]}
    v, d = adjudicate(row, t, b, addr_bsp)
    check("  ...and when NEITHER side records a 0 displacement (each anchor "
          "reaches the other's) it is still ANCHOR_DISPLACEMENT",
          d[0][1] == "ANCHOR_DISPLACEMENT", d[0][1])
    # Negative control for that widening: displacements that reach neither
    # anchor are still two different variables.
    row = {"pairs": [{"target": _bsp_pair[0], "base": _bsp_pair[1],
                      "target_disps": [12], "base_disps": [12]}]}
    v, d = adjudicate(row, t, b, addr_bsp)
    check("  ...but displacements that reach NEITHER anchor stay IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])

    # A genuine identity pair must survive the filter.  Two shapes: a `bl`
    # pair has no anchor at all, and a data pair whose anchors are consumed
    # at +0 on both sides names two different globals, full stop.
    row = {"pairs": [{"target": "??$MakeString@E@@YAPBDPBDABE@Z",
                      "base": "??$MakeString@D@@YAPBDPBDABD@Z",
                      "target_disps": [], "base_disps": []}]}
    t = collections.Counter({"??$MakeString@E@@YAPBDPBDABE@Z": 1})
    b = collections.Counter({"??$MakeString@D@@YAPBDPBDABD@Z": 1})
    v, d = adjudicate(row, t, b, addr)
    check("genuine identity (wrong callee, no anchor) stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])
    row = {"pairs": [{"target": "?gNumHeaps@@3HA", "base": "?gElsewhere@@3HA",
                      "target_disps": [0], "base_disps": [0]}]}
    t = collections.Counter({"?gNumHeaps@@3HA": 2})
    b = collections.Counter({"?gElsewhere@@3HA": 2})
    v, d = adjudicate(row, t, b, addr)
    check("genuine identity (two named globals, both consumed at +0) stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])

    # The displacement resolves to a THIRD symbol: the target reads
    # gNumHeaps-0x13 (gInitted) but OURS names gElsewhere, a named global at a
    # different address.  That is a real disagreement and must stay IDENTITY.
    row = {"pairs": [{"target": "?gNumHeaps@@3HA", "base": "?gElsewhere@@3HA",
                      "target_disps": [-19], "base_disps": [0]}]}
    v, d = adjudicate(row, t, b, addr)
    check("displacement resolving to a THIRD symbol stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])
    # ...also when OUR name is unresolvable but the displaced address holds a
    # NAMED symbol rather than an lbl_: the lbl_ fallback is for statics dtk
    # could not name, never for a named global we happen not to reference.
    addr3 = AddressIndex({"?gNumHeaps@@3HA": 0x830E56EC,
                          "?gNamedThird@@3_NA": 0x830E56D9})
    row = {"pairs": [{"target": "?gNumHeaps@@3HA", "base": "gInitted",
                      "target_disps": [-19], "base_disps": [0]}]}
    b = collections.Counter({"gInitted": 2})
    v, d = adjudicate(row, t, b, addr3)
    check("unresolvable base + displacement onto a NAMED third symbol stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])
    # ...and when the displaced address resolves to nothing at all.
    addr4 = AddressIndex({"?gNumHeaps@@3HA": 0x830E56EC})
    v, d = adjudicate(row, t, b, addr4)
    check("unresolvable base + displacement onto NOTHING stays IDENTITY",
          d[0][1] == "IDENTITY", d[0][1])

    # Vacuity: with no address index nothing may be reclassified.
    row = {"pairs": [{"target": "gUsingCD", "base": "gSystemConfig",
                      "target_disps": [-8], "base_disps": [0]}]}
    t = collections.Counter({"gUsingCD": 2})
    b = collections.Counter({"gSystemConfig": 2})
    v, d = adjudicate(row, t, b, None)
    check("no address index => never ANCHOR_DISPLACEMENT",
          d[0][1] == "IDENTITY", d[0][1])
    # ...and a pair whose rows.json predates the gate's disps fields (no
    # displacement data at all) must not be reclassified either.
    row = {"pairs": [{"target": "gUsingCD", "base": "gSystemConfig"}]}
    v, d = adjudicate(row, t, b, addr)
    check("no displacement data on the pair => IDENTITY (never guessed)",
          d[0][1] == "IDENTITY", d[0][1])

    print("\nSELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rows", help="JSON written by reloc_name_gate.py --json-out")
    ap.add_argument("--project", default=".")
    ap.add_argument("--cli", default="bin/objdiff-cli")
    ap.add_argument("--config", default=None,
                    help="dtk symbols.txt (default: config/<title>/symbols.txt); "
                         "resolves anchor+displacement for ANCHOR_DISPLACEMENT")
    ap.add_argument("--map", default=None,
                    help="MSVC linker map (default: orig/<title>/ham_xbox_r.map)")
    ap.add_argument("--no-anchor", action="store_true",
                    help="disable the ANCHOR_DISPLACEMENT verdict (every such "
                         "pair then reads IDENTITY, as before 2026-09-11)")
    ap.add_argument("--leads-out", help="write the IDENTITY rows here")
    ap.add_argument("--anchor-out", help="write the ANCHOR_DISPLACEMENT rows here")
    ap.add_argument("--list-identity", action="store_true",
                    help="list every surviving IDENTITY row (unit, symbol, size, pairs)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()
    if not args.rows:
        ap.error("--rows is required (or --selftest)")

    project = Path(args.project).resolve()
    rows = [r for r in json.load(open(args.rows)) if r.get("pairs")]
    ruler = ruler_mod.resolve_ruler(args.project, ruler_mod.RULER_GRADED)
    print(f"ruler: {' '.join(ruler.args)}")
    print(f"standing rows: {len(rows)}  bytes: {sum(r['size'] for r in rows)}")

    # ── address index for ANCHOR_DISPLACEMENT ────────────────────────────────
    addr = None
    if not args.no_anchor:
        cfgfile = args.config or next(
            (str(p) for p in sorted(project.glob("config/*/symbols.txt"))), None)
        mapfile = args.map or next(
            (str(p) for p in sorted(project.glob("orig/*/ham_xbox_r.map"))), None)
        if cfgfile:
            cfg = gate_mod.load_config_symbols(cfgfile)
            midx = gate_mod.load_map_index(mapfile) if mapfile else {}
            addr = AddressIndex(cfg, midx)
            print(f"address index: {len(cfg)} config symbols ({cfgfile})"
                  f" + {len(midx)} map names ({mapfile or '-'})")
        else:
            print("address index: NO config/*/symbols.txt -- ANCHOR_DISPLACEMENT "
                  "DISABLED, every anchor pair will read IDENTITY")
    else:
        print("address index: --no-anchor, ANCHOR_DISPLACEMENT DISABLED")

    by_unit = collections.defaultdict(list)
    for r in rows:
        by_unit[r["unit"]].append(r["symbol"])

    sets = {}
    for unit, syms in sorted(by_unit.items()):
        sets.update(relocation_multisets(args.cli, args.project, ruler, unit, syms))

    # A rows.json written before the gate carried displacement data gets it
    # from the diff fetched above; counted so the provenance is visible.
    filled = 0
    for r in rows:
        k = (r["unit"], r["symbol"])
        if k not in sets:
            continue
        pd = sets[k][2]
        for p in r["pairs"]:
            if "target_disps" not in p and "base_disps" not in p:
                td, bd = pd.get((p["target"], p["base"]), ([], []))
                p["target_disps"], p["base_disps"] = td, bd
                filled += 1
    if filled:
        print(f"displacements filled from the diff (rows.json lacked them): {filled} pairs")

    VERDICTS = ("ORDER", "ANCHOR_DISPLACEMENT", "IDENTITY")
    pair_tot = collections.Counter()
    pair_v = {v: collections.Counter() for v in VERDICTS}
    verdicts, verdict_bytes = collections.Counter(), collections.Counter()
    leads, anchors, no_data = [], [], 0
    for r in rows:
        k = (r["unit"], r["symbol"])
        if k not in sets:
            no_data += 1
            continue
        t, b, _pd = sets[k]
        v, details = adjudicate(r, t, b, addr)
        verdicts[v] += 1
        verdict_bytes[v] += r["size"]
        for label, pv, only_t, only_b, note in details:
            pair_tot[label] += 1
            pair_v[pv][label] += 1
        rec = {"unit": r["unit"], "symbol": r["symbol"],
               "size": r["size"], "other_charges": r["other_charges"],
               "verdict": v,
               "pairs": [{"class": c, "verdict": pv, "target_only": ot,
                          "base_only": ob, "note": note}
                         for c, pv, ot, ob, note in details]}
        if v == "IDENTITY":
            leads.append(rec)
        elif v == "ANCHOR_DISPLACEMENT":
            anchors.append(rec)

    print("\n=== per-charged-pair adjudication, by symbol class ===")
    print(f"{'class':36s} {'pairs':>6s} {'ORDER':>7s} {'ANCHOR_DISP':>12s} {'IDENTITY':>9s}")
    for c in sorted(pair_tot, key=lambda c: -pair_tot[c]):
        print(f"{c:36s} {pair_tot[c]:6d} {pair_v['ORDER'][c]:7d} "
              f"{pair_v['ANCHOR_DISPLACEMENT'][c]:12d} {pair_v['IDENTITY'][c]:9d}")
    tot = sum(pair_tot.values())
    print(f"{'TOTAL':36s} {tot:6d} {sum(pair_v['ORDER'].values()):7d} "
          f"{sum(pair_v['ANCHOR_DISPLACEMENT'].values()):12d} "
          f"{sum(pair_v['IDENTITY'].values()):9d}")

    print(f"\n=== rows ===   (no diff data: {no_data})")
    for v in ("IDENTITY", "ANCHOR_DISPLACEMENT", "ORDER_ONLY"):
        print(f"  {v:20s} {verdicts[v]:5d} rows  {verdict_bytes[v]:8d} B")

    if args.list_identity:
        print("\n=== surviving IDENTITY rows ===")
        for rec in sorted(leads, key=lambda r: -r["size"]):
            print(f"{rec['size']:>7} B  {rec['unit']} :: {rec['symbol']}")
            for p in rec["pairs"]:
                if p["verdict"] == "IDENTITY":
                    print(f"           [{p['class']}] target-only {p['target_only']}"
                          f"  ours-only {p['base_only']}")

    if args.leads_out:
        json.dump(leads, open(args.leads_out, "w"), indent=1)
        print(f"wrote {len(leads)} IDENTITY leads -> {args.leads_out}")
    if args.anchor_out:
        json.dump(anchors, open(args.anchor_out, "w"), indent=1)
        print(f"wrote {len(anchors)} ANCHOR_DISPLACEMENT rows -> {args.anchor_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
