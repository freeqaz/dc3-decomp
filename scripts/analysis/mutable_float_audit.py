#!/usr/bin/env python3
"""Audit mutable float statics: target `.data` floats vs ours, per FUNCTION and by NAME.

WHY THIS EXISTS
---------------
MSVC puts an immutable float literal in `.rdata` as a `__real@<hex>` COMDAT.
A float that ends up in **`.data`** was written there by an initialiser into
*writable* storage, i.e. the source said `static float sFoo = <v>;`.  dtk names
the ones `symbols.txt` does not name `lbl_8xxxxxxx`.

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
TARGET side: `data_float_labels.collect()` parses `build/373307D9/asm/**/*.s`
for `.data` blobs with `.float`/`.double` slots, then walks each `.fn`'s
INSTRUCTIONS -- both the folded `lfs fN, sym@l(rX)` form and a materialised
base (`addi rD, rA, sym@l` then `lfs fN, <disp>(rD)`) -- to resolve every float
read to the exact byte it reads.

OUR side: read the built COFF objects.  For each function COMDAT, walk its
INSTRUCTIONS in address order (see `our_float_statics`) and resolve every float
load to a catalogued `.data` float static.

TWO JOINS, and the second is the stronger one:
  * per FUNCTION, on the mangled function name, comparing value lists;
  * per SYMBOL, on an exact name match between a target `.data` float blob and
    one of our `.data` symbols -- no pairing inference at all.

Every count is reported with its denominator, and every discard is routed
through `CoverageReport.drop()`.  A sweep that reports only hits is not
believable.

THREE GAPS CLOSED 2026-09-16 (read before quoting a zero from this tool)
-----------------------------------------------------------------------
1. **The target parser assumed the folded addressing mode**, exactly as the
   our-side walker did before `1b75dc677`.  29 `addi`-materialised references
   reach `.data` float labels and 9 labels were reachable no other way.  Fixed
   in `data_float_labels.parse_file`.
2. **The named join required a MANGLED `@4MA`/`@3MA` shape on the TARGET side.**
   That single filter dropped **17 of 50** target-named `.data` float blobs --
   every unmangled file-scope static the image happens to name
   (`gLowCut`, `kSampleRate`, `gNoiseThreshold`, `sValidHandFloats`, ...).
   Our side never had the filter, so these were joinable all along and were
   being thrown away one line before the intersection.
3. **Neither side could read a float ARRAY.**  Our side skipped any symbol whose
   width was not exactly 4 or 8; the target side read only `floats[0]`.  So a
   `Vector3`, a `Color`, a `Vector2[5]` or a 3-float parameter block was 1/N
   visible on one side and 0/N on the other.  `?sOffset@@3VVector3@@A`,
   `sValidHandFloats`, `?sDOFOverride@...`, `gBeatLineData` and MoveDir's five
   colours are all in this class.

WHAT THIS TOOL STILL CANNOT SEE
-------------------------------
* **A static our source lands in `.bss`.**  The comparison is `.data` against
  `.data`.  `kBlurTaps` is the worked example: the image statically initialises
  slot 0 to 0.1 and stores the other nine at runtime, so it sits in `.data`;
  our `static Vector2 kBlurTaps[5] = {...}` has non-constant initialisers, so
  MSVC puts it in `.bss` and this tool cannot pair it.  Counted as
  `our-counterpart-is-in-bss-not-data`, never silently skipped.
* **A constant the image made mutable and we wrote as a literal.**  If we spelled
  it `0.666f` inline (or `static const float`), it is in OUR `.rdata` and there
  is nothing in our `.data` to compare -- the row lands in MISSING, which is a
  lead, not a proven bug.
* **Lookup TABLES.**  A `.data` float blob larger than `MAX_CONSTANT_GROUP_BYTES`
  is a generated table (`ATH` is 5,818 floats), not a hand-written constant; it
  is excluded from the per-function join and counted.
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
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402
from data_float_labels import collect as collect_target, is_xdk  # noqa: E402

REL_PPC_REFHI = 0x10  # `lis rX, sym@ha`
REL_PPC_REFLO = 0x11  # `lfs fN, sym@l(rX)`  -- or the `addi` that materialises it

# `?name@...@4MA` -> float static;  `@4NA` -> double.  The char before the
# trailing `A` (cv-qualifier) is the type: M = float, N = double, H/I = int.
MANGLED_FLOAT = re.compile(r"@[0-9]M[A-D]$")
MANGLED_DOUBLE = re.compile(r"@[0-9]N[A-D]$")
#: `@3PAMA` / `@1PAMA` -- an ARRAY of float (`float x[]`), still float-typed.
MANGLED_FLOAT_ARRAY = re.compile(r"@[0-9]PAM[A-D]$")
MANGLED_DOUBLE_ARRAY = re.compile(r"@[0-9]PAN[A-D]$")
MANGLED_ANY_TYPED = re.compile(r"@[0-9][A-Z_][A-D]$")

NOISE_PREFIXES = ("??_R", "??_7", "??_8", "__unwind$", "__catch$")

#: A `.data` float run bigger than this is a generated LOOKUP TABLE, not a
#: hand-written constant group, and does not belong in the per-function join.
#: Measured: the non-XDK `.data` float blob sizes are
#: {4,8,12,16,24,28,40,112,23272} -- a clean gap between 0x70 and `ATH`'s 0x5ae8.
MAX_CONSTANT_GROUP_BYTES = 1024
#: Beyond this we do not even decode our side, to keep the sweep cheap.
MAX_STATIC_BYTES = 65536

FLOAT_TOL_REL = 1e-6


def plausible(v: float) -> bool:
    """Could this 4-byte pattern be a hand-written float constant?"""
    if v != v or math.isinf(v):
        return False
    if v == 0.0:
        return True
    a = abs(v)
    return 1e-6 <= a <= 1e9


def same_value(a: float, b: float) -> bool:
    return abs(a - b) <= FLOAT_TOL_REL * max(1.0, abs(a))


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


def _decode_floats(raw: bytes, width: int, as_double: bool):
    """Decode a datum as a list of big-endian floats (or one double)."""
    if as_double:
        if width % 8:
            return None
        return [struct.unpack_from(">d", raw, i)[0] for i in range(0, width, 8)]
    if width % 4:
        return None
    return [struct.unpack_from(">f", raw, i)[0] for i in range(0, width, 4)]


def our_float_statics(path: str):
    """Return (fn -> [(value, sym, confident)], statics, all_data_by_name).

    `statics`   : symbol index -> (values, name, confident) for the float ones.
    `all_data_by_name`: name -> (values, width) for EVERY `.data` datum that
                  decodes as float32 and holds no pointer, plausibility NOT
                  applied.  Only the exact-name join reads it, where a name
                  match is already proof of identity and a plausibility gate
                  would only drop true pairs (`gBSPMaxDepth` is an int the
                  image's listing renders as a denormal `.float`).
    """
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
    slot_at = {}      # (section, byte offset) -> (value, name, confident)
    all_data_by_name = {}
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
        for s in group:
            n = s["name"]
            if n.startswith(NOISE_PREFIXES):
                continue
            start = s["value"]
            end = anchors[anchors.index(start) + 1]
            width = end - start
            # A float ARRAY is still a float constant group.  Requiring width
            # in (4, 8) made every Vector3/Color/float[] invisible on our side
            # while the image reads them slot by slot.
            if width <= 0 or width % 4 or width > MAX_STATIC_BYTES:
                continue
            if any(start <= o < end for o in reloc_offs):
                continue
            raw = b[sec["ptr"] + start : sec["ptr"] + end]
            if len(raw) != width:
                continue

            is_double = bool(MANGLED_DOUBLE.search(n) or MANGLED_DOUBLE_ARRAY.search(n))
            vals = _decode_floats(raw, width, is_double)
            if vals is None:
                continue
            if not is_double:
                all_data_by_name.setdefault(n, (vals, width))

            if MANGLED_FLOAT.search(n) or MANGLED_FLOAT_ARRAY.search(n):
                conf = True
            elif is_double:
                conf = True
            elif MANGLED_ANY_TYPED.search(n):
                continue  # explicitly typed as something that is not a float
            else:
                if not all(plausible(v) for v in vals):
                    continue
                conf = False

            statics[s["idx"]] = (vals, n, conf)
            step = 8 if is_double else 4
            for i, v in enumerate(vals):
                slot_at[(secnum, start + i * step)] = (v, n, conf)
        sec_of_sym.update({s["idx"]: secnum for s in group})
        sym_off.update({s["idx"]: s["value"] for s in group})

    # 2. for each function COMDAT, walk relocations in address order
    fn_secs = defaultdict(list)  # section number -> function symbol names
    for s in syms:
        # sc 2 is external, sc 3 is STATIC.  Requiring external skipped every
        # internal-linkage function -- which in a C translation unit is most of
        # them: `dradf4`, `dradfg` and `dradbg` are all `sc=3` in our
        # smallft.obj, so their function-local statics could never pair and the
        # rows read as "the image has a mutable static we do not", which is the
        # opposite of the truth (we have it, under the same name).
        if s["sc"] in (2, 3) and 1 <= s["sec"] <= len(secs):
            sec = secs[s["sec"] - 1]
            if sec["chars"] & 0x20 and s["name"] != sec["name"]:  # CNT_CODE
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
    OP_ADDI, OP_ADDIS, OP_LFS, OP_LFD = 14, 15, 48, 50
    # A call is a BRANCH WITH THE LK BIT SET, not "opcode 18".  Treating every
    # opcode-18 word as a call dropped the volatile bases at every plain `b`
    # too: in `?ParseMarkup@RndText@@...` the anchor for gSuperscriptScale is
    # materialised into r10 at +0xac, and `0x48000010` at +0xc0 -- op 18,
    # **LK=0**, an unconditional forward branch -- cleared r10 four bytes
    # before `lfs f0, 0x4(r10)` (gGuitarScale) and again before
    # `lfs f0, 0x8(r10)` (gGuitarZOffset).  Our source defines all three
    # statics at exactly the image's values, and the tool reported us reading
    # one of them.
    BRANCH_OPS = (16, 18, 19)
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
                    datum = slot_at.get(key)
                    if datum is None:
                        st = statics.get(symidx)
                        if st is not None:
                            datum = (st[0][0], st[1], st[2])
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
                datum = slot_at.get((bsec, boff + disp))
                if datum is not None:
                    seq.append((off, datum))
                continue

            # Invalidate stale bases, conservatively.  A stale base is exactly
            # how a value gets attributed to the wrong static, which is the
            # failure this block exists to prevent.
            if op in BRANCH_OPS:
                if w & 1:                   # LK set => this branch is a CALL
                    for g in VOLATILE_GPRS:
                        base.pop(g, None)   # a call clobbers r0, r3-r12
            elif op in (OP_ADDI, OP_ADDIS) or 32 <= op <= 47:
                base.pop(rd, None)          # plain addi / any integer load

        if not seq:
            continue
        seq.sort()
        for n in names:
            out[n] = [st for _off, st in seq]
    return out, statics, all_data_by_name


# --------------------------------------------------------------------------- #
# Comparison
# --------------------------------------------------------------------------- #

def classify(target_values, our_values):
    """Bucket one function's two value lists.

    The tiers are deliberately ordered from strongest evidence of agreement to
    weakest, and each tier states what a WRONG VALUE would have to do to hide
    in it:

      agree        exact ordered match.
      order_only   equal MULTISETS, different issue order.  Our MSVC and the
                   image schedule the loads of one static group differently --
                   on IsValidSwipePosition the image reads the +4 slot before
                   the +0 slot and we read +0 before +4 -- so an ORDERED
                   comparison manufactures a value disagreement out of correct
                   source.  A wrong VALUE cannot hide here: the multisets are
                   equal, so every constant the image loads is one we load.
      count_only   equal distinct-value SETS, different read COUNTS.  The two
                   instruction walkers do not always see a constant the same
                   number of times: the image reads gSuperscriptScale/
                   gGuitarScale/gGuitarZOffset off one materialised anchor in
                   ParseMarkup where our object reaches only one of them.  A
                   wrong VALUE cannot hide here either -- entry requires every
                   distinct constant the image reads to be one we also read --
                   but a DROPPED or DUPLICATED read can, so this is strictly
                   weaker than order_only and is reported separately rather
                   than folded into it.
      disagree     the image reads a constant we never read in this function.
    """
    tv, ov = list(target_values), list(our_values)
    if len(tv) == len(ov) and all(same_value(p, q) for p, q in zip(tv, ov)):
        return "agree"
    if len(tv) == len(ov) and all(
        same_value(p, q) for p, q in zip(sorted(tv), sorted(ov))
    ):
        return "order_only"

    def _set_eq(xs, ys):
        xs, ys = sorted(set(xs)), sorted(set(ys))
        return len(xs) == len(ys) and all(same_value(p, q) for p, q in zip(xs, ys))

    if tv and ov and _set_eq(tv, ov):
        return "count_only"
    return "disagree"


def compare_named(our_vals, our_width, blob):
    """Compare one exact-name pair slot by slot.  Returns (n_cmp, disagreements).

    Only the OVERLAPPING prefix is compared, and only at offsets the target
    calls a float.  Both halves matter:

      * dtk ends a blob at the next NAMED symbol, so a run of unnamed siblings
        folds into one blob -- `?tpi@?1??dradfg@@9@9` is 0x18 in the image and
        four bytes in ours, and the five extra floats belong to four OTHER
        statics.  Requiring equal lengths would call that a bug.
      * a blob can interleave non-float slots (`gRemoteGain` is
        `.float 3` then two `.4byte`), and decoding those as floats would
        manufacture a disagreement out of a pointer.
    """
    n_cmp = 0
    bad = []
    limit = min(our_width, blob.size)
    for off, (directive, text) in sorted(blob.slots.items()):
        if off >= limit or directive != ".float":
            continue
        if off % 4 or off // 4 >= len(our_vals):
            continue
        try:
            tv = float(text)
        except ValueError:
            continue
        ov = our_vals[off // 4]
        n_cmp += 1
        if not same_value(tv, ov):
            bad.append({"offset": off, "target": tv, "ours": ov})
    return n_cmp, bad


def selftest() -> int:
    """Exercise the comparator on synthetic input, both directions.

    A sweep whose instrument has not been shown to FIRE on a known instance,
    and to STAY SILENT on a known-good one, is not evidence of anything.
    """
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    check("exact order -> agree",
          classify([0.9, 1.3], [0.9, 1.3]) == "agree")
    check("permuted, equal multiset -> order_only",
          classify([1.1, 0.8, 1.3, 0.9], [1.1, 0.8, 0.9, 1.3]) == "order_only")
    check("a WRONG VALUE cannot land in order_only",
          classify([1.1, 0.8, 1.3, 0.9], [1.1, 0.8, 0.42, 1.3]) == "disagree")
    check("same set, different counts -> count_only",
          classify([0.7, 0.7, 0.2], [0.7, 0.2]) == "count_only")
    check("a WRONG VALUE cannot land in count_only",
          classify([0.7, 0.7, 0.2], [0.7, 0.42]) == "disagree")
    check("ours empty -> disagree, never count_only",
          classify([0.7], []) == "disagree")
    check("float tolerance accepts a float32 round-trip",
          classify([0.66843998], [0.66844]) == "agree")

    class _B:
        def __init__(self, slots, size):
            self.slots, self.size = slots, size

    # prefix rule: the image's blob is longer because unnamed siblings folded in
    n, bad = compare_named([6.2831855], 4,
                           _B({0: (".float", "6.2831855"),
                               4: (".float", "-0.5")}, 0x18))
    check("named join compares only the overlapping prefix", n == 1 and not bad)

    # a non-float slot inside the blob must not be decoded as a float
    n, bad = compare_named([3.0, 1.0], 8,
                           _B({0: (".float", "3"),
                               4: (".4byte", "0xFFFFFFFF # NaN")}, 8))
    check("named join skips a non-float slot", n == 1 and not bad)

    n, bad = compare_named([800.0], 4, _B({0: (".float", "800")}, 4))
    check("named join silent when the value agrees", n == 1 and not bad)
    n, bad = compare_named([42.0], 4, _B({0: (".float", "800")}, 4))
    check("named join FIRES when the value differs",
          n == 1 and len(bad) == 1 and bad[0]["target"] == 800.0)

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--asm-root", default="build/373307D9/asm")
    ap.add_argument("--obj-root", default="build/373307D9/src")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    # Rule: a missing input is never a clean verdict.
    if not os.path.isdir(args.asm_root):
        print(f"INCONCLUSIVE: target asm not found: {args.asm_root}")
        print("The target side of this comparison is unavailable; this run "
              "checked nothing.")
        return EXIT_NO_INPUT
    if not os.path.isdir(args.obj_root):
        print(f"INCONCLUSIVE: built objects not found: {args.obj_root}")
        print("Run `ninja` first; an unbuilt tree cannot be compared.")
        return EXIT_NO_INPUT

    cov = CoverageReport("mutable_float_audit", args=args)
    cov.require_examined("no .data float constant was comparable against ours")

    # ---- target side --------------------------------------------------
    all_blobs, by_name, sites, _fn_refs, fn_file = collect_target(args.asm_root)
    data_float_blobs = [b for b in all_blobs if b.section == ".data" and b.floats]
    cov.universe(len(data_float_blobs),
                 "target `.data` blobs holding at least one float slot "
                 "(whole binary)")

    tgt_fn = defaultdict(list)
    sites_by_blob = defaultdict(list)
    n_site_table = 0
    n_site_named = 0
    for s in sites:
        if s.blob_name is None or is_xdk(s.file):
            continue
        blob = by_name[s.blob_name]
        if blob.section != ".data":
            continue
        if blob.size > MAX_CONSTANT_GROUP_BYTES:
            n_site_table += 1
            continue
        # The per-function join is for the statics dtk could NOT name.  A blob
        # the image names is joined by EXACT NAME below, which is strictly
        # stronger evidence -- no function pairing, no load-order inference.
        #
        # Feeding named blobs into this join as well does not add coverage, it
        # adds false leads: our per-function walker can only see statics
        # defined in the SAME object, so every CROSS-TU named static reads as
        # "the image loads a constant we have nothing for".  Measured: 20 of
        # 28 MISSING rows were one of `?gUnitsPerMeter@@3MA` (39.3701),
        # `?gCharHighlightY@@3MA` (-1), `?sIntensity@FlowNode@@1MA` (1),
        # `?sDOFOverride@RndPostProc@@...` or `?sBloomLocFactor@RndPostProc@@1MA`
        # -- every one of which our tree defines, at the image's value, in
        # another TU, and every one of which the name join already checks.
        # `?sBloomLocFactor@RndPostProc@@1MA` is an UNDEFINED EXTERNAL in
        # PostProc_NG.obj (`sec=0`) and defined in PostProc.obj; it produced
        # the one DISAGREE row that was not a real disagreement.
        if not s.blob_name.startswith("lbl_"):
            n_site_named += 1
            continue
        tgt_fn[s.fn].append((float(s.value), s.blob_name, f"0x{s.addr:08X}",
                             s.line, s.blob_off, s.via))
        sites_by_blob[s.blob_name].append(s)

    # ---- our side -----------------------------------------------------
    ours_fn = {}
    ours_named = {}        # name -> (values, confident, object path)
    ours_raw_by_name = {}  # name -> (values, width, object path)
    n_objs = 0
    n_obj_unparseable = 0
    # sorted(): `setdefault` is FIRST-write-wins, so with an unsorted glob the
    # winner for a symbol defined in more than one object is decided by
    # filesystem order -- the scope_index_census defect exactly.  Measured
    # 2026-09-16 this is LATENT, not live (1 symbol in >1 object, 0 conflicting
    # values, 0 conflicting function lists), which is the right time to pin it
    # rather than after it starts flipping a verdict.
    for p in sorted(glob.glob(os.path.join(args.obj_root, "**", "*.obj"),
                              recursive=True)):
        n_objs += 1
        try:
            o, st, raw = our_float_statics(p)
        except Exception as e:  # noqa: BLE001
            # This swallow used to be silent.  An unparseable object SHORTENS
            # our side, which INFLATES the "target only" counts -- a drop that
            # manufactures findings rather than hiding them.
            print(f"!! {p}: {e}", file=sys.stderr)
            n_obj_unparseable += 1
            continue
        for k, v in o.items():
            ours_fn.setdefault(k, v)
        for vals, n, c in st.values():
            ours_named.setdefault(n, (vals, c, p))
        for n, (vals, width) in raw.items():
            ours_raw_by_name.setdefault(n, (vals, width, p))

    # ---- join 1: per FUNCTION -----------------------------------------
    paired, missing, agree, disagree, lowconf, order_only, count_only = (
        [], [], [], [], [], [], [])
    examined_blobs = set()
    for fn, tl in sorted(tgt_fn.items()):
        ol = ours_fn.get(fn)
        rec = {
            "fn": fn,
            "file": fn_file.get(fn, "?"),
            "target": [{"value": v, "label": n, "addr": a, "asm_line": ln,
                        "slot": off, "via": via}
                       for v, n, a, ln, off, via in tl],
            "ours": None if ol is None else
                    [{"value": v, "sym": n, "confident": c} for v, n, c in ol],
        }
        if ol is None:
            missing.append(rec)
            continue
        paired.append(rec)
        for _v, n, _a, _l, _o, _via in tl:
            examined_blobs.add(n)
        verdict = classify([e["value"] for e in rec["target"]],
                           [e["value"] for e in rec["ours"]])
        {"agree": agree, "order_only": order_only,
         "count_only": count_only, "disagree": disagree}[verdict].append(rec)
        if any(not c for _v, _n, c in ol):
            lowconf.append(fn)

    # ---- join 2: exact SYMBOL NAME ------------------------------------
    # dtk invents a `lbl_*` name only for a datum `symbols.txt` does not name.
    # Everything it DOES name can be joined by NAME with no pairing inference
    # at all -- strictly stronger evidence than the per-function path.
    #
    # The shape filter that used to sit here (`MANGLED_FLOAT or MANGLED_DOUBLE`
    # on the TARGET side) dropped 17 of 50 such blobs, i.e. every unmangled
    # file-scope static the image happens to name.  Our side never had it.
    named_rows, named_dis = [], []
    n_named_absent = 0
    for blob in sorted(data_float_blobs, key=lambda b: b.addr):
        if is_xdk(blob.file):
            continue
        nm = blob.name.strip('"')
        entry = ours_raw_by_name.get(nm)
        if entry is None:
            n_named_absent += 1
            continue
        our_vals, our_width, our_path = entry
        n_cmp, bad = compare_named(our_vals, our_width, blob)
        if not n_cmp:
            continue
        examined_blobs.add(blob.name)
        conf = ours_named.get(nm, (None, False, None))[1]
        row = {
            "symbol": nm,
            "addr": f"0x{blob.addr:08X}",
            "file": blob.file,
            "object": os.path.relpath(our_path, args.obj_root),
            "slots_compared": n_cmp,
            "target_slots": len(blob.floats),
            "our_slots": len(our_vals),
            "confident": conf,
            "placeholder_name": nm.startswith("lbl_"),
            "disagreements": bad,
        }
        named_rows.append(row)
        if bad:
            named_dis.append(row)

    # ---- coverage accounting ------------------------------------------
    # Every blob in the universe is examined or dropped, with a reason.
    n_examined = 0
    for blob in data_float_blobs:
        if blob.name in examined_blobs:
            n_examined += 1
            cov.examine()
        elif is_xdk(blob.file):
            cov.drop("xdk-or-autogenerated-tu",
                     note="not our source; the image links the Microsoft libs")
        elif blob.size > MAX_CONSTANT_GROUP_BYTES:
            cov.drop("lookup-table-over-size-threshold",
                     note=f"> {MAX_CONSTANT_GROUP_BYTES} B is a generated table, "
                          f"not a hand-written constant group")
        elif blob.name in ours_raw_by_name or blob.name.strip('"') in ours_raw_by_name:
            cov.drop("name-matches-but-no-float-slot-overlapped")
        elif not sites_by_blob.get(blob.name):
            cov.drop("no-float-read-reaches-it-and-no-name-match",
                     note="nothing in the image reads it as a float and we "
                          "define no symbol of that name")
        else:
            cov.drop("read-by-a-function-we-do-not-pair",
                     note="our build defines no function of that mangled name, "
                          "or our function holds no .data float static")

    n_bss = sum(1 for r in missing if r["fn"])
    cov.note("compares `.data` against `.data`; a static our source lands in "
             "`.bss` (e.g. kBlurTaps) cannot be paired")
    cov.note("MISSING rows are LEADS, not proven bugs: a constant we spelled "
             "as a literal or `static const` lives in our `.rdata`")
    cov.extra("our_objects", n_objs)
    cov.extra("our_objects_unparseable", n_obj_unparseable)

    den = {
        "asm_files_parsed": len({b.file for b in all_blobs}),
        # "parsed" used to be the GLOBBED count, so it claimed 990 while 989
        # parsed and StreamRecorder.obj (a 0-byte orphan of a deleted TU) threw.
        "our_objects_globbed": n_objs,
        "our_objects_PARSED": n_objs - n_obj_unparseable,
        "our_objects_UNPARSEABLE": n_obj_unparseable,
        "target_data_float_blobs_whole_binary": len(data_float_blobs),
        "target_data_float_blobs_nonxdk": len(
            [b for b in data_float_blobs if not is_xdk(b.file)]),
        "target_data_float_blobs_EXAMINED": n_examined,
        "target_float_read_sites_nonxdk": sum(len(v) for v in tgt_fn.values()),
        "target_sites_dropped_lookup_table": n_site_table,
        "target_sites_deferred_to_the_NAME_join": n_site_named,
        "target_functions_loading_one": len(tgt_fn),
        "paired_with_our_function": len(paired),
        "unpaired_ours_has_no_float_static": len(missing),
        "paired_and_agreeing": len(agree),
        "paired_and_DISAGREEING": len(disagree),
        "paired_same_values_DIFFERENT_ORDER": len(order_only),
        "paired_same_value_SET_different_READ_COUNT": len(count_only),
        "paired_with_a_low_confidence_our_side_symbol": len(lowconf),
        "our_data_float_statics_by_name": len(ours_named),
        "our_data_symbols_decodable_as_float": len(ours_raw_by_name),
        "named_join_PAIRS": len(named_rows),
        "named_join_slots_compared": sum(r["slots_compared"] for r in named_rows),
        "named_target_only_we_define_no_such_symbol": n_named_absent,
        "named_exact_name_VALUE_DISAGREEMENTS": len(named_dis),
    }

    if args.json:
        json.dump(
            {"denominators": den, "disagree": disagree, "missing": missing,
             "order_only": order_only, "count_only": count_only,
             "named_disagreements": named_dis, "named_pairs": named_rows,
             "agree": agree, "_coverage": cov.as_dict()},
            sys.stdout, indent=2,
        )
        print()
        return cov.emit()

    print("== DENOMINATORS ==")
    for k, v in den.items():
        print(f"  {k:52} {v}")

    print(f"\n== NAMED-SYMBOL VALUE DISAGREEMENTS ({len(named_dis)}) ==")
    print("   exact name match on both sides -- no pairing inference")
    for r in named_dis:
        print(f"-- {r['symbol']}  @{r['addr']}  [{r['file']}]  ours: {r['object']}")
        for d in r["disagreements"]:
            print(f"   +0x{d['offset']:<4x} TGT {d['target']:<14g} OURS {d['ours']:g}")

    def show(title, recs, note=""):
        print(f"\n== {title} ({len(recs)}) ==")
        if note:
            print(f"   {note}")
        for r in recs:
            print(f"-- {r['fn']}\n   {r['file']}")
            t = "  ".join(
                f"{e['label']}{('+0x%x' % e['slot']) if e['slot'] else ''}={e['value']:g}"
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

    show("DISAGREE (paired, the image reads a constant we never read)", disagree)
    show("SAME VALUES, DIFFERENT LOAD ORDER (scheduling, not a bug)", order_only,
         "equal multisets -- a wrong VALUE cannot be in here")
    show("SAME VALUE SET, DIFFERENT READ COUNT (walker asymmetry)", count_only,
         "equal distinct-value sets -- a wrong VALUE cannot be in here either")
    show("MISSING (target has a mutable static, our function has none)", missing,
         "a LEAD: we may have spelled it as a literal or a `static const`")
    show("AGREE", agree)

    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
