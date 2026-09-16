#!/usr/bin/env python3
"""this_offset_scan.py — find WRONG STRUCT FIELDS by comparing `this`-relative
access coordinates between the target object and ours, function by function.

WHY THIS EXISTS
===============
Taxonomy class 1 (`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`)
is 121 behavioural bugs and had no safe detector.  A same-width, same-shape
wrong field is nearly free under every ruler this project owns: the instruction
is byte-identical and only the 16-bit displacement differs.  `RndFlare::Load`
read `mOffset` where the image reads `mSteps` (`subi r4,r31,0x5c` vs `0x58`) and
`CharUpperTwist::Load` 3-cycled three bone references; BOTH hid under a rendered
100.0%.

The one instrument that existed —  `run_analyze_function`'s "Offset Mismatches
(resolved)" block — **ignores the base register**, so it renders a pure `(r1)`
stack-slot diff as "source accesses X but target accesses Y".  It manufactured
exactly that story for `FxSendChorus::Load` and sent a lane hunting a member of
`FxSend` that does not exist.  That block is a lead, never a finding.

WHAT THIS DOES INSTEAD
======================
For every function defined in BOTH the dtk-carved target object and our built
object:

  1. Prove, per register, whether it holds `this + K` — by a flow-INSENSITIVE
     fixpoint over the whole body.  A register qualifies only if EVERY
     definition of it in the function is `mr rD,rS` / `addi rD,rS,imm` from an
     already-proven this-register and they all agree on K.  `r3` is `this + 0`
     only until its first redefinition (a `bl` redefines r3-r12).  Anything
     else — a load, an `add`, an unknown opcode — poisons the register
     permanently.  A stack slot is therefore never mistaken for a field:
     `r1` and every r1-derived register fail the test by construction.
  2. Reduce each memory reference through such a register to a BIAS-INVARIANT
     coordinate `K + displacement`, tagged with its access kind (`w`/`b`/`h`/
     `fs`/`fd`/`addr`/...).  This matters: MSVC routinely hands a function a
     BIASED `this`.  `RndFlare::Load` runs with `r31 = this + 0x188` and reaches
     every field by a NEGATIVE displacement.  Comparing raw displacements
     without tracking K compares two different coordinate systems.
  3. Compare the two multisets.  Report only the SUBSTITUTION shape — the two
     sides touch the same number of coordinates of the same kind, and a small
     number of them differ.  That is the class-1 fingerprint.

WHAT IT CANNOT SEE  (read this before calling the class exhausted)
-----------------------------------------------------------------
* **A byte-identical function cannot carry an offset-visible wrong field.**
  Identical instruction words have identical displacements, so the strongest
  slice — "100% with zero mismatch rows" — is empty of this class BY
  CONSTRUCTION, not by this scanner's judgement.  It is counted and reported as
  `agree-byte-identical`, which on the current tree is ~73% of all pairs.  What
  CAN hide there is a mislabelled LAYOUT: our header naming offset 0x5c
  `mOffset` when the image's class has `mSteps` there.  Then both sides emit the
  same displacement and this scanner — which only ever compares our object with
  the target's — is structurally blind.  That needs independent layout truth
  (RB2 DWARF, Ghidra), not a target diff.
* **Only ~979 of 2,223 target objects have a built counterpart.**  A wrong field
  in a TU that does not build yet is invisible here.  Stated on every run.
* **A whole-function bias difference is dropped, deliberately** — if every
  differing coordinate moves by ONE constant delta, that is our build choosing a
  different `this` bias than the image, not a wrong field.  Counted as
  `uniform-bias-delta`.
* **Insertion/deletion shapes are counted, not reported** (`shape-differs`).
  A different inline depth changes WHICH fields are touched and by how many
  instructions; that bucket is dominated by ordinary unmatched codegen and is a
  work list for matching, not a bug list.  It is the honest place for the false
  positives to live.
* **Field NAMES are annotation, never the finding.**  The finding is the
  coordinate divergence, which is measured.  The name requires an ANCHOR (the
  function's `this` bias), inferred by fitting `anchor + coord` against the
  class's declared member offsets; `RndFlare::Load` fits at 0x188, `CalcScale`
  at 0.  When the fit is weak or the class is unknown to `struct_db.sqlite`, the
  row still reports its coordinates and says `anchor=unresolved`.

USAGE
-----
    python3 scripts/analysis/this_offset_scan.py --selftest   # validate first
    python3 scripts/analysis/this_offset_scan.py
    python3 scripts/analysis/this_offset_scan.py --json out.json --show-shape-differs

Exit code is `CoverageReport.emit()`'s.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_OBJDIFF = os.path.join(REPO, "objdiff.json")
DEFAULT_REPORT = os.path.join(REPO, "build", "373307D9", "report.json")


def _default_struct_db():
    """`struct_db.sqlite` lives in the MAIN checkout and is not copied into a
    worktree.  Fall back to the main checkout the way native_configure.sh finds
    it -- via the git common dir -- rather than hardcoding a path.  Annotation
    only: a miss costs field NAMES, never a finding."""
    local = os.path.join(REPO, "struct_db.sqlite")
    if os.path.exists(local):
        return local
    common = os.path.join(REPO, ".git")
    try:
        with open(common) as f:              # a worktree's .git is a FILE
            line = f.read().strip()
        if line.startswith("gitdir:"):
            main_git = line.split(":", 1)[1].strip()
            main_root = main_git.split("/.git/")[0]
            cand = os.path.join(main_root, "struct_db.sqlite")
            if os.path.exists(cand):
                return cand
    except OSError:
        pass
    return local

# --------------------------------------------------------------------------- #
# COFF slicing.
#
# Deliberately a local reader rather than an import: this scanner needs the
# per-section function slices and nothing else, and the two existing readers
# disagree about EH prefixes in ways that would change the instruction stream
# (see scripts/analysis/coff_bodies.py).  An 8-byte EH prefix billed to the
# previous function appends two RELOCATED DATA WORDS to its body, and those
# words decode as arbitrary instructions -- including, sometimes, a load.  So
# the prefix trim is applied here too: a body's last two words are dropped when
# they are the zero-filled `__CxxFrameHandler` / `__ehfuncinfo$` pointer pair.
# --------------------------------------------------------------------------- #
IMAGE_SYM_DTYPE_FUNCTION = 0x20
IMAGE_REL_PPC_PAIR = 0x0010


def read_coff(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20:
        return None, None, None, None
    _mach, nsec, _tds, symoff, nsym, optsz, _ch = struct.unpack_from("<HHIIIHH", data, 0)
    if not symoff or not nsym:
        return None, None, None, None
    strtab = symoff + nsym * 18

    def name_at(off):
        end = data.find(b"\x00", off)
        return data[off:end if end >= 0 else len(data)].decode("ascii", "replace")

    secs = []
    so = 20 + optsz
    for i in range(nsec):
        o = so + i * 40
        if o + 40 > len(data):
            return None, None, None, None
        nb = data[o:o + 8]
        if nb[:1] == b"/":
            try:
                nm = name_at(strtab + int(nb[1:].rstrip(b"\x00").decode()))
            except Exception:
                nm = nb.rstrip(b"\x00").decode("ascii", "replace")
        else:
            nm = nb.rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr, relptr, _lp, nrel, _nl = \
            struct.unpack_from("<IIIIIIHH", data, o + 8)
        secs.append({"idx": i + 1, "name": nm, "rawsize": rawsize, "rawptr": rawptr,
                     "relptr": relptr, "nrel": nrel})
    syms = []
    by_index = {}
    i = 0
    off = symoff
    while i < nsym:
        if off + 18 > len(data):
            break
        nb = data[off:off + 8]
        if nb[:4] == b"\x00\x00\x00\x00":
            nm = name_at(strtab + struct.unpack_from("<I", nb, 4)[0])
        else:
            nm = nb.rstrip(b"\x00").decode("ascii", "replace")
        value, sec, typ, _cls, naux = struct.unpack_from("<IhHBB", data, off + 8)
        s = {"name": nm, "value": value, "section": sec, "type": typ}
        syms.append(s)
        by_index[i] = s
        i += 1 + naux
        off += 18 * (1 + naux)
    return data, secs, syms, by_index


def function_bodies(path):
    """symbol name -> list of big-endian instruction words."""
    data, secs, syms, by_index = read_coff(path)
    if data is None:
        return {}
    by_sec = {s["idx"]: s for s in secs}
    per_sec = {}
    for s in syms:
        if s["section"] <= 0 or s["type"] != IMAGE_SYM_DTYPE_FUNCTION:
            continue
        sec = by_sec.get(s["section"])
        if sec is None or not sec["name"].startswith(".text"):
            continue
        per_sec.setdefault(s["section"], []).append(s)

    # relocation targets, per section, keyed by section-relative address --
    # used only to recognise the interior EH prefix.
    relnames = {}
    for sec in secs:
        if not sec["nrel"] or not sec["name"].startswith(".text"):
            continue
        m = {}
        for r in range(sec["nrel"]):
            ro = sec["relptr"] + r * 10
            if ro + 10 > len(data):
                break
            va, si, ty = struct.unpack_from("<IIH", data, ro)
            if ty != IMAGE_REL_PPC_PAIR:
                m[va] = by_index.get(si, {}).get("name", "?")
        relnames[sec["idx"]] = m

    out = {}
    for sec_idx, group in per_sec.items():
        sec = by_sec[sec_idx]
        group.sort(key=lambda x: x["value"])
        rmap = relnames.get(sec_idx, {})
        for n, s in enumerate(group):
            start = s["value"]
            end = group[n + 1]["value"] if n + 1 < len(group) else sec["rawsize"]
            if end <= start or start % 4:
                continue
            # interior EH prefix trim: 8 zero bytes whose two words relocate to
            # the C++ EH handler pair.  Data, not code -- never decode it.
            if end - start >= 16:
                a, b = rmap.get(end - 8, ""), rmap.get(end - 4, "")
                blob = data[sec["rawptr"] + end - 8: sec["rawptr"] + end]
                if blob == b"\x00" * 8 and (a.startswith("__CxxFrameHandler")
                                            or b.startswith("__ehfuncinfo$")):
                    end -= 8
            raw = data[sec["rawptr"] + start: sec["rawptr"] + end]
            words = [struct.unpack_from(">I", raw, o)[0]
                     for o in range(0, len(raw) - 3, 4)]
            calls = {}
            for k, w in enumerate(words):
                if (w >> 26) == 18 and (w & 1):          # bl
                    nm = rmap.get(start + k * 4)
                    if nm:
                        calls[k] = nm
            if s["name"] not in out or len(words) > len(out[s["name"]][0]):
                out[s["name"]] = (words, calls)
    return out


# --------------------------------------------------------------------------- #
# PPC decoding — only what a field access can be.
# --------------------------------------------------------------------------- #
# D-form memory ops: primary opcode -> (kind, updates_rA)
DFORM_MEM = {
    32: ("w", False), 33: ("w", True),     # lwz  lwzu
    34: ("b", False), 35: ("b", True),     # lbz  lbzu
    36: ("w", False), 37: ("w", True),     # stw  stwu
    38: ("b", False), 39: ("b", True),     # stb  stbu
    40: ("h", False), 41: ("h", True),     # lhz  lhzu
    42: ("h", False), 43: ("h", True),     # lha  lhau
    44: ("h", False), 45: ("h", True),     # sth  sthu
    46: ("m", False), 47: ("m", False),    # lmw  stmw
    48: ("fs", False), 49: ("fs", True),   # lfs  lfsu
    50: ("fd", False), 51: ("fd", True),   # lfd  lfdu
    52: ("fs", False), 53: ("fs", True),   # stfs stfsu
    54: ("fd", False), 55: ("fd", True),   # stfd stfdu
}
DSFORM_MEM = {58: "d", 62: "d"}            # ld/ldu/lwa, std/stdu
OP_ADDI = 14
OP_ADDIS = 15
OP_BRANCH = 18
OP_BC = 16
OP_BCLR_BCCTR = 19
OP_X = 31
XO_OR = 444
XO_MR_MASK = 0x3FF
VOLATILE_GPRS = frozenset(range(3, 13)) | {0, 11, 12}


def _op(w):
    return w >> 26


def _d(w):
    return (w >> 21) & 0x1F


def _a(w):
    return (w >> 16) & 0x1F


def _b(w):
    return (w >> 11) & 0x1F


def _simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _ds(w):
    v = w & 0xFFFC
    return v - 0x10000 if v & 0x8000 else v


# X-form (primary 31) extended opcodes that write NO general register.  Without
# these a `cmpw cr6, r31, r4` would be read as a definition of r31 and poison
# the `this` register of most functions in the binary -- safe, but it throws
# away most of the coverage this scanner exists to provide.
X_NO_GPR_WRITE = frozenset({
    0, 32,          # cmp, cmpl
    467, 144, 512,  # mtspr, mtcrf, mtcrf/mcrxr
    4, 598, 854,    # tw, sync, eieio
    151, 215, 407, 663, 727, 149,   # stwx stbx sthx stfsx stfdx stdx
    86, 470, 54, 1014,              # dcbf dcbi dcbst dcbz
})
X_OR = 444          # `or rA,rS,rB` (and therefore `mr`) writes rA ONLY.
OP_LMW = 46


def _defs_gpr(w):
    """Registers this instruction writes.  Conservative: when unsure, say it
    writes its rD field, and for a call say it clobbers every volatile."""
    o = _op(w)
    if o == OP_BRANCH and (w & 1):                 # bl
        return VOLATILE_GPRS
    if o == OP_BC and (w & 1):                     # bcl
        return VOLATILE_GPRS
    if o == OP_BCLR_BCCTR and (w & 1):             # bctrl / blrl
        return VOLATILE_GPRS
    if o == OP_LMW:                                # loads rD..r31
        return set(range(_d(w), 32))
    if o in DFORM_MEM:
        _kind, upd = DFORM_MEM[o]
        wrote = set()
        if o in (32, 33, 34, 35, 40, 41, 42, 43, 58):   # integer loads
            wrote.add(_d(w))
        if upd:
            wrote.add(_a(w))
        return wrote
    if o in DSFORM_MEM:
        if o == 58:
            return {_d(w)}
        return {_a(w)} if (w & 3) == 1 else set()  # stdu writes rA
    if o in (OP_ADDI, OP_ADDIS, 7, 8, 12, 13, 24, 25, 26, 27, 28, 29, 20, 21, 23, 30):
        return {_d(w)}
    if o == OP_X:
        xo = (w >> 1) & 0x3FF
        if xo in X_NO_GPR_WRITE:
            return set()
        if xo == X_OR:
            return {_a(w)}          # `or rA,rS,rB` / `mr rA,rS`
        # Everything else in X/XO form: most write rA (logical/shift) or rD
        # (arithmetic).  Claim BOTH -- over-claiming only poisons registers,
        # which loses coverage; under-claiming would INVENT a `this`.
        return {_a(w), _d(w)}
    if o in (OP_BRANCH, OP_BC, OP_BCLR_BCCTR, 10, 11, 17, 47, 59, 63):
        return set()
    return {_d(w), _a(w)}


# MSVC's register save/restore helpers are NOT ordinary calls: they touch r12
# and the stack and preserve every argument register.  Treating `bl
# __savegprlr_25` -- the second instruction of every EH-bearing prologue in this
# binary -- as a clobber of r3 kills the `this` proof before `mr r31,r3` ever
# runs, and the scanner then reports ZERO this-relative accesses for the
# majority of the corpus while looking perfectly healthy.  (The same defect in
# the unicorn emulation harness overstated real bugs ~8x; see CLAUDE.md.)
HELPER_PREFIXES = ("__savegprlr_", "__restgprlr_", "__savegpr_", "__restgpr_",
                   "__savefpr_", "__restfpr_", "__savevmx_", "__restvmx_")


def analyse(words, calls=None):
    """(coords, ok) — Counter-like dict {(kind, coord): n} of this-relative
    accesses, and whether any this-register was proven at all."""
    calls = calls or {}

    def defs_at(i, w):
        nm = calls.get(i)
        if nm and nm.startswith(HELPER_PREFIXES):
            return set()
        return _defs_gpr(w)

    # WHY A LINEAR SCAN AND NOT A FLOW-INSENSITIVE PROOF.
    #
    # The first cut proved `reg == this + K` flow-insensitively: a register
    # qualified only if EVERY definition of it in the body agreed.  That is
    # sound in isolation and WRONG FOR A COMPARISON, because it is ASYMMETRIC --
    # whether a register happens to be reused later is a register-allocation
    # choice, and poisoning it on one side only invents a difference.
    #
    # Measured: `HamNavList::NumItems` is a pure r30<->r31 permutation scoring
    # **100.0000** normalized, where no displacement can differ by construction.
    # The image does `mr r31,r3` then `addi r31,r31,0x190`; we do
    # `addi r31,r3,0x70` then `addi r31,r30,0x190`.  Same values, but OUR r31
    # has two disagreeing definitions and the image's r30 has one -- so the
    # flow-insensitive rule proved the image's `this + 0x70` and refused ours,
    # and the scanner reported a wrong-field candidate on a function that cannot
    # have one.  A rule whose verdict depends on which side you are standing on
    # is not a measurement.
    #
    # The linear scan is unsound at a backward branch (a register reassigned in
    # a loop reads as its fall-through value).  That unsoundness is SYMMETRIC --
    # both sides get it -- so it can mis-NAME a coordinate but cannot invent a
    # difference between two identically-shaped instruction streams.

    # -- what counts as "touching a field" ---------------------------------- #
    #
    # NOT every `addi rD, rThis, K`.  MSVC freely materialises `this - A` and
    # then reaches the real address with a second `addi ..., +B`: `Flow::PreSave`
    # computes `subi r11,r3,0x180` / `addi r3,r11,0x68` where our build computes
    # `subi r31,r3,0x118` -- the SAME address, two spellings.  Counting the
    # intermediate anchor manufactures a wrong-field story out of a register
    # materialisation choice, the `this`-relative twin of
    # docs/decomp/patterns/anchor-displacement-false-wrong-global.md.
    #
    # An earlier cut tried to SUPPRESS anchors ("an addi whose result is later
    # used as a base").  That rule is ASYMMETRIC -- whether a pointer is reused
    # as a base is itself a codegen choice -- and it silently moved a real
    # candidate (`CharMirror::Poll`) into the shape-differs bucket.  A rule that
    # can hide a finding depending on which side you look from is worse than no
    # rule.
    #
    # So the invariant is what the function actually TOUCHES:
    #   * an address that is DEREFERENCED (a load or a store), and
    #   * an address LIVE IN AN ARGUMENT REGISTER at a call -- because that is
    #     how a serializer touches a field: `subi r4,r31,0x5c; bl ReadEndian`
    #     is the RndFlare::Load bug and dereferences nothing locally.
    # Both are properties of an instruction that exists on both sides, so the
    # two coordinate sets are comparable.
    #
    # Volatile registers get a LINEAR scan (they are block-local and dead across
    # calls); non-volatiles keep the sound flow-insensitive proof above.
    coords = {}
    cur = {}                            # volatile reg -> this-offset
    # An argument register only counts if it was SET UP FOR THIS CALL.  Without
    # this, a stale `this`-relative value left in r4 by an earlier call is
    # counted as an argument of the next one, and the count then depends on
    # register allocation: `HamNavList::NumItems` and `Spotlight::Generate` are
    # both pure r30<->r31 permutations that produced phantom argument
    # coordinates this way -- NumItems at a *100.0000* normalized score, where
    # by construction no displacement can differ.
    fresh = set()
    cur[3] = 0                          # `this` arrives in r3
    fresh.add(3)

    def val(r):
        return cur.get(r)

    for i, w in enumerate(words):
        o = _op(w)
        nm = calls.get(i)
        helper = bool(nm and nm.startswith(HELPER_PREFIXES))
        is_call = (not helper) and (w & 1) and o in (OP_BRANCH, OP_BC,
                                                     OP_BCLR_BCCTR)
        if is_call:
            for areg in range(3, 11):
                v = val(areg)
                if v is not None and areg in fresh:
                    coords[("arg", v)] = coords.get(("arg", v), 0) + 1

        base = None
        if o in DFORM_MEM:
            kind = DFORM_MEM[o][0]
            base, disp = _a(w), _simm(w)
        elif o in DSFORM_MEM:
            kind = DSFORM_MEM[o]
            base, disp = _a(w), _ds(w)
        if base is not None and base != 0:
            k = val(base)
            if k is not None:
                key = (kind, k + disp)
                coords[key] = coords.get(key, 0) + 1

        # -- register update ------------------------------------------------ #
        src_val = None
        dst = None
        if o == OP_ADDI and _a(w) != 0:
            dst, src_val = _d(w), val(_a(w))
            if src_val is not None:
                src_val += _simm(w)
        elif o == OP_X and ((w >> 1) & XO_MR_MASK) == XO_OR and _d(w) == _b(w):
            dst, src_val = _a(w), val(_d(w))
        if is_call:
            fresh.clear()
        for r in (VOLATILE_GPRS if is_call else defs_at(i, w)):
            cur.pop(r, None)
            fresh.discard(r)
        if dst is not None:
            fresh.add(dst)
            if src_val is None:
                cur.pop(dst, None)
            else:
                cur[dst] = src_val
    return coords, True


# --------------------------------------------------------------------------- #
# Comparison
# --------------------------------------------------------------------------- #
def classify(tgt, base):
    """Compare two coordinate multisets.

    Returns (verdict, detail) where verdict is one of
      'agree'              identical multisets
      'uniform-bias'       every difference is one constant delta -> artifact
      'substitution'       equal counts per kind, a few coordinates swapped
      'shape-differs'      counts differ -- inline depth / unmatched codegen
    """
    if tgt == base:
        return "agree", {}
    only_t = {k: tgt[k] - base.get(k, 0) for k in tgt if tgt[k] > base.get(k, 0)}
    only_b = {k: base[k] - tgt.get(k, 0) for k in base if base[k] > tgt.get(k, 0)}
    n_t = sum(only_t.values())
    n_b = sum(only_b.values())
    if n_t != n_b:
        return "shape-differs", {"target_only": n_t, "base_only": n_b}
    # per-kind counts must match too, or it is not a like-for-like swap
    kt, kb = {}, {}
    for (kind, _c), n in only_t.items():
        kt[kind] = kt.get(kind, 0) + n
    for (kind, _c), n in only_b.items():
        kb[kind] = kb.get(kind, 0) + n
    if kt != kb:
        return "shape-differs", {"target_only": n_t, "base_only": n_b,
                                 "kind_mismatch": True}
    # MULTIPLICITY SWAP, not substitution.  When every differing coordinate is
    # touched by BOTH sides and only the counts moved, the two sides reach the
    # same fields and the difference is where, not what: swapped `switch` case
    # blocks, a peeled loop iteration, a hoisted load.  `PartyModeMgr::ClearTeam`
    # is the worked example -- the image emits the team-1 block first and we
    # emit the team-2 block first, so objdiff pairs case 1 against case 2 and
    # every offset row reads +-12.  A real wrong field looks different: the
    # coordinate we use is one the image NEVER touches.
    t_all = {c for (_k, c) in tgt}
    b_all = {c for (_k, c) in base}
    if (all(c in b_all for (_k, c) in only_t)
            and all(c in t_all for (_k, c) in only_b)):
        return "multiplicity-swap", {"target_only": n_t, "base_only": n_b}
    # Uniform bias: our build chose a different `this` bias than the image.  That
    # moves EVERY coordinate by ONE delta, so the test is deliberately strict --
    # every coordinate on both sides must be in the differing set.  A single
    # swapped field also has "one delta", and demoting THAT to an artifact would
    # throw away exactly the bug this scanner is for (RndFlare::Load is one
    # coordinate, delta -4).
    deltas = set()
    for kind in sorted(kt):
        ts = sorted(c for (k, c) in only_t if k == kind)
        bs = sorted(c for (k, c) in only_b if k == kind)
        if len(ts) != len(bs):
            deltas.add(None)
            break
        deltas.update(b - t for t, b in zip(ts, bs))
    whole_set_moved = (len(only_t) == len(tgt) and len(only_b) == len(base))
    if (len(deltas) == 1 and None not in deltas and deltas != {0}
            and whole_set_moved and n_t >= 3):
        return "uniform-bias", {"delta": deltas.pop()}
    pairs = []
    for kind in sorted(kt):
        ts = sorted(c for (k, c) in only_t if k == kind)
        bs = sorted(c for (k, c) in only_b if k == kind)
        for t, b in zip(ts, bs):
            pairs.append({"kind": kind, "target": t, "ours": b, "delta": b - t})
    return "substitution", {"pairs": pairs, "n": n_t}


# --------------------------------------------------------------------------- #
# Symbol classification and field annotation
# --------------------------------------------------------------------------- #
# MSVC function access/storage char, the one right after the final `@@`.
# Non-static member functions (the ones with a `this`) are everything except the
# static forms C,D (private), K,L (protected), S,T (public) and the free-function
# forms Y,Z.
NONSTATIC_MEMBER = set("ABEFGHIJMNOPQRUVWX")
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
SPECIAL_RE = re.compile(r"^\?\?(?:[0-9]|_[A-Za-z0-9])(.*)$")


def classify_symbol(name):
    """(kind, class) for a mangled symbol.

    kind is one of 'member' (a non-static member function: r3 is `this`),
    'free-or-static' (no `this`), 'thunk' (adjusts `this` before dispatch),
    'template-or-complex' (a mangling this parser will not guess at) or
    'unparsable'.

    ⚠ The access/storage character is the one after the FIRST `@@`, not the
    last.  `rfind` is wrong here and wrong in a way that hides functions rather
    than mis-reads them: `?Load@RndFlare@@UAAXAAVBinStream@@@Z` ends with a
    PARAMETER type that carries its own `@@`, so rfind lands on `Z` and the
    symbol is silently skipped.  (`access_specifier_scan.py` uses rfind for its
    own key and drops every member function whose parameter list names a class.)
    """
    if not name.startswith("?"):
        return "unparsable", None
    if "$" in name:                 # `$4...` adjustor thunks, `?$` templates
        return ("thunk" if "@$" in name else "template-or-complex"), None
    toks = name.split("@")
    try:
        first_empty = toks.index("")
    except ValueError:
        return "unparsable", None
    if first_empty + 1 >= len(toks) or not toks[first_empty + 1]:
        return "unparsable", None
    access = toks[first_empty + 1][0]
    quals = toks[1:first_empty]
    if any(not IDENT_RE.match(q) for q in quals):
        return "template-or-complex", None
    if access not in NONSTATIC_MEMBER:
        return "free-or-static", None
    if quals:
        cls = quals[0]
    else:
        m = SPECIAL_RE.match(toks[0])       # ??0Class / ??1Class / ??_GClass
        if not m or not IDENT_RE.match(m.group(1)):
            return "template-or-complex", None
        cls = m.group(1)
    return "member", cls


def triage(pairs, norm):
    """'finding' | 'contradicted-by-100pct' | 'multi-delta-block-swap'.

    Two cross-checks that cost nothing and each killed a plausible-looking row:

    `contradicted-by-100pct` -- `report.json`'s `match_percent_normalized` is an
    exact score-weighted f32 that DOES charge an immediate/displacement diff
    (docs/decomp/patterns/rounded-100-hides-real-bugs.md); it forgives only
    register permutation and relocation names.  So a function scoring exactly
    100.0 there CANNOT have a displacement difference, and a coordinate row on
    it is my bug, not the decomp's.  `HamNavList::NumItems` is that row: a pure
    r30<->r31 permutation.  Using an independent instrument to contradict my own
    is cheaper than trusting either alone.

    `multi-delta-block-swap` -- more than one distinct delta among the swapped
    pairs is the signature of two `switch`/`if` BLOCKS emitted in opposite
    order: objdiff pairs block A against block B and every offset in them reads
    as shifted by that block's own constant.  `PartyModeMgr::FinalizeTeam`
    shows deltas -12 (the team vectors) and -20 (the pickers) at once, and its
    objdiff rows are perfectly symmetric -- `addi r11,r30,0x68`/`0x74` at one
    index and `0x74`/`0x68` at another.  A real wrong field has ONE delta.
    """
    if norm is not None and norm >= 100.0:
        return "contradicted-by-100pct"
    if len({p["delta"] for p in pairs}) > 1:
        return "multi-delta-block-swap"
    return "finding"


ACCESS_WORDS = ("public: ", "protected: ", "private: ")


def member_from_demangled(dm):
    """(kind, class) from objdiff's demangled name, or None if it says nothing.

    Used ONLY to rescue the `template-or-complex` bucket: `?$`-qualified names
    are 8,872 of this corpus and a hand-rolled mangled-name parser has no
    business guessing at a template argument list.  The demangler already did
    it.  `[thunk]:` and ` static ` are the two forms that must NOT be read as
    this-bearing.
    """
    if not dm or dm.startswith("[thunk]"):
        return None
    if not dm.startswith(ACCESS_WORDS):
        return None                      # free function / no access specifier
    head = dm.split("__cdecl ", 1)
    if len(head) != 2:
        return None
    if " static " in head[0] or head[0].split(":", 1)[1].strip().startswith("static"):
        return None                      # static member: r3 is not `this`
    sig = head[1]
    # qualified name = up to the first '(' outside <> template brackets
    depth = 0
    end = len(sig)
    for i, ch in enumerate(sig):
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif ch == "(" and depth == 0:
            end = i
            break
    qual = sig[:end]
    # strip template arguments before taking the last '::'
    out, depth = [], 0
    for ch in qual:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        elif depth == 0:
            out.append(ch)
    flat = "".join(out)
    if "::" not in flat:
        return None
    cls = flat.rsplit("::", 1)[0]
    return "member", (cls.rsplit("::", 1)[-1] if "::" in cls else cls)


def infer_anchor(coords, member_offsets):
    """Best `this` bias: the anchor A maximising |{A + coord} ∩ members|.

    Returns (anchor, hits, total) or (None, 0, total) when no anchor fits.
    `RndFlare::Load` fits A=0x188; `RndFlare::CalcScale` fits A=0.
    """
    total = len(coords)
    if not member_offsets or not coords:
        return None, 0, total
    cands = {}
    for (_kind, c) in coords:
        for m in member_offsets:
            a = m - c
            if 0 <= a <= 0x10000:
                cands[a] = cands.get(a, 0) + 1
    if not cands:
        return None, 0, total
    # Prefer the UNBIASED anchor whenever it fits as well as any other.  A
    # bias-fitting anchor that merely ties is a coincidence, and a confident
    # wrong field name is worse than none: `PartyModeMgr::ClearTeam`'s
    # coordinates 0x68/0x74 (mTeam1Players / mTeam2Players, anchor 0) also fit
    # 6/6 at anchor 0x20, where they read as
    # `mRightTeamPrevScore` / `mRightTeamStarBonus` -- a completely different and
    # entirely fictional story, in the exact shape of a true positive.
    best_n = max(cands.values())
    best = 0 if cands.get(0, -1) == best_n else min(a for a, n in sorted(cands.items())
                                                    if n == best_n)
    if best_n < 2 or best_n < total:
        # Not every coordinate lands on a declared member: the anchor is a
        # guess, so say so by refusing it.
        return None, best_n, total
    return best, best_n, total


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
def load_struct_db(path):
    """class -> {offset: (member, type)}.  Empty dict when unavailable."""
    if not path or not os.path.exists(path):
        return {}
    import sqlite3
    out = {}
    # No `except sqlite3.Error: return {}` here on purpose.  A corrupt or
    # schema-changed struct_db would then read exactly like "this class has no
    # members", i.e. an instrument failure rendered as a clean result -- the
    # honesty_lint W1 shape.  A missing FILE is a declared, checked condition
    # above; a broken one is a crash.
    con = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        for cname, off, mname, mtype in con.execute(
                "SELECT c.name, m.offset, m.name, m.type_str FROM members m "
                "JOIN classes c ON m.class_id = c.id ORDER BY c.name, m.offset"):
            if off is None:
                continue
            out.setdefault(cname, {})[int(off)] = (mname, mtype)
    finally:
        con.close()
    return out


def load_report(report_path):
    """((unit,symbol) -> normalized%, (unit,symbol) -> demangled name)."""
    if not os.path.exists(report_path):
        return {}, {}
    with open(report_path) as f:
        rep = json.load(f)
    norms, dm = {}, {}
    for u in rep.get("units", []):
        un = u.get("name", "")
        for fn in u.get("functions", []):
            key = (un, fn.get("name", ""))
            norms[key] = fn.get("match_percent_normalized")
            d = (fn.get("metadata") or {}).get("demangled_name")
            if d:
                dm[key] = d
    return norms, dm


def scan(args, cov):
    with open(args.objdiff) as f:
        units = json.load(f)["units"]
    structs = load_struct_db(args.struct_db)
    norms, demangled = load_report(args.report)

    pairs = []
    n_units_unpaired = 0
    for u in sorted(units, key=lambda x: x.get("name", "")):
        tp, bp = u.get("target_path"), u.get("base_path")
        if not (tp and bp):
            n_units_unpaired += 1
            continue
        tp = tp if os.path.isabs(tp) else os.path.join(REPO, tp)
        bp = bp if os.path.isabs(bp) else os.path.join(REPO, bp)
        if not (os.path.exists(tp) and os.path.exists(bp)):
            n_units_unpaired += 1
            continue
        pairs.append((u.get("name", ""), tp, bp))

    findings = []
    contradicted = []
    n_rescued = 0
    buckets = {"agree-byte-identical": 0, "agree": 0, "uniform-bias": 0,
               "substitution": 0, "shape-differs": 0, "multiplicity-swap": 0,
               "contradicted-by-100pct": 0, "multi-delta-block-swap": 0}
    shape_rows = []
    universe = 0
    rows = []
    for unit, tp, bp in pairs:
        tb = function_bodies(tp)
        bb = function_bodies(bp)
        for name in sorted(set(tb) & set(bb)):
            universe += 1
            rows.append((unit, name, tb[name], bb[name]))
    rows.sort(key=lambda r: (r[0], r[1]))
    cov.universe(universe, "function bodies defined in BOTH the target object "
                           "and ours, across paired units")

    for unit, name, (tw, tcalls), (bw, bcalls) in rows:
        kind, cls = classify_symbol(name)
        if kind == "template-or-complex":
            rescue = member_from_demangled(demangled.get((unit, name)))
            if rescue:
                kind, cls = rescue
                n_rescued += 1
        if kind != "member":
            cov.drop({
                "free-or-static": "not-a-member-function",
                "thunk": "adjustor-thunk",
                "template-or-complex": "template-or-complex-mangling",
                "unparsable": "unparsable-mangled-name",
            }[kind], note={
                "free-or-static": "free or static function: r3 is not `this`",
                "thunk": "adjusts `this` before dispatch, so its coordinates are "
                         "in a different frame of reference",
                "template-or-complex": "this parser will not guess a template's "
                                       "qualifier list -- counted, not examined",
                "unparsable": "not an MSVC mangled name (C symbol, dtk label)",
            }[kind])
            continue
        if tw == bw:
            cov.examine()
            buckets["agree-byte-identical"] += 1
            continue
        tc, _tok = analyse(tw, tcalls)
        bc, _bok = analyse(bw, bcalls)
        if not tc and not bc:
            cov.drop("no-this-relative-access-proven",
                     note="no register provably held `this + K` (poisoned by a "
                          "load/arith def, or the function touches no field)")
            continue
        cov.examine()
        verdict, detail = classify(tc, bc)
        buckets[verdict] += 1
        if verdict in ("agree", "uniform-bias", "multiplicity-swap"):
            continue
        if verdict == "shape-differs":
            shape_rows.append({"unit": unit, "symbol": name, **detail})
            continue
        # substitution -> a candidate, unless an independent check contradicts it.
        verdict2 = triage(detail["pairs"], norms.get((unit, name)))
        if verdict2 != "finding":
            buckets[verdict2] += 1
            contradicted.append({"unit": unit, "symbol": name, "why": verdict2,
                                 "match_percent_normalized": norms.get((unit, name)),
                                 "pairs": detail["pairs"]})
            continue
        members = structs.get(cls, {})
        anchor, hits, total = infer_anchor(tc, set(members))
        pr = []
        for p in detail["pairs"]:
            row = dict(p)
            if anchor is not None:
                row["target_field"] = members.get(anchor + p["target"], ("?", "?"))[0]
                row["our_field"] = members.get(anchor + p["ours"], ("?", "?"))[0]
                row["target_off"] = anchor + p["target"]
                row["our_off"] = anchor + p["ours"]
            pr.append(row)
        findings.append({
            "unit": unit, "symbol": name, "class": cls,
            "match_percent_normalized": norms.get((unit, name)),
            "anchor": anchor, "anchor_fit": f"{hits}/{total}",
            "n_swapped": detail["n"], "pairs": pr,
        })
    cov.extra("template_symbols_rescued_by_demangler", n_rescued)
    cov.note(f"{n_rescued} `?$`-mangled symbols were classified from objdiff's "
             f"demangled name rather than dropped as template-or-complex")
    cov.extra("paired_units", len(pairs))
    cov.extra("units_without_both_objects", n_units_unpaired)
    cov.extra("buckets", buckets)
    cov.note(f"{len(pairs)} unit object pairs on disk of {len(units)} declared "
             f"units -- a wrong field in a TU that does not build yet is NOT visible")
    cov.note("agree-byte-identical is a PROOF of absence for this class, not a "
             "blind spot: identical words have identical displacements")
    cov.note("shape-differs is COUNTED, NOT REPORTED by default: it is dominated "
             "by inline-depth and unmatched-codegen differences (--show-shape-differs)")
    if not structs:
        cov.note("struct_db.sqlite unavailable -- field NAMES are absent; the "
                 "coordinate findings are unaffected")
    return findings, buckets, shape_rows, contradicted


# --------------------------------------------------------------------------- #
# Selftest
# --------------------------------------------------------------------------- #
def _asm(*words):
    return list(words)


def _mr(dst, src):
    return (31 << 26) | (src << 21) | (dst << 16) | (src << 11) | (444 << 1)


def _addi(dst, a, imm):
    return (14 << 26) | (dst << 21) | (a << 16) | (imm & 0xFFFF)


def _lwz(dst, a, imm):
    return (32 << 26) | (dst << 21) | (a << 16) | (imm & 0xFFFF)


def _stw(src, a, imm):
    return (36 << 26) | (src << 21) | (a << 16) | (imm & 0xFFFF)


def _bl():
    return (18 << 26) | 1


def _lwzx(dst, a, b):
    return (31 << 26) | (dst << 21) | (a << 16) | (b << 11) | (23 << 1)


def selftest():
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    # -- base-register proof ------------------------------------------------ #
    body = _asm(_mr(31, 3), _lwz(4, 31, 0x10), _stw(4, 1, 0x50), _lwz(5, 31, 0x14))
    coords, _ = analyse(body)
    check("mr r31,r3 makes r31 a this-register",
          coords == {("w", 0x10): 1, ("w", 0x14): 1})
    check("a pure (r1) stack slot is NOT reported as a field "
          "(the FxSendChorus false positive)",
          all(c != 0x50 for (_k, c) in coords))

    # a frame pointer built from r1 must never qualify
    body = _asm(_addi(31, 1, -0x470), _lwz(4, 31, 0x10))
    coords, _ = analyse(body)
    check("subi r31,r1,N (frame pointer) yields NO this-coordinates", coords == {})

    # biased this: RndFlare::Load's shape
    # the RndFlare::Load fingerprint: a field ADDRESS handed to a callee.
    body = _asm(_mr(31, 3), _addi(4, 31, -0x5c), _addi(3, 31, -0x188), _bl())
    coords, _ = analyse(body)
    check("an address live in an argument register at a call is a coordinate "
          "(the RndFlare::Load `subi r4,r31,0x5c; bl ReadEndian` shape)",
          coords == {("arg", -0x5c): 1, ("arg", -0x188): 1})

    check("an address that is neither dereferenced nor passed is NOT a "
          "coordinate (an intermediate anchor)",
          analyse(_asm(_mr(31, 3), _addi(4, 31, -0x5c)))[0] == {})

    # Flow::PreSave's shape: the image anchors at this-0x180 and adds 0x68 to
    # reach this-0x118; our build subtracts 0x118 directly.  Same address.
    c2, _ = analyse(_asm(_addi(11, 3, -0x180), _addi(4, 11, 0x68), _bl()))
    c1, _ = analyse(_asm(_addi(4, 3, -0x118), _bl()))
    check("a two-step and a one-step computation of the SAME address agree "
          "(the Flow::PreSave artifact)",
          c2 == c1 and ("arg", -0x118) in c1)

    # poisoning
    body = _asm(_mr(31, 3), _lwzx(31, 31, 4), _lwz(5, 31, 0x10))
    coords, _ = analyse(body)
    check("a register redefined by a load stops being `this` from there on",
          coords == {})
    # ...and the documented limit of a linear scan, asserted rather than hoped:
    # a use BEFORE the redefinition is still counted (correctly), and a loop
    # that reassigns the register reads as its fall-through value.
    body = _asm(_mr(31, 3), _lwz(5, 31, 0x10), _lwzx(31, 31, 4), _lwz(6, 31, 0x14))
    coords, _ = analyse(body)
    check("a use before that redefinition is still a field access",
          coords == {("w", 0x10): 1})

    body = _asm(_lwz(4, 3, 0x8), _bl(), _lwz(5, 3, 0x8))
    coords, _ = analyse(body)
    check("r3 is `this` before the first bl and not after (the second load is "
          "through a return value, not a field)",
          coords.get(("w", 0x8)) == 1)

    # -- comparator --------------------------------------------------------- #
    same = {("w", 0x10): 1, ("w", 0x14): 2}
    check("identical multisets -> agree", classify(same, dict(same))[0] == "agree")

    tgt = {("w", 0x12c): 1, ("w", 0x10): 3}
    ours = {("w", 0x128): 1, ("w", 0x10): 3}
    v, d = classify(tgt, ours)
    check("one swapped coordinate of the same kind -> substitution",
          v == "substitution" and d["n"] == 1
          and d["pairs"][0] == {"kind": "w", "target": 0x12c, "ours": 0x128,
                                "delta": -4})
    # NEGATIVE CONTROL: fixing our side must clear the finding.
    check("negative control: correcting our coordinate clears the finding",
          classify(tgt, dict(tgt))[0] == "agree")

    v, _ = classify({("w", 0x10): 1, ("w", 0x20): 1, ("w", 0x30): 1},
                    {("w", 0x18): 1, ("w", 0x28): 1, ("w", 0x38): 1})
    check("a uniform shift of EVERY coordinate -> uniform-bias, NOT a finding",
          v == "uniform-bias")
    v, _ = classify({("w", 0x10): 1, ("w", 0x20): 1, ("w", 0x30): 1},
                    {("w", 0x10): 1, ("w", 0x20): 1, ("w", 0x34): 1})
    check("ONE field moved while the rest hold still is a substitution, not a "
          "bias (the RndFlare shape has exactly one delta too)",
          v == "substitution")

    v, _ = classify({("w", 0x10): 1, ("w", 0x20): 1}, {("w", 0x10): 1})
    check("unequal counts -> shape-differs, NOT a finding", v == "shape-differs")

    v, _ = classify({("w", 0x10): 1}, {("fs", 0x10): 1})
    check("a swap of DIFFERENT kinds -> shape-differs, not a silent substitution",
          v == "shape-differs")

    # PartyModeMgr::ClearTeam: both sides touch BOTH vectors, the counts moved.
    v, _ = classify({("w", 0x68): 1, ("w", 0x74): 2},
                    {("w", 0x68): 2, ("w", 0x74): 1})
    check("counts redistributed between coordinates BOTH sides touch -> "
          "multiplicity-swap, NOT a finding (the ClearTeam case-block swap)",
          v == "multiplicity-swap")
    # ...but a coordinate the image never touches is still a finding.
    v, _ = classify({("w", 0x68): 1, ("w", 0x74): 2},
                    {("w", 0x68): 1, ("w", 0x74): 1, ("w", 0x64): 1})
    check("a coordinate the image NEVER touches is still a substitution",
          v == "substitution")

    # freshness: a stale argument register must not be counted as an argument.
    stale = _asm(_mr(31, 3), _addi(4, 31, 0x70), _mr(3, 31), _bl(), _mr(3, 31), _bl())
    coords, _ = analyse(stale)
    check("a this-relative value left in r4 by an earlier call is NOT counted "
          "as an argument of the next one (the NumItems phantom)",
          coords.get(("arg", 0x70)) == 1)

    # -- triage against the independent instrument --------------------------- #
    one = [{"kind": "w", "target": 0x12c, "ours": 0x128, "delta": -4}]
    two = one + [{"kind": "w", "target": 0x1e4, "ours": 0x1d0, "delta": -20}]
    check("a row on a function report.json scores exactly 100.0 is MY artifact, "
          "not a finding (NumItems)",
          triage(one, 100.0) == "contradicted-by-100pct")
    check("the same row below 100.0 is a finding",
          triage(one, 98.6114) == "finding")
    check("two distinct deltas -> swapped blocks, not a finding (FinalizeTeam)",
          triage(two, 99.9506) == "multi-delta-block-swap")
    check("a missing normalized score does not silently suppress a row",
          triage(one, None) == "finding")

    # -- symbol classification ---------------------------------------------- #
    check("public virtual member is a this-bearing function -- and the access "
          "char is read after the FIRST @@, not the last (the parameter type "
          "carries its own)",
          classify_symbol("?Load@RndFlare@@UAAXAAVBinStream@@@Z")
          == ("member", "RndFlare"))
    check("regression pin: rfind() would land on 'Z' here and skip the symbol",
          "?Load@RndFlare@@UAAXAAVBinStream@@@Z"[
              "?Load@RndFlare@@UAAXAAVBinStream@@@Z".rfind("@@") + 2]
          not in NONSTATIC_MEMBER)
    check("a free function is excluded",
          classify_symbol("?PathName@@YAPBDPBVObject@Hmx@@@Z")[0] == "free-or-static")
    check("a public STATIC member is excluded (no `this`)",
          classify_symbol("?Init@RndFlare@@SAXXZ")[0] == "free-or-static")
    check("an adjustor thunk is excluded (it adjusts `this`)",
          classify_symbol(
              "?Load@RndFlare@@$4PPPPPPPM@A@AAXAAVBinStream@@@Z")[0] == "thunk")
    check("a constructor's class comes from the special-name token",
          classify_symbol("??0RndFlare@@IAA@XZ") == ("member", "RndFlare"))
    check("a nested-namespace member resolves to its innermost qualifier",
          classify_symbol("?Load@Object@Hmx@@UAAXAAVBinStream@@@Z")
          == ("member", "Object"))
    check("a template instantiation is counted, not guessed at",
          classify_symbol(
              "?Load@?$ObjRefConcrete@VRndMat@@VObjectDir@@@@QAA_NAAVBinStream@@"
              "_NPAVObjectDir@@@Z")[0] == "template-or-complex")

    # -- anchor inference --------------------------------------------------- #
    members = {0x100, 0x104, 0x10c, 0x120, 0x128, 0x12c, 0x134}
    coords = {("addr", -0x88): 1, ("addr", -0x7c): 1, ("addr", -0x5c): 1,
              ("addr", -0x60): 1}
    a, hits, total = infer_anchor(coords, members)
    check("anchor inferred from a member-offset fit (RndFlare::Load = 0x188)",
          a == 0x188 and hits == 4 and total == 4)
    a, _h, _t = infer_anchor({("w", 0x999): 1}, members)
    check("no fit -> anchor None, never a guessed field name", a is None)

    # -- live corpus -------------------------------------------------------- #
    tp = os.path.join(REPO, "build", "373307D9", "obj", "system", "rndobj", "Flare.obj")
    bp = os.path.join(REPO, "build", "373307D9", "src", "system", "rndobj", "Flare.obj")
    if os.path.exists(tp) and os.path.exists(bp):
        tb = function_bodies(tp)
        name = "?Load@RndFlare@@UAAXAAVBinStream@@@Z"
        coords, _ = analyse(*tb[name])
        check("live: RndFlare::Load yields this-coordinates at all", len(coords) > 4)
        blind = analyse(tb[name][0], {})[0]     # pretend the save helper is a call
        check("live: the save-helper call does NOT kill the `this` proof -- "
              "treating `bl __savegprlr_25` as a clobber loses the field "
              "coordinates entirely",
              ("arg", -0x5c) in coords and ("arg", -0x5c) not in blind)
        check("live: the documented bug's coordinate (-0x5c = mSteps) is extracted",
              ("arg", -0x5c) in coords)
        check("live: the stack slots (0x50/0x54/0x58/0x60/0x64/0x68 off r1) are "
              "NOT among them",
              not any(c in (0x50, 0x54, 0x58, 0x64) for (_k, c) in coords))
        bcoords, _ = analyse(*function_bodies(bp)[name])
        check("live: ours and the target agree on RndFlare::Load today "
              "(the bug is FIXED -- so this is a negative, not a positive)",
              classify(coords, bcoords)[0] == "agree")
    else:
        print("  SKIP  live-corpus checks (objects absent)")
        print("        This is NOT a pass: the comparator was exercised, the "
              "extractor was not.")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


def explain(args):
    """Print both sides' coordinate multisets for one symbol.  No verdict."""
    with open(args.objdiff) as f:
        units = json.load(f)["units"]
    for u in sorted(units, key=lambda x: x.get("name", "")):
        tp, bp = u.get("target_path"), u.get("base_path")
        if not (tp and bp):
            continue
        tp = tp if os.path.isabs(tp) else os.path.join(REPO, tp)
        bp = bp if os.path.isabs(bp) else os.path.join(REPO, bp)
        if not (os.path.exists(tp) and os.path.exists(bp)):
            continue
        tb, bb = function_bodies(tp), function_bodies(bp)
        if args.explain not in tb or args.explain not in bb:
            continue
        print(f"unit {u['name']}   symbol {args.explain}")
        print(f"  classify_symbol -> {classify_symbol(args.explain)}")
        tc, _ = analyse(*tb[args.explain])
        bc, _ = analyse(*bb[args.explain])
        print(f"  target words {len(tb[args.explain][0])}   "
              f"ours {len(bb[args.explain][0])}")
        keys = sorted(set(tc) | set(bc), key=lambda k: (k[1], k[0]))
        print(f"  {'kind':5s} {'coord':>10s}  {'target':>6s} {'ours':>6s}")
        for k in keys:
            t, b = tc.get(k, 0), bc.get(k, 0)
            flag = "   <-- differs" if t != b else ""
            print(f"  {k[0]:5s} {k[1]:#10x}  {t:6d} {b:6d}{flag}")
        print(f"  verdict: {classify(tc, bc)}")
        return 0
    print(f"symbol not found in any paired unit: {args.explain}")
    return EXIT_NO_INPUT


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--objdiff", default=DEFAULT_OBJDIFF)
    ap.add_argument("--report", default=DEFAULT_REPORT)
    ap.add_argument("--struct-db", default=_default_struct_db(),
                    help="field-name annotation source; findings do not depend on it")
    ap.add_argument("--json", default=None)
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT only; the counts above it are complete")
    ap.add_argument("--show-shape-differs", action="store_true",
                    help="also list the insert/delete bucket (not findings)")
    ap.add_argument("--fail-on", type=int, default=0)
    ap.add_argument("--explain", default=None, metavar="SYMBOL",
                    help="dump BOTH sides' this-coordinates for one symbol, with "
                         "the instruction that produced each. This is the "
                         "adjudication surface: a row is a lead until you have "
                         "read it.")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    if args.explain:
        return explain(args)

    if not os.path.exists(args.objdiff):
        print(f"INCONCLUSIVE: objdiff.json not found: {args.objdiff}")
        return EXIT_NO_INPUT

    cov = CoverageReport("this_offset_scan", args=args)
    cov.require_examined("no function pair was comparable")
    findings, buckets, shape_rows, contradicted = scan(args, cov)
    cov.extra("findings", len(findings))

    print(f"WRONG-FIELD CANDIDATES (this-relative coordinate substitutions): "
          f"{len(findings)}")
    print(f"  agree, byte-identical : {buckets['agree-byte-identical']}"
          f"   (proof of absence for this class)")
    print(f"  agree, after decode   : {buckets['agree']}")
    print(f"  uniform bias delta    : {buckets['uniform-bias']}   (artifact)")
    print(f"  shape differs         : {buckets['shape-differs']}   "
          f"(inline depth / unmatched codegen -- NOT findings)")
    print(f"  multiplicity swap     : {buckets['multiplicity-swap']}   "
          f"(both sides touch both coordinates -- block order, NOT findings)")
    print(f"  contradicted at 100%  : {buckets['contradicted-by-100pct']}   "
          f"(report.json says no displacement can differ -- MY artifact)")
    print(f"  multi-delta swap      : {buckets['multi-delta-block-swap']}   "
          f"(>1 delta = swapped blocks -- NOT findings)")
    print()
    for c in contradicted:
        norm = c["match_percent_normalized"]
        print(f"  [{c['why']}] {c['symbol']}  norm="
              f"{'n/a' if norm is None else f'{norm:.4f}'}  "
              f"deltas={sorted({p['delta'] for p in c['pairs']})}")
    if contradicted:
        print()
    shown = findings if args.limit <= 0 else findings[: args.limit]
    if len(shown) < len(findings):
        print(f"  showing {len(shown)} of {len(findings)}")
    for f in shown:
        norm = f["match_percent_normalized"]
        norm_s = "n/a" if norm is None else f"{norm:.4f}"
        anch = "unresolved" if f["anchor"] is None else hex(f["anchor"])
        print(f"  {f['symbol']}")
        print(f"      unit={f['unit']}  norm={norm_s}  class={f['class']}  "
              f"anchor={anch} (fit {f['anchor_fit']})")
        for p in f["pairs"]:
            if "target_field" in p:
                print(f"      {p['kind']:4s} target {p['target_off']:#06x} "
                      f"{p['target_field']!r}  vs  ours {p['our_off']:#06x} "
                      f"{p['our_field']!r}   (delta {p['delta']:+d})")
            else:
                print(f"      {p['kind']:4s} target this{p['target']:+#x}  vs  "
                      f"ours this{p['ours']:+#x}   (delta {p['delta']:+d})")
    if args.show_shape_differs:
        print(f"\n  shape-differs rows ({len(shape_rows)}):")
        for r in shape_rows[: (args.limit or len(shape_rows))]:
            print(f"      {r['symbol']}  target_only={r['target_only']} "
                  f"ours_only={r['base_only']}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"findings": findings, "buckets": buckets,
                       "shape_differs": shape_rows,
                       "contradicted": contradicted,
                       "_coverage": cov.as_dict()}, fh, indent=2)

    rc = cov.emit()
    if rc:
        return rc
    if args.fail_on and len(findings) >= args.fail_on:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
