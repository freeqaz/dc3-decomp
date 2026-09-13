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

    out = {}
    _ = REL_PPC_REFHI
    for secnum, names in fn_secs.items():
        sec = secs[secnum - 1]
        # Each `lis rX, sym@ha` / `lfs fN, sym@l(rX)` pair emits TWO
        # relocations: REFHI (0x10) then REFLO (0x11).  Counting both doubles
        # every list and destroys the ordered comparison -- count only REFLO,
        # which is the load itself and is 1:1 with the target's `lfs ...@l`.
        seq = []
        for r in range(sec["nrel"]):
            off = sec["prel"] + 10 * r
            va, symidx, rtype = struct.unpack_from("<IIH", b, off)
            if rtype != REL_PPC_REFLO:
                continue
            if symidx not in sec_of_sym:
                continue
            # MSVC packs a TU's file-scope statics into ONE .data section and
            # relocates the whole group against its FIRST symbol, carrying the
            # +4/+8/+12 in the instruction's own 16-bit displacement field.
            # Ignoring that addend reports `sSwipeEllipseWidth` four times and
            # manufactures a disagreement out of a perfectly correct source.
            disp = struct.unpack_from(">h", b, sec["ptr"] + va + 2)[0]
            key = (sec_of_sym[symidx], sym_off[symidx] + disp)
            datum = by_sec_off.get(key) or statics.get(symidx)
            if datum is not None:
                seq.append((va, datum))
        if not seq:
            continue
        seq.sort()
        for n in names:
            out[n] = [st for _va, st in seq]
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
    for p in glob.glob(os.path.join(args.obj_root, "**", "*.obj"), recursive=True):
        n_objs += 1
        try:
            o, _st = our_float_statics(p)
        except Exception as e:  # noqa: BLE001
            print(f"!! {p}: {e}", file=sys.stderr)
            continue
        for k, v in o.items():
            ours_fn.setdefault(k, v)

    # ---- join ---------------------------------------------------------
    paired, missing, agree, disagree, lowconf = [], [], [], [], []
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
        if len(tv) == len(ov) and all(
            abs(a - b) <= 1e-6 * max(1.0, abs(a)) for a, b in zip(tv, ov)
        ):
            agree.append(rec)
        else:
            disagree.append(rec)
        if any(not c for _v, _n, c in ol):
            lowconf.append(fn)

    den = {
        "asm_files_parsed": len({b.file for b in all_blobs}),
        "our_objects_parsed": n_objs,
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
        "paired_with_a_low_confidence_our_side_symbol": len(lowconf),
    }

    if args.json:
        json.dump(
            {"denominators": den, "disagree": disagree, "missing": missing,
             "agree": agree},
            sys.stdout, indent=2,
        )
        print()
        return

    print("== DENOMINATORS ==")
    for k, v in den.items():
        print(f"  {k:52} {v}")

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
    show("MISSING (target has a mutable static, our function has none)", missing)
    show("AGREE", agree)


if __name__ == "__main__":
    main()
