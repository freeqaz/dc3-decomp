#!/usr/bin/env python3
"""Audit mutable float statics: target `.data` `lbl_*` floats vs ours, per FUNCTION.

WHY THIS EXISTS
---------------
MSVC puts an immutable float literal in `.rdata` as a `__real@<hex>` COMDAT.
A float that ends up in **`.data`** was written there by an initialiser into
*writable* storage, i.e. the source said `static float sFoo = <v>;`.  dtk names
those `lbl_8xxxxxxx` in the split listings.

Both the EXISTENCE and the VALUE of such a constant are things our decomp
guessed, and objdiff cannot check either one:

  * the `lfs` instruction is byte-identical whether the operand lives in
    `.rdata` or `.data`, so the code-shape ruler sees nothing;
  * `lbl_*` is a placeholder name, and the graded (`name_check`) ruler exempts
    placeholder names before the relocation-name detector runs.

So a wrong value here costs **zero** match% and changes behaviour a lot.  Three
confirmed defects were found by hand this way (HamMaster::CheckLevels 96 vs 40,
EaseElasticIn, ArcDetector::UpdateOverlay's missing colour fade); this is the
systematic version.

METHOD
------
TARGET side: parse `build/373307D9/asm/**/*.s` for `.data` `.obj lbl_*` blocks
whose body is `.float`/`.double`, then for every `.fn` collect the ordered list
of `lfs/lfd fN, lbl_X@l(rY)` sites that resolve to one of them.

OUR side: read the built COFF objects.  For each function COMDAT, walk its
relocations in address order and keep the ones targeting a `.data` symbol whose
*mangled type* is float (`@4MA`/`@3MA`/`@1MA`/`@2MA`) or double (`...NA`).
Unmangled C-style names are kept only if the datum decodes to a plausible float
(see `plausible`), and are reported as LOW-CONFIDENCE.

JOIN: on the mangled function name, comparing the ordered value lists.

Every count is reported with its denominator.  A sweep that reports only hits
is not believable.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from data_float_labels import collect as collect_target, is_xdk  # noqa: E402

REL_PPC_REFHI = 0x10  # `lis rX, sym@ha`
REL_PPC_REFLO = 0x11  # `lfs fN, sym@l(rX)`  -- the load itself

# `?name@...@4MA` -> float static;  `@4NA` -> double.  The char before the
# trailing `A` (cv-qualifier) is the type: M = float, N = double, H/I = int.
MANGLED_FLOAT = re.compile(r"@[0-9]M[A-D]$")
MANGLED_DOUBLE = re.compile(r"@[0-9]N[A-D]$")
MANGLED_ANY_TYPED = re.compile(r"@[0-9][A-Z_][A-D]$")

NOISE_PREFIXES = ("??_R", "??_7", "??_8", "__unwind$", "__catch$")


def plausible(v: float) -> bool:
    """Could this 4-byte pattern be a hand-written float constant?"""
    if v != v or math.isinf(v):
        return False
    if v == 0.0:
        return True
    a = abs(v)
    return 1e-6 <= a <= 1e9


def read_coff(path: str):
    b = open(path, "rb").read()
    _mach, nsec, _ts, symptr, nsym, optsz, _ch = struct.unpack_from("<HHIIIHH", b, 0)
    secs = []
    off = 20 + optsz
    for i in range(nsec):
        raw = b[off + 40 * i : off + 40 * i + 40]
        name = raw[:8].rstrip(b"\0").decode("latin1")
        _vsz, _va, sz, ptr, prel, _pl, nrel, _nl, chars = struct.unpack_from(
            "<IIIIIIHHI", raw, 8
        )
        secs.append(
            {"name": name, "size": sz, "ptr": ptr, "prel": prel, "nrel": nrel,
             "chars": chars}
        )
    strtab = symptr + 18 * nsym
    syms = []
    i = 0
    while i < nsym:
        rec = b[symptr + 18 * i : symptr + 18 * i + 18]
        nm = rec[:8]
        if nm[:4] == b"\0\0\0\0":
            o = struct.unpack_from("<I", nm, 4)[0]
            e = b.index(b"\0", strtab + o)
            name = b[strtab + o : e].decode("latin1")
        else:
            name = nm.rstrip(b"\0").decode("latin1")
        val, secnum, _typ, sc, naux = struct.unpack_from("<IhHBB", rec, 8)
        syms.append({"name": name, "value": val, "sec": secnum, "sc": sc,
                     "idx": i})
        i += 1 + naux
    # relocations are indexed by SYMBOL TABLE INDEX, so build that map
    by_index = {s["idx"]: s for s in syms}
    return secs, syms, by_index, b


def our_float_statics(path: str):
    """Return (fn_name -> [(value, sym, confident)]), plus the unit's static table."""
    secs, syms, by_index, b = read_coff(path)

    # 1. catalogue .data float statics.
    #
    # COFF records no symbol size, and MSVC packs every non-COMDAT
    # function-local static of a TU into ONE `.data` section -- so the section
    # size is NOT the datum's size.  Bound each symbol by the next symbol's
    # value within the same section instead.  (Getting this wrong silently
    # DROPS statics: `?sRectX@...DrawDebug@HamNavList...@4MA` shares a section
    # with `sRectColor`, so a `section.size in (4,8)` test never saw it.)
    per_sec = defaultdict(list)
    for s in syms:
        if 1 <= s["sec"] <= len(secs) and secs[s["sec"] - 1]["name"].startswith(
            ".data"
        ):
            if s["sc"] in (2, 3) and s["name"] != secs[s["sec"] - 1]["name"]:
                per_sec[s["sec"]].append(s)

    statics = {}
    by_sec_off = {}   # (section, offset) -> datum, for addend-resolved relocs
    sec_of_sym = {}   # symbol index -> its .data section number
    sym_off = {}      # symbol index -> its offset within that section
    for secnum, group in per_sec.items():
        sec = secs[secnum - 1]
        group.sort(key=lambda s: s["value"])
        # relocated bytes are a pointer, never a float constant
        reloc_offs = set()
        for r in range(sec["nrel"]):
            va, _si, _rt = struct.unpack_from("<IIH", b, sec["prel"] + 10 * r)
            reloc_offs.add(va)
        anchors = sorted({s["value"] for s in group} | {sec["size"]})
        for i, s in enumerate(group):
            n = s["name"]
            if n.startswith(NOISE_PREFIXES):
                continue
            start = s["value"]
            end = anchors[anchors.index(start) + 1]
            width = end - start
            if width not in (4, 8):
                continue
            if any(start <= o < end for o in reloc_offs):
                continue
            raw = b[sec["ptr"] + start : sec["ptr"] + end]
            if len(raw) != width:
                continue
            v = struct.unpack(">f" if width == 4 else ">d", raw)[0]
            if MANGLED_FLOAT.search(n) and width == 4:
                conf = True
            elif MANGLED_DOUBLE.search(n) and width == 8:
                conf = True
            elif MANGLED_ANY_TYPED.search(n):
                continue  # explicitly typed as something that is not a float
            else:
                if not plausible(v):
                    continue
                conf = False
            statics[s["idx"]] = (v, n, conf)
            by_sec_off[(secnum, start)] = (v, n, conf)
        sec_of_sym.update({s["idx"]: secnum for s in group})
        sym_off.update({s["idx"]: s["value"] for s in group})

    # 2. for each function COMDAT, walk relocations in address order
    fn_secs = defaultdict(list)  # section number -> function symbol names
    for s in syms:
        if s["sc"] == 2 and 1 <= s["sec"] <= len(secs):
            sec = secs[s["sec"] - 1]
            if sec["chars"] & 0x20:  # IMAGE_SCN_CNT_CODE
                fn_secs[s["sec"]].append(s["name"])

    # Walk INSTRUCTIONS, not relocations.  A relocation walk is wrong on our
    # objects, and wrong in BOTH directions at once.
    #
    # The target folds the low half into the load (`lfs f0, lbl@l(r11)`), so
    # there one relocation IS one load site.  Our MSVC frequently MATERIALISES
    # the address instead -- `lis`/`addi` into a GPR, then `lfs fN, <disp>(rGPR)`
    # -- and then:
    #   * the REFLO lands on the `addi`, which is NOT a load, so counting it
    #     over-counts the site list, and
    #   * the sibling statics at +4/+8/+12 are read by loads carrying NO
    #     RELOCATION AT ALL, which a relocation walk cannot see at any effort.
    #
    # Measured 2026-09-16, whole binary: 16 functions, 74 such hidden sites.
    # On IsValidSwipePosition the old walk reported sSwipeEllipseWidth (0.9)
    # three times where the source really reads four distinct statics
    # (0.9/1.3/0.8/1.1) -- a DISAGREE manufactured against correct source.
    OP_ADDI, OP_ADDIS, OP_LFS, OP_LFD, OP_BL = 14, 15, 48, 50, 18
    VOLATILE_GPRS = {0} | set(range(3, 13))

    out = {}
    for secnum, names in fn_secs.items():
        sec = secs[secnum - 1]
        # One `va` carries SEVERAL relocation records: MSVC emits a `@comp.id`
        # record (type 0x12) at the SAME address as the real REFHI/REFLO.  A
        # dict keyed by va with last-write-wins lets that record SHADOW the
        # real one -- which silently zeroed a whole-binary measurement during
        # this investigation until the raw table was dumped.  Keep only the
        # address relocations.
        rel = {}
        for r in range(sec["nrel"]):
            va, symidx, rtype = struct.unpack_from("<IIH", b, sec["prel"] + 10 * r)
            if rtype in (REL_PPC_REFHI, REL_PPC_REFLO):
                rel[va] = (rtype, symidx)

        seq = []
        base = {}  # GPR -> (data section, byte offset) the register now holds
        for off in range(0, max(0, sec["size"] - 3), 4):
            w = struct.unpack_from(">I", b, sec["ptr"] + off)[0]
            op = w >> 26
            rd = (w >> 21) & 31
            ra = (w >> 16) & 31
            disp = struct.unpack_from(">h", b, sec["ptr"] + off + 2)[0]
            rl = rel.get(off)

            if rl is not None and rl[0] == REL_PPC_REFLO and rl[1] in sec_of_sym:
                symidx = rl[1]
                # MSVC packs a TU's file-scope statics into ONE .data section
                # and relocates the group against its FIRST symbol, carrying
                # +4/+8/+12 in the instruction's own displacement field.
                key = (sec_of_sym[symidx], sym_off[symidx] + disp)
                if op in (OP_ADDI, OP_ADDIS):
                    base[rd] = key          # address materialisation, not a read
                    continue
                if op in (OP_LFS, OP_LFD):
                    datum = by_sec_off.get(key) or statics.get(symidx)
                    if datum is not None:
                        seq.append((off, datum))
                    continue

            if op in (OP_LFS, OP_LFD) and rl is None and ra in base and ra != 1:
                bsec, boff = base[ra]
                # Gate on the computed slot actually BEING a catalogued .data
                # float static.  A stray base+disp landing exactly on one is
                # far less likely than the silent mis-attribution it prevents.
                # (Displacements are signed: IsValidScrollPos reads its base
                # at -4, a static earlier in the section.)
                datum = by_sec_off.get((bsec, boff + disp))
                if datum is not None:
                    seq.append((off, datum))
                continue

            # Invalidate stale bases, conservatively.  A stale base is exactly
            # how a value gets attributed to the wrong static, which is the
            # failure this block exists to prevent.
            if op == OP_BL:
                for g in VOLATILE_GPRS:
                    base.pop(g, None)       # a call clobbers r0, r3-r12
            elif op in (OP_ADDI, OP_ADDIS) or 32 <= op <= 47:
                base.pop(rd, None)          # plain addi / any integer load

        if not seq:
            continue
        seq.sort()
        for n in names:
            out[n] = [st for _off, st in seq]
    return out, statics


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--asm-root", default="build/373307D9/asm")
    ap.add_argument("--obj-root", default="build/373307D9/src")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    # ---- target side --------------------------------------------------
    all_blobs, by_name, sites, _fn_refs, fn_file = collect_target(args.asm_root)
    data_lbls = {
        b.name: b
        for b in all_blobs
        if b.section == ".data" and b.name.startswith("lbl_") and b.floats
    }
    tgt_fn = defaultdict(list)
    for s in sites:
        if is_xdk(s.file):
            continue
        b = data_lbls.get(s.label)
        if b is None:
            continue
        tgt_fn[s.fn].append((float(b.floats[0]), b.name, f"0x{b.addr:08X}", s.line))

    # ---- our side -----------------------------------------------------
    ours_fn = {}
    n_objs = 0
    n_obj_unparseable = 0
    # sorted(): `ours_fn.setdefault` is FIRST-write-wins, so with an unsorted
    # glob the winner for a symbol defined in more than one object is decided
    # by filesystem order -- the scope_index_census defect exactly.  Measured
    # 2026-09-16 this is LATENT, not live (1 symbol in >1 object, 0 conflicting
    # values, 0 conflicting function lists), which is the right time to pin it
    # rather than after it starts flipping a verdict.
    for p in sorted(glob.glob(os.path.join(args.obj_root, "**", "*.obj"), recursive=True)):
        n_objs += 1
        try:
            o, _st = our_float_statics(p)
        except Exception as e:  # noqa: BLE001
            print(f"!! {p}: {e}", file=sys.stderr)
            n_obj_unparseable += 1
            continue
        for k, v in o.items():
            ours_fn.setdefault(k, v)

    # ---- join ---------------------------------------------------------
    paired, missing, agree, disagree, lowconf, order_only = [], [], [], [], [], []
    for fn, tl in sorted(tgt_fn.items()):
        ol = ours_fn.get(fn)
        rec = {
            "fn": fn,
            "file": fn_file.get(fn, "?"),
            "target": [{"value": v, "label": n, "addr": a, "asm_line": ln}
                       for v, n, a, ln in tl],
            "ours": None if ol is None else
                    [{"value": v, "sym": n, "confident": c} for v, n, c in ol],
        }
        if ol is None:
            missing.append(rec)
            continue
        paired.append(rec)
        tv = [v for v, _n, _a, _l in tl]
        ov = [v for v, _n, _c in ol]

        def _same(xs, ys):
            return len(xs) == len(ys) and all(
                abs(p - q) <= 1e-6 * max(1.0, abs(p)) for p, q in zip(xs, ys)
            )

        if _same(tv, ov):
            agree.append(rec)
        elif _same(sorted(tv), sorted(ov)):
            # Same MULTISET, different ISSUE ORDER.  Our MSVC and the image
            # schedule the loads of one static group differently -- on
            # IsValidSwipePosition the image reads the +4 slot before the +0
            # slot and we read +0 before +4 -- so an ORDERED comparison
            # manufactures a value disagreement out of correct source.
            # A wrong VALUE cannot hide in here: the multisets are equal, so
            # every constant the image loads is one we also load.
            order_only.append(rec)
        else:
            disagree.append(rec)
        if any(not c for _v, _n, c in ol):
            lowconf.append(fn)

    den = {
        "asm_files_parsed": len({b.file for b in all_blobs}),
        # "parsed" used to be the GLOBBED count, so it claimed 990 while 989
        # parsed and StreamRecorder.obj (a 0-byte orphan of a deleted TU) threw.
        "our_objects_globbed": n_objs,
        "our_objects_PARSED": n_objs - n_obj_unparseable,
        "our_objects_UNPARSEABLE": n_obj_unparseable,
        "data_float_labels_whole_binary": len(
            [b for b in all_blobs
             if b.section == ".data" and b.name.startswith("lbl_") and b.floats]
        ),
        "data_float_labels_nonxdk": len(
            [b for b in all_blobs
             if b.section == ".data" and b.name.startswith("lbl_") and b.floats
             and not is_xdk(b.file)]
        ),
        "target_functions_loading_one": len(tgt_fn),
        "paired_with_our_function": len(paired),
        "unpaired_ours_has_no_float_static": len(missing),
        "paired_and_agreeing": len(agree),
        "paired_and_DISAGREEING": len(disagree),
        "paired_same_values_DIFFERENT_ORDER": len(order_only),
        "paired_with_a_low_confidence_our_side_symbol": len(lowconf),
    }

    if args.json:
        json.dump(
            {"denominators": den, "disagree": disagree, "missing": missing,
             "order_only": order_only, "agree": agree},
            sys.stdout, indent=2,
        )
        print()
        return

    # ---- named-symbol join --------------------------------------------
    # dtk only invents a `lbl_*` name for a datum symbols.txt does NOT name.
    # Everything it DOES name is a `.data` float static too, and joining those
    # by NAME is an exact match with no pairing inference at all -- strictly
    # stronger evidence than the lbl_* path.  Leaving them out understated the
    # denominator and hid three real defects (HighFiveGestureFilter's three
    # thresholds, one of them sign-flipped).
    tgt_named = {}
    for b in all_blobs:
        if b.section != ".data" or b.name.startswith("lbl_") or is_xdk(b.file):
            continue
        if not b.floats:
            continue
        n = b.name.strip('"')
        if MANGLED_FLOAT.search(n) or MANGLED_DOUBLE.search(n):
            tgt_named[n] = (float(b.floats[0]), b.file, b.addr)
    ours_named = {}
    n_named_unparseable = 0
    for p in sorted(glob.glob(os.path.join(args.obj_root, "**", "*.obj"), recursive=True)):
        try:
            _o, st = our_float_statics(p)
        except Exception:  # noqa: BLE001
            # This swallow used to be silent.  An unparseable object here
            # SHORTENS ours_named, which INFLATES named_target_only_we_emit_none
            # -- a drop that manufactures findings rather than hiding them.
            n_named_unparseable += 1
            continue
        for v, n, _c in st.values():
            ours_named.setdefault(n, (v, p))
    both = sorted(set(tgt_named) & set(ours_named))
    named_dis = [
        n for n in both
        if abs(tgt_named[n][0] - ours_named[n][0])
        > 1e-6 * max(1.0, abs(tgt_named[n][0]))
    ]
    den["our_objects_UNPARSEABLE_in_named_pass"] = n_named_unparseable
    den["target_NAMED_data_float_statics_nonxdk"] = len(tgt_named)
    den["our_NAMED_data_float_statics"] = len(ours_named)
    den["named_present_on_both_sides"] = len(both)
    den["named_target_only_we_emit_none"] = len(set(tgt_named) - set(ours_named))
    den["named_exact_name_VALUE_DISAGREEMENTS"] = len(named_dis)

    print("== DENOMINATORS ==")
    for k, v in den.items():
        print(f"  {k:52} {v}")

    print(f"\n== NAMED-SYMBOL VALUE DISAGREEMENTS ({len(named_dis)}) ==")
    print("   exact name match on both sides -- no pairing inference")
    for n in named_dis:
        tv, tf, ta = tgt_named[n]
        ov, op = ours_named[n]
        print(f"-- {n}\n   TGT {tv:g} @ 0x{ta:08X} [{tf}]\n   OURS {ov:g} [{op}]")

    def show(title, recs):
        print(f"\n== {title} ({len(recs)}) ==")
        for r in recs:
            print(f"-- {r['fn']}\n   {r['file']}")
            t = "  ".join(f"{e['label']}({e['addr']})={e['value']:g}"
                          for e in r["target"])
            print(f"   TGT : {t}")
            if r["ours"] is None:
                print("   OURS: (no .data float static reaches this function)")
            else:
                o = "  ".join(
                    f"{e['value']:g}{'' if e['confident'] else '?'}[{e['sym']}]"
                    for e in r["ours"]
                )
                print(f"   OURS: {o}")

    show("DISAGREE (paired, values differ)", disagree)
    show("SAME VALUES, DIFFERENT LOAD ORDER (scheduling, not a bug)", order_only)
    show("MISSING (target has a mutable static, our function has none)", missing)
    show("AGREE", agree)


if __name__ == "__main__":
    main()
