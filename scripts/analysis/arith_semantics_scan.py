#!/usr/bin/env python3
"""arith_semantics_scan.py — keep only the objdiff rows that change a computed VALUE.

WHY THIS EXISTS
===============
Taxonomy class 4 (`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`)
is 68 historical behavioural bugs — arithmetic with the wrong sign, the wrong
width, or a dropped/extra term — and had no detector.  The canonical instance is
`Rand::Seed` (`c88ef743a`): our `int j >> 16` lowered to `srawi` where the
image has `srwi`, so every RNG table word whose draw had bit 31 set came out
`0xFFFFxxxx` instead of `0x5665xxxx`.

A wrong-arithmetic bug costs match points under the FUZZY ruler: an opcode, an
immediate, an instruction count or a source register differs.  So every
instance has an objdiff row.  They go unfound because those rows are drowned in
register-permutation, scheduling and address-materialisation rows — and, worse,
the CANONICAL ruler forgives register permutation outright, so a store of the
register holding +1.0 where the image stores the one holding -1.0 reads 100.0
(`ObjectDir::ResetViewports`, `SkeletonClip::LoadFrame`, `PoseFatalities::
EndFatal`, all fixed on the branch that added this file).  This scanner is the
filter: across EVERY function objdiff can pair it discards rows that are pure
register/order/address noise and keeps rows whose difference changes a value.

WHAT IT LOOKS AT
================
One `objdiff-cli diff --batch --include-instructions` pass (sharded by unit,
run in parallel, with a watchdog: see run_shard) over EVERY function
`report.json` lists, under the project's own ruler (`objdiff.json`:
`functionRelocDiffs=name_check`).  For each aligned row it canonicalises both
sides (`subi`->`addi -imm`, every `rlwinm` alias — `slwi`/`srwi`/`clrlwi`/
`extrwi`/`rotlwi`/`clrlslwi`/... — back to (rotate, mask32)), then classifies.
Register-only rows are not thrown away: each operand is traced to its DEFINING
instruction on its own side (a linear walk), and the two definitions are
compared through objdiff's alignment.

REPORTED buckets (a value claim — adjudicate every row)
  signedness      srawi<->srwi, sraw<->srw, extsb/extsh<->clrlwi 24/16,
                  lha<->lhz, lwa<->lwz, divw<->divwu, mulhw<->mulhwu,
                  extsw<->clrldi 32, fctiwz<->fctidz
  int-width       lbz/lhz/lwz/ld (and store) width change on an aligned row
  float-width     lfs<->lfd, stfs<->stfd, fadds<->fadd (single vs double math)
  float-sign      fadds<->fsubs, any two of fmadds/fmsubs/fnmadds/fnmsubs,
                  fneg/fabs/fnabs/fmr substitutions
  int-op          add<->subf, and<->andc, or<->orc, xor<->eqv, a sign-flipped
                  addi immediate
  operand-order   a NON-commutative op (subf, fsubs, fdivs, divw, slw/srw/sraw,
                  the addend of a fused multiply-add) whose two source operands
                  are SWAPPED -- decided by where each operand was defined
  operand-source  a register-only row whose consumed value comes from a
                  different, non-equivalent definition, left over after
                  multiset cancellation of exchanged values (two independent
                  x/y computations aligned crosswise cancel)
  const-operand   an immediate that feeds VALUE arithmetic differs:
                  rlwinm shift/mask, srawi amount, mulli, andi./ori/xori/oris,
                  subfic, and an `addi` whose result is not used as an address
  op-substitution two different value-producing ops on one aligned row
  cond-mask       a BRANCH-FREE CONDITION differs: carry-chain idioms
                  (subfic/subic/subfe/addze/neg/cntlzw ...).  det-cond's
                  detector sees branches only; this bucket is its blind spot.
                  `subfe` masks and `cntlzw/extrwi` zero tests are evaluated to
                  a truth predicate on both sides -- equal truth is counted as
                  cond-mask-equivalent, never reported
  net-term        after cancelling every instruction that merely MOVED (it is
                  one-sided in both directions), exactly one arithmetic atom
                  (fmul, fadd, fneg, iadd, imul, shift, ext, ...) is present
                  on one side more often than the other.  The "dropped term"

COUNTED, NOT REPORTED (each is another lane's class or a measured equivalence)
  register-only        pure register permutation
  operand-exchanged    operand-source rows whose values cancel as a multiset
  displacement         load/store displacement -- a FIELD offset (this_offset_scan)
  stack-displacement   displacement or `addi` off r1 or an r1-derived frame pointer
  compare              cmpw/cmplw/cmpwi/... -- owned by det-cond
  branch               branch opcode/target differences -- det-cond
  relocation           symbol operand differences -- the wrong-callee tools
  addi-address         `addi` immediate whose result is used as a base/argument
  li-constant          `li`/`lis` immediate: a wrong CONSTANT (printed with
                       --show-li; dominated by call arguments)
  li-misaligned        two `li` rows objdiff paired across different registers
  bit-test-encoding    `extrwi. 1,b` vs `rlwinm. 0,b,b`: one source bit, two
                       spellings
  flag-bit-renumbering single-bit test/set on a static guard (`?$S9@`) or an
                       `li`-seeded conditional-destructor flag word: the bit
                       NUMBER is a per-function counter
  const-reordered      immediates that exchanged rows (every value has a partner)
  sign-folded-literal  fused-op sign flip that moved into a literal
                       (`x*(-2) + y` vs `y - x*2`): equal values
  cond-mask-equivalent two spellings of one branch-free truth value
  disjoint-or-add      add<->or / add<->xor: EXACT when the operand bits are
                       disjoint (the Rand::Seed fix itself is `+` vs `or`)
  insert-fusion        rlwimi against rlwinm/or -- disjoint-field fusion
  strength-reduction   mullw/mulli against a shift
  reciprocal-fold      an fdiv imbalance (/fp:fast: a/x,b/x -> 1/x + muls)
  fsel-lowering        fsel imbalance: Min/Max/Clamp lowering
  multi-atom           more than one atom kind out of balance: inline depth,
                       tail duplication or cross-jumping -- a matching work list
  other-opcode         any other substitution

WHAT IT CANNOT SEE (read before calling class 4 exhausted)
----------------------------------------------------------
* A function we do not define has no base side -- `not-defined-in-our-build`,
  16,072 on the day this was written.  Arithmetic arrives with the body.
* A DROPPED STORE is not an arithmetic atom (DxRnd::SavePreBuffer's missing
  w = 0 store, class 10).  Neither is a wrong field feeding the op (class 1).
* Definitions are resolved by a LINEAR backward walk: across a join the
  walk can pick the wrong reaching definition, which is the dominant source
  of operand-source false positives in branchy functions (measured:
  SaveLoadManager::SetState, whose MemFree file-name register was resolved
  to a different arm's definition).  It is symmetric, so it invents noise,
  not systematic bias -- but the precision of operand-source falls with the
  function's mismatch ratio.  The printout is sorted so the clean shapes
  come first.
* objdiff's ALIGNMENT decides which rows are aligned; a substituted opcode it
  paired with an unrelated instruction reaches the net-term pool instead.
* A bucket hit is a VALUE claim, never a behaviour claim.  `add` vs `or`, a
  sign-extension of a never-negative value, a 0/-1 vs 0/1 truth mask ANDed
  into a bool, and a reassociated float sum are all equal at runtime.

USAGE
-----
    python3 scripts/analysis/arith_semantics_scan.py --selftest
    python3 scripts/analysis/arith_semantics_scan.py
    python3 scripts/analysis/arith_semantics_scan.py --json out.json --show-li
    python3 scripts/analysis/arith_semantics_scan.py --only '?Seed@Rand@@QAAXH@Z'
    python3 scripts/analysis/arith_semantics_scan.py --save-rows rows.jsonl
    python3 scripts/analysis/arith_semantics_scan.py --replay rows.jsonl

Exit code is `CoverageReport.emit()`'s (0 clean, 3 truncated, 4 unaccounted,
5 no input, 6 no denominator).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from collections import Counter, defaultdict
from concurrent.futures import ProcessPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_REPORT = os.path.join(REPO, "build", "373307D9", "report.json")
DEFAULT_OBJDIFF = os.path.join(REPO, "bin", "objdiff-cli")

REPORTED = ("signedness", "int-width", "float-width", "float-sign", "int-op",
            "operand-order", "operand-source", "const-operand", "op-substitution",
            "net-term", "cond-mask")
COUNTED = ("register-only", "displacement", "stack-displacement", "compare",
           "branch", "relocation", "addi-address", "li-constant", "li-misaligned",
           "bit-test-encoding", "flag-bit-renumbering", "const-reordered",
           "sign-folded-literal", "operand-exchanged", "cond-mask-equivalent",
           "swap-zero-tested", "bool-mask", "const-folded",
           "disjoint-or-add", "insert-fusion", "strength-reduction",
           "reciprocal-fold", "fsel-lowering", "multi-atom", "other-opcode")

# --------------------------------------------------------------------------- #
# Canonicalisation
# --------------------------------------------------------------------------- #
LOADS_INT = {"lbz": 1, "lhz": 2, "lha": 2, "lwz": 4, "lwa": 4, "ld": 8}
STORES_INT = {"stb": 1, "sth": 2, "stw": 4, "std": 8}
LOADS_F = {"lfs": 4, "lfd": 8}
STORES_F = {"stfs": 4, "stfd": 8}
MEM_SUFFIXES = ("", "u", "x", "ux")


def _mem_base(op):
    """('lwz', 'u') for 'lwzu', or None for a non-memory opcode."""
    for tab in (LOADS_INT, STORES_INT, LOADS_F, STORES_F):
        for base in tab:
            for suf in MEM_SUFFIXES:
                if op == base + suf:
                    return base, suf
    if op in ("lmw", "stmw", "stfiwx", "lwbrx", "stwbrx", "lhbrx", "sthbrx",
              "lvx", "stvx", "lvx128", "stvx128", "lvlx", "lvrx", "stvlx", "stvrx",
              "lvlx128", "lvrx128", "stvlx128", "stvrx128", "lvewx", "stvewx",
              "lvewx128", "stvewx128", "lvsl", "lvsr", "dcbt", "dcbz", "dcbz128",
              "dcbf", "dcbst"):
        return op, ""
    return None


def _int(v):
    if isinstance(v, int):
        return v
    s = str(v).strip()
    try:
        return int(s, 0)
    except ValueError:
        try:
            return int(s.lstrip("-"), 0) * (-1 if s.startswith("-") else 1)
        except ValueError:
            return None


def _mask32(mb, me):
    """PowerPC MASK(mb, me) over bits numbered 0 (MSB) .. 31 (LSB)."""
    def bits(a, b):
        m = 0
        for i in range(a, b + 1):
            m |= 1 << (31 - i)
        return m
    if mb <= me:
        return bits(mb, me)
    return bits(0, me) | bits(mb, 31)


RLW_ALIASES = {"slwi", "srwi", "clrlwi", "clrrwi", "extrwi", "extlwi", "rotlwi",
               "rotrwi", "clrlslwi", "rlwinm"}


def canon(side):
    """side = [opcode, args_text, [(type, value), ...]] ->
    dict(op, rec, regs, imms, syms, raw, rlw=(rot, mask) | None)."""
    op, text, targs = side
    rec = op.endswith(".") and op not in (".",)
    o = op.rstrip(".")
    regs, imms, syms, other = [], [], [], []
    for t, v in targs:
        if t == "Register":
            regs.append(str(v))
        elif t in ("Signed", "Unsigned"):
            imms.append(_int(v))
        elif t == "Other":
            iv = _int(v)
            (imms if iv is not None else other).append(iv if iv is not None else str(v))
        elif t == "Symbol":
            syms.append(str(v))
        elif t == "BranchDest":
            other.append(("bd", v))
        else:
            other.append(str(v))
    d = {"op": o, "rec": rec, "regs": regs, "imms": imms, "syms": syms,
         "other": other, "text": f"{op} {text}".strip(), "rlw": None}
    # subi / subis / subic are addi / addis / addic with a negated immediate
    if o in ("subi", "subis", "subic") and imms:
        d["op"] = {"subi": "addi", "subis": "addis", "subic": "addic"}[o]
        d["imms"] = [-imms[0]] + imms[1:]
    elif o == "sub" and len(regs) == 3:          # sub rD,rA,rB == subf rD,rB,rA
        d["op"] = "subf"
        d["regs"] = [regs[0], regs[2], regs[1]]
    elif o == "not" and len(regs) == 2:
        d["op"] = "nor"
        d["regs"] = [regs[0], regs[1], regs[1]]
    if o in RLW_ALIASES and len(imms) >= 1 and all(i is not None for i in imms):
        n = imms
        sh = mb = me = None
        if o == "rlwinm" and len(n) == 3:
            sh, mb, me = n
        elif o == "slwi":
            sh, mb, me = n[0], 0, 31 - n[0]
        elif o == "srwi":
            sh, mb, me = (32 - n[0]) % 32, n[0], 31
        elif o == "clrlwi":
            sh, mb, me = 0, n[0], 31
        elif o == "clrrwi":
            sh, mb, me = 0, 0, 31 - n[0]
        elif o == "extrwi" and len(n) == 2:
            sh, mb, me = (n[1] + n[0]) % 32, 32 - n[0], 31
        elif o == "extlwi" and len(n) == 2:
            sh, mb, me = n[1], 0, n[0] - 1
        elif o == "rotlwi":
            sh, mb, me = n[0], 0, 31
        elif o == "rotrwi":
            sh, mb, me = (32 - n[0]) % 32, 0, 31
        elif o == "clrlslwi" and len(n) == 2:
            sh, mb, me = n[1], n[0] - n[1], 31 - n[1]
        if sh is not None and 0 <= mb <= 31 and 0 <= me <= 31:
            d["op"] = "rlwinm"
            d["rlw"] = (sh % 32, _mask32(mb, me))
            d["imms"] = [sh % 32, _mask32(mb, me)]
    return d


def rlw_kind(rlw):
    """Classify a canonical rlwinm: 'srl' (logical right shift), 'sll', 'mask',
    'rot', or 'field' (shift + mask)."""
    if rlw is None:
        return None
    sh, mask = rlw
    if sh == 0:
        return "mask"
    if mask == (0xFFFFFFFF >> (32 - sh)) and sh:          # srwi (32-sh)
        return "srl"
    if mask == ((0xFFFFFFFF << sh) & 0xFFFFFFFF):          # slwi sh
        return "sll"
    if mask == 0xFFFFFFFF:
        return "rot"
    return "field"


def rlw_right_shift_amount(rlw):
    if rlw_kind(rlw) == "srl":
        return (32 - rlw[0]) % 32
    return None


def rlw_is_zext(rlw, width):
    return rlw is not None and rlw[0] == 0 and rlw[1] == (1 << width) - 1


# --------------------------------------------------------------------------- #
# Opcode families
# --------------------------------------------------------------------------- #
BRANCH_RE = re.compile(r"^(b|bl|ba|bla|bc|bcl|bclr|bcctr|blr|bctr|bctrl|blrl|"
                       r"bdnz.*|bdz.*|b(eq|ne|lt|le|gt|ge|so|ns|un|nu|dnz|dz)(lr|ctr)?(l)?[+-]?)$")
COMPARE_OPS = {"cmpw", "cmplw", "cmpwi", "cmplwi", "cmpd", "cmpld", "cmpdi",
               "cmpldi", "fcmpu", "fcmpo", "cmp", "cmpl", "cmpi", "cmpli",
               "vcmpeqfp", "vcmpgefp", "vcmpgtfp", "vcmpbfp", "vcmpequw",
               "vcmpeqfp128", "vcmpgefp128", "vcmpgtfp128", "vcmpbfp128",
               "vcmpequw128"}
TRAP_OPS = {"tw", "twi", "td", "tdi", "twllei", "twlgt", "twgti", "tweq",
            "twlgei", "twnei", "twlti", "twui", "twlge"}

# atoms: what a value-producing instruction contributes to the function's
# arithmetic.  FMA is split so contraction (fmadds vs fmuls+fadds) balances.
FLOAT_ATOMS = {
    "fadds": ("fadd",), "fadd": ("fadd",), "fsubs": ("fadd",), "fsub": ("fadd",),
    "fmuls": ("fmul",), "fmul": ("fmul",),
    "fmadds": ("fmul", "fadd"), "fmadd": ("fmul", "fadd"),
    "fmsubs": ("fmul", "fadd"), "fmsub": ("fmul", "fadd"),
    "fnmsubs": ("fmul", "fadd"), "fnmsub": ("fmul", "fadd"),
    "fnmadds": ("fmul", "fadd", "fneg"), "fnmadd": ("fmul", "fadd", "fneg"),
    "fneg": ("fneg",), "fnabs": ("fneg", "fabs"), "fabs": ("fabs",),
    "fdivs": ("fdiv",), "fdiv": ("fdiv",), "fres": ("fdiv",),
    "frsp": ("frsp",), "fsel": ("fsel",),
    "fctiwz": ("fcvt",), "fctidz": ("fcvt",), "fctiw": ("fcvt",), "fctid": ("fcvt",),
    "fcfid": ("fcfid",), "fsqrts": ("fsqrt",), "fsqrt": ("fsqrt",),
    "frsqrte": ("frsqrte",),
}
INT_ATOMS = {
    "add": ("iadd",), "subf": ("iadd",),
    # carry-chain ops: in this binary they are overwhelmingly BRANCH-FREE
    # CONDITIONS (`subfic/subfe` = -(x != 0), `subic/subfe/and`, the 0/-1
    # selects), so they get their own atom and their own bucket (cond-mask)
    "addc": ("carry",), "adde": ("carry",), "addze": ("carry",), "addme": ("carry",),
    "subfc": ("carry",), "subfe": ("carry",), "subfic": ("carry",),
    "subfze": ("carry",), "subfme": ("carry",), "addic": ("carry",),
    "neg": ("ineg",),
    "mullw": ("imul",), "mulli": ("imul",), "mulhw": ("imulh",), "mulhwu": ("imulh",),
    "mulld": ("imul",), "mulhd": ("imulh",), "mulhdu": ("imulh",),
    "divw": ("idiv",), "divwu": ("idiv",), "divd": ("idiv",), "divdu": ("idiv",),
    "and": ("iand",), "andc": ("iand",), "andi": ("iand",), "andis": ("iand",),
    "xor": ("ixor",), "xori": ("ixor",), "xoris": ("ixor",), "eqv": ("ixor",),
    "nor": ("inot",), "nand": ("inot",), "orc": ("ior",),
    "slw": ("shl",), "srw": ("shr",), "sraw": ("sar",), "srawi": ("sar",),
    "sld": ("shl",), "srd": ("shr",), "srad": ("sar",), "sradi": ("sar",),
    "extsb": ("ext",), "extsh": ("ext",), "extsw": ("ext",),
    "cntlzw": ("clz",), "cntlzd": ("clz",),
}
# Branch-free condition idioms.  det-cond (cond_semantics_scan.py) cannot see a
# condition MSVC compiled WITHOUT a branch -- it is arithmetic, so it lands
# here.  UIManager::IsGameScreenActive was this shape.  The bar is a different
# TRUTH VALUE, not different instructions: objdiff's five BOOLEAN_NEGATION rows
# were all equivalent mask spellings.
MASK_OPS = {"subfic", "subfe", "subfc", "subic", "addic", "addze", "addme",
            "subfze", "subfme", "adde", "addc", "neg", "cntlzw", "cntlzd",
            "andc", "nor"}
NONCOMM_2SRC = {"subf", "subfc", "subfe", "fsubs", "fsub", "fdivs", "fdiv",
                "divw", "divwu", "divd", "divdu", "slw", "srw", "sraw", "sld",
                "srd", "srad", "andc", "orc", "vsubfp", "vsubfp128"}
FUSED = {"fmadds", "fmadd", "fmsubs", "fmsub", "fnmadds", "fnmadd",
         "fnmsubs", "fnmsub", "vmaddfp", "vnmsubfp", "vmaddfp128", "vnmsubfp128"}


def atoms_of(c):
    o = c["op"]
    if o in FLOAT_ATOMS:
        return FLOAT_ATOMS[o]
    if o in INT_ATOMS:
        return INT_ATOMS[o]
    if o == "or" and len(c["regs"]) == 3 and c["regs"][1] != c["regs"][2]:
        return ("ior",)
    if o in ("ori", "oris") and c["imms"] and c["imms"][-1] != 0:
        return ("ior",)
    if o == "rlwinm" and c["rlw"] is not None:
        if c["rlw"] == (0, 0xFFFFFFFF):
            return ()                       # `clrrwi rD,rS,0` is a register move
        if c["rlw"][0] == 0 and c["rlw"][1] in (0xFF, 0x1):
            return ("boolmask",)            # clrlwi 24/31: MSVC's bool truncation
        k = rlw_kind(c["rlw"])
        return {"srl": ("shr",), "sll": ("shl",), "mask": ("iand",),
                "rot": ("rot",), "field": ("field",)}[k]
    if o == "rlwimi":
        return ("insert",)
    if o in ("rldicl", "rldicr", "rldic", "clrldi", "sldi", "srdi", "extldi",
             "extrdi", "rldimi"):
        return ("rld",)
    return ()


def is_plumbing(c):
    o = c["op"]
    return (o in ("mr", "li", "lis", "addi", "addis", "nop", "mflr", "mtlr",
                  "mtctr", "mfctr", "mfcr", "mtcrf", "crxor", "creqv", "cror",
                  "crnot", "crset", "crclr", "isync", "sync", "lwsync", "eieio",
                  "mftb", "mfspr", "mtspr", "fmr", "vor", "vor128", "mfmsr", "mtmsrd")
            or _mem_base(o) is not None or BRANCH_RE.match(o) is not None
            or o in COMPARE_OPS or o in TRAP_OPS)


# --------------------------------------------------------------------------- #
# Row-level substitution table
# --------------------------------------------------------------------------- #
SIGN_PAIRS = [({"lha"}, {"lhz"}), ({"lwa"}, {"lwz"}), ({"divw"}, {"divwu"}),
              ({"mulhw"}, {"mulhwu"}), ({"divd"}, {"divdu"}), ({"mulhd"}, {"mulhdu"}),
              ({"sraw"}, {"srw"}), ({"srad"}, {"srd"}), ({"fctiwz"}, {"fctidz"})]
INT_OP_PAIRS = [({"add"}, {"subf"}), ({"and"}, {"andc"}), ({"or"}, {"orc"}),
                ({"xor"}, {"eqv"}), ({"and"}, {"or"}), ({"and"}, {"xor"}),
                ({"nor"}, {"or"}), ({"nand"}, {"and"}), ({"neg"}, {"mr"}),
                ({"addc"}, {"subfc"}), ({"adde"}, {"subfe"}),
                ({"addze"}, {"subfze"}), ({"addme"}, {"subfme"})]
FLOAT_SIGN_SET = {"fmadds", "fmsubs", "fnmadds", "fnmsubs"}
FLOAT_SIGN_SET_D = {"fmadd", "fmsub", "fnmadd", "fnmsub"}
FLOAT_SIGN_PAIRS = [({"fadds"}, {"fsubs"}), ({"fadd"}, {"fsub"}),
                    ({"fneg"}, {"fabs"}), ({"fneg"}, {"fnabs"}), ({"fabs"}, {"fnabs"}),
                    ({"fneg"}, {"fmr"}), ({"fabs"}, {"fmr"}), ({"fnabs"}, {"fmr"}),
                    ({"vaddfp"}, {"vsubfp"}), ({"vaddfp128"}, {"vsubfp128"}),
                    ({"vmaddfp"}, {"vnmsubfp"}), ({"vmaddfp128"}, {"vnmsubfp128"})]
PRECISION = {"fadds": "fadd", "fsubs": "fsub", "fmuls": "fmul", "fdivs": "fdiv",
             "fmadds": "fmadd", "fmsubs": "fmsub", "fnmadds": "fnmadd",
             "fnmsubs": "fnmsub", "fsqrts": "fsqrt", "fres": "fre"}


def _pair_in(a, b, pairs):
    return any((a in x and b in y) or (a in y and b in x) for x, y in pairs)


def classify_substitution(t, b):
    """Two aligned rows whose canonical opcodes differ -> (bucket, why) or None
    when they are not an arithmetic pair at all (a mispairing)."""
    to, bo = t["op"], b["op"]
    tm, bm = _mem_base(to), _mem_base(bo)
    if tm and bm:
        if tm[1] != bm[1]:
            return "other-opcode", "addressing-form change"
        a, c = tm[0], bm[0]
        if {a, c} in ({"lha", "lhz"}, {"lwa", "lwz"}):
            return "signedness", f"{a} vs {c}: sign- vs zero-extending load"
        if a in LOADS_INT and c in LOADS_INT:
            return "int-width", f"{a}({LOADS_INT[a]}B) vs {c}({LOADS_INT[c]}B) load"
        if a in STORES_INT and c in STORES_INT:
            return "int-width", f"{a}({STORES_INT[a]}B) vs {c}({STORES_INT[c]}B) store"
        if a in LOADS_F and c in LOADS_F:
            return "float-width", f"{a} vs {c}: single vs double load"
        if a in STORES_F and c in STORES_F:
            return "float-width", f"{a} vs {c}: single vs double store"
        return "other-opcode", f"memory {a} vs {c}"
    if BRANCH_RE.match(to) or BRANCH_RE.match(bo):
        return "branch", "branch opcode"
    if to in COMPARE_OPS or bo in COMPARE_OPS:
        return "compare", "compare opcode (det-cond's lane)"
    if _pair_in(to, bo, SIGN_PAIRS):
        return "signedness", f"{to} vs {bo}"
    # srawi vs a logical right shift of the same amount
    if {to, bo} == {"srawi", "rlwinm"}:
        sr = t if to == "srawi" else b
        rl = b if to == "srawi" else t
        amt = rlw_right_shift_amount(rl["rlw"])
        if amt is not None and sr["imms"] and sr["imms"][0] == amt:
            return "signedness", f"srawi {amt} vs srwi {amt}: arithmetic vs logical shift"
        if amt is not None:
            return "signedness", (f"srawi {sr['imms'][:1]} vs srwi {amt}: arithmetic vs "
                                  f"logical shift, and the amount differs")
        return "other-opcode", "srawi vs rlwinm (not a right shift)"
    for ext, width in (("extsb", 8), ("extsh", 16)):
        if {to, bo} == {ext, "rlwinm"}:
            rl = b if to == ext else t
            if rlw_is_zext(rl["rlw"], width):
                return "signedness", f"{ext} vs clrlwi {32 - width}: sign vs zero extension"
            return "other-opcode", f"{ext} vs rlwinm {rl['rlw']}"
    if {to, bo} == {"extsw", "rldicl"} or {to, bo} == {"extsw", "clrldi"}:
        return "signedness", "extsw vs clrldi 32: sign vs zero extension to 64"
    if {to, bo} in ({"extsb", "extsh"},):
        return "int-width", "extsb vs extsh: 8- vs 16-bit sign extension"
    if to in PRECISION and PRECISION[to] == bo or bo in PRECISION and PRECISION[bo] == to:
        return "float-width", f"{to} vs {bo}: single vs double precision arithmetic"
    if (to in FLOAT_SIGN_SET and bo in FLOAT_SIGN_SET) or \
            (to in FLOAT_SIGN_SET_D and bo in FLOAT_SIGN_SET_D):
        return "float-sign", f"{to} vs {bo}: fused multiply-add sign variant"
    if _pair_in(to, bo, FLOAT_SIGN_PAIRS):
        return "float-sign", f"{to} vs {bo}"
    if {to, bo} in ({"add", "or"}, {"add", "xor"}, {"or", "xor"}):
        return "disjoint-or-add", f"{to} vs {bo}: equal when the operand bits are disjoint"
    if "rlwimi" in (to, bo) and ({to, bo} & {"rlwinm", "or", "ori", "oris", "andc", "and"}):
        return "insert-fusion", f"{to} vs {bo}: disjoint-field insert fusion"
    if {to, bo} <= {"mullw", "mulli", "rlwinm", "add"} and ("mullw" in (to, bo) or "mulli" in (to, bo)):
        return "strength-reduction", f"{to} vs {bo}: multiply vs shift/add"
    if _pair_in(to, bo, INT_OP_PAIRS):
        return "int-op", f"{to} vs {bo}"
    if {to, bo} == {"rlwinm", "srawi"}:
        return "signedness", "srawi vs rlwinm"
    if to == "rlwinm" and bo == "rlwinm":
        return None
    ta, ba = atoms_of(t), atoms_of(b)
    if ta and ba:
        return "op-substitution", (f"{to} vs {bo}: two different value-producing "
                                   f"operations on one aligned row")
    return None                                  # not an arithmetic pair


# --------------------------------------------------------------------------- #
# Per-function analysis
# --------------------------------------------------------------------------- #
GPR_ARG = {f"r{i}" for i in range(3, 11)}


def _is_call(c):
    return c["op"] in ("bl", "bctrl", "blrl", "bla")


def _mem_base_reg(c):
    """The base register of a D-form memory access, else None."""
    if _mem_base(c["op"]) is None:
        return None
    if len(c["regs"]) >= 2:
        return c["regs"][-1] if not c["op"].endswith("x") else None
    return None


def addi_use(seq, i):
    """How is the result of the addi at seq[i] consumed on its own side?
    'address' | 'value' | 'unknown'."""
    c = seq[i]
    if len(c["regs"]) < 2:
        return "unknown"
    d = c["regs"][0]
    for j in range(i + 1, min(len(seq), i + 16)):
        x = seq[j]
        if x is None:
            continue
        if _mem_base(x["op"]) is not None:
            if d in x["regs"][1:]:
                return "address"
            if x["regs"] and x["regs"][0] == d and x["op"].startswith("l"):
                return "unknown"                      # redefined by a load
            continue
        if _is_call(x):
            return "address" if d in GPR_ARG else "unknown"
        if x["op"] in ("mtctr",) and d in x["regs"]:
            return "address"
        if d in x["regs"][1:] or (x["op"] in COMPARE_OPS and d in x["regs"]):
            if x["op"] == "mr":
                return "unknown"
            if x["op"] in ("addi", "add") and len(x["regs"]) >= 2:
                return "unknown"
            return "value"
        if x["regs"] and x["regs"][0] == d:
            return "unknown"
    return "unknown"


def register_map(pairs):
    """Majority target-register -> base-register mapping over aligned rows
    with the same canonical op."""
    votes = defaultdict(Counter)
    for t, b in pairs:
        if t["op"] != b["op"] or len(t["regs"]) != len(b["regs"]):
            continue
        for x, y in zip(t["regs"], b["regs"]):
            votes[x][y] += 1
    out = {}
    for x in sorted(votes):
        best = sorted(votes[x].items(), key=lambda kv: (-kv[1], kv[0]))[0]
        out[x] = best[0]
    return out


VOLATILE = ({f"r{i}" for i in range(0, 13)} | {f"f{i}" for i in range(0, 14)}) - {"r1", "r2"}


def writes(c):
    """Registers a canonical instruction writes (conservative)."""
    o = c["op"]
    if _is_call(c):
        return VOLATILE
    mb = _mem_base(o)
    if mb is not None:
        w = set()
        if o.startswith("l") and c["regs"]:
            w.add(c["regs"][0])
        if mb[1] in ("u", "ux") and len(c["regs"]) >= 2:
            w.add(c["regs"][1] if mb[1] == "ux" else c["regs"][-1])
        return w
    if BRANCH_RE.match(o) or o in COMPARE_OPS or o in TRAP_OPS or \
            o in ("mtctr", "mtlr", "mtspr", "mtcrf", "nop", "sync", "isync"):
        return set()
    return {c["regs"][0]} if c["regs"] else set()


def last_def(seq, i, reg, horizon=256):
    """Index of the nearest instruction before seq[i] that writes `reg`, on a
    linear walk (branches ignored -- symmetric on both sides), else None."""
    for j in range(i - 1, max(-1, i - 1 - horizon), -1):
        if reg in writes(seq[j]):
            return j
    return None


def operand_swap(t, b, ctx):
    """For a same-op non-commutative row, are the two source operands SWAPPED?

    Decided by where each operand was DEFINED, not by register names: operand
    A on our side must come from the instruction objdiff aligned with the
    definition of operand B on the target side, and vice versa.  A majority
    register map was tried first and flagged `EQEffect::SetParameter`'s
    `(x-1)/(x+1)` -- same order on both sides, registers merely renamed -- as a
    swap.  A definition we cannot resolve (function entry, a call) is never
    a swap."""
    tseq, bseq, ti, bi, t2b = ctx
    o = t["op"]
    if len(t["regs"]) != len(b["regs"]):
        return False
    if o in NONCOMM_2SRC and len(t["regs"]) >= 3:
        pos = (1, 2)
    elif o in FUSED and len(t["regs"]) == 4:
        pos = None
    else:
        return False

    def D(seq, i, r):
        return last_def(seq, i, r)

    ctx5 = (tseq, bseq, ti, bi, t2b)

    def same(da, ea):
        """Do these two definitions produce the same value?  Aligned with each
        other, or value-equivalent (GlitchPoker::Dump loads the two fsubs
        operands in the opposite ORDER, so the alignment pairs each load with
        the other one -- but the values are the same)."""
        return val_eq(tseq, da, bseq, ea)

    if pos:
        ta, tb = t["regs"][1], t["regs"][2]
        ba, bb = b["regs"][1], b["regs"][2]
        if ta == tb or ba == bb:
            return False
        da, db = D(tseq, ti, ta), D(tseq, ti, tb)
        ea, eb = D(bseq, bi, ba), D(bseq, bi, bb)
        if None in (da, db, ea, eb):
            return False
        if same(da, ea) and same(db, eb):
            return False
        return same(da, eb) and same(db, ea)
    # fused: fD, fA, fC, fB = A*C +/- B.  {A,C} commute; B is the addend.
    tA, tC, tB = t["regs"][1], t["regs"][2], t["regs"][3]
    bA, bC, bB = b["regs"][1], b["regs"][2], b["regs"][3]
    dA, dC, dB = D(tseq, ti, tA), D(tseq, ti, tC), D(tseq, ti, tB)
    eA, eC, eB = D(bseq, bi, bA), D(bseq, bi, bC), D(bseq, bi, bB)
    if None in (dA, dC, dB, eA, eC, eB):
        return False
    if same(dB, eB):
        return False
    return (same(dB, eA) or same(dB, eC)) and (same(dA, eB) or same(dC, eB))


REAL_SYM = re.compile(r"__real@([0-9a-fA-F]{8}|[0-9a-fA-F]{16})$")


def _real_value(sym):
    import struct as _st
    m = REAL_SYM.search(sym)
    if not m:
        return None
    h = m.group(1)
    if len(h) == 8:
        return _st.unpack(">f", bytes.fromhex(h))[0]
    return _st.unpack(">d", bytes.fromhex(h))[0]


def reachable_literals(seq, i, regs, depth=4):
    """Float literals (`__real@...`) that feed the given source registers of
    seq[i], following definitions back up to `depth` instructions."""
    out = []
    frontier = [(i, r, depth) for r in regs]
    seen = set()
    while frontier:
        at, r, d = frontier.pop()
        if d == 0 or (at, r) in seen:
            continue
        seen.add((at, r))
        j = last_def(seq, at, r)
        if j is None or _is_call(seq[j]):
            continue
        dj = seq[j]
        for sy in dj["syms"]:
            v = _real_value(sy)
            if v is not None:
                out.append(v)
        for rr in dj["regs"][1:]:
            if rr.startswith("f"):
                frontier.append((j, rr, d - 1))
    return sorted(out, key=lambda v: (abs(v), v))


def sign_folded(tseq, ti, t, bseq, bi, b):
    """`3x^2 - 2x^3` spelled `x2*3 + x3*(-2)` (image) vs `x2*3 - x3*2` (ours):
    the sign moved from the opcode into a literal.  Equal values; /fp:fast
    picks either.  True when both sides reach the same literal MAGNITUDES and
    at least one literal's sign differs."""
    lt = reachable_literals(tseq, ti, t["regs"][1:])
    lb = reachable_literals(bseq, bi, b["regs"][1:])
    if not lt or len(lt) != len(lb):
        return False
    if sorted(abs(v) for v in lt) != sorted(abs(v) for v in lb):
        return False
    return sorted(lt) != sorted(lb)


GUARD_SYM = re.compile(r"\?\$S\d+@|^\$S\d+|_dw@|\?\$TSS\d+@")


def is_frame_reg(seq, i, reg, depth=4):
    """Does `reg` hold r1 + K at seq[i]?  EH-bearing functions address their
    locals through a frame pointer (`subi r31, r1, 0x1c0`), so an `addi`/load
    off r31 there is a STACK slot, not arithmetic and not a field."""
    if reg == "r1":
        return True
    if depth == 0:
        return False
    j = last_def(seq, i, reg)
    if j is None:
        return False
    d = seq[j]
    if d["op"] in ("addi", "mr") and len(d["regs"]) >= 2:
        return is_frame_reg(seq, j, d["regs"][1], depth - 1)
    return False


def _single_bit(v):
    return isinstance(v, int) and v > 0 and (v & (v - 1)) == 0


def flag_bit_source(seq, i, c):
    """Is this single-bit test/set operating on a compiler-owned flags word --
    a function-local-static guard (`?$S9@...`) or a register seeded by `li`
    (MSVC's conditional-temporary destructor flags)?  Their bit NUMBERS are a
    per-function counter: `Hmx::Object::SyncProperty` tests guard bit 2 where
    we test bit 1 because the image has one more local static above it."""
    if c["op"] == "rlwinm" and c["rlw"] and c["rlw"][0] == 0 and _single_bit(c["rlw"][1]):
        src = c["regs"][1] if len(c["regs"]) >= 2 else None
    elif c["op"] in ("ori", "andi", "xori") and c["imms"] and _single_bit(c["imms"][-1]):
        src = c["regs"][1] if len(c["regs"]) >= 2 else None
    else:
        return False
    for _ in range(3):
        if src is None:
            return False
        j = last_def(seq, i, src)
        if j is None:
            return False
        d = seq[j]
        if any(GUARD_SYM.search(sy) for sy in d["syms"]):
            return True
        if d["op"] == "li":
            return True
        if d["op"] in ("ori", "mr") and len(d["regs"]) >= 2:
            i, src = j, d["regs"][1]
            continue
        return False
    return False


def rlw_src_mask(rlw):
    """The SOURCE bits a canonical rlwinm reads: result = rotl(src, sh) & mask,
    so the contributing source bits are rotr(mask, sh)."""
    sh, mask = rlw
    return ((mask >> sh) | (mask << (32 - sh))) & 0xFFFFFFFF if sh else mask


def _src_regs(c):
    """Every register VALUE an instruction reads, base registers included."""
    o = c["op"]
    mb = _mem_base(o)
    if mb is not None:
        if o.startswith("st"):
            return c["regs"][:]
        return c["regs"][1:]
    if BRANCH_RE.match(o) or o in COMPARE_OPS or o in TRAP_OPS:
        return c["regs"][:]
    return c["regs"][1:]


def _spill_source(seq, i):
    """For a load from r1+K, the (index, register) of the last store to r1+K
    before it on the same side, else None."""
    c = seq[i]
    mb = _mem_base(c["op"])
    if mb is None or not c["op"].startswith("l") or c["syms"] or len(c["regs"]) < 2 \
            or c["regs"][-1] != "r1" or not c["imms"]:
        return None
    k = c["imms"][0]
    for j in range(i - 1, max(-1, i - 64), -1):
        x = seq[j]
        if x["op"].startswith("st") and _mem_base(x["op"]) is not None and \
                len(x["regs"]) >= 2 and x["regs"][-1] == "r1" and x["imms"] and \
                x["imms"][0] == k:
            return (j, x["regs"][0])
        if _is_call(x):
            return None
    return None


def val_eq(tseq, da, bseq, ea, depth=3):
    """Do target instruction `da` and base instruction `ea` compute the same
    value?  A bounded VALUE-NUMBERING comparison: same canonical op,
    immediates and symbols, and every source register's reaching definition
    equal in turn (commutative pairs compared either way round).  An aligned
    row is NOT assumed equal -- objdiff aligns `add r9,r10,r27` with
    `add r9,r10,r26` (RhythmDetector SetupFrame), and trusting that alignment
    made two correctly-ordered subtractions look swapped."""
    if da is None or ea is None:
        return False
    x, y = tseq[da], bseq[ea]
    # A reload from a stack slot is the value last STORED there (the
    # int->float `std; lfd; fcfid` idiom goes through memory).  Two loads of
    # 0x58(r1) are not equal if the two sides stored different values there
    # (CharBonesSamples::EvaluateChannel).
    sx, sy = _spill_source(tseq, da), _spill_source(bseq, ea)
    if sx is not None or sy is not None:
        if sx is None or sy is None:
            return False
        (xi, xr), (yi, yr) = sx, sy
        return val_eq(tseq, last_def(tseq, xi, xr), bseq, last_def(bseq, yi, yr), depth)
    if x["op"] == "mr" and len(x["regs"]) == 2:
        return val_eq(tseq, last_def(tseq, da, x["regs"][1]), bseq, ea, depth)
    if y["op"] == "mr" and len(y["regs"]) == 2:
        return val_eq(tseq, da, bseq, last_def(bseq, ea, y["regs"][1]), depth)
    if not (x["op"] == y["op"] and x["imms"] == y["imms"] and x["syms"] == y["syms"]):
        return False
    xs, ys = _src_regs(x), _src_regs(y)
    if len(xs) != len(ys):
        return False
    if depth == 0:
        return True

    def one(xr, yr):
        dx, dy = last_def(tseq, da, xr), last_def(bseq, ea, yr)
        if dx is None and dy is None:
            return xr == yr                 # both function inputs
        return val_eq(tseq, dx, bseq, dy, depth - 1)

    if x["op"] in COMMUTATIVE and len(xs) == 2:
        return (one(xs[0], ys[0]) and one(xs[1], ys[1])) or \
               (one(xs[0], ys[1]) and one(xs[1], ys[0]))
    return all(one(a, b) for a, b in zip(xs, ys))


def _value_equivalent(x, y, xi=None, yi=None, ctx=None):
    """Two defining instructions compute the same value as far as a single
    instruction can say: same canonical op, immediates and symbols (registers
    ignored).  `lfs f13, __real@bf800000` vs `lfs f0, __real@3f800000` is NOT.
    For a LOAD the base register matters too -- `lfs f8, 0x0(r3)` and
    `lfs f13, 0x0(r31)` read different objects -- so, given the context, the
    two base registers must themselves be defined by aligned instructions."""
    if not (x["op"] == y["op"] and x["imms"] == y["imms"] and x["syms"] == y["syms"]
            and [o for o in x["other"] if not isinstance(o, tuple)]
            == [o for o in y["other"] if not isinstance(o, tuple)]):
        return False
    if ctx is None or _mem_base(x["op"]) is None or x["syms"]:
        return True
    tseq, bseq, _ti, _bi, t2b = ctx
    xb = x["regs"][-1] if len(x["regs"]) >= 2 else None
    yb = y["regs"][-1] if len(y["regs"]) >= 2 else None
    if xb is None or yb is None:
        return True
    dx, dy = last_def(tseq, xi, xb), last_def(bseq, yi, yb)
    if dx is None or dy is None:
        return dx is None and dy is None and xb == yb
    return t2b.get(dx) == dy


def value_sources(c):
    """Positions of the register operands whose VALUE the instruction consumes
    (not addresses, not destinations)."""
    o = c["op"]
    mb = _mem_base(o)
    if mb is not None:
        if o.startswith("st") and c["regs"]:
            return [0]                       # the stored value; base = address
        return []
    if BRANCH_RE.match(o) or o in COMPARE_OPS or o in TRAP_OPS:
        return []
    if o in ("mr", "fmr", "vor", "vor128") and len(c["regs"]) >= 2:
        return [1]
    if len(c["regs"]) >= 2:
        return list(range(1, len(c["regs"])))
    return []


COMMUTATIVE = {"add", "addc", "adde", "and", "or", "xor", "nand", "nor", "eqv",
               "mullw", "mulhw", "mulhwu", "mulld", "fadds", "fadd", "fmuls", "fmul",
               "vaddfp", "vaddfp128", "vmulfp128", "vand", "vor", "vxor", "vand128",
               "vor128", "vxor128", "vmaxfp", "vminfp", "vmaxfp128", "vminfp128"}


def _operand_groups(c):
    """Value-source operand positions, grouped so that positions a commutative
    op may exchange are compared as a SET: `fadds f0,f13,f0` vs `fadds f0,f0,f13`
    is the same sum.  First cut compared positionally and flagged 1,512 rows,
    almost all of them exactly that."""
    o = c["op"]
    pos = value_sources(c)
    if not pos:
        return []
    if o in COMMUTATIVE and pos == [1, 2]:
        return [[1, 2]]
    if o in FUSED and len(c["regs"]) == 4:
        return [[1, 2], [3]]            # A*C commute; B is the addend
    return [[q] for q in pos]


def operand_source(t, b, ctx):
    """A register-only row whose consumed VALUE comes from a different place:
    some operand's definition on the target side is aligned with a base
    instruction that defines none of the base row's operands (in the same
    commutative group), and no base operand's definition is value-equivalent
    to it.  `ObjectDir::ResetViewports` stored the register holding +1.0 where
    the image stores the one holding -1.0: same `stfs`, different value, and a
    register-only row to every ruler."""
    tseq, bseq, ti, bi, t2b = ctx
    if len(t["regs"]) != len(b["regs"]):
        return None
    for group in _operand_groups(t):
        tdefs, bdefs = [], []
        for q in group:
            tr, br = t["regs"][q], b["regs"][q]
            if tr in ("r1", "r2") or br in ("r1", "r2"):
                return None
            tdefs.append((tr, last_def(tseq, ti, tr)))
            bdefs.append((br, last_def(bseq, bi, br)))
        if any(d is None for _, d in tdefs + bdefs):
            continue
        bset = {d for _, d in bdefs}
        for tr, da in tdefs:
            ma = t2b.get(da)
            if ma is None or ma in bset:
                continue
            x = tseq[da]
            if _is_call(x):
                continue
            if any(_is_call(bseq[ea]) or val_eq(tseq, da, bseq, ea) for _, ea in bdefs):
                continue
            br, ea = bdefs[tdefs.index((tr, da))]
            return (f"operand {tr} vs {br}: the image's value comes from "
                    f"`{x['text']}`, ours from `{bseq[ea]['text']}`",
                    _def_key(x), _def_key(bseq[ea]))
    return None


def _def_key(c):
    """What a defining instruction contributes, for exchange cancellation.
    Symbol NAMES are kept only for float literals (`__real@...`, whose name IS
    the value); any other name is a wildcard, because the target side names a
    function-local static `lbl_<addr>` where ours names it `?rot@?1??...`, and
    those two spellings of one object must still cancel."""
    return (c["op"], tuple(c["imms"]),
            tuple(sy if REAL_SYM.search(sy) else "*" for sy in c["syms"]))


CA_WRITERS = {"addic", "subfic", "subfc", "addc", "adde", "subfe", "addze",
              "addme", "subfze", "subfme", "srawi", "sraw"}


def subfe_truth(seq, i):
    """When is `subfe` at seq[i] NONZERO, as a predicate on the value y its
    carry was computed from?  `subfe rD,rX,rX` is CA-1 (nonzero iff NOT CA);
    `subfe rD,rA,rB` after `subic rA,rB,1` is CA (nonzero iff CA).  The carry
    producer decides what CA means: `subic y,1` (addic y,-1) sets CA iff
    y != 0; `subfic y,0` sets CA iff y == 0.  Returns 'y!=0' / 'y==0', or None
    when the producer is anything else.

    Without the producer this rule is WRONG: `subfic y,0; subfe rX,rX` and
    `subic y,1; subfe rA,rB` are the SAME truth value, and det-cond measured
    five such equivalent spellings in objdiff's BOOLEAN_NEGATION rows."""
    c = seq[i]
    for j in range(i - 1, max(-1, i - 12), -1):
        p = seq[j]
        if p["op"] not in CA_WRITERS:
            continue
        if p["op"] == "addic" and p["imms"] and p["imms"][0] == -1:
            ca = "y!=0"
        elif p["op"] == "subfic" and p["imms"] and p["imms"][0] == 0:
            ca = "y==0"
        else:
            return None
        direct = c["regs"][1] != c["regs"][2]
        if direct:
            return ca
        return "y==0" if ca == "y!=0" else "y!=0"
    return None


def zero_test_truth(seq, i):
    """Truth predicate of a branch-free zero test ending at seq[i]:
    `cntlzw t,y ; extrwi d,t,1,26` (== srwi 5) is 1 iff y == 0, and a `subfe`
    mask is decided by subfe_truth().  None for anything else."""
    c = seq[i]
    if c["op"] == "subfe" and len(c["regs"]) == 3:
        return subfe_truth(seq, i)
    if c["op"] == "rlwinm" and c["rlw"] in ((27, 1), (27, 0x7FFFFFF)) and len(c["regs"]) >= 2:
        j = last_def(seq, i, c["regs"][1])
        if j is not None and seq[j]["op"] == "cntlzw":
            return "y==0"
    return None


def only_zero_tested(seq, i):
    """Is the result of seq[i] consumed ONLY as an equality-with-zero test?
    Then `a - b` and `b - a` are the same answer: `sSelected == this` spelled
    either way is `subf; cntlzw; extrwi` (RndShockwave::SyncProperty)."""
    c = seq[i]
    if not c["regs"]:
        return False
    d = c["regs"][0]
    for j in range(i + 1, min(len(seq), i + 12)):
        x = seq[j]
        if d in x["regs"][1:] or (x["op"] in COMPARE_OPS and d in x["regs"]):
            if x["op"] == "cntlzw":
                return True
            if x["op"] in ("cmpwi", "cmplwi") and x["imms"] and x["imms"][-1] == 0:
                return True
            if x["op"] == "addic" and x["imms"] and x["imms"][0] == -1:
                return True
            if x["op"] == "subfic" and x["imms"] and x["imms"][0] == 0:
                return True
            return False
        if d in writes(x):
            return False
    return False


def folded_addi(seq, i):
    """`addi d,s,4 ; addi d,d,4` is `addi d,s,8` (RndMultiMesh::CollideList,
    CamShotCrowd::GetSelectedCrowd): fold the immediately following
    same-register increments into one immediate."""
    c = seq[i]
    if c["op"] != "addi" or not c["imms"] or not c["regs"]:
        return None
    total = c["imms"][0]
    d = c["regs"][0]
    for j in range(i + 1, min(len(seq), i + 4)):
        x = seq[j]
        if x["op"] == "addi" and x["regs"][:2] == [d, d] and x["imms"] and not x["syms"]:
            total += x["imms"][0]
            continue
        if d in x["regs"]:
            break
    return total


def analyse_function(rows):
    """rows: [(match_type, target_side|None, base_side|None)].
    Returns (row_findings, bucket_counter, net_atoms)."""
    tseq, bseq = [], []
    aligned = []           # (ti, bi, mt) indices into tseq/bseq
    for mt, ts, bs in rows:
        tc = canon(ts) if ts else None
        bc = canon(bs) if bs else None
        ti = bi = None
        if tc is not None:
            ti = len(tseq)
            tseq.append(tc)
        if bc is not None:
            bi = len(bseq)
            bseq.append(bc)
        aligned.append((mt, ti, bi))

    same_pairs = [(tseq[ti], bseq[bi]) for mt, ti, bi in aligned
                  if ti is not None and bi is not None]
    rmap = register_map(same_pairs)
    t2b = {ti: bi for mt, ti, bi in aligned if ti is not None and bi is not None}

    findings = []
    buckets = Counter()
    t_only, b_only = Counter(), Counter()
    t_only_rows, b_only_rows = defaultdict(list), defaultdict(list)
    const_rows = []
    source_rows = []

    def one_sided(c, side, idx):
        for a in atoms_of(c):
            (t_only if side == "t" else b_only)[a] += 1
            (t_only_rows if side == "t" else b_only_rows)[a].append(c["text"])

    for mt, ti, bi in aligned:
        if mt == "equal":
            continue
        t = tseq[ti] if ti is not None else None
        b = bseq[bi] if bi is not None else None
        if t is None or b is None:
            one_sided(t or b, "t" if t else "b", ti if t else bi)
            continue
        if t["op"] != b["op"]:
            r = classify_substitution(t, b)
            if r is None:
                one_sided(t, "t", ti)
                one_sided(b, "b", bi)
                continue
            bucket, why = r
            if bucket == "float-sign" and sign_folded(tseq, ti, t, bseq, bi, b):
                bucket = "sign-folded-literal"
            zt, zb = zero_test_truth(tseq, ti), zero_test_truth(bseq, bi)
            if zt is not None and zb is not None:
                # two spellings of one zero test (UIManager::IsGameScreenActive:
                # subic/subfe mask vs cntlzw/extrwi).  0/-1 vs 0/1 -- the same
                # TRUTH; whether the magnitude matters is the consumer's
                # business, which is why this is a lead when they differ and
                # a counted equivalence when they agree.
                if zt == zb:
                    buckets["cond-mask-equivalent"] += 1
                    continue
                bucket = "cond-mask"
                why = f"zero-test truth: image nonzero iff {zt}, ours iff {zb}"
            buckets[bucket] += 1
            if bucket in REPORTED:
                findings.append({"bucket": bucket, "why": why,
                                 "target": t["text"], "ours": b["text"]})
            continue
        # same canonical opcode -- what differs?
        o = t["op"]
        if t["syms"] != b["syms"]:
            buckets["relocation"] += 1
            continue
        if t["imms"] != b["imms"] or t["other"] != b["other"]:
            if BRANCH_RE.match(o) or any(isinstance(x, tuple) for x in t["other"] + b["other"]):
                buckets["branch"] += 1
                continue
            if o in COMPARE_OPS:
                buckets["compare"] += 1
                continue
            if o in TRAP_OPS:
                buckets["compare"] += 1
                continue
            if _mem_base(o) is not None:
                base_t = t["regs"][-1] if t["regs"] else None
                framed = base_t is not None and is_frame_reg(tseq, ti, base_t)
                buckets["stack-displacement" if framed else "displacement"] += 1
                continue
            if o in ("li", "lis"):
                if t["regs"] and b["regs"] and rmap.get(t["regs"][0], t["regs"][0]) != b["regs"][0]:
                    buckets["li-misaligned"] += 1      # two different constants paired
                    continue
                buckets["li-constant"] += 1
                findings.append({"bucket": "li-constant", "why": "load-immediate differs",
                                 "target": t["text"], "ours": b["text"]})
                continue
            if o in ("addi", "addis", "addic"):
                if (len(t["regs"]) >= 2 and is_frame_reg(tseq, ti, t["regs"][1])) or \
                        (len(b["regs"]) >= 2 and is_frame_reg(bseq, bi, b["regs"][1])):
                    buckets["stack-displacement"] += 1
                    continue
                ut = addi_use(tseq, ti)
                ub = addi_use(bseq, bi)
                if "address" in (ut, ub):
                    buckets["addi-address"] += 1
                    continue
                ft, fb_ = folded_addi(tseq, ti), folded_addi(bseq, bi)
                if ft is not None and ft == fb_:
                    buckets["const-folded"] += 1
                    continue
                if t["imms"] and b["imms"] and t["imms"][0] == -b["imms"][0]:
                    buckets["int-op"] += 1
                    findings.append({"bucket": "int-op", "why": "addi immediate sign flipped",
                                     "target": t["text"], "ours": b["text"]})
                    continue
                if ut == "value" or ub == "value":
                    const_rows.append((t, b, f"addi immediate feeds value arithmetic ({ut}/{ub})"))
                else:
                    buckets["addi-address"] += 1
                continue
            if o == "rlwinm":
                if t["rec"] and b["rec"] and t["rlw"] and b["rlw"] and \
                        rlw_src_mask(t["rlw"]) == rlw_src_mask(b["rlw"]) and \
                        t["rlw"][1] and (t["rlw"][1] & (t["rlw"][1] - 1)) == 0:
                    buckets["bit-test-encoding"] += 1    # extrwi. vs rlwinm. of one bit
                    continue
                if flag_bit_source(tseq, ti, t) and flag_bit_source(bseq, bi, b):
                    buckets["flag-bit-renumbering"] += 1
                    continue
                kt, kb = rlw_kind(t["rlw"]), rlw_kind(b["rlw"])
                const_rows.append((t, b, f"rlwinm {kt} vs {kb}: shift/mask differs"))
                continue
            if o in ("srawi", "sradi", "mulli", "andi", "andis", "ori", "oris", "xori",
                     "xoris", "subfic", "addic", "rlwimi", "rldicl", "rldicr", "rldic",
                     "rldimi", "sldi", "srdi", "clrldi"):
                if flag_bit_source(tseq, ti, t) and flag_bit_source(bseq, bi, b):
                    buckets["flag-bit-renumbering"] += 1
                    continue
                const_rows.append((t, b, f"{o} immediate differs"))
                continue
            buckets["other-opcode"] += 1
            continue
        # registers only
        if o == "subfe" and len(t["regs"]) == 3 and len(b["regs"]) == 3 and \
                (t["regs"][1] == t["regs"][2]) != (b["regs"][1] == b["regs"][2]):
            tt, tb_ = subfe_truth(tseq, ti), subfe_truth(bseq, bi)
            if tt is not None and tb_ is not None and tt == tb_:
                buckets["cond-mask-equivalent"] += 1
                continue
            buckets["cond-mask"] += 1
            findings.append({"bucket": "cond-mask",
                             "why": (f"subfe mask: image is nonzero iff {tt or '?'}, ours "
                                     f"iff {tb_ or '?'}"),
                             "target": t["text"], "ours": b["text"]})
            continue
        if operand_swap(t, b, (tseq, bseq, ti, bi, t2b)):
            if o in ("subf", "subfc") and only_zero_tested(tseq, ti) and \
                    only_zero_tested(bseq, bi):
                buckets["swap-zero-tested"] += 1     # a-b == 0 iff b-a == 0
                continue
            buckets["operand-order"] += 1
            findings.append({"bucket": "operand-order",
                             "why": f"{o}: non-commutative source operands swapped "
                                    f"(by where each was defined)",
                             "target": t["text"], "ours": b["text"]})
            continue
        src = operand_source(t, b, (tseq, bseq, ti, bi, t2b))
        if src:
            source_rows.append((t, b) + src)
            continue
        buckets["register-only"] += 1

    # ---- const-operand rows: cancel pure REORDERINGS ----------------------- #
    # Two rows that exchanged places (target `ori 0x1617` / `ori 0x1e1f`, ours
    # `ori 0x1e1f` / `ori 0x1617`) are scheduling: every immediate the image
    # uses, we use too.  Only an immediate with no partner is a value change.
    def key(c):
        return (c["op"], tuple(c["imms"]))
    tk = Counter(key(t) for t, b, w in const_rows)
    bk = Counter(key(b) for t, b, w in const_rows)
    for t, b, why in const_rows:
        if tk[key(t)] > 0 and bk[key(t)] > 0 and bk[key(b)] > 0 and tk[key(b)] > 0:
            buckets["const-reordered"] += 1
            continue
        buckets["const-operand"] += 1
        findings.append({"bucket": "const-operand", "why": why,
                         "target": t["text"], "ours": b["text"]})

    # ---- operand-source rows: cancel exchanged VALUES ---------------------- #
    # Two independent computations that objdiff aligned crosswise (x-component
    # against y-component: `fadds f13,f10,f12` / `fadds f0,f11,f0` vs the other
    # way round) consume the same multiset of values.  Only the values left
    # over after multiset cancellation are a changed input.  ResetViewports'
    # single -1.0 -> +1.0 store has no partner and survives.
    tk = Counter(r[3] for r in source_rows)
    bk = Counter(r[4] for r in source_rows)
    t_left = tk - bk
    b_left = bk - tk
    for t, b, why, kt, kb in source_rows:
        if t_left[kt] > 0 and b_left[kb] > 0:
            t_left[kt] -= 1
            b_left[kb] -= 1
            buckets["operand-source"] += 1
            findings.append({"bucket": "operand-source", "why": why,
                             "target": t["text"], "ours": b["text"]})
        else:
            buckets["operand-exchanged"] += 1

    # ---- re-bucket anything touching a branch-free condition idiom -------- #
    def _ops(txt):
        return {tok.split()[0].rstrip(".") for tok in txt.split(";") if tok.strip()}
    keep = []
    for f in findings:
        if f["bucket"] in ("op-substitution", "int-op", "const-operand", "signedness"):
            ops = _ops(f["target"]) | _ops(f["ours"])
            if ops & MASK_OPS:
                buckets[f["bucket"]] -= 1
                # the carry PRODUCER swap (subfic y,0 <-> subic y,1) of a pair
                # whose subfe was proven equivalent is part of that spelling
                if ops <= {"subfic", "addic", "subic", "cntlzw"} and \
                        buckets["cond-mask-equivalent"]:
                    buckets["cond-mask-equivalent"] += 1
                    continue
                buckets["cond-mask"] += 1
                f["why"] = f"[{f['bucket']}] " + f["why"]
                f["bucket"] = "cond-mask"
        keep.append(f)
    findings[:] = keep

    # ---- net-term: atom balance over one-sided instructions -------------- #
    net = {}
    for a in sorted(set(t_only) | set(b_only)):
        d = t_only[a] - b_only[a]
        if d:
            net[a] = d
    if net:
        kinds = set(net)
        if kinds & {"fdiv"}:
            buckets["reciprocal-fold"] += 1
        elif kinds & {"fsel"}:
            buckets["fsel-lowering"] += 1
        elif kinds == {"boolmask"}:
            buckets["bool-mask"] += 1       # BOOL_MASK: clrlwi 24 inserted/omitted
        elif len(kinds) == 1:
            a = next(iter(kinds))
            nb = "cond-mask" if a in ("carry", "clz", "ineg") else "net-term"
            buckets[nb] += 1
            findings.append({
                "bucket": nb,
                "why": (f"atom '{a}' net {net[a]:+d} (target minus ours) after "
                        f"cancelling moved instructions"),
                "target": "; ".join(t_only_rows.get(a, [])[:6]),
                "ours": "; ".join(b_only_rows.get(a, [])[:6]),
            })
        else:
            buckets["multi-atom"] += 1
    return findings, buckets, net


# --------------------------------------------------------------------------- #
# objdiff batch driver
# --------------------------------------------------------------------------- #
def _slim(side):
    if not side:
        return None
    return [side.get("opcode", ""), side.get("args", ""),
            [(a.get("type"), a.get("value")) for a in side.get("typed_args", [])]]


def run_shard(job):
    """Run one objdiff --batch process over `names`; return a list of compact
    per-symbol results.  Everything objdiff says comes back -- including
    errors -- so the parent can account for it.

    WATCHDOG.  objdiff-cli 4.2.9 never returns on at least one symbol
    (`?Terminate@VirtualKeyboard@@QAAXXZ`, a 4-byte function we do not define:
    measured >20 s alone, and it held a whole 4,000-symbol shard for 584 s --
    the entire runtime of the first cut of this scanner).  A shard that emits
    nothing for `idle` seconds is killed; the parent re-runs whatever it did
    not return in smaller pieces, down to single symbols, and a single symbol
    that still times out is DROPPED AS `objdiff-timeout` -- counted, named,
    never silently absent."""
    objdiff, repo, names, idle, keep_rows = job
    p = subprocess.Popen([objdiff, "diff", "-p", repo, "--batch",
                          "--include-instructions", "-f", "json"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, cwd=repo)
    err_chunks = []
    last = [time.monotonic()]
    killed = [False]
    done = threading.Event()

    def feed():
        try:
            p.stdin.write("\n".join(names) + "\n")
        except BrokenPipeError:
            pass
        finally:
            try:
                p.stdin.close()
            except BrokenPipeError:
                pass

    def drain_err():
        err_chunks.append(p.stderr.read())

    def watchdog():
        while not done.wait(1.0):
            if time.monotonic() - last[0] > idle:
                killed[0] = True
                p.kill()
                return

    threads = [threading.Thread(target=f) for f in (feed, drain_err, watchdog)]
    for t in threads:
        t.start()
    out = []
    for line in p.stdout:
        last[0] = time.monotonic()
        line = line.strip()
        if not line:
            continue
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            out.append({"symbol": None, "error": "unparseable-jsonl"})
            continue
        if "error" in d:
            out.append({"symbol": d.get("symbol"), "error": str(d["error"])[:200]})
            continue
        ins = d.get("instructions") or []
        has_base = any("base" in i for i in ins)
        rows = []
        mism = 0
        for i in ins:
            mt = i.get("match_type", "?")
            if mt != "equal":
                mism += 1
            rows.append((mt, _slim(i.get("target")), _slim(i.get("base"))))
        rec = {"symbol": d.get("symbol"), "unit": d.get("unit"),
               "base_unit": d.get("base_unit"), "has_base": has_base,
               "norm": d.get("normalized_match_percent"),
               "fuzzy": d.get("fuzzy_match_percent"), "n_rows": len(ins),
               "n_mismatch": mism}
        if mism and has_base:
            f, b, net = analyse_function(rows)
            rec["findings"] = f
            rec["buckets"] = dict(b)
            rec["net"] = net
            if keep_rows:
                rec["rows"] = rows
        out.append(rec)
    p.wait()
    done.set()
    for t in threads:
        t.join()
    return {"rc": p.returncode, "killed": killed[0], "names": names, "results": out,
            "stderr_tail": "".join(err_chunks)[-400:]}


def run_all(objdiff, names_by_shard, idle, workers, cov, keep_rows=False):
    """Drive the shards; re-run anything a killed shard did not return, in
    ever smaller pieces.  Returns (results, errors, timed_out)."""
    results, errors, timed_out = {}, {}, []
    pending = [s for s in names_by_shard if s]
    rounds = 0
    while pending:
        rounds += 1
        jobs = [(objdiff, REPO, s, idle, keep_rows) for s in pending]
        pending = []
        with ProcessPoolExecutor(max_workers=max(1, min(workers, len(jobs)))) as ex:
            for res in ex.map(run_shard, jobs):          # ordered: deterministic
                got = set()
                for r in res["results"]:
                    if "error" in r:
                        errors[r["symbol"]] = r["error"]
                    else:
                        results[r["symbol"]] = r
                    got.add(r.get("symbol"))
                missing = [n for n in res["names"] if n not in got]
                if not missing:
                    continue
                if not res["killed"] and res["rc"] == 0:
                    for n in missing:
                        errors[n] = "no batch row returned"
                    continue
                if len(missing) == 1:
                    timed_out.append(missing[0])
                    continue
                k = 16
                step = (len(missing) + k - 1) // k
                pending.extend(missing[i:i + step] for i in range(0, len(missing), step))
    cov.extra("objdiff_rounds", rounds)
    return results, errors, timed_out


def replay(path, cov):
    """Re-classify a pass recorded with --save-rows, without running objdiff.
    A development aid: the classification changes, the objdiff pass does not.
    The universe still comes from report.json, independently."""
    results = {}
    with open(path) as fh:
        head = json.loads(fh.readline())
        for line in fh:
            r = json.loads(line)
            if "rows" in r:
                rows = [tuple(x) for x in r.pop("rows")]
                f, b, net = analyse_function(rows)
                r["findings"], r["buckets"], r["net"] = f, dict(b), net
            results[r["symbol"]] = r
    cov.note(f"REPLAYED from {path}: objdiff was not run; the rows are as recorded")
    return results, head["_errors"], head["_timed_out"]


def load_universe(report_path):
    """Pass 1, independent of every classification below: every function
    report.json lists, as (unit, symbol)."""
    with open(report_path) as f:
        rep = json.load(f)
    uni = []
    for u in rep.get("units", []):
        for fn in u.get("functions", []):
            uni.append((u.get("name", ""), fn.get("name", "")))
    return uni


def scan(args, cov):
    universe = load_universe(args.report)
    if args.only:
        wanted = set(args.only)
        before = len(universe)
        universe_sel = [x for x in universe if x[1] in wanted]
        cov.universe(before, "functions listed in report.json")
        cov.drop("not-selected-by---only", before - len(universe_sel))
        universe = universe_sel
    else:
        cov.universe(len(universe), "functions listed in report.json")

    by_unit = defaultdict(list)
    for unit, name in universe:
        by_unit[unit].append(name)
    units = sorted(by_unit)
    k = max(1, args.jobs) * 4          # more shards than workers: one slow unit
    shards = [[] for _ in range(k)]   # no longer holds a quarter of the corpus
    seen = set()
    for n, u in enumerate(units):
        for name in by_unit[u]:
            if name in seen:            # batch resolves by NAME: send once
                continue
            seen.add(name)
            shards[n % k].append(name)
    if args.replay:
        results, errors, timed_out = replay(args.replay, cov)
    else:
        results, errors, timed_out = run_all(args.objdiff, shards, args.idle_timeout,
                                             args.jobs, cov, keep_rows=bool(args.save_rows))
        if args.save_rows:
            with open(args.save_rows, "w") as fh:
                fh.write(json.dumps({"_errors": errors, "_timed_out": sorted(timed_out)},
                                    sort_keys=True) + "\n")
                for name in sorted(results):
                    fh.write(json.dumps(results[name], sort_keys=True) + "\n")
    cov.extra("objdiff_shards", len([x for x in shards if x]))
    cov.extra("objdiff_timed_out_symbols", sorted(timed_out))
    timed_out = set(timed_out)

    per_function = []
    for unit, name in sorted(universe):
        r = results.get(name)
        if r is None:
            if name in timed_out:
                cov.drop("objdiff-timeout",
                         note="objdiff-cli emitted nothing for --idle-timeout seconds "
                              "on this symbol ALONE; see objdiff_timed_out_symbols")
            elif name in errors:
                e = errors[name]
                cov.drop("objdiff-error:" + ("not-found" if "not found" in e.lower()
                                               else "other"),
                         note="objdiff could not diff the symbol")
            else:
                cov.drop("objdiff-returned-nothing",
                         note="no batch row came back for this name")
            continue
        if r.get("unit") != unit and r.get("base_unit") != unit:
            cov.drop("duplicate-name-resolved-to-another-unit",
                     note="batch mode resolves by symbol NAME; the other unit's "
                          "copy was diffed and this one was not")
            continue
        if not r["has_base"]:
            cov.drop("not-defined-in-our-build",
                     note="no base-side instructions: we do not define this function")
            continue
        cov.examine()
        per_function.append((unit, name, r))
    return per_function


# --------------------------------------------------------------------------- #
# Selftest — synthetic rows through the real classifier
# --------------------------------------------------------------------------- #
def _S(op, args):
    """Build an objdiff-shaped side from assembler text."""
    typed = []
    for tok in [a.strip() for a in args.split(",")] if args else []:
        m = re.match(r"^(-?(?:0x[0-9a-fA-F]+|\d+))\((r\d+)\)$", tok)
        ms = re.match(r"^(.+)@(?:l|h|ha)\((r\d+)\)$", tok)
        if ms:
            typed.append(("Symbol", ms.group(1)))
            typed.append(("Register", ms.group(2)))
        elif m:
            typed.append(("Signed", int(m.group(1), 0)))
            typed.append(("Register", m.group(2)))
        elif re.match(r"^(r|f|cr|v)\d+$", tok):
            typed.append(("Register", tok))
        elif re.match(r"^-?(0x[0-9a-fA-F]+|\d+)$", tok):
            typed.append(("Other" if op.startswith(("rlw", "slwi", "srwi", "clr", "ext",
                                                     "rot", "rld"))
                          else "Signed", int(tok, 0)))
        else:
            typed.append(("Symbol", tok))
    return [op, args, typed]


def selftest():
    fails = 0

    def check(label, cond):
        nonlocal fails
        print(f"  [{'ok' if cond else 'FAIL'}] {label}")
        if not cond:
            fails += 1

    def run(rows):
        f, b, net = analyse_function(rows)
        return [x["bucket"] for x in f], b, net

    E = lambda op, a: ("equal", _S(op, a), _S(op, a))  # noqa: E731

    # Rand::Seed: srawi vs srwi
    fb, b, _ = run([E("li", "r11, 0x100"),
                    ("replace", _S("srwi", "r7, r10, 16"), _S("srawi", "r7, r10, 16"))])
    check("srawi vs srwi -> signedness", fb == ["signedness"])
    # register-only permutation is not a finding
    fb, b, _ = run([("diff_arg", _S("add", "r3, r4, r5"), _S("add", "r3, r5, r4"))])
    check("commutative add swap -> register-only", fb == [] and b["register-only"] == 1)
    # non-commutative swap under a consistent map
    fb, b, _ = run([E("lwz", "r4, 0x0(r3)"), E("lwz", "r5, 0x4(r3)"),
                    E("mr", "r6, r4"), E("mr", "r7, r5"),
                    ("diff_arg", _S("subf", "r3, r4, r5"), _S("subf", "r3, r5, r4"))])
    check("subf swapped -> operand-order", fb == ["operand-order"])
    # renamed registers, same order: EQEffect::SetParameter's (x-1)/(x+1)
    fb, b, _ = run([("diff_arg", _S("lfs", "f13, 0x5c(r31)"), _S("lfs", "f30, 0x5c(r31)")),
                    ("diff_arg", _S("fsubs", "f12, f13, f30"), _S("fsubs", "f13, f30, f29")),
                    ("diff_arg", _S("fadds", "f13, f13, f30"), _S("fadds", "f12, f30, f29")),
                    ("diff_arg", _S("fdivs", "f13, f12, f13"), _S("fdivs", "f13, f13, f12"))])
    check("renamed fdivs operands (same order) -> NOT operand-order",
          "operand-order" not in fb)
    # ResetViewports: the stored register holds a different constant
    fb, b, _ = run([E("lfs", "f13, __real@bf800000@l(r10)"),
                    E("lfs", "f0, __real@3f800000@l(r9)"),
                    ("diff_arg", _S("stfs", "f13, 0xd8(r31)"), _S("stfs", "f0, 0xd8(r31)"))])
    check("stfs of a different constant -> operand-source", fb == ["operand-source"])
    # ...but a renamed register holding the same constant is not
    fb, b, _ = run([("diff_arg", _S("lfs", "f30, __real@c4400000@l(r8)"),
                     _S("lfs", "f12, __real@c4400000@l(r8)")),
                    ("diff_arg", _S("stfs", "f30, 0x70(r31)"), _S("stfs", "f12, 0x70(r31)"))])
    check("stfs of a renamed same constant -> register-only", fb == []
          and b["register-only"] == 2)
    # frame-pointer-relative addi is a stack slot (InitializePlaylists)
    fb, b, _ = run([E("addi", "r31, r1, -0x1c0"),
                    ("diff_arg", _S("addi", "r11, r31, 0x60"), _S("addi", "r11, r31, 0x70")),
                    E("cmplw", "r3, r11")])
    check("addi off a frame pointer -> stack-displacement", fb == []
          and b["stack-displacement"] == 1)
    # one-bit test, two encodings (MovieOpen)
    fb, b, _ = run([("diff_arg", _S("extrwi.", "r11, r11, 1, 5"),
                     _S("rlwinm.", "r11, r11, 0, 5, 5"))])
    check("extrwi. 1,5 vs rlwinm. 0,5,5 -> bit-test-encoding", fb == []
          and b["bit-test-encoding"] == 1)
    # static-guard bit renumbering (Hmx::Object::SyncProperty)
    fb, b, _ = run([E("lwz", "r11, ?$S9@foo@4IA@l(r30)"),
                    ("diff_arg", _S("rlwinm.", "r9, r11, 0, 29, 29"), _S("rlwinm.", "r9, r11, 0, 30, 30")),
                    ("diff_arg", _S("ori", "r11, r11, 0x4"), _S("ori", "r11, r11, 0x2"))])
    check("guard-bit renumbering -> flag-bit-renumbering", fb == []
          and b["flag-bit-renumbering"] == 2)
    # ...but a single-bit test on a MEMBER is reported
    fb, b, _ = run([E("lwz", "r11, 0x10(r31)"),
                    ("diff_arg", _S("rlwinm.", "r9, r11, 0, 29, 29"), _S("rlwinm.", "r9, r11, 0, 30, 30"))])
    check("member flag bit differs -> const-operand", fb == ["const-operand"])
    # two rows exchanged
    fb, b, _ = run([("diff_arg", _S("xori", "r9, r9, 0xf"), _S("xori", "r9, r9, 0x19")),
                    ("diff_arg", _S("xori", "r11, r11, 0x19"), _S("xori", "r11, r11, 0xf"))])
    check("exchanged immediates -> const-reordered", fb == [] and b["const-reordered"] == 2)
    # GetBlendState: 3x^2 - 2x^3 with the sign in a literal on one side
    fb, b, _ = run([("diff_arg", _S("lfs", "f0, __real@40400000@l(r11)"), _S("lfs", "f0, __real@40000000@l(r11)")),
                    ("diff_arg", _S("lfs", "f13, __real@c0000000@l(r10)"), _S("lfs", "f13, __real@40400000@l(r10)")),
                    ("diff_arg", _S("fmuls", "f0, f11, f0"), _S("fmuls", "f0, f12, f0")),
                    ("replace", _S("fmadds", "f0, f12, f13, f0"), _S("fmsubs", "f0, f11, f13, f0"))])
    check("fmadds(-2) vs fmsubs(2) -> sign-folded-literal", "float-sign" not in fb
          and b["sign-folded-literal"] == 1)
    # ...while the same substitution with the same literals is a real sign flip
    fb, b, _ = run([E("lfs", "f13, __real@40000000@l(r10)"),
                    ("replace", _S("fmadds", "f0, f12, f13, f0"), _S("fmsubs", "f0, f12, f13, f0"))])
    check("fmadds vs fmsubs, same literals -> float-sign", fb == ["float-sign"])
    # a mask-idiom substitution goes to cond-mask
    fb, b, _ = run([("replace", _S("subfic", "r11, r3, 0x0"), _S("cntlzw", "r11, r3"))])
    check("subfic vs cntlzw -> cond-mask", fb == ["cond-mask"])
    fb, b, net = run([E("lwz", "r3, 0x0(r31)"), ("delete", _S("subfe", "r3, r3, r3"), None)])
    check("one-sided subfe -> cond-mask", fb == ["cond-mask"])
    # two exchanged components are not a changed value
    fb, b, _ = run([E("lfs", "f10, 0x14(r11)"), E("lfs", "f11, 0x18(r11)"),
                    E("lfs", "f12, 0x0(r30)"), E("lfs", "f0, 0x4(r30)"),
                    ("diff_arg", _S("fadds", "f13, f10, f12"), _S("fadds", "f13, f11, f12")),
                    ("diff_arg", _S("fadds", "f0, f11, f0"), _S("fadds", "f0, f10, f0"))])
    check("exchanged component loads -> operand-exchanged", fb == []
          and b["operand-exchanged"] == 2)
    # EndFatal: subfe polarity
    fb, b, _ = run([("diff_arg", _S("subic", "r10, r10, 0x1"), _S("subic", "r8, r10, 0x1")),
                    ("diff_arg", _S("subfe", "r10, r10, r10"), _S("subfe", "r10, r8, r10"))])
    check("subfe rX,rX vs subfe rA,rB, both after subic 1 -> cond-mask", fb == ["cond-mask"])
    # ...but subfic 0 + subfe rX,rX is the SAME truth as subic 1 + subfe rA,rB
    fb, b, _ = run([("replace", _S("subfic", "r11, r3, 0x0"), _S("subic", "r11, r3, 0x1")),
                    ("diff_arg", _S("subfe", "r11, r11, r11"), _S("subfe", "r28, r11, r3"))])
    check("subfic/subfe vs subic/subfe (same truth) -> cond-mask-equivalent",
          b["cond-mask-equivalent"] == 2 and fb == [])
    # IsGameScreenActive: subic/subfe mask vs cntlzw/extrwi, same truth
    fb, b, _ = run([E("subf", "r11, r11, r10"),
                    ("replace", _S("subic", "r11, r11, 0x1"), _S("cntlzw", "r11, r11")),
                    ("replace", _S("subfe", "r11, r11, r11"), _S("extrwi", "r11, r11, 1, 26")),
                    E("and", "r9, r11, r9")])
    check("subic/subfe(x,x) vs cntlzw/extrwi (both y==0) -> cond-mask-equivalent",
          fb == [] and b["cond-mask-equivalent"] == 2)
    # ...and the INVERTED spelling is a finding
    fb, b, _ = run([E("subf", "r11, r11, r10"),
                    ("replace", _S("subfic", "r11, r11, 0x0"), _S("cntlzw", "r11, r11")),
                    ("replace", _S("subfe", "r11, r11, r11"), _S("extrwi", "r11, r11, 1, 26")),
                    E("and", "r9, r11, r9")])
    check("subfic/subfe(x,x) (y!=0) vs cntlzw/extrwi (y==0) -> cond-mask", "cond-mask" in fb)
    # a swapped subtraction consumed only by a zero test is an equality
    fb, b, _ = run([E("lwz", "r10, 0x0(r31)"), E("lwz", "r11, 0x4(r31)"),
                    ("diff_arg", _S("subf", "r11, r10, r11"), _S("subf", "r11, r11, r10")),
                    E("cntlzw", "r11, r11")])
    check("subf swap feeding cntlzw -> swap-zero-tested", fb == []
          and b["swap-zero-tested"] == 1)
    # addi 4 ; addi 4  ==  addi 8
    fb, b, _ = run([E("add", "r9, r9, r11"),
                    ("diff_arg", _S("addi", "r8, r9, 0x4"), _S("addi", "r8, r9, 0x8")),
                    ("delete", _S("addi", "r8, r8, 0x4"), None),
                    E("cmplw", "r9, r8")])
    check("addi 4;addi 4 vs addi 8 -> const-folded", fb == [] and b["const-folded"] == 1)
    # extsb vs clrlwi 24
    fb, _, _ = run([("replace", _S("extsb", "r3, r3"), _S("clrlwi", "r3, r3, 24"))])
    check("extsb vs clrlwi 24 -> signedness", fb == ["signedness"])
    # lbz vs lwz width
    fb, _, _ = run([("replace", _S("lbz", "r3, 0x8(r31)"), _S("lwz", "r3, 0x8(r31)"))])
    check("lbz vs lwz -> int-width", fb == ["int-width"])
    # lfs vs lfd
    fb, _, _ = run([("replace", _S("lfs", "f1, 0x8(r31)"), _S("lfd", "f1, 0x8(r31)"))])
    check("lfs vs lfd -> float-width", fb == ["float-width"])
    # fmsubs vs fnmsubs
    fb, _, _ = run([("replace", _S("fmsubs", "f1, f2, f3, f4"), _S("fnmsubs", "f1, f2, f3, f4"))])
    check("fmsubs vs fnmsubs -> float-sign", fb == ["float-sign"])
    # contraction balances
    fb, b, net = run([("delete", _S("fmadds", "f1, f2, f3, f4"), None),
                      ("insert", None, _S("fmuls", "f0, f2, f3")),
                      ("insert", None, _S("fadds", "f1, f0, f4"))])
    check("fmadds vs fmuls+fadds -> balanced, no finding", fb == [] and not net)
    # dropped add
    fb, b, net = run([E("lwz", "r3, 0x0(r31)"),
                      ("delete", _S("add", "r10, r10, r11"), None)])
    check("one-sided add -> net-term", fb == ["net-term"] and net == {"iadd": 1})
    # a moved instruction cancels
    fb, b, net = run([("delete", _S("fmuls", "f1, f2, f3"), None),
                      E("lwz", "r3, 0x0(r31)"),
                      ("insert", None, _S("fmuls", "f1, f2, f3"))])
    check("moved fmuls cancels", fb == [] and not net)
    # displacement is not ours
    fb, b, _ = run([("diff_arg", _S("lwz", "r3, 0x8(r31)"), _S("lwz", "r3, 0xc(r31)"))])
    check("field displacement -> counted, not reported", fb == [] and b["displacement"] == 1)
    # rlwinm alias canonicalisation: srwi 16 == rlwinm 16,16,31
    fb, b, _ = run([("diff_op", _S("srwi", "r3, r4, 16"), _S("rlwinm", "r3, r4, 16, 16, 31"))])
    check("srwi == rlwinm 16,16,31 -> equal after canon", fb == [] and not
          [k for k in b if k in REPORTED])
    # shift constant differs
    fb, b, _ = run([("diff_arg", _S("slwi", "r3, r4, 2"), _S("slwi", "r3, r4, 3"))])
    check("slwi 2 vs 3 -> const-operand", fb == ["const-operand"])
    # add vs or is a known equivalence, counted
    fb, b, _ = run([("replace", _S("or", "r3, r4, r5"), _S("add", "r3, r4, r5"))])
    check("or vs add -> disjoint-or-add (counted)", fb == [] and b["disjoint-or-add"] == 1)
    # addi used as address is not arithmetic
    fb, b, _ = run([("diff_arg", _S("addi", "r4, r31, 0x40"), _S("addi", "r4, r31, 0x44")),
                    E("lwz", "r3, 0x0(r4)")])
    check("addi feeding a load base -> addi-address", fb == [] and b["addi-address"] == 1)
    # addi feeding arithmetic is
    fb, b, _ = run([("diff_arg", _S("addi", "r4, r4, 0x1"), _S("addi", "r4, r4, 0x2")),
                    E("mullw", "r3, r4, r5")])
    check("addi feeding mullw -> const-operand", fb == ["const-operand"])
    # compare is det-cond's
    fb, b, _ = run([("replace", _S("cmpw", "r3, r4"), _S("cmplw", "r3, r4"))])
    check("cmpw vs cmplw -> compare (not ours)", fb == [] and b["compare"] == 1)
    print(f"selftest: {'PASS' if not fails else f'{fails} FAILED'}")
    return 1 if fails else 0


# --------------------------------------------------------------------------- #
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--report", default=DEFAULT_REPORT)
    ap.add_argument("--objdiff", default=DEFAULT_OBJDIFF)
    ap.add_argument("--jobs", type=int, default=12)
    ap.add_argument("--idle-timeout", type=float, default=30.0,
                    help="kill an objdiff shard that emits nothing for this long "
                         "and re-run its unreturned symbols in smaller pieces")
    ap.add_argument("--only", action="append", default=None, metavar="SYMBOL",
                    help="restrict to these mangled names (counted as a drop)")
    ap.add_argument("--json", default=None)
    ap.add_argument("--save-rows", default=None, metavar="JSONL",
                    help="record every mismatched function's aligned rows for --replay")
    ap.add_argument("--replay", default=None, metavar="JSONL",
                    help="re-classify a --save-rows recording instead of running objdiff")
    ap.add_argument("--show-li", action="store_true",
                    help="also print li-constant rows (a wrong CONSTANT, not arithmetic)")
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT only; the counts above it are complete")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not os.path.exists(args.report):
        print(f"INCONCLUSIVE: report.json not found: {args.report}")
        return EXIT_NO_INPUT
    if not os.path.exists(args.objdiff):
        print(f"INCONCLUSIVE: objdiff-cli not found: {args.objdiff}")
        return EXIT_NO_INPUT

    cov = CoverageReport("arith_semantics_scan", args=args)
    cov.require_examined("objdiff paired no function with a base side")
    per_function = scan(args, cov)

    row_buckets = Counter()
    fn_buckets = Counter()
    flagged = []
    n_mismatch_fns = 0
    for unit, name, r in per_function:
        if r["n_mismatch"]:
            n_mismatch_fns += 1
        b = Counter(r.get("buckets", {}))
        row_buckets.update(b)
        fs = r.get("findings", [])
        kinds = sorted({f["bucket"] for f in fs})
        for kd in kinds:
            fn_buckets[kd] += 1
        rep = [f for f in fs if f["bucket"] in REPORTED]
        if rep or (args.show_li and fs):
            flagged.append((unit, name, r, fs))

    n_fn_findings = sum(1 for u, n, r, fs in flagged
                        if any(f["bucket"] in REPORTED for f in fs))
    cov.extra("functions_with_a_mismatch_row", n_mismatch_fns)
    cov.extra("functions_with_a_reported_row", n_fn_findings)
    cov.extra("row_buckets", dict(sorted(row_buckets.items())))
    cov.extra("function_buckets", dict(sorted(fn_buckets.items())))
    cov.note("examined = functions objdiff paired with a base side; a function "
             "with zero mismatch rows is examined and clean BY CONSTRUCTION")
    cov.note("a bucket hit is a VALUE claim, never a behaviour claim -- adjudicate "
             "every row against src/ and build/373307D9/asm/")

    print(f"ARITHMETIC-SEMANTICS ROWS: {n_fn_findings} functions carry a reported "
          f"row, of {len(per_function)} examined ({n_mismatch_fns} with any mismatch "
          f"row) out of {len(load_universe(args.report))} functions in report.json")
    print("  reported buckets (rows / functions):")
    for kd in REPORTED:
        print(f"    {kd:20s} {row_buckets.get(kd, 0):6d} / {fn_buckets.get(kd, 0):5d}")
    print("  counted, not reported (rows):")
    for kd in COUNTED:
        print(f"    {kd:20s} {row_buckets.get(kd, 0):6d}")
    print()
    # Clean shapes first: a reported row in a function that otherwise matches
    # is far more likely to be real than one in a 60% function.
    flagged.sort(key=lambda x: (x[2]["n_mismatch"] / max(1, x[2]["n_rows"]), x[0], x[1]))
    shown = flagged if args.limit <= 0 else flagged[: args.limit]
    if len(shown) < len(flagged):
        print(f"  showing {len(shown)} of {len(flagged)} flagged functions")
    for unit, name, r, fs in shown:
        norm = r.get("norm")
        print(f"  {name}")
        print(f"      unit={unit}  norm={'n/a' if norm is None else f'{norm:.4f}'}  "
              f"mismatch_rows={r['n_mismatch']}/{r['n_rows']}")
        for f in fs:
            if f["bucket"] == "li-constant" and not args.show_li:
                continue
            print(f"      [{f['bucket']}] {f['why']}")
            print(f"          target: {f['target']}")
            print(f"          ours  : {f['ours']}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"flagged": [{"unit": u, "symbol": n, "norm": r.get("norm"),
                                    "n_mismatch": r["n_mismatch"], "n_rows": r["n_rows"],
                                    "net": r.get("net", {}), "findings": fs}
                                   for u, n, r, fs in flagged],
                       "row_buckets": dict(sorted(row_buckets.items())),
                       "function_buckets": dict(sorted(fn_buckets.items())),
                       "_coverage": cov.as_dict()}, fh, indent=1, sort_keys=True)
    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
