#!/usr/bin/env python3
"""cond_semantics_scan.py — triage every conditional branch in every sub-100%
function for a difference in WHAT IT TESTS, not in how it was laid out.

WHY THIS EXISTS
===============
Taxonomy classes 2 (inverted / missing / extra condition, 128 historical bugs,
the largest class) and 3 (wrong loop bound / off-by-one, 33) had no tooling
(`docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`).  Unlike a
wrong field, a wrong condition always costs match points -- the mnemonic, the
compare opcode or the compare immediate changes -- so every instance sits in a
function below 100% and objdiff already has a row for it.  The rows are simply
drowned: a `beq` against a `bne` is at least as often MSVC laying the two arms
out the other way round as it is a bug, and a `cmplwi` against a `cmpwi` is
usually a declared type that does not matter for an equality test.

WHAT IT DOES
============
For every function report.json scores strictly between 0 and 100 (the only
functions that can carry this class -- see "proof of absence" below), one
`objdiff-cli diff --batch` process yields the aligned instruction rows.  Then,
for every row where BOTH sides hold a conditional branch:

  1. Find each side's CR PRODUCER by walking back along that side's own
     fall-through chain to the instruction that last wrote the tested CR field
     (`cmp*`, `fcmpu`, or a record-form `x.` for cr0).
  2. Turn producer + branch into a PREDICATE: the set of compare outcomes
     (LT/GT/EQ, plus UN for a float compare) on which the branch is TAKEN.  A
     register-vs-immediate integer compare is widened into the exact set of
     32-bit VALUES that take the branch, so `x < 5` and `x <= 4` are the same
     predicate and `unsigned x > 0` is the same predicate as `x != 0`.
  3. Resolve the SUCCESSORS on BLOCKS, not on mnemonics.  Both functions are cut
     into basic blocks; blocks are paired across sides by register-blind
     content (unique exact signature first, then shared identical rows, then a
     mutual-best fuzzy match).  The target's taken/fall-through successors are
     mapped through that pairing:  `same` (taken<->taken) or `swapped`
     (taken<->fall-through).  Block REORDERING inverts the condition AND swaps
     the successors, so under `swapped` our predicate is negated before
     comparing.  A real inversion flips the test and leaves the arms alone.
  4. For a register-register compare, prove the OPERAND ORDER before comparing:
     `cmpw a,b; blt` and `cmpw b,a; bgt` are the same test.  The order is proven
     by value provenance through the row alignment (the target instruction that
     defines `a` is aligned with the one of ours that defines the register in
     our first operand).  Unproven order is carried as a caveat on the row, and
     a difference that an operand swap would explain is not reported as a bug.

Every pair lands in exactly one bucket:

  agree              same predicate on the same successors (incl. every layout
                     inversion, operand swap, and `x>0`/`x!=0` unsigned)
  INVERTED           our predicate is the COMPLEMENT of the target's   (class 2)
  OFF-BY-ONE         the two predicates differ on exactly ONE value --
                     `<` vs `<=` against the same immediate, or `N` vs `N+-1`
                     under the same relation                           (class 3)
  STRICTNESS         reg-reg / float: differ only on the EQUAL outcome (class 3)
  SIGNEDNESS         differ only on values with the sign bit set, or a reg-reg
                     ordering test whose signedness differs            (class 2/3)
  DIRECTION          reg-reg `<` vs `>` with the operand order PROVEN  (class 2)
  OTHER-PREDICATE    any other difference
  NAN-ONLY           float: differ only on the unordered outcome -- recorded,
                     not a finding (`!(a<b)` vs `a>=b`)

LEAD buckets -- listed on every run, never counted as findings, each one the
home of a measured false positive:

  ORIENTATION-CONFLICT  block pairing and the arms' distinguishing effects
                     disagree about which successor is which
                     (CacheXbox::ThreadGetDir: two destructor runs differing only
                     in the returned value)
  UNPAIRED-DIFFERS   blocks did not pair; a DECISIVE arm-effects reading
                     oriented the row and the predicates still differ
  PRODUCER-SHAPE     integer vs float producer, or immediate vs register: the
                     row pairing matched two different tests
  LOOP-LOWERING      a backward decrement-and-test latch against a bound compare
  SELECT             DIRECTION/STRICTNESS where an arm is 1-2 pure register
                     moves: min/max spelled a<b?a:b vs b<a?b:a (JoypadPollCommon)

An OFF-BY-ONE whose single differing value v is assigned v by the other arm on
BOTH sides is a clamp and reads `agree` (libvorbis seed_curve: min(choice,7)).

and, separately, rows that are not a PAIR of conditional branches:

  ONE-SIDED          a conditional branch on one side only (missing / extra
                     condition).  A LEAD, never a finding: an inline boundary,
                     a peeled loop, a CTR loop, a tail merge or cross-jump
                     produce exactly this shape -- 11 of 11 hand-checked on
                     2026-09-30 were one of those.

WHAT IT CANNOT SEE  (read before calling the class exhausted)
-----------------------------------------------------------
* **A condition that is not a branch.**  MSVC if-converts small conditionals
  into branch-free masks: `subic/subfe` (x != 0 as a -1/0 mask), `cntlzw/extrwi`
  (x == 0 as 0/1), `fsel`, `neg/srawi` sign masks.  The real bug
  `UIManager::IsGameScreenActive` (`4b1bffd5a`) was exactly this -- `==` spelled
  `!=` -- and has no conditional branch at all.  Manual recognizer: a replace
  row pairing `subic`+`subfe` with `cntlzw`+`extrwi` (or `fsel` operands
  swapped) is a POLARITY question, not register noise.
* **A wrong condition in a function we have not written** (report 0%, no base
  symbol) or in a TU that does not build.  Counted as drops.
* **A different value under the same test** (`cmpwi r3,5` where r3 is the wrong
  field).  That is class 1; `this_offset_scan.py`.
* **The CTR trip count.**  `bdnz` pairs are counted `agree-ctr`: the count lives
  in the `mtctr` operand, which this scanner does not trace.  A CTR loop on one
  side and a compare loop on the other is a lowering difference (see
  `docs/decomp/patterns/` on hand-rotated loops) and is dropped with a reason.
* **Restructured control flow.**  When the arms were rewritten, not just
  flipped, the blocks do not pair and the row drops as
  `successor-blocks-unpaired`.  That drop is where the rest of class 2 lives;
  it is counted on every run so nobody reads this scanner's zero as the class's.

PROOF OF ABSENCE AT 100%
------------------------
An inverted branch changes the branch mnemonic, a strictness or signedness bug
changes the compare opcode or the branch mnemonic, and an off-by-one changes an
immediate.  `match_percent_normalized` forgives register permutation and
relocation names only, so a function scoring EXACTLY 100.0 there (unrounded
f32) cannot carry any of the shapes above.  Those functions are dropped as
`exact-100-normalized` -- a proof, not a blind spot.  The two-sided sabotage
control in the pattern doc is what makes that claim measured rather than
argued: the sabotaged function leaves the 100% set and lands in INVERTED.

USAGE
-----
    python3 scripts/analysis/cond_semantics_scan.py --selftest
    python3 scripts/analysis/cond_semantics_scan.py
    python3 scripts/analysis/cond_semantics_scan.py --json out.json --show-one-sided
    python3 scripts/analysis/cond_semantics_scan.py --explain '<mangled symbol>'

Exit code is the worse of the two CoverageReport.emit() codes (functions, rows).
"""
from __future__ import annotations

import argparse
import difflib
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_REPORT = os.path.join(REPO, "build", "373307D9", "report.json")
DEFAULT_OBJDIFF_CLI = os.path.join(REPO, "bin", "objdiff-cli")

# --------------------------------------------------------------------------- #
# Instruction model (objdiff's rendered text; one side of one row)
# --------------------------------------------------------------------------- #
# The UNIVERSE of "conditional branch" rows is decided by this regex, which is
# deliberately broader than the parser table below: any `b*` mnemonic that is
# not one of the unconditional forms.  A mnemonic the parser does not know
# therefore lands in the universe and is DROPPED with a reason, rather than
# silently falling out of the denominator.
UNCONDITIONAL = frozenset({"b", "bl", "blr", "bctr", "bctrl", "blrl", "ba", "bla"})
COND_UNIVERSE_RE = re.compile(r"^b[a-z]*[+-]?$")


def is_cond_branch_text(op):
    return bool(op) and bool(COND_UNIVERSE_RE.match(op)) and op not in UNCONDITIONAL


# mnemonic stem -> (cr bit, branch-if-bit-is-set)
BRANCH_STEM = {
    "lt": ("LT", True), "ge": ("LT", False), "nl": ("LT", False),
    "gt": ("GT", True), "le": ("GT", False), "ng": ("GT", False),
    "eq": ("EQ", True), "ne": ("EQ", False),
    "so": ("SO", True), "ns": ("SO", False),
    "un": ("SO", True), "nu": ("SO", False),
}
CALLS = frozenset({"bl", "bctrl", "blrl", "bla"})
VOLATILE_CR = frozenset({0, 1, 5, 6, 7})
VOLATILE_GPR = frozenset({0} | set(range(3, 13)))
VOLATILE_FPR = frozenset(range(0, 14))
NO_DEST_PREFIX = ("st", "cmp", "fcmp", "b", "mt", "dcb", "icb", "sync", "isync",
                  "eieio", "tw", "td", "nop", "lwsync", "trap")

REG_RE = re.compile(r"^(r|f|v)(\d+)$")
CR_RE = re.compile(r"^cr([0-7])$")
IMM_RE = re.compile(r"^-?(0x[0-9a-fA-F]+|\d+)$")
MEM_RE = re.compile(r"^(-?(?:0x[0-9a-fA-F]+|\d+))\((r\d+)\)$")


def split_args(s):
    return [a.strip() for a in s.split(",")] if s else []


def parse_imm(a):
    neg = a.startswith("-")
    body = a[1:] if neg else a
    v = int(body, 16) if body.lower().startswith("0x") else int(body)
    return -v if neg else v


def dest_reg(ins):
    """The register this instruction writes, or None.  Text-based and
    conservative; used ONLY for operand provenance, never for a verdict."""
    op = ins["opcode"]
    if op.startswith(NO_DEST_PREFIX):
        return None
    args = split_args(ins.get("args", ""))
    if args and REG_RE.match(args[0]):
        return args[0]
    return None


def cr_written(ins):
    """CR field number this instruction writes, or None."""
    op = ins["opcode"]
    args = split_args(ins.get("args", ""))
    if op.startswith("cmp") or op.startswith("fcmp"):
        m = CR_RE.match(args[0]) if args else None
        return int(m.group(1)) if m else 0
    if op.endswith(".") and not op.startswith("st"):
        return 1 if op.startswith("f") else 0
    if op in ("mtcrf", "mtcr", "mcrf", "mcrxr") or op.startswith("cr"):
        return -1           # writes CR in a way this model does not follow
    return None


def parse_branch(ins):
    """{'kind': 'cond'|'ctr'|'ctr-cond', 'cr', 'bit', 'sense', 'dest'|None,
    'ret': bool} or None when the mnemonic is not understood."""
    op = ins["opcode"].rstrip("+-")
    args = split_args(ins.get("args", ""))
    dest = None
    for a in args:
        if IMM_RE.match(a) and a.startswith("0x"):
            dest = int(a, 16)
    if op in ("bdnz", "bdz"):
        return {"kind": "ctr", "op": op, "dest": dest, "ret": False}
    if op in ("bdnzf", "bdnzt", "bdzf", "bdzt"):
        return {"kind": "ctr-cond", "op": op, "dest": dest, "ret": False}
    ret = False
    stem = op[1:]
    if stem.endswith("lr"):
        ret, stem = True, stem[:-2]
    elif stem.endswith("ctr"):
        return {"kind": "ctr-cond", "op": op, "dest": None, "ret": False}
    if stem not in BRANCH_STEM:
        return None
    cr = 0
    for a in args:
        m = CR_RE.match(a)
        if m:
            cr = int(m.group(1))
    bit, sense = BRANCH_STEM[stem]
    if not ret and dest is None:
        return None
    return {"kind": "cond", "op": op, "cr": cr, "bit": bit, "sense": sense,
            "dest": dest, "ret": ret}


def parse_producer(ins):
    """Predicate source for a CR field.
    ('icmp', signed, width, lhs, rhs) with lhs/rhs ('reg', name)|('imm', N),
    ('fcmp', lhs, rhs), or None."""
    op = ins["opcode"]
    args = split_args(ins.get("args", ""))
    if args and CR_RE.match(args[0]):
        args = args[1:]
    if op in ("cmpw", "cmplw", "cmpd", "cmpld", "cmpwi", "cmplwi", "cmpdi", "cmpldi"):
        if len(args) != 2 or not REG_RE.match(args[0]):
            return None
        signed = "l" not in op[3:]
        width = 64 if "d" in op[3:] else 32
        if op.endswith("i"):
            if not IMM_RE.match(args[1]):
                return None
            return ("icmp", signed, width, ("reg", args[0]), ("imm", parse_imm(args[1])))
        if not REG_RE.match(args[1]):
            return None
        return ("icmp", signed, width, ("reg", args[0]), ("reg", args[1]))
    if op in ("fcmpu", "fcmpo"):
        if len(args) != 2:
            return None
        return ("fcmp", ("reg", args[0]), ("reg", args[1]))
    if op.endswith(".") and not op.startswith("f") and not op.startswith("st"):
        # record form: result compared SIGNED against 0.  64-bit ops set cr0 on
        # the 64-bit result; every 32-bit op on the low word.
        d = dest_reg(ins)
        if d is None:
            return None
        width = 64 if op.startswith(("rld", "sld", "srd", "srad", "extsw", "mulld",
                                     "divd", "cntlzd")) else 32
        return ("icmp", True, width, ("reg", d), ("imm", 0))
    return None


# --------------------------------------------------------------------------- #
# Predicates
# --------------------------------------------------------------------------- #
INT_OUT = ("LT", "GT", "EQ")
FLT_OUT = ("LT", "GT", "EQ", "UN")
MIRROR = {"LT": "GT", "GT": "LT", "EQ": "EQ", "UN": "UN"}


def outcome_set(bit, sense, is_float):
    outs = FLT_OUT if is_float else INT_OUT
    b = "UN" if (bit == "SO" and is_float) else bit
    return frozenset(o for o in outs if (o == b) == sense)


def mirror(s):
    return frozenset(MIRROR[o] for o in s)


def _merge(iv):
    iv = sorted(iv)
    out = []
    for lo, hi in iv:
        if lo > hi:
            continue
        if out and lo <= out[-1][1] + 1:
            out[-1] = (out[-1][0], max(out[-1][1], hi))
        else:
            out.append((lo, hi))
    return tuple(out)


def value_set(signed, width, n, outs):
    """Exact set of `width`-bit VALUES (unsigned representation) for which
    `x <op> n` lands in `outs`, as merged inclusive intervals."""
    M = 1 << width
    half = M >> 1
    if signed:
        lo_d, hi_d = -half, half - 1
        n = max(min(n, hi_d + 1), lo_d - 1)
    else:
        lo_d, hi_d = 0, M - 1
        n %= M
    iv = []
    if "LT" in outs:
        iv.append((lo_d, n - 1))
    if "EQ" in outs:
        iv.append((n, n))
    if "GT" in outs:
        iv.append((n + 1, hi_d))
    res = []
    for lo, hi in iv:
        lo, hi = max(lo, lo_d), min(hi, hi_d)
        if lo > hi:
            continue
        if signed and lo < 0 <= hi:
            res.append((lo + M, M - 1))
            res.append((0, hi))
        elif signed and hi < 0:
            res.append((lo + M, hi + M))
        else:
            res.append((lo, hi))
    return _merge(res)


def iv_complement(iv, width):
    M = 1 << width
    out, cur = [], 0
    for lo, hi in iv:
        if lo > cur:
            out.append((cur, lo - 1))
        cur = hi + 1
    if cur <= M - 1:
        out.append((cur, M - 1))
    return tuple(out)


def iv_symdiff_size_and_span(a, b, width):
    """(number of values in the symmetric difference, (min, max) or None)."""
    M = 1 << width
    pts = sorted({0, M} | {lo for lo, _ in a} | {hi + 1 for _, hi in a}
                 | {lo for lo, _ in b} | {hi + 1 for _, hi in b})

    def inside(iv, x):
        return any(lo <= x <= hi for lo, hi in iv)
    n, span = 0, None
    for x0, x1 in zip(pts, pts[1:]):
        if inside(a, x0) != inside(b, x0):
            n += x1 - x0
            span = (x0 if span is None else span[0], x1 - 1)
    return n, span


def compare_predicates(pt, pb, operand_order):
    """Verdict for target predicate `pt` vs ours `pb`, both already oriented to
    the SAME successor.  Each is (producer, outcome_set).

    operand_order: 'same' | 'swapped' | 'unknown' (reg-reg only).
    Returns (bucket, detail)."""
    prod_t, st = pt
    prod_b, sb = pb
    if prod_t[0] != prod_b[0]:
        return "PRODUCER-SHAPE", "producer kinds differ (integer vs float compare)"
    if prod_t[0] == "fcmp":
        return _cmp_sets(st, sb, FLT_OUT, operand_order, True, True)
    _k, sig_t, w_t, _l, rhs_t = prod_t
    _k, sig_b, w_b, _l, rhs_b = prod_b
    if w_t != w_b:
        return "OTHER-PREDICATE", f"compare width differs ({w_t} vs {w_b})"
    if rhs_t[0] == "imm" and rhs_b[0] == "imm":
        vt = value_set(sig_t, w_t, rhs_t[1], st)
        vb = value_set(sig_b, w_b, rhs_b[1], sb)
        if vt == vb:
            return "agree", ""
        if vt == iv_complement(vb, w_t):
            return "INVERTED", "our value set is the complement of the target's"
        n, span = iv_symdiff_size_and_span(vt, vb, w_t)
        if n == 1:
            how = ("same immediate, strict vs non-strict" if rhs_t[1] == rhs_b[1]
                   else f"immediate {rhs_t[1]} vs {rhs_b[1]}")
            return "OFF-BY-ONE", f"predicates differ on exactly one value ({how})"
        half = 1 << (w_t - 1)
        if sig_t != sig_b and span and span[0] >= half:
            return "SIGNEDNESS", ("differ only on sign-bit-set values: "
                                  f"{'signed' if sig_t else 'unsigned'} target vs "
                                  f"{'signed' if sig_b else 'unsigned'} ours")
        return "OTHER-PREDICATE", f"predicates differ on {n} values"
    if rhs_t[0] == "reg" and rhs_b[0] == "reg":
        b, d = _cmp_sets(st, sb, INT_OUT, operand_order, False, sig_t == sig_b)
        return b, d
    return "PRODUCER-SHAPE", "one side compares against an immediate, the other a register"


def _cmp_sets(st, sb, outs, order, is_float, same_sign):
    full = frozenset(outs)
    ordering = lambda s: s not in (frozenset({"EQ"}), frozenset({"LT", "GT"}),
                                   frozenset({"EQ", "UN"}), frozenset({"LT", "GT", "UN"}))

    def one(sb_):
        if st == sb_:
            if not same_sign and ordering(st):
                return "SIGNEDNESS", "same relation, different signedness of an ordering test"
            return "agree", ""
        if st == full - sb_:
            return "INVERTED", "our outcome set is the complement of the target's"
        diff = st ^ sb_
        if is_float and diff == frozenset({"UN"}):
            return "NAN-ONLY", "differs only on the unordered (NaN) outcome"
        if diff == frozenset({"EQ"}) or (is_float and diff == frozenset({"EQ", "UN"})):
            return "STRICTNESS", "differs only on the EQUAL outcome (< vs <=)"
        if mirror(st) == sb_:
            return "DIRECTION", "less-than vs greater-than with the operand order held"
        return "OTHER-PREDICATE", f"outcome sets {sorted(st)} vs {sorted(sb_)}"

    if order == "same":
        return one(sb)
    if order == "swapped":
        return one(mirror(sb))
    a, b = one(sb), one(mirror(sb))
    if a[0] == "agree" or b[0] == "agree":
        return "agree", "operand order unproven; agrees under one order"
    if a[0] == "NAN-ONLY" or b[0] == "NAN-ONLY":
        return "NAN-ONLY", "operand order unproven"
    if a[0] == "DIRECTION":
        a = ("OTHER-PREDICATE", "a mirror, but the operand order is unproven, so it "
             "may be an operand swap")
    return a[0], a[1] + " [operand order unproven]"


# --------------------------------------------------------------------------- #
# One function
# --------------------------------------------------------------------------- #
def norm_sig(ins, stack=frozenset()):
    """Register-blind, address-blind signature of one instruction.

    A displacement off the stack pointer or a frame pointer (`stack`) is
    rendered `STK`: MSVC routinely shifts a whole frame's slots by 4 between two
    otherwise identical builds, and a signature that kept the raw slot offset
    matched the image's `lwz r11,0xb0(r31)` arm against OUR `0xb0` in the
    OTHER arm (RhythmBattle::OnBeat, frame shifted +4), reporting a phantom
    arm swap."""
    if ins is None:
        return None
    op = ins["opcode"]
    out = [op]
    args = split_args(ins.get("args", ""))
    if op in ("addi", "subi") and len(args) == 3 and args[1] in stack:
        return (op, "STK")
    for a in args:
        if REG_RE.match(a) or CR_RE.match(a):
            continue
        m = MEM_RE.match(a)
        if m:
            out.append("STK" if m.group(2) in stack else m.group(1))
            continue
        if op.startswith("b") and IMM_RE.match(a):
            continue                    # branch destination: an address
        out.append(a)
    return tuple(out)


class Side:
    """One side's instruction stream reconstructed from the aligned rows."""

    def __init__(self, rows, key):
        self.pos_of_row = {}
        self.ins = []           # list of (row_index, instruction)
        self.addr_pos = {}
        for r in rows:
            x = r.get(key)
            if not x:
                continue
            p = len(self.ins)
            self.pos_of_row[r["index"]] = p
            self.ins.append((r["index"], x))
            try:
                self.addr_pos[int(x["address"], 16)] = p
            except (KeyError, ValueError):
                pass
        # stack pointer + any frame pointer the prologue derives from it
        self.stack = {"r1"}
        for _r, x in self.ins[:16]:
            a = split_args(x.get("args", ""))
            if (x["opcode"] in ("addi", "subi", "mr") and len(a) >= 2
                    and a[1] == "r1" and a[0] != "r1"):
                self.stack.add(a[0])
        self.stack = frozenset(self.stack)
        self._blocks()

    def _blocks(self):
        n = len(self.ins)
        leaders = {0} if n else set()
        for p, (_r, x) in enumerate(self.ins):
            op = x["opcode"]
            if op.startswith("b") and op not in CALLS:
                if p + 1 < n:
                    leaders.add(p + 1)
                br = parse_branch(x) if op not in UNCONDITIONAL else None
                dest = None
                if op == "b":
                    for a in split_args(x.get("args", "")):
                        if a.startswith("0x"):
                            dest = int(a, 16)
                elif br:
                    dest = br.get("dest")
                if dest is not None and dest in self.addr_pos:
                    leaders.add(self.addr_pos[dest])
        self.leaders = sorted(leaders)
        self.block_of = [0] * n
        self.blocks = []
        for i, s in enumerate(self.leaders):
            e = self.leaders[i + 1] if i + 1 < len(self.leaders) else n
            self.blocks.append((s, e))
            for p in range(s, e):
                self.block_of[p] = i

    def block_sig(self, b):
        s, e = self.blocks[b]
        return tuple(self.sig(p) for p in range(s, e))

    def sig(self, p):
        return norm_sig(self.ins[p][1], self.stack)

    def thread(self, b, limit=8):
        """Follow a block that is nothing but `b X` to X's block."""
        seen = 0
        while b is not None and seen < limit:
            s, e = self.blocks[b]
            if e - s == 1 and self.ins[s][1]["opcode"] == "b":
                dest = None
                for a in split_args(self.ins[s][1].get("args", "")):
                    if a.startswith("0x"):
                        dest = int(a, 16)
                if dest is None or dest not in self.addr_pos:
                    return b
                b = self.block_of[self.addr_pos[dest]]
                seen += 1
                continue
            return b
        return b

    def producer(self, p, cr):
        """Walk back along the fall-through chain from position p for the
        instruction that last wrote CR field `cr`.  (pos, parsed) or a reason."""
        q = p - 1
        steps = 0
        while q >= 0 and steps < 64:
            x = self.ins[q][1]
            op = x["opcode"]
            if op in ("b", "blr", "bctr") or (op.startswith("b") and op.endswith("lr")
                                             and op != "blr" and False):
                return None, "cr-producer-not-on-fallthrough-chain"
            if op in CALLS and cr in VOLATILE_CR:
                return None, "cr-producer-behind-a-call"
            w = cr_written(x)
            if w == cr:
                pr = parse_producer(x)
                if pr is None:
                    return None, "cr-producer-unparsed"
                return (q, pr), None
            if w == -1:
                return None, "cr-written-by-cr-logic"
            q -= 1
            steps += 1
        return None, "cr-producer-not-found"

    def definer(self, p, reg):
        """Position of the instruction defining `reg` before position p, or
        ('call', pos) when a call clobbered it, or None."""
        m = REG_RE.match(reg)
        if not m:
            return None
        kind, num = m.group(1), int(m.group(2))
        q = p - 1
        while q >= 0 and p - q < 256:
            x = self.ins[q][1]
            if x["opcode"] in CALLS:
                vol = VOLATILE_GPR if kind == "r" else VOLATILE_FPR
                if num in vol:
                    return ("call", q)
            if dest_reg(x) == reg:
                return ("ins", q)
            q -= 1
        return None


def pair_blocks(T, B, rows):
    """Block correspondence T -> B, register-blind.  Returns (map, method)."""
    tsig = [T.block_sig(i) for i in range(len(T.blocks))]
    bsig = [B.block_sig(i) for i in range(len(B.blocks))]
    m, how = {}, {}
    # 1. unique exact signature on both sides
    tc, bc = {}, {}
    for i, s in enumerate(tsig):
        tc.setdefault(s, []).append(i)
    for i, s in enumerate(bsig):
        bc.setdefault(s, []).append(i)
    for s in sorted(tc, key=lambda k: tc[k][0]):
        if len(tc[s]) == 1 and len(bc.get(s, [])) == 1:
            m[tc[s][0]] = bc[s][0]
            how[tc[s][0]] = "exact"
    used = set(m.values())
    # 2. shared register-blind-identical rows, mutual best
    votes = {}
    for r in rows:
        t, b = r.get("target"), r.get("base")
        if not (t and b) or (T.sig(T.pos_of_row[r["index"]])
                             != B.sig(B.pos_of_row[r["index"]])):
            continue
        tb = T.block_of[T.pos_of_row[r["index"]]]
        bb = B.block_of[B.pos_of_row[r["index"]]]
        if tb in m or bb in used:
            continue
        votes.setdefault(tb, {}).setdefault(bb, 0)
        votes[tb][bb] += 1
    best_for_b = {}
    for tb in sorted(votes):
        for bb, n in sorted(votes[tb].items()):
            if n > best_for_b.get(bb, (0, None))[0]:
                best_for_b[bb] = (n, tb)
    for tb in sorted(votes):
        bb, n = max(sorted(votes[tb].items()), key=lambda kv: kv[1])
        ls = min(len(tsig[tb]), len(bsig[bb]))
        if best_for_b.get(bb, (0, None))[1] == tb and bb not in used and n * 2 >= ls:
            m[tb] = bb
            how[tb] = "rows"
            used.add(bb)
    # 3. mutual-best fuzzy on what is left
    rem_t = [i for i in range(len(tsig)) if i not in m]
    rem_b = [i for i in range(len(bsig)) if i not in used]
    if rem_t and rem_b and len(rem_t) * len(rem_b) <= 40000:
        score = {}
        for i in rem_t:
            for j in rem_b:
                score[(i, j)] = difflib.SequenceMatcher(None, tsig[i], bsig[j],
                                                        autojunk=False).ratio()
        for i in rem_t:
            cands = sorted(((score[(i, j)], j) for j in rem_b), reverse=True)
            if not cands or cands[0][0] < 0.75:
                continue
            if len(cands) > 1 and cands[0][0] - cands[1][0] < 0.1:
                continue
            j = cands[0][1]
            back = sorted(((score[(k, j)], k) for k in rem_t), reverse=True)
            if back[0][1] != i or (len(back) > 1 and back[0][0] - back[1][0] < 0.1):
                continue
            if j in used:
                continue
            m[i] = j
            how[i] = "fuzzy"
            used.add(j)
    return m, how


def successors(S, p, br):
    """(taken_block, fall_block) as block indices, 'RET', or None."""
    n = len(S.ins)
    fall = S.thread(S.block_of[p + 1]) if p + 1 < n else "END"
    if br["ret"]:
        return "RET", fall
    dest = br["dest"]
    if dest not in S.addr_pos:
        return None, fall
    return S.thread(S.block_of[S.addr_pos[dest]]), fall


def arm_tokens(S, pos, limit=48):
    """Register-blind multiset of what one ARM does: instructions from `pos`,
    following unconditional `b` inside the function, up to the next
    conditional branch, a return, or `limit` instructions."""
    from collections import Counter
    c = Counter()
    seen = set()
    n = len(S.ins)
    steps = 0
    while pos is not None and 0 <= pos < n and steps < limit and pos not in seen:
        seen.add(pos)
        x = S.ins[pos][1]
        op = x["opcode"]
        steps += 1
        if op == "b":
            dest = None
            for a in split_args(x.get("args", "")):
                if a.startswith("0x"):
                    dest = int(a, 16)
            if dest is not None and dest in S.addr_pos:
                pos = S.addr_pos[dest]
                continue
            c[S.sig(pos)] += 1            # tail call / restore helper
            break
        c[S.sig(pos)] += 1
        if op == "blr" or op in ("bctr",) or (is_cond_branch_text(op)):
            break
        pos += 1
    return c


def effects_orientation(T, B, t_taken, t_fall, b_taken, b_fall):
    """Second, independent opinion on the successor orientation, from what the
    two ARMS DO rather than from which blocks look alike.

    WHY.  Block pairing matches a block by its bulk.  MSVC tail-merges
    destructor runs, so `CacheXbox::ThreadGetDir`'s two arms are both "destroy
    the Strings" and differ only in `li r3,-1` vs `li r3,8` -- and pairing by
    bulk matched our `return 8` block against the image's `return -1` block, so
    an arm-layout difference read as an INVERTED test.  The tokens that
    DISTINGUISH one arm from the other (here, the two return values) are what
    the branch decides between; they must follow the orientation.
    Returns 'same' | 'swapped' | 'undecided'."""
    tt, tf = arm_tokens(T, t_taken), arm_tokens(T, t_fall)
    bt, bf = arm_tokens(B, b_taken), arm_tokens(B, b_fall)
    dT, dF = tt - tf, tf - tt
    dBt, dBf = bt - bf, bf - bt

    def inter(a, b):
        return sum((a & b).values())
    same = inter(dT, dBt) + inter(dF, dBf)
    swap = inter(dT, dBf) + inter(dF, dBt)
    if same > swap:
        return "same", same, swap
    if swap > same:
        return "swapped", same, swap
    return "undecided", same, swap


def _arm_pos(S, p, br, which):
    if which == "fall":
        return p + 1 if p + 1 < len(S.ins) else None
    if br["ret"]:
        return None
    return S.addr_pos.get(br["dest"])


def is_ret_block(S, b):
    if not isinstance(b, int):
        return False
    s, e = S.blocks[b]
    return any(S.ins[p][1]["opcode"] == "blr" for p in range(s, e))


def value_sig(S, pos, reg, depth=3):
    """Register-blind description of the VALUE held in `reg` just before
    position `pos`: the defining instruction's signature plus, recursively, the
    signatures of its source registers.  Incoming argument registers are named
    by the ABI, so they are their own identity; `mr` is transparent.

    WHY NOT ROW ALIGNMENT.  The first cut mapped the defining instruction to
    our side through objdiff's row pairing.  Two independent loads scheduled in
    the opposite order are paired crosswise by the row alignment
    (`lwz r11,0x10(r30)` against `lwz r27,0x0(r6)`), so that rule "proved" an
    operand swap on STLport's `_Rb_tree::insert_unique` -- whose two sides
    compare byte-identical text -- and reported it as a DIRECTION bug.  The
    value a register holds is a property of its definition, not of where
    objdiff happened to put it."""
    if depth <= 0:
        return ("?",)
    d = S.definer(pos, reg)
    if d is None:
        return ("in", reg)
    kind, q = d
    x = S.ins[q][1]
    if kind == "call":
        return ("call", x.get("args", ""), reg[0])
    op = x["opcode"]
    args = split_args(x.get("args", ""))
    if op == "mr" and len(args) == 2:
        return value_sig(S, q, args[1], depth)
    srcs = []
    for a in args[1:]:
        m = MEM_RE.match(a)
        if m:
            srcs.append(value_sig(S, q, m.group(2), depth - 1))
        elif REG_RE.match(a):
            srcs.append(value_sig(S, q, a, depth - 1))
    return (S.sig(q),) + tuple(srcs)


def operand_order(T, B, pt_pos, pb_pos, prod_t, prod_b):
    """'same' | 'swapped' | 'unknown' for a register-register compare, proven
    by what each operand register HOLDS (see value_sig), never by its name."""
    lt, rt = prod_t[-2][1], prod_t[-1][1]
    lb, rb = prod_b[-2][1], prod_b[-1][1]
    vlt, vrt = value_sig(T, pt_pos, lt), value_sig(T, pt_pos, rt)
    vlb, vrb = value_sig(B, pb_pos, lb), value_sig(B, pb_pos, rb)
    if vlt == vrt or vlb == vrb:
        return "unknown", "operands indistinguishable"
    same = (vlt == vlb) + (vrt == vrb)
    swap = (vlt == vrb) + (vrt == vlb)
    if same and not swap:
        return "same", f"value provenance {same}/2"
    if swap and not same:
        return "swapped", f"value provenance {swap}/2"
    return "unknown", "unproven"


def is_down_counter(ins):
    """`subic. rX, rX, 1` / `addic. rX, rX, -1`: a decrement-and-test loop
    latch.  The other lowering of the same loop is a compare against the bound."""
    a = split_args(ins.get("args", ""))
    if ins["opcode"] == "subic." and len(a) == 3 and a[0] == a[1] and a[2] in ("0x1", "1"):
        return True
    if ins["opcode"] == "addic." and len(a) == 3 and a[0] == a[1] and a[2] in ("-0x1", "-1"):
        return True
    return False


def _single_arm_assign(S, p, br, reg, value):
    """True when one arm of the branch at p is exactly `li reg, value` that then
    rejoins the other arm (the clamp `x = x < N ? x : N` diamond)."""
    n = len(S.ins)
    dest = S.addr_pos.get(br.get("dest")) if not br["ret"] else None
    for arm, other in ((p + 1, dest), (dest, p + 1)):
        if arm is None or other is None or not (0 <= arm < n):
            continue
        x = S.ins[arm][1]
        a = split_args(x.get("args", ""))
        if x["opcode"] != "li" or len(a) != 2 or a[0] != reg or not IMM_RE.match(a[1]):
            continue
        if parse_imm(a[1]) != value:
            continue
        nxt = arm + 1
        if nxt < n and S.ins[nxt][1]["opcode"] == "b":
            d = None
            for aa in split_args(S.ins[nxt][1].get("args", "")):
                if aa.startswith("0x"):
                    d = int(aa, 16)
            nxt = S.addr_pos.get(d)
        if nxt == other:
            return True
    return False


def _is_backward(ins, br):
    try:
        return br.get("dest") is not None and br["dest"] <= int(ins["address"], 16)
    except (KeyError, ValueError):
        return False


MOVE_OPS = frozenset({"mr", "li", "fmr", "lis"})


def _select_arm(S, p, br):
    """True when an arm of the branch at p is 1-2 pure register moves that
    rejoin the other arm: a select (min/max/ternary), not a guarded action."""
    n = len(S.ins)
    dest = S.addr_pos.get(br.get("dest")) if not br["ret"] else None
    for arm, other in ((p + 1, dest), (dest, p + 1)):
        if arm is None or other is None:
            continue
        q, k = arm, 0
        while q is not None and 0 <= q < n and k < 3 and q != other:
            op = S.ins[q][1]["opcode"]
            if op == "b":
                d = None
                for aa in split_args(S.ins[q][1].get("args", "")):
                    if aa.startswith("0x"):
                        d = int(aa, 16)
                q = S.addr_pos.get(d)
                continue
            if op not in MOVE_OPS:
                break
            q += 1
            k += 1
        if q == other and 1 <= k <= 2:
            return True
    return False


def analyse_function(fn, rcov, dropped=None):
    """Classify every conditional-branch row.  Every row the universe regex
    counts is either examined (one bucket) or dropped (one reason).  When
    `dropped` is a list, each dropped row is also appended to it, so the drop
    buckets can be READ -- they are where restructured class-2 bugs live."""
    rows = fn.get("instructions") or []
    cur_row = {}
    real = rcov

    class _Drops:
        def drop(self, reason, n=1, note=""):
            real.drop(reason, n, note=note)
            if dropped is not None:
                r = cur_row.get("r")
                dropped.append({"row": r["index"], "reason": reason,
                                "target": _txt(r.get("target")),
                                "ours": _txt(r.get("base"))})

        def examine(self, n=1):
            real.examine(n)
    rcov = _Drops()
    T, B = Side(rows, "target"), Side(rows, "base")
    bmap = None
    out = []
    for r in rows:
        t, b = r.get("target"), r.get("base")
        tc = t is not None and is_cond_branch_text(t["opcode"])
        bc = b is not None and is_cond_branch_text(b["opcode"])
        if not (tc or bc):
            continue
        cur_row["r"] = r
        # ---- one of the universe's rows from here on ---------------------- #
        if not (tc and bc):
            rcov.examine()
            out.append({"bucket": "ONE-SIDED", "row": r["index"],
                        "target": _txt(t), "ours": _txt(b),
                        "side": "target-only" if tc else "ours-only",
                        "paired_with": "nothing" if (t is None or b is None)
                        else "a non-branch"})
            continue
        brt, brb = parse_branch(t), parse_branch(b)
        if brt is None or brb is None:
            rcov.drop("unknown-branch-mnemonic",
                      note="a b* mnemonic the parser table does not know")
            continue
        if brt["kind"] == "ctr" and brb["kind"] == "ctr":
            rcov.examine()
            out.append({"bucket": "agree-ctr", "row": r["index"]})
            continue
        if brt["kind"] != "cond" or brb["kind"] != "cond":
            rcov.drop("ctr-branch-involved",
                      note="bdnz/bdzf/bctr-conditional on a side: the loop test "
                           "lives in CTR, which is not traced")
            continue
        pt_pos, pb_pos = T.pos_of_row[r["index"]], B.pos_of_row[r["index"]]
        prt, why_t = T.producer(pt_pos, brt["cr"])
        prb, why_b = B.producer(pb_pos, brb["cr"])
        if prt is None or prb is None:
            rcov.drop(why_t or why_b)
            continue
        (qt, prod_t), (qb, prod_b) = prt, prb
        is_f_t, is_f_b = prod_t[0] == "fcmp", prod_b[0] == "fcmp"
        if (brt["bit"] == "SO" and not is_f_t) or (brb["bit"] == "SO" and not is_f_b):
            rcov.drop("summary-overflow-bit-on-integer-compare")
            continue
        st = outcome_set(brt["bit"], brt["sense"], is_f_t)
        sb = outcome_set(brb["bit"], brb["sense"], is_f_b)
        # ---- successors on blocks ---------------------------------------- #
        if bmap is None:
            bmap, bhow = pair_blocks(T, B, rows)
        tk, tf = successors(T, pt_pos, brt)
        bk, bf = successors(B, pb_pos, brb)
        if tk is None or bk is None:
            rcov.drop("branch-destination-outside-function")
            continue

        def mapped(x):
            if x in ("RET", "END"):
                return x
            if is_ret_block(T, x) and x not in bmap:
                return "RET?"
            return bmap.get(x)

        def canon_b(x):
            if x in ("RET", "END"):
                return x
            if is_ret_block(B, x) and x not in set(bmap.values()):
                return "RET?"
            return x
        mtk, mtf = mapped(tk), mapped(tf)
        cbk, cbf = canon_b(bk), canon_b(bf)
        eff, e_same, e_swap = effects_orientation(
            T, B, _arm_pos(T, pt_pos, brt, "taken"), _arm_pos(T, pt_pos, brt, "fall"),
            _arm_pos(B, pb_pos, brb, "taken"), _arm_pos(B, pb_pos, brb, "fall"))
        # When the blocks do not pair, a DECISIVE arm-effects reading (at least
        # two distinguishing tokens one way, none the other) orients the row on
        # its own.  Such a row is never a finding: a non-agree verdict lands in
        # the UNPAIRED-DIFFERS lead bucket.  Anything weaker stays a drop.
        decisive = max(e_same, e_swap) >= 2 and min(e_same, e_swap) == 0
        orient_by = "blocks"
        if mtk is None or mtf is None or mtk == mtf or not (
                (mtk == cbk and mtf == cbf) or (mtk == cbf and mtf == cbk)):
            if not decisive:
                if mtk is None or mtf is None:
                    rcov.drop("successor-blocks-unpaired",
                              note="a successor block of the target branch has no "
                                   "counterpart on our side and the arms' effects "
                                   "are not decisive -- restructured arms; the "
                                   "residue of class 2 lives here")
                elif mtk == mtf:
                    rcov.drop("degenerate-successors")
                else:
                    rcov.drop("successors-map-to-a-third-block",
                              note="the target's successors pair with blocks that "
                                   "are neither of our branch's successors, and "
                                   "the arms' effects are not decisive")
                continue
            orient_by = "arm-effects-only"
            orient = eff
        elif mtk == cbk and mtf == cbf:
            orient = "same"
        else:
            orient = "swapped"
        if orient == "same":
            sb_or = sb
        else:
            sb_or = frozenset(FLT_OUT if is_f_b else INT_OUT) - sb
        order, order_how = ("n/a", "")
        if prod_t[-1][0] == "reg" and prod_b[-1][0] == "reg":
            order, order_how = operand_order(T, B, qt, qb, prod_t, prod_b)
        # A latch only when the branch is BACKWARD: `subic. r11,r11,1; beq`
        # forward is a switch case test (GetExpCode's `code - 0x80000001`).
        dc_t = is_down_counter(T.ins[qt][1]) and _is_backward(t, brt)
        dc_b = is_down_counter(B.ins[qb][1]) and _is_backward(b, brb)
        if dc_t != dc_b:
            bucket, detail = ("LOOP-LOWERING", "a decrement-and-test latch on one "
                              "side, a bound compare on the other: the same loop "
                              "lowered two ways, not a condition")
        else:
            bucket, detail = compare_predicates((prod_t, st), (prod_b, sb_or), order)
        if (bucket == "OFF-BY-ONE" and prod_t[-1][0] == "imm"
                and prod_b[-1][0] == "imm"):
            n_diff, span = iv_symdiff_size_and_span(
                value_set(prod_t[1], prod_t[2], prod_t[-1][1], st),
                value_set(prod_b[1], prod_b[2], prod_b[-1][1], sb_or), prod_t[2])
            v = span[0]
            if v >= 1 << (prod_t[2] - 1):
                v -= 1 << prod_t[2]
            if (_single_arm_assign(T, pt_pos, brt, prod_t[-2][1], v)
                    and _single_arm_assign(B, pb_pos, brb, prod_b[-2][1], v)):
                bucket, detail = ("agree", f"clamp boundary: the only differing "
                                  f"value {v} is assigned {v} by the other arm")
        if (bucket in ("DIRECTION", "STRICTNESS")
                and (_select_arm(T, pt_pos, brt) or _select_arm(B, pb_pos, brb))):
            detail = (f"{bucket} under a SELECT: an arm is a pure register move, "
                      f"so which value wins also depends on the moved operands "
                      f"(min/max spelled a<b?a:b vs b<a?b:a) -- {detail}")
            bucket = "SELECT"
        if orient_by == "arm-effects-only" and bucket in FINDING_BUCKETS:
            detail = (f"blocks did not pair; oriented by arm effects alone "
                      f"({e_same} vs {e_swap} tokens) -> would be {bucket}: {detail}")
            bucket = "UNPAIRED-DIFFERS"
        elif eff != "undecided" and eff != orient:
            # The arms' distinguishing effects say the other orientation.  Under
            # that orientation the verdict flips agree <-> INVERTED, so neither
            # verdict can be trusted: a LEAD, never a finding and never agree.
            detail = (f"block pairing says successors {orient}, the arms' "
                      f"distinguishing effects say {eff} ({e_same} vs {e_swap} "
                      f"tokens); predicate verdict under the pairing was {bucket}")
            bucket = "ORIENTATION-CONFLICT"
        rcov.examine()
        out.append({
            "bucket": bucket, "row": r["index"], "detail": detail,
            "successors": orient if orient_by == "blocks" else f"{orient} (arm effects only)",
            "arm_effects": f"{eff} ({e_same}/{e_swap})",
            "block_pairing": ",".join(sorted({bhow.get(x, "?") for x in (tk, tf)
                                              if isinstance(x, int)})),
            "operand_order": order if order == "n/a" else f"{order} ({order_how})",
            "target": f"{_txt(T.ins[qt][1])} ; {_txt(t)}",
            "ours": f"{_txt(B.ins[qb][1])} ; {_txt(b)}",
            "target_addr": t.get("address"), "ours_addr": b.get("address"),
        })
    return out


def _txt(x):
    if not x:
        return "-"
    return f"{x['opcode']} {x.get('args', '')}".strip()


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #
FINDING_BUCKETS = ("INVERTED", "OFF-BY-ONE", "STRICTNESS", "SIGNEDNESS",
                   "DIRECTION", "OTHER-PREDICATE")
LEAD_BUCKETS = ("NAN-ONLY", "ORIENTATION-CONFLICT", "PRODUCER-SHAPE",
                "LOOP-LOWERING", "SELECT", "UNPAIRED-DIFFERS")
ALL_BUCKETS = FINDING_BUCKETS + LEAD_BUCKETS + ("agree", "agree-ctr", "ONE-SIDED")


def run_batch(objdiff_cli, symbols):
    p = subprocess.run([objdiff_cli, "diff", "-p", REPO, "--batch", "-f", "json",
                        "--include-instructions"],
                       input="\n".join(symbols) + "\n", capture_output=True,
                       text=True, cwd=REPO)
    if p.returncode != 0:
        sys.stderr.write(p.stderr)
        raise SystemExit(f"objdiff-cli --batch exited {p.returncode}")
    rows = []
    for line in p.stdout.splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--report", default=DEFAULT_REPORT)
    ap.add_argument("--objdiff-cli", default=DEFAULT_OBJDIFF_CLI)
    ap.add_argument("--batch-jsonl", default=None,
                    help="reuse a saved `objdiff-cli diff --batch` JSONL instead of "
                         "running it (development only; the rows must come from "
                         "the same tree as --report)")
    ap.add_argument("--json", default=None)
    ap.add_argument("--show-one-sided", action="store_true",
                    help="list the ONE-SIDED leads per function (counted always)")
    ap.add_argument("--show-agree", action="store_true")
    ap.add_argument("--show-dropped", default=None, metavar="REASON",
                    help="list the dropped rows for one drop reason (or 'all'); "
                         "the drop buckets are where restructured arms live")
    ap.add_argument("--explain", default=None, metavar="SYMBOL",
                    help="print every classified conditional-branch row of one "
                         "function, agree rows included")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not os.path.exists(args.report):
        print(f"INCONCLUSIVE: report.json not found: {args.report}")
        return EXIT_NO_INPUT

    with open(args.report) as f:
        rep = json.load(f)

    fcov = CoverageReport("cond_semantics_scan[functions]", args=args)
    rcov = CoverageReport("cond_semantics_scan[branch rows]", args=args)
    fcov.require_examined("no sub-100% function with a body on both sides")

    # ---- universe: every function report.json lists (independent pass) ---- #
    universe = [(u.get("name", ""), fn.get("name", ""), fn)
                for u in rep.get("units", []) for fn in u.get("functions", [])]
    universe.sort(key=lambda x: (x[0], x[1]))
    fcov.universe(len(universe), "function rows in report.json")

    wanted = {}
    for unit, sym, fn in universe:
        if args.explain and sym != args.explain:
            fcov.drop("not-the---explain-symbol")
            continue
        norm = fn.get("match_percent_normalized")
        if norm is not None and norm >= 100.0:
            fcov.drop("exact-100-normalized",
                      note="unrounded f32 100.0: no opcode/immediate can differ, "
                           "so none of this scanner's shapes can be present")
            continue
        if fn.get("fuzzy_match_percent") is None:
            fcov.drop("not-defined-in-our-build",
                      note="report.json carries no fuzzy score: we emit no body, "
                           "so there is nothing to align")
            continue
        wanted.setdefault(sym, []).append(unit)

    syms = sorted(wanted)
    if args.batch_jsonl:
        with open(args.batch_jsonl) as f:
            got = [json.loads(line) for line in f if line.strip()]
        fcov.note(f"rows read from saved batch {args.batch_jsonl}, NOT a live diff")
    else:
        got = run_batch(args.objdiff_cli, syms) if syms else []
    by_key = {}
    for d in got:
        if "error" in d:
            continue
        by_key[(d.get("unit", ""), d.get("symbol", ""))] = d

    results = []
    rows_universe = 0
    per_fn = []
    for sym in syms:
        for unit in sorted(wanted[sym]):
            d = by_key.get((unit, sym))
            if d is None:
                fcov.drop("objdiff-batch-returned-no-row-for-this-unit",
                          note="not found, or a COMDAT resolved to another unit")
                continue
            if not d.get("instructions"):
                fcov.drop("objdiff-row-without-instructions")
                continue
            fcov.examine()
            n_cond = sum(1 for r in d["instructions"]
                         if (r.get("target") and is_cond_branch_text(r["target"]["opcode"]))
                         or (r.get("base") and is_cond_branch_text(r["base"]["opcode"])))
            rows_universe += n_cond
            per_fn.append((unit, sym, d))
    rcov.universe(rows_universe, "rows with a conditional branch on either side, "
                                 "in the examined functions")
    norms = {(u, fn.get("name", "")): fn.get("match_percent_normalized")
             for u, _s, fn in universe}
    dropped_rows = []
    for unit, sym, d in per_fn:
        dr = []
        for row in analyse_function(d, rcov, dr):
            row.update({"unit": unit, "symbol": sym,
                        "match_percent_normalized": norms.get((unit, sym))})
            results.append(row)
        for row in dr:
            row.update({"unit": unit, "symbol": sym,
                        "match_percent_normalized": norms.get((unit, sym))})
            dropped_rows.append(row)

    counts = {b: 0 for b in ALL_BUCKETS}
    for row in results:
        counts[row["bucket"]] = counts.get(row["bucket"], 0) + 1
    fcov.extra("buckets", counts)
    rcov.extra("buckets", counts)
    fcov.note("exact-100-normalized is a PROOF of absence for these shapes, not a "
              "blind spot (see the docstring and the sabotage control)")
    rcov.note("ONE-SIDED rows are LEADS: inline boundaries, peeled loops and CTR "
              "loops make the same shape")
    rcov.note("branch-free conditionals (subic/subfe, cntlzw, fsel masks) are NOT "
              "branches and are outside this universe entirely")

    findings = [r for r in results if r["bucket"] in FINDING_BUCKETS]
    fn_count = len({(r["unit"], r["symbol"]) for r in findings})
    print(f"CONDITION-SEMANTICS CANDIDATES: {len(findings)} rows in {fn_count} "
          f"functions, of {rcov.as_dict()['examined']} classified conditional-branch "
          f"rows in {fcov.as_dict()['examined']} examined functions "
          f"(universe {len(universe)} functions / {rows_universe} branch rows)")
    for b in ALL_BUCKETS:
        tag = ("   <- finding bucket" if b in FINDING_BUCKETS
               else "   (lead: listed, not a finding)" if b in LEAD_BUCKETS else "")
        print(f"  {b:20s}: {counts.get(b, 0)}{tag}")
    print()
    for b in FINDING_BUCKETS + LEAD_BUCKETS:
        rows = [r for r in results if r["bucket"] == b]
        if not rows:
            continue
        print(f"== {b} ({len(rows)}){'' if b in FINDING_BUCKETS else '   [LEAD]'}")
        for r in rows:
            nm = r["match_percent_normalized"]
            print(f"  {r['symbol']}  [{r['unit']}]  norm="
                  f"{'n/a' if nm is None else f'{nm:.4f}'}  row {r['row']}")
            print(f"      target: {r['target']}")
            print(f"      ours  : {r['ours']}")
            print(f"      successors={r['successors']} (blocks paired by "
                  f"{r['block_pairing']}; arm effects {r['arm_effects']})  "
                  f"operands={r['operand_order']}  -- {r['detail']}")
        print()
    if args.show_agree or args.explain:
        for r in results:
            if r["bucket"] in ("agree", "agree-ctr"):
                print(f"  [agree] {r['symbol']} row {r['row']}  {r.get('target', '')}"
                      f"  |  {r.get('ours', '')}  succ={r.get('successors', '')}")
    if args.show_dropped:
        for r in dropped_rows:
            if args.show_dropped in ("all", r["reason"]):
                nm = r["match_percent_normalized"]
                print(f"  [dropped:{r['reason']}] {r['symbol']} row {r['row']} "
                      f"norm={'n/a' if nm is None else f'{nm:.4f}'}  "
                      f"{r['target']}  |  {r['ours']}")
    one = {}
    for r in results:
        if r["bucket"] == "ONE-SIDED":
            one.setdefault((r["unit"], r["symbol"]), []).append(r)
    print(f"ONE-SIDED leads: {counts['ONE-SIDED']} rows in {len(one)} functions "
          f"(missing/extra condition shape -- NOT findings)")
    if args.show_one_sided or args.explain:
        for (u, s), rs in sorted(one.items(), key=lambda kv: (-len(kv[1]), kv[0])):
            print(f"  {len(rs):3d}  {s}  [{u}]")
            if args.explain:
                for r in rs:
                    print(f"        row {r['row']} {r['side']}: {r['target']} | {r['ours']}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"rows": results, "buckets": counts, "dropped_rows": dropped_rows,
                       "_coverage_functions": fcov.as_dict(),
                       "_coverage_rows": rcov.as_dict()}, fh, indent=2, sort_keys=True)
    rc1 = fcov.emit(sys.stdout)
    rc2 = rcov.emit(sys.stdout)
    return rc1 or rc2


# --------------------------------------------------------------------------- #
# Selftest -- synthetic rows in objdiff's own JSON shape
# --------------------------------------------------------------------------- #
def _fn(pairs):
    """pairs: list of (target_text|None, ours_text|None).  Addresses are laid
    out per side; `@N` in a branch operand means 'row N' on that side."""
    rows = []
    ta = ba = 0x1000
    taddr, baddr = {}, {}
    for i, (t, b) in enumerate(pairs):
        if t is not None:
            taddr[i] = ta
            ta += 4
        if b is not None:
            baddr[i] = ba
            ba += 4

    def ins(txt, amap, i):
        op, _, args = txt.partition(" ")
        args = re.sub(r"@(\d+)", lambda m: hex(amap[int(m.group(1))]), args)
        return {"address": hex(amap[i]), "opcode": op, "args": args}
    for i, (t, b) in enumerate(pairs):
        rows.append({"index": i,
                     "target": ins(t, taddr, i) if t is not None else None,
                     "base": ins(b, baddr, i) if b is not None else None,
                     "match_type": "equal" if t == b else "diff_op"})
    return {"instructions": rows}


def _classify(pairs):
    cov = CoverageReport("selftest", stream=open(os.devnull, "w"))
    cov.universe(10 ** 6)
    return [r["bucket"] for r in analyse_function(_fn(pairs), cov)
            if r["bucket"] != "agree-ctr"]


def selftest():
    ok = True

    def check(label, got, want):
        nonlocal ok
        good = got == want
        print(f"  {'PASS' if good else 'FAIL'}  {label}" + ("" if good else f"   got {got}"))
        ok = ok and good

    body = [("lwz r11, 0x10(r3)", "lwz r11, 0x10(r3)")]
    tail = [("li r3, 1", "li r3, 1"), ("blr", "blr"),
            ("li r3, 0", "li r3, 0"), ("blr", "blr")]
    # if (x == 0) return 0; else return 1   -- target: beq to the `li 0` block
    check("identical branch -> agree",
          _classify(body + [("cmpwi r11, 0x0", "cmpwi r11, 0x0"),
                            ("beq @5", "beq @5")] + tail), ["agree"])
    check("inverted test, SAME successors -> INVERTED (the bug shape)",
          _classify(body + [("cmpwi r11, 0x0", "cmpwi r11, 0x0"),
                            ("beq @5", "bne @5")] + tail), ["INVERTED"])
    # layout inversion: ours tests bne and lays the arms out the other way round
    lay = body + [("cmpwi r11, 0x0", "cmpwi r11, 0x0"), ("beq @5", "bne @5"),
                  ("li r3, 1", "li r3, 0"), ("blr", "blr"),
                  ("li r3, 0", "li r3, 1"), ("blr", "blr")]
    check("inverted test AND swapped arms (block reordering) -> agree, NOT a finding",
          _classify(lay), ["agree"])
    swapped_only = body + [("cmpwi r11, 0x0", "cmpwi r11, 0x0"), ("beq @5", "beq @5"),
                           ("li r3, 1", "li r3, 0"), ("blr", "blr"),
                           ("li r3, 0", "li r3, 1"), ("blr", "blr")]
    check("same test, arms swapped -> INVERTED (the arm-swap bug shape)",
          _classify(swapped_only), ["INVERTED"])
    check("strict vs non-strict against the same immediate -> OFF-BY-ONE",
          _classify(body + [("cmpwi r11, 0x8", "cmpwi r11, 0x8"),
                            ("blt @5", "ble @5")] + tail), ["OFF-BY-ONE"])
    check("`x < 5` vs `x <= 4` is the same predicate -> agree",
          _classify(body + [("cmpwi r11, 0x5", "cmpwi r11, 0x4"),
                            ("blt @5", "ble @5")] + tail), ["agree"])
    check("immediate N vs N+1 under the same relation -> OFF-BY-ONE",
          _classify(body + [("cmpwi r11, 0x5", "cmpwi r11, 0x6"),
                            ("blt @5", "blt @5")] + tail), ["OFF-BY-ONE"])
    check("signed vs unsigned ordering against 5 -> SIGNEDNESS",
          _classify(body + [("cmpwi r11, 0x5", "cmplwi r11, 0x5"),
                            ("blt @5", "blt @5")] + tail), ["SIGNEDNESS"])
    check("cmpwi vs cmplwi on an EQUALITY test is irrelevant -> agree",
          _classify(body + [("cmpwi cr6, r11, 0x0", "cmplwi r11, 0x0"),
                            ("beq cr6, @5", "beq @5")] + tail), ["agree"])
    check("unsigned `x > 0` is `x != 0` (fixable-comparison pattern) -> agree",
          _classify(body + [("cmplwi r11, 0x0", "cmplwi r11, 0x0"),
                            ("bne @5", "bgt @5")] + tail), ["agree"])
    check("record-form clrlwi. vs cmplwi 0 on an equality -> agree",
          _classify(body + [("clrlwi. r0, r11, 24", "cmplwi r11, 0x0"),
                            ("beq @5", "beq @5")] + tail), ["agree"])
    two = [("lwz r10, 0x10(r3)", "lwz r10, 0x10(r3)"),
           ("lwz r11, 0x14(r3)", "lwz r11, 0x14(r3)")]
    check("cmpw a,b; blt  vs  cmpw b,a; bgt (operands swapped, PROVEN) -> agree",
          _classify(two + [("cmpw r10, r11", "cmpw r11, r10"),
                           ("blt @6", "bgt @6")] + tail), ["agree"])
    check("cmpw a,b; blt  vs  cmpw a,b; bgt (order PROVEN held) -> DIRECTION",
          _classify(two + [("cmpw r10, r11", "cmpw r10, r11"),
                           ("blt @6", "bgt @6")] + tail), ["DIRECTION"])
    cross = [("lwz r11, 0x10(r30)", "lwz r27, 0x0(r6)"),
             ("lwz r27, 0x0(r6)", "lwz r11, 0x10(r30)")]
    check("identical `cmpw r11,r27; ble` after two loads the ROW alignment pairs "
          "crosswise -> agree (the _Rb_tree::insert_unique false DIRECTION)",
          _classify(cross + [("cmpw cr6, r11, r27", "cmpw cr6, r11, r27"),
                             ("ble cr6, @6", "ble cr6, @6")] + tail), ["agree"])
    check("cmpw a,b; blt vs ble -> STRICTNESS",
          _classify(two + [("cmpw r10, r11", "cmpw r10, r11"),
                           ("blt @6", "ble @6")] + tail), ["STRICTNESS"])
    check("cmpw vs cmplw on an ordering test -> SIGNEDNESS",
          _classify(two + [("cmpw r10, r11", "cmplw r10, r11"),
                           ("blt @6", "blt @6")] + tail), ["SIGNEDNESS"])
    ftwo = [("lfs f0, 0x10(r3)", "lfs f0, 0x10(r3)"),
            ("lfs f13, 0x14(r3)", "lfs f13, 0x14(r3)")]
    check("fcmpu blt vs bge -> INVERTED",
          _classify(ftwo + [("fcmpu cr6, f0, f13", "fcmpu cr6, f0, f13"),
                            ("blt cr6, @6", "bge cr6, @6")] + tail), ["INVERTED"])
    check("fcmpu a,b bge vs fcmpu b,a ble (operand swap) -> agree",
          _classify(ftwo + [("fcmpu cr6, f0, f13", "fcmpu cr6, f13, f0"),
                            ("bge cr6, @6", "ble cr6, @6")] + tail), ["agree"])
    check("a compare with no branch counterpart -> ONE-SIDED lead",
          _classify(body + [("cmpwi r11, 0x0", None), ("beq @5", None)] + tail),
          ["ONE-SIDED"])
    check("NEGATIVE CONTROL: repairing the inverted row returns it to agree",
          _classify(body + [("cmpwi r11, 0x0", "cmpwi r11, 0x0"),
                            ("bne @5", "bne @5")] + tail), ["agree"])
    # CacheXbox::ThreadGetDir: two destructor-run arms that differ only in the
    # returned value; bulk pairing matches our `return 8` arm with the image's
    # `return -1` arm.  The distinguishing tokens must overrule it.
    tgd = [("clrlwi. r11, r3, 24", "clrlwi. r11, r3, 24"), ("bne @5", "beq @5"),
           ("bl dtor", "bl dtor"), ("li r3, 0x8", "li r3, -0x1"), ("blr", "blr"),
           ("bl dtor", "bl dtor"), ("bl dtor", "bl dtor"),
           ("li r3, -0x1", "li r3, 0x8"), ("blr", "blr")]
    check("arms paired by bulk but their distinguishing effects swapped -> "
          "ORIENTATION-CONFLICT, NOT INVERTED (the ThreadGetDir false positive)",
          _classify(tgd), ["ORIENTATION-CONFLICT"])
    clamp = [("lwz r11, 0x10(r3)", "lwz r11, 0x10(r3)"),
             ("cmpwi cr6, r11, 0x7", "cmpwi cr6, r11, 0x7"),
             ("blt cr6, @4", "ble cr6, @4"), ("li r11, 0x7", "li r11, 0x7"),
             ("stw r11, 0x14(r3)", "stw r11, 0x14(r3)"), ("blr", "blr")]
    check("`x<7 ? x : 7` vs `x<=7 ? x : 7` is the same clamp -> agree "
          "(libvorbis seed_curve)", _classify(clamp), ["agree"])
    clamp_bug = [c if c[0] != "li r11, 0x7" else ("li r11, 0x6", "li r11, 0x6")
                 for c in clamp]
    check("...but the same strictness change guarding a DIFFERENT value stays "
          "OFF-BY-ONE", _classify(clamp_bug), ["OFF-BY-ONE"])
    loop = [("li r11, 0x0", "li r10, 0xa"), ("stw r0, 0x0(r9)", "stw r0, 0x0(r9)"),
            ("addi r9, r9, 0x4", "addi r9, r9, 0x4"),
            ("addi r11, r11, 0x1", "subic. r10, r10, 0x1"),
            ("cmpwi cr6, r11, 0xa", None),
            ("blt cr6, @1", "bne @1"), ("blr", "blr")]
    check("a bound-compare latch vs a decrement-and-test latch -> LOOP-LOWERING",
          _classify(loop), ["LOOP-LOWERING"])
    sel = [("lwz r7, 0x0(r26)", "lwz r9, 0x0(r10)"),
           ("lwz r9, 0x0(r10)", "lwz r7, 0x0(r26)"),
           ("cmpw cr6, r7, r9", "cmpw cr6, r9, r7"),
           ("mr r9, r26", "mr r9, r10"), ("blt cr6, @6", "blt cr6, @6"),
           ("mr r9, r10", "mr r9, r26"), ("lwz r30, 0x0(r9)", "lwz r30, 0x0(r9)"),
           ("blr", "blr")]
    check("min() spelled a<b?a:b vs b<a?b:a -> SELECT lead, not DIRECTION "
          "(JoypadPollCommon)", _classify(sel), ["SELECT"])
    # -- live corpus: two adjudicated rows, pinned ----------------------------
    live = {"?CacheResource@@YAPBDPBDAAW4CacheResourceResult@@@Z": (28, "agree"),
            "?ThreadGetDir@CacheXbox@@IAAHVString@@0@Z": (143, "ORIENTATION-CONFLICT"),
            # blocks do not pair; oriented by arm effects alone, adjudicated:
            # `if (!n) return 0;` laid out inline vs out of line
            "?SetHighlightID@NavListSort@@QAA_NPAVDataArray@@@Z": (11, "agree")}
    if os.path.exists(DEFAULT_OBJDIFF_CLI) and os.path.exists(DEFAULT_REPORT):
        got = {d.get("symbol"): d for d in run_batch(DEFAULT_OBJDIFF_CLI, sorted(live))
               if "error" not in d}
        for sym, (row, want) in sorted(live.items()):
            d = got.get(sym)
            if d is None:
                print(f"  SKIP  live: {sym} not diffable here -- NOT a pass")
                continue
            cov = CoverageReport("selftest-live", stream=open(os.devnull, "w"))
            cov.universe(10 ** 6)
            rows = {r["row"]: r["bucket"] for r in analyse_function(d, cov)}
            check(f"live: {sym} row {row} -> "
                  f"{want} (a bne/beq pair adjudicated by hand against the listing)",
                  rows.get(row), want)
    else:
        print("  SKIP  live-corpus checks (objdiff-cli or report.json absent) -- "
              "the classifier was exercised, the live extractor was not")
    # value-set arithmetic pins
    check("value_set: signed x<0 is exactly the sign-bit-set half",
          value_set(True, 32, 0, frozenset({"LT"})), ((1 << 31, (1 << 32) - 1),))
    check("value_set: unsigned x<0 is empty", value_set(False, 32, 0, frozenset({"LT"})), ())
    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
