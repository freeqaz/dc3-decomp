#!/usr/bin/env python3
"""serializer_field_trace.py -- Save/Load field-order desync (taxonomy class 7).

WHY THIS EXISTS
===============
A `BEGIN_SAVES`/`BEGIN_LOADS` block whose field order or field SET disagrees with
the shipped image corrupts every object of that type on disk, and it is cheap for
the match ruler to miss:

  * `StreamRenderer::Save` wrote `mPlayer1/2/3DepthColor` TWICE and never wrote
    players 4-6 (`6fa132015`).  Same number of stores, same shape, same call
    sequence -- only three immediates differed.
  * `CharUpperTwist::Load` and `RndFlare::Load` both hid under a *displayed*
    100.0 (`docs/decomp/patterns/rounded-100-hides-real-bugs.md`).

The class is a FIELD-IDENTITY bug, and the instrument for a field-identity bug is
the sequence of `this`-relative offsets, not a percentage.

WHAT IT DOES
------------
For every `Save`/`Load`/`Copy` body we build, and the same-named body in the
paired dtk-split target object, it emits the ORDERED SEQUENCE of `this`-relative
memory offsets and compares the two:

    sequences equal                      -> IDENTICAL
    same multiset, different order       -> FIELD_ORDER_DIFF   (the permutation)
    multiset differs                     -> FIELD_SET_DIFF     (missing/dup field)

Both sides are disassembled from raw COFF bytes.  It runs no objdiff, reads no
report.json and no decomp.db, so no ruler, cache or pairing heuristic sits
between the bytes and the verdict.

THE TRAP THIS TOOL IS BUILT TO AVOID
------------------------------------
⚠ **The `this` register varies.**  It is r31 in `RndFlare`/`CharUpperTwist`/
`StreamRenderer`, and **r30 in `CharFeedback` and `RndText`**, where r31 is the
frame pointer (`addi r31, r1, -0x140`).  A detector that assumes r31 rebuilds the
documented `FxSendChorus::Load` false positive, in which `run_analyze_function`
resolved pure `(r1)` STACK SLOTS against the class struct and sent a lane hunting
a member that does not exist.

So `this` is derived, per side, per function, from the instruction stream:

  * r3 holds `this` at entry, and STOPS holding it at the first `bl` or the first
    write to r3 -- which is what keeps `Copy`'s `dynamic_cast` result (also
    returned in r3) from being mistaken for the receiver.
  * an alias is created only by `mr rN, rThis` (`or rA,rS,rS`).  It is NOT
    propagated through `addi`, because `addi rD, rThis, -0x44` materialises a
    FIELD ADDRESS -- that is the `subi rN, r31, off` fingerprint the RndFlare bug
    was found by -- and treating it as a new receiver would double-count the
    field.
  * every instruction's written-GPR set is computed, and an alias that is written
    stops being an alias at that instruction.  An unknown opcode is not assumed
    harmless: it drops the function (`undecodable-instruction`).
  * r1 and any register derived from r1 is never a receiver, so a stack slot can
    not be rendered as a field.

WHAT THIS TOOL CANNOT SEE  (read before calling this class exhausted)
--------------------------------------------------------------------
1. **A stream read whose destination is a stack local.**  This is not a corner
   case -- it is half of the two worked examples.  `CharFeedback::Load`
   (`e1a9425e5`) under-read the rev 3-5 arm by four bytes into a throwaway
   `int x`; the missing read touched NO member, so it leaves no `this`-relative
   footprint and this tool is structurally blind to it.  The manual recognizer is
   in the pattern doc; the axis that would see it (the `bl` callee sequence) is
   measured below and REFUTED.
2. **A TU we do not build.**  Coverage is stated on every run as paired objects
   out of target objects.
3. **A field whose offset we also get wrong.**  If our struct layout is wrong the
   two sides disagree for a different reason; the row is reported, and the
   adjudication is the reader's.
4. **Ordering the compiler is free to choose.**  Two adjacent accesses with no
   call between them can be scheduled either way, so a FIELD_ORDER_DIFF of
   ADJACENT offsets is weaker evidence than a set difference.  The rendered row
   prints the edit so the reader can tell.

MEASURED AND REFUTED: the callee-name axis
------------------------------------------
The obvious complement -- compare the ordered `bl` callee names -- does not work
on this binary and must not be added back.  `CharUpperTwist::Save` calls
`??$?6VRndTransformable@@...` in our object and `??$?6VRndCamAnim@@...` in the
target: one ICF-folded template body under two names.  The names disagree on a
function that is byte-identical.  Callee-name work belongs to class 6, which has
seven tools (`reloc_name_gate.py`, `callee_emitted_anywhere.py`, ...).

USAGE
-----
    python3 scripts/analysis/serializer_field_trace.py --selftest
    python3 scripts/analysis/serializer_field_trace.py
    python3 scripts/analysis/serializer_field_trace.py --json out.json
"""
from __future__ import annotations

import argparse
import collections
import glob
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from coverage import CoverageReport, add_coverage_args, EXIT_NO_INPUT  # noqa: E402
from coff_bodies import function_bodies  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SERIALIZER_PREFIXES = ("?Save@", "?Load@", "?Copy@")
#: MSVC virtual-adjustor thunks (`?Save@C@@$4PPPPPPPM@A@...`).  Three instructions
#: that add a constant to r3 and jump; no field access of their own.
THUNK_MARKERS = ("@$4", "@$R", "@$B", "@$0")

VOLATILE_GPRS = frozenset([0] + list(range(3, 13)))

#: MSVC's register save/restore helpers are called with `bl` but are NOT ABI
#: calls: they spill/reload GPRs, FPRs and VMX registers to the frame and
#: preserve every argument register.  `this` survives them in r3.
#:
#: Getting this wrong is not hypothetical.  The first cut of this tool cleared
#: r3 at `bl __savegprlr_24` -- the SECOND instruction of most serializers --
#: so the `mr r31, r3` four instructions later established nothing and the
#: whole function traced EMPTY.  `CharUpperTwist::Load` and
#: `StreamRenderer::Save` both came back with zero field references and a clean
#: bill of health.  CLAUDE.md records the identical defect in the unicorn
#: harness, where stubbing these helpers zeroed `this` and overstated real bugs
#: by ~8x.  An empty trace compares equal to an empty trace: it is the
#: quiet-success failure this whole class of tooling exists to prevent.
SAVE_RESTORE_HELPER_PREFIXES = ("__savegpr", "__restgpr", "__savefpr", "__restfpr",
                                "__savevmx", "__restvmx")


def is_save_restore_helper(name) -> bool:
    return bool(name) and name.startswith(SAVE_RESTORE_HELPER_PREFIXES)

# --------------------------------------------------------------------------- #
# A deliberately small PPC decoder.
#
# It answers exactly two questions -- "which GPRs does this instruction write?"
# and "is this a `this`-relative memory reference?" -- and REFUSES (returns None)
# on anything it does not recognise, so an unknown encoding drops the function
# instead of being silently assumed harmless.
# --------------------------------------------------------------------------- #

# primary opcode -> (mnemonic, is_update_form)
DFORM_MEM = {
    32: ("lwz", False), 33: ("lwzu", True), 34: ("lbz", False), 35: ("lbzu", True),
    36: ("stw", False), 37: ("stwu", True), 38: ("stb", False), 39: ("stbu", True),
    40: ("lhz", False), 41: ("lhzu", True), 42: ("lha", False), 43: ("lhau", True),
    44: ("sth", False), 45: ("sthu", True),
    46: ("lmw", False), 47: ("stmw", False),
    48: ("lfs", False), 49: ("lfsu", True), 50: ("lfd", False), 51: ("lfdu", True),
    52: ("stfs", False), 53: ("stfsu", True), 54: ("stfd", False), 55: ("stfdu", True),
}
#: these write a GPR (the rD field); the float ones write an FPR, not a GPR
DFORM_MEM_WRITES_GPR = {32, 33, 34, 35, 40, 41, 42, 43, 46}
DFORM_MEM_STORE = {36, 37, 38, 39, 44, 45, 47, 52, 53, 54, 55}

#: DS-form: ld/ldu/lwa (58) and std/stdu (62).  Low two bits are the sub-opcode.
DSFORM = {58: "ld", 62: "std"}

#: D-form arithmetic/logical that write the rD field (bits 21-25)
DFORM_WRITE_RD = {7, 8, 12, 13, 14, 15}
#: D-form logical that write the rA field (bits 16-20)
DFORM_WRITE_RA = {24, 25, 26, 27, 28, 29}
#: rotate forms, all write rA
ROTATE_WRITE_RA = {20, 21, 23, 30}
#: no GPR written
NO_GPR_WRITE = {10, 11, 16, 17, 18, 19, 59, 63}

#: X-form (primary 31) extended opcodes that write the rA field
X31_WRITE_RA = frozenset({
    24, 26, 27, 28, 58, 60, 122, 124, 154, 202, 284, 316, 412, 413, 444, 476,
    536, 539, 794, 792, 824, 826, 922, 954, 986,
})
#: X-form (primary 31) extended opcodes that write the rD field
X31_WRITE_RD = frozenset({
    8, 9, 10, 11, 19, 20, 21, 23, 40, 53, 55, 84, 87, 88, 104, 119, 136, 138,
    139, 151, 183, 200, 202 - 202, 215, 232, 233, 234, 235, 247, 266, 279, 282,
    310, 339, 341, 343, 373, 375, 407, 439, 457, 459, 489, 491, 747, 758, 918,
})
#: X-form (primary 31) extended opcodes that write NO GPR (compares, stores,
#: mtspr, cache ops, traps, condition-register ops)
X31_NO_WRITE = frozenset({
    0, 4, 32, 54, 86, 144, 146, 150, 151 - 151, 181, 210, 214, 242, 246, 274,
    278, 306, 370, 402, 438, 454, 467, 470, 498, 512, 566, 598, 630, 662, 758 - 758,
    786, 790, 822, 854, 918 - 918, 982, 1014,
    # stores (X-form): stwx/stbx/sthx/stwux/...
    23 - 23, 135, 149, 183 - 183, 215 - 215, 247 - 247, 407 - 407, 439 - 439,
    663, 695, 727, 759, 983,
})


def _s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


class Undecodable(Exception):
    """An opcode this decoder will not guess about."""


def written_gprs(w: int):
    """The set of GPRs this instruction writes.  Raises on an unknown encoding.

    Over-approximating is SAFE here (it can only end an alias early, which drops
    a function or truncates a trace); under-approximating is not, because it
    would let a reused register masquerade as `this`.
    """
    p = w >> 26
    rd = (w >> 21) & 31
    ra = (w >> 16) & 31
    if p in NO_GPR_WRITE:
        return set()
    if p in DFORM_WRITE_RD:
        return {rd}
    if p in DFORM_WRITE_RA or p in ROTATE_WRITE_RA:
        return {ra}
    if p in DFORM_MEM:
        out = set()
        if p in DFORM_MEM_WRITES_GPR:
            out.add(rd)
        if DFORM_MEM[p][1]:
            out.add(ra)
        if p == 46:  # lmw rD..r31
            out |= set(range(rd, 32))
        return out
    if p in DSFORM:
        sub = w & 3
        if p == 58:
            return {rd, ra} if sub == 1 else {rd}      # ldu writes rA too
        return {ra} if sub == 1 else set()             # stdu writes rA; std none
    if p == 31:
        xo = (w >> 1) & 0x3FF
        if xo in X31_WRITE_RA:
            return {ra}
        if xo in X31_WRITE_RD:
            return {rd}
        if xo in X31_NO_WRITE:
            return set()
        raise Undecodable(f"op31 xo={xo}")
    if p in (56, 57, 60, 61, 62 - 62):
        return set()
    raise Undecodable(f"primary={p}")


def mem_ref(w: int):
    """(base_reg, displacement, mnemonic) for a D/DS-form memory reference, else None."""
    p = w >> 26
    if p in DFORM_MEM:
        return (w >> 16) & 31, _s16(w & 0xFFFF), DFORM_MEM[p][0]
    if p in DSFORM:
        d = w & 0xFFFC
        if d & 0x8000:
            d -= 0x10000
        return (w >> 16) & 31, d, DSFORM[p]
    return None


def is_mr(w: int):
    """(dest, src) for `mr rA, rS` (== `or rA,rS,rS`), else None."""
    if (w >> 26) != 31 or ((w >> 1) & 0x3FF) != 444:
        return None
    rs = (w >> 21) & 31
    ra = (w >> 16) & 31
    rb = (w >> 11) & 31
    return (ra, rs) if rs == rb else None


def is_addi(w: int):
    """(rD, rA, imm) for `addi`/`subi`, else None.  `addi rD,0,imm` is `li`."""
    if (w >> 26) != 14:
        return None
    return (w >> 21) & 31, (w >> 16) & 31, _s16(w & 0xFFFF)


def is_bl(w: int) -> bool:
    return (w >> 26) == 18 and (w & 1) == 1


def is_branch(w: int) -> bool:
    return (w >> 26) in (16, 18) or ((w >> 26) == 19)


# --------------------------------------------------------------------------- #
# The trace
# --------------------------------------------------------------------------- #

#: PPC argument registers.  An address already sitting in one of these is
#: consumed by the next call.
ARG_GPRS = frozenset(range(3, 11))


def _consumption_index(words, i, rd):
    """Index of the CALL that consumes the address `addi` #i put in `rd`.

    Why this exists, measured rather than assumed.  MSVC hoists operand
    addresses above the calls that use them, **right to left**, so the order in
    which a serializer MATERIALISES field addresses is not the order in which it
    STREAMS them.  `DepthBuffer3D::Load` in the shipped image:

        158  subi r4,  r31, 0x193
        160  subi r29, r31, 0x191     <- materialised SECOND
        161  subi r28, r31, 0x192     <- materialised THIRD
        162  bl   operator>>          ( r4 = -0x193 )   field 1
        163  mr   r4, r28
        164  bl   operator>>          ( r4 = -0x192 )   field 2
        165  mr   r4, r29
        166  bl   operator>>          ( r4 = -0x191 )   field 3

    The wire order is -0x193, -0x192, -0x191 -- exactly ours.  Sorting by
    materialisation made it read as -0x193, -0x191, -0x192, i.e. a swap of two
    adjacent bools, and the `STRADDLES-A-CALL` shape rated it the STRONG kind of
    evidence.  It was a false positive, and so were the same idiom's instances
    in `RndMatAnim::Load` and `RndParticleSysAnim::Load` -- 3 of the 5 findings
    this tool produced before consumption ordering was added.
    """
    n = len(words)
    if rd in ARG_GPRS:
        for j in range(i + 1, n):
            if is_bl(words[j]):
                return j
        return i
    for j in range(i + 1, n):
        m = is_mr(words[j])
        if m is not None and m[1] == rd:        # `mr rARG, rd` -- handed over
            for k in range(j, n):
                if is_bl(words[k]):
                    return k
            return j
        if rd in written_gprs(words[j]):
            break
    return i


def _addi_feeds_memory(words):
    """Indices of `addi` instructions whose result is used as a MEMORY BASE.

    This is the difference between "the address itself is the reference" (it is
    passed to `operator>>`) and "the reference is the load that follows".  Both
    shapes exist in this binary for the SAME field, so a tracer that does not
    separate them manufactures wrong-field findings out of addressing modes --
    see the comment at the use site.
    """
    out = set()
    n = len(words)
    for i, w in enumerate(words):
        ai = is_addi(w)
        if ai is None:
            continue
        rd, ra, _imm = ai
        if ra == 0:
            continue                       # `li rD, imm` -- not an address
        for j in range(i + 1, n):
            wj = words[j]
            m = mem_ref(wj)
            if m is not None and m[0] == rd:
                out.add(i)
                break
            if is_bl(wj) and rd in VOLATILE_GPRS:
                break                      # the callee owns it from here
            if rd in written_gprs(wj):
                break                      # redefined before any memory use
    return out


class TraceResult:
    __slots__ = ("offsets", "ops", "bl_before", "sort_keys", "n_bl",
                 "this_regs_seen", "frame_regs", "reason", "lost_at",
                 "n_after_loss", "n_insns", "n_derived_nonvol")

    def __init__(self):
        self.offsets = []
        self.ops = []
        #: how many real calls preceded each reference.  Two references with the
        #: same value have NO call between them, so the compiler was free to
        #: schedule them in either order -- which is what separates a real field
        #: permutation from regalloc noise.
        self.bl_before = []
        #: per reference, the instruction index at which it is CONSUMED (the
        #: load/store itself, or the call an address is handed to).  Events are
        #: ordered by this, not by the instruction that computed the address.
        self.sort_keys = []
        self.n_bl = 0
        self.this_regs_seen = set()
        self.frame_regs = set()
        self.reason = None
        self.lost_at = None
        self.n_after_loss = 0
        self.n_insns = 0
        #: `addi rNONVOL, this, off` -- a field address parked in a callee-saved
        #: register, usually a loop induction pointer over an array member.  It
        #: is recorded as ONE reference at `off`; anything the loop then reads
        #: through it (`lwzu r11, 0x4(r28)`) is NOT separately attributed.  Both
        #: sides are treated the same way, so this is symmetric -- but a row
        #: with a high count on one side only is weaker evidence, and the
        #: finding prints it.
        self.n_derived_nonvol = 0


def trace_this_refs(body: bytes, relocs=None) -> TraceResult:
    """Ordered `this`-relative offsets touched by one function body.

    `relocs` is `coff_bodies`' `(offset, symbol_name, type)` list; it is used
    ONLY to tell a real call from a register save/restore helper.  Passing none
    treats every `bl` as a real call, which is the conservative direction.

    `reason` is set (and the trace is not usable) when the receiver could not be
    followed safely.  Every such condition is a counted drop at the call site --
    this function never silently returns a short trace.
    """
    r = TraceResult()
    if len(body) < 4:
        r.reason = "body-too-short"
        return r
    relnames = {}
    for entry in (relocs or ()):
        relnames.setdefault(entry[0], entry[1])

    # reg -> BIAS: the constant offset from the incoming `this`, for as long as
    # the register provably holds `this + bias`.
    aliases = {3: 0}
    frame_regs = set()
    former_aliases = set()

    n = len(body) // 4
    r.n_insns = n
    words = [int.from_bytes(body[k * 4:k * 4 + 4], "big") for k in range(n)]
    try:
        feeds_mem = _addi_feeds_memory(words)
    except Undecodable as e:
        r.reason = f"undecodable-instruction ({e})"
        return r
    for i in range(n):
        w = words[i]

        # --- read side, BEFORE this instruction's writes take effect --------
        mr = mem_ref(w)
        if mr is not None:
            base, disp, mnem = mr
            if base in aliases:
                r.offsets.append(aliases[base] + disp)
                r.ops.append(mnem)
                r.bl_before.append(r.n_bl)
                r.sort_keys.append(i)       # a load/store consumes itself
            elif base in former_aliases:
                # A register that used to hold `this` is being used as a base
                # AFTER we stopped being able to prove it does.  We cannot say
                # whose field this is, so the function is refused at the call
                # site rather than traced short.
                r.n_after_loss += 1
        ai = is_addi(w)
        if ai is not None:
            rd, ra, imm = ai
            if ra in aliases:
                # `subi rD, this, off` materialises a FIELD ADDRESS.  Whether
                # THAT is the reference depends on what happens to rD next:
                #
                #   passed to a call      -> the addi IS the reference
                #   used as a memory base -> the reference is the load that
                #                            follows, at bias + its own disp
                #
                # Recording the addi unconditionally is what made
                # `RndText::Load` look like a wrong-field bug: the image
                # computes `subi r11, r30, 0xd0` and stores at `D(r11)`, while
                # our build stores at `(D-0xd0)(r30)`.  Identical addresses --
                # and the earlier cut of this tool recorded -0xd0 for one side
                # and -0xd0+D for the other, then reported the pair as a field
                # set difference.  objdiff's own base-register-aware enrichment
                # says of that same function: 27 offset mismatches examined,
                # 27 excluded as non-field.
                if i not in feeds_mem:
                    r.offsets.append(aliases[ra] + imm)
                    r.ops.append("addi")
                    r.bl_before.append(r.n_bl)
                    r.sort_keys.append(_consumption_index(words, i, rd))
            elif ra in former_aliases:
                r.n_after_loss += 1

        helper_call = is_bl(w) and is_save_restore_helper(relnames.get(i * 4))
        if is_bl(w) and not helper_call:
            # Helper calls are excluded from the count on purpose: how many the
            # prologue needs is a function of register pressure, so counting
            # them would make the bl-count lead disagree on every function whose
            # allocation differs harmlessly.
            r.n_bl += 1

        # --- write side ------------------------------------------------------
        try:
            wr = written_gprs(w)
        except Undecodable as e:
            r.reason = f"undecodable-instruction ({e})"
            return r

        new_alias = None
        m = is_mr(w)
        if m is not None:
            dst, src = m
            # `src in aliases`, never `aliases.get(src)`: a bias of 0 is the
            # NORMAL case (the receiver itself) and is falsy, so a truth test
            # here silently stops `mr r31, r3` from parking anything.
            if src in aliases:
                new_alias = (dst, aliases[src])
        if ai is not None:
            rd, ra, imm = ai
            if ra == 1:
                frame_regs.add(rd)          # r1-derived: a frame base, never `this`
            elif ra in aliases:
                # A pointer DERIVED from the receiver is still a receiver, at a
                # known bias.  Following it is what makes the two addressing
                # shapes above compare equal, and it is also what stops a
                # SELF-REBASE from truncating a side's trace: the image emits
                # `subi r30, r30, 0x2c` in RndMatAnim::Load and
                # `subi r30, r30, 0x30` in RndParticleSysAnim::Load, computing
                # the last operand address into the register that held `this`.
                # An earlier cut treated that as the receiver dying, lost every
                # later reference on the TARGET side only, and reported both
                # functions as missing a field.
                derived_bias = aliases[ra] + imm
                if rd not in VOLATILE_GPRS and rd != ra:
                    r.n_derived_nonvol += 1
                new_alias = (rd, derived_bias)

        for reg in wr:
            if reg in aliases:
                del aliases[reg]
                # Only a NON-VOLATILE is worth remembering as ambiguous.  A
                # volatile that stops holding `this` is not a puzzle: after a
                # call r3 holds a RETURN VALUE, and `bl F; lwz r11, 0(r3)` is
                # an ordinary read through the result.  Remembering r3 made
                # that shape read as "a former this-register reused as a base"
                # and refused 101 comparable pairs on it.
                if reg not in VOLATILE_GPRS:
                    former_aliases.add(reg)
            frame_regs.discard(reg)
        if new_alias is not None:
            reg_, bias_ = new_alias
            aliases[reg_] = bias_
            former_aliases.discard(reg_)
            if reg_ not in VOLATILE_GPRS:
                r.this_regs_seen.add(reg_)

        if is_bl(w) and not helper_call:
            # The callee owns every volatile.  r3 in particular stops being the
            # receiver here -- which is what keeps `Copy`'s dynamic_cast result
            # from being adopted as `this`.
            for reg in list(aliases):
                if reg in VOLATILE_GPRS:
                    del aliases[reg]
            for reg in list(frame_regs):
                if reg in VOLATILE_GPRS:
                    frame_regs.discard(reg)

        if not aliases and r.lost_at is None:
            r.lost_at = i

    # Re-order into CONSUMPTION order.  Stable, so references consumed at the
    # same instruction keep the order they were computed in.
    if r.offsets:
        perm = sorted(range(len(r.offsets)), key=lambda k: (r.sort_keys[k], k))
        r.offsets = [r.offsets[k] for k in perm]
        r.ops = [r.ops[k] for k in perm]
        r.bl_before = [r.bl_before[k] for k in perm]
        r.sort_keys = [r.sort_keys[k] for k in perm]

    r.frame_regs = frame_regs
    if r.n_bl and not r.this_regs_seen:
        # `this` only ever lived in r3, and r3 does not survive a call.  Any
        # member access after the first call would be reached through a reload
        # we do not model, so the trace is a PREFIX of the truth, not the truth.
        r.reason = "receiver-never-parked-in-a-nonvolatile"
    return r


# --------------------------------------------------------------------------- #
# Comparison
# --------------------------------------------------------------------------- #

BUCKET_IDENTICAL = "IDENTICAL"
BUCKET_ORDER = "FIELD_ORDER_DIFF"
BUCKET_SET = "FIELD_SET_DIFF"


def classify(ours: list, target: list) -> str:
    if ours == target:
        return BUCKET_IDENTICAL
    if collections.Counter(ours) == collections.Counter(target):
        return BUCKET_ORDER
    return BUCKET_SET


def set_delta(ours: list, target: list):
    """(only-ours, only-target) as sorted (offset, count) lists."""
    co, ct = collections.Counter(ours), collections.Counter(target)
    only_o = sorted((k, v) for k, v in (co - ct).items())
    only_t = sorted((k, v) for k, v in (ct - co).items())
    return only_o, only_t


def order_diff_shape(ours, target, bl_ours, bl_target) -> str:
    """How much a FIELD_ORDER_DIFF is worth looking at.

    `adjacent-transposition-no-call-between` is the WEAK shape and it is not
    hypothetical -- it is what the only finding on the current tree turned out
    to be.  `RndPostProc::Copy` reads 100.0% canonical with two `diff_arg` rows
    that are pure register swaps (`addi` #19 and #21, r4<->r3 / r30<->r31): the
    same two members are copied on both sides, and MSVC simply materialised the
    two addresses in the other order.  Two independent scalar copies with no
    call between them commute, so that is regalloc, not a wire-format bug.

    A transposition that STRADDLES a call is a different animal: the stream
    operation between them fixes the wire order, so the bytes really do land on
    different members.
    """
    diff_idx = [i for i, (a, b) in enumerate(zip(ours, target)) if a != b]
    if len(ours) != len(target):
        return "length-differs"
    if len(diff_idx) != 2:
        return f"{len(diff_idx)}-positions-differ"
    i, j = diff_idx
    if j != i + 1 or ours[i] != target[j] or ours[j] != target[i]:
        return "2-positions-differ"
    straddles = (bl_ours[i] != bl_ours[j]) or (bl_target[i] != bl_target[j])
    return ("adjacent-transposition-STRADDLES-A-CALL" if straddles
            else "adjacent-transposition-no-call-between")


def first_divergence(ours: list, target: list):
    for i, (a, b) in enumerate(zip(ours, target)):
        if a != b:
            return i
    return min(len(ours), len(target))


def is_serializer(name: str) -> bool:
    return name.startswith(SERIALIZER_PREFIXES)


def is_thunk(name: str) -> bool:
    return any(m in name for m in THUNK_MARKERS)


def fmt_off(v: int) -> str:
    return ("-0x%x" % -v) if v < 0 else ("0x%x" % v)


# --------------------------------------------------------------------------- #
# Selftest -- two-sided, on real bytes
# --------------------------------------------------------------------------- #

def _synth(instrs):
    return b"".join(w.to_bytes(4, "big") for w in instrs)


def _mr(ra, rs):
    return (31 << 26) | (rs << 21) | (ra << 16) | (rs << 11) | (444 << 1)


def _lwz(rd, ra, d):
    return (32 << 26) | (rd << 21) | (ra << 16) | (d & 0xFFFF)


def _addi(rd, ra, imm):
    return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)


def _blr():
    return (19 << 26) | (20 << 21) | (16 << 1)


def _bl():
    return (18 << 26) | 1


def selftest() -> int:
    ok = True

    def check(label, cond):
        nonlocal ok
        print(f"  {'PASS' if cond else 'FAIL'}  {label}")
        if not cond:
            ok = False

    # -- classifier, both directions ---------------------------------------
    check("identical sequences -> IDENTICAL", classify([8, 16, 24], [8, 16, 24]) == BUCKET_IDENTICAL)
    check("permutation -> FIELD_ORDER_DIFF", classify([16, 8, 24], [8, 16, 24]) == BUCKET_ORDER)
    check("duplicate/lost field -> FIELD_SET_DIFF", classify([8, 16, 8], [8, 16, 24]) == BUCKET_SET)
    check("StreamRenderer shape lands in SET, not ORDER",
          classify([0x60, 0x70, 0x80, 0x60, 0x70, 0x80],
                   [0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0]) == BUCKET_SET)

    # -- order-diff shape, both directions ----------------------------------
    check("adjacent swap with no call between is called WEAK",
          order_diff_shape([8, 16, 24], [16, 8, 24], [0, 0, 0], [0, 0, 0])
          == "adjacent-transposition-no-call-between")
    check("the SAME swap straddling a call is NOT called weak",
          order_diff_shape([8, 16, 24], [16, 8, 24], [0, 1, 1], [0, 1, 1])
          == "adjacent-transposition-STRADDLES-A-CALL")
    check("a non-adjacent permutation is not a transposition",
          order_diff_shape([24, 16, 8], [8, 16, 24], [0, 0, 0], [0, 0, 0])
          == "2-positions-differ")

    # -- receiver tracking on synthetic bodies ------------------------------
    # r31 = this; two fields read.
    t = trace_this_refs(_synth([_mr(31, 3), _lwz(11, 31, 0x20), _lwz(11, 31, 0x24), _blr()]))
    check("mr r31,r3 makes r31 the receiver", t.reason is None and t.offsets == [0x20, 0x24])

    # r31 = FRAME POINTER (addi r31,r1,-0x140), r30 = this.  The FxSendChorus trap:
    # the r31 slots must NOT be rendered as fields.
    t = trace_this_refs(_synth([_addi(31, 1, -0x140), _mr(30, 3),
                                _lwz(11, 31, 0x50), _lwz(11, 30, 0x20), _blr()]))
    check("frame pointer in r31 + this in r30: only the r30 access is a field",
          t.reason is None and t.offsets == [0x20])

    # r3 stops being the receiver at the first bl -- Copy's dynamic_cast result.
    t = trace_this_refs(_synth([_mr(31, 3), _bl(), _mr(30, 3), _lwz(11, 30, 0x40), _blr()]))
    check("a post-call `mr rN, r3` is NOT adopted as the receiver",
          t.reason is None and t.offsets == [])

    # `addi rD, this, -0x44` is a field reference, not a new receiver: a load
    # through rD afterwards must not be counted a second time.
    t = trace_this_refs(_synth([_mr(31, 3), _addi(4, 31, -0x44), _lwz(3, 4, 0), _blr()]))
    check("subi rD,this,off + a load through rD counts the field ONCE",
          t.reason is None and t.offsets == [-0x44])

    # THE ADDRESSING-MODE EQUIVALENCE, both directions.  These two bodies touch
    # the SAME field and must produce the same trace; this is the RndText::Load
    # false positive, reduced.
    via_pointer = _synth([_mr(30, 3), _addi(11, 30, -0xd0), _lwz(3, 11, 0x2c), _blr()])
    direct = _synth([_mr(30, 3), _lwz(3, 30, -0xa4), _blr()])
    check("`subi r11,this,0xd0` + `lwz 0x2c(r11)` == `lwz -0xa4(this)`",
          trace_this_refs(via_pointer).offsets == trace_this_refs(direct).offsets
          == [-0xa4])
    # ...and the control: a genuinely different inner offset must NOT compare equal
    other = _synth([_mr(30, 3), _lwz(3, 30, -0xa0), _blr()])
    check("a REAL one-word difference still diverges (control)",
          trace_this_refs(via_pointer).offsets != trace_this_refs(other).offsets)

    # A SELF-REBASE keeps the receiver alive at a new bias rather than killing
    # it.  Consumed by a LOAD, the rebase is not itself a reference -- the load
    # at bias+disp is.
    t = trace_this_refs(_synth([_mr(30, 3), _addi(29, 30, -0x14), _addi(30, 30, -0x2c),
                                _lwz(3, 30, 0x4), _blr()]))
    check("`subi r30,r30,0x2c` consumed by a load -> one ref at bias+disp",
          t.reason is None and t.offsets == [-0x14, -0x28])
    # Consumed by a CALL instead -- this is the real RndMatAnim::Load shape,
    # where the image computes all the operand addresses up front and passes
    # them -- the rebase IS the reference, and the receiver stays alive.
    t = trace_this_refs(_synth([_mr(30, 3), _addi(29, 30, -0x14), _addi(30, 30, -0x2c),
                                _mr(4, 30), _bl(), _blr()]),
                        [(16, "?op@@QAAXXZ", 0)])
    check("`subi r30,r30,0x2c` passed to a call -> the rebase IS the reference",
          t.reason is None and t.offsets == [-0x14, -0x2c])

    # CONSUMPTION ORDER, both directions.  These two bodies stream the same
    # three fields in the same order; one hoists two of the addresses above the
    # calls, right-to-left, the way the image does in DepthBuffer3D::Load.
    hoisted = _synth([_mr(31, 3),
                      _addi(4, 31, -0x193), _addi(29, 31, -0x191), _addi(28, 31, -0x192),
                      _bl(), _mr(4, 28), _bl(), _mr(4, 29), _bl(), _blr()])
    inline = _synth([_mr(31, 3),
                     _addi(4, 31, -0x193), _bl(),
                     _addi(4, 31, -0x192), _bl(),
                     _addi(4, 31, -0x191), _bl(), _blr()])
    calls_h = [(k * 4, "?op@@QAAXXZ", 0) for k in (4, 6, 8)]
    calls_i = [(k * 4, "?op@@QAAXXZ", 0) for k in (2, 4, 6)]
    th = trace_this_refs(hoisted, calls_h)
    ti = trace_this_refs(inline, calls_i)
    check("hoisted right-to-left materialisation == inline, in WIRE order",
          th.offsets == ti.offsets == [-0x193, -0x192, -0x191])
    # ...and the control: a genuinely swapped WIRE order must still diverge.
    swapped = _synth([_mr(31, 3),
                      _addi(4, 31, -0x193), _bl(),
                      _addi(4, 31, -0x191), _bl(),
                      _addi(4, 31, -0x192), _bl(), _blr()])
    check("a real wire-order swap is STILL caught (control)",
          trace_this_refs(swapped, calls_i).offsets == [-0x193, -0x191, -0x192]
          and classify(trace_this_refs(swapped, calls_i).offsets, ti.offsets)
          == BUCKET_ORDER)

    # a this-alias that gets overwritten stops being one
    t = trace_this_refs(_synth([_mr(31, 3), _lwz(31, 1, 0x10), _lwz(11, 31, 0x20), _blr()]))
    check("an overwritten alias stops being the receiver",
          t.reason is None and t.offsets == [] and t.n_after_loss == 1)

    # an unknown opcode refuses rather than assuming
    t = trace_this_refs(_synth([_mr(31, 3), (31 << 26) | (1000 << 1), _blr()]))
    check("an unknown opcode REFUSES the function", (t.reason or "").startswith("undecodable"))

    # -- the register-save-helper defect, both directions -------------------
    # This is the shape of nearly every serializer prologue: `bl __savegprlr_N`
    # BEFORE the `mr rN, r3` that parks the receiver.  Treating that helper as
    # an ABI call empties the trace of the entire function, silently.
    helper_body = _synth([_bl(), _mr(31, 3), _lwz(11, 31, 0x20), _blr()])
    t = trace_this_refs(helper_body, [(0, "__savegprlr_24", 0)])
    check("`bl __savegprlr_N` preserves r3, so the receiver is still parked",
          t.reason is None and t.offsets == [0x20])
    t = trace_this_refs(helper_body, [(0, "?Poll@Foo@@QAAXXZ", 0)])
    check("a REAL call at the same site does clear r3 (control)",
          t.reason == "receiver-never-parked-in-a-nonvolatile")

    # -- live corpus: byte-level sabotage of a real body --------------------
    ours_root = os.path.join(REPO, "build", "373307D9", "src")
    tgt_root = os.path.join(REPO, "build", "373307D9", "obj")
    probe = os.path.join("system", "char", "CharUpperTwist.obj")
    op, tp = os.path.join(ours_root, probe), os.path.join(tgt_root, probe)
    if os.path.exists(op) and os.path.exists(tp):
        name = "?Load@CharUpperTwist@@UAAXAAVBinStream@@@Z"
        obe = {n: (b, rl) for n, b, rl, _o in function_bodies(op)}.get(name)
        tbe = {n: (b, rl) for n, b, rl, _o in function_bodies(tp)}.get(name)
        ob, orl = obe if obe else (None, None)
        tb, trl = tbe if tbe else (None, None)
        if ob and tb:
            to_, tt = trace_this_refs(ob, orl), trace_this_refs(tb, trl)
            check("live: CharUpperTwist::Load traces cleanly on both sides",
                  to_.reason is None and tt.reason is None and len(to_.offsets) > 2)
            check("live: CharUpperTwist::Load is IDENTICAL today",
                  classify(to_.offsets, tt.offsets) == BUCKET_IDENTICAL)
            # Sabotage the BYTES: swap the two distinct field immediates.
            distinct = [o for o in dict.fromkeys(to_.offsets)]
            if len(distinct) >= 3:
                a, b = distinct[-2], distinct[-1]
                sab = bytearray(ob)
                for i in range(0, len(sab) - 3, 4):
                    w = int.from_bytes(sab[i:i + 4], "big")
                    m = mem_ref(w) or (is_addi(w) and (is_addi(w)[1], is_addi(w)[2], "addi"))
                    if not m:
                        continue
                    base, disp, _ = m
                    if base not in to_.this_regs_seen:
                        continue
                    if disp in (a, b):
                        new = b if disp == a else a
                        w = (w & ~0xFFFF) | (new & 0xFFFF)
                        sab[i:i + 4] = w.to_bytes(4, "big")
                ts = trace_this_refs(bytes(sab), orl)
                check("live sabotage: swapping two field immediates -> FIELD_ORDER_DIFF",
                      ts.reason is None
                      and classify(ts.offsets, tt.offsets) == BUCKET_ORDER)
                # and the negative half: the unsabotaged body is still clean
                check("live sabotage: the ORIGINAL body is still IDENTICAL",
                      classify(trace_this_refs(ob, orl).offsets, tt.offsets)
                      == BUCKET_IDENTICAL)
        else:
            print("  SKIP  live probe: CharUpperTwist::Load absent from one side")
    else:
        print("  SKIP  live-corpus checks (objects absent). This is NOT a pass:")
        print("        the classifier was exercised, the extractor was not.")

    print("\nselftest:", "OK" if ok else "FAILED")
    return 0 if ok else 1


# --------------------------------------------------------------------------- #

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--project", default=REPO, help="tree to read build/373307D9 from")
    ap.add_argument("--json", default=None, help="write findings to this path")
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT only; the counts above it are complete")
    ap.add_argument("--fail-on", type=int, default=0,
                    help="exit 1 when findings >= N (0 disables)")
    ap.add_argument("--selftest", action="store_true")
    add_coverage_args(ap)
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()

    ours_root = os.path.join(args.project, "build", "373307D9", "src")
    tgt_root = os.path.join(args.project, "build", "373307D9", "obj")
    if not os.path.isdir(ours_root):
        print(f"INCONCLUSIVE: built objects not found: {ours_root}")
        print("Run `ninja` first; an unbuilt tree cannot be compared against the target.")
        return EXIT_NO_INPUT
    if not os.path.isdir(tgt_root):
        print(f"INCONCLUSIVE: target objects not found: {tgt_root}")
        return EXIT_NO_INPUT

    cov = CoverageReport("serializer_field_trace", args=args)
    cov.require_examined("no serializer was comparable against a target body")

    our_objs = sorted(glob.glob(os.path.join(ours_root, "**", "*.obj"), recursive=True))
    n_tgt_objs = len(glob.glob(os.path.join(tgt_root, "**", "*.obj"), recursive=True))

    # PASS 1 -- the denominator, computed without reference to any disposition.
    work = []
    universe = 0
    n_obj_unpaired = 0
    for op in our_objs:
        rel = os.path.relpath(op, ours_root)
        tp = os.path.join(tgt_root, rel)
        try:
            ours = [(n, b, rl) for n, b, rl, _o in function_bodies(op) if is_serializer(n)]
        except Exception:
            ours = []
        universe += len(ours)
        if not ours:
            continue
        if not os.path.exists(tp):
            n_obj_unpaired += 1
            work.append((rel, None, ours))
        else:
            work.append((rel, tp, ours))
    cov.universe(universe, "Save/Load/Copy bodies defined by our built objects")

    # PASS 2 -- classify.  Every discard is counted.
    findings = []
    buckets = collections.Counter()
    n_bl_diff_on_identical = 0
    bl_diff_rows = []
    for rel, tp, ours in sorted(work, key=lambda x: x[0]):
        if tp is None:
            cov.drop("object-not-paired-with-target", len(ours))
            continue
        try:
            tgt = {n: (b, rl) for n, b, rl, _o in function_bodies(tp) if is_serializer(n)}
        except Exception:
            cov.drop("target-object-unreadable", len(ours))
            continue
        for name, body, our_relocs in sorted(ours, key=lambda x: x[0]):
            if is_thunk(name):
                cov.drop("adjustor-thunk",
                         note="3-instruction vtable adjustor; has no field access")
                continue
            entry = tgt.get(name)
            if entry is None:
                cov.drop("no-target-body-of-that-name",
                         note="inlined away, ICF-folded under another name, or not "
                              "emitted by the image")
                continue
            tb, tgt_relocs = entry
            to_ = trace_this_refs(body, our_relocs)
            tt = trace_this_refs(tb, tgt_relocs)
            if to_.reason or tt.reason:
                why = to_.reason or tt.reason
                cov.drop("receiver-not-followable:" + why.split(" ")[0])
                continue
            if to_.n_after_loss or tt.n_after_loss:
                cov.drop("receiver-register-reused-later",
                         note="a former this-register is used as a base after we "
                              "stopped being able to prove it holds this")
                continue
            if not to_.offsets and not tt.offsets:
                cov.drop("no-this-relative-access")
                continue
            cov.examine()
            bucket = classify(to_.offsets, tt.offsets)
            buckets[bucket] += 1
            if bucket == BUCKET_IDENTICAL:
                if to_.n_bl != tt.n_bl:
                    n_bl_diff_on_identical += 1
                    bl_diff_rows.append({"unit": rel, "symbol": name,
                                         "our_bl": to_.n_bl, "target_bl": tt.n_bl})
                continue
            only_o, only_t = set_delta(to_.offsets, tt.offsets)
            findings.append({
                "unit": rel,
                "symbol": name,
                "bucket": bucket,
                "kind": name[1:].split("@")[0],
                "n_ours": len(to_.offsets),
                "n_target": len(tt.offsets),
                "first_divergence_index": first_divergence(to_.offsets, tt.offsets),
                "shape": (order_diff_shape(to_.offsets, tt.offsets,
                                           to_.bl_before, tt.bl_before)
                          if bucket == BUCKET_ORDER else ""),
                "derived_nonvol_ours": to_.n_derived_nonvol,
                "derived_nonvol_target": tt.n_derived_nonvol,
                "only_ours": [[o, c] for o, c in only_o],
                "only_target": [[o, c] for o, c in only_t],
                "our_offsets": to_.offsets,
                "target_offsets": tt.offsets,
                "our_bl": to_.n_bl,
                "target_bl": tt.n_bl,
            })

    n_pairs = sum(1 for _r, tp, _o in work if tp)
    cov.note(f"our build covers {len(our_objs)} of {n_tgt_objs} target objects "
             f"({100.0 * len(our_objs) / max(1, n_tgt_objs):.1f}%) -- a field-order "
             f"desync in a TU that does not build yet is NOT visible here")
    cov.note("compares ORDERED this-relative offsets, ours vs the dtk-split target "
             "body of the same name; no objdiff, no report.json, no ruler")
    cov.note("BLIND to a stream read whose destination is a stack local "
             "(CharFeedback::Load, e1a9425e5) -- it touches no member")
    cov.extra("our_objects", len(our_objs))
    cov.extra("target_objects", n_tgt_objs)
    cov.extra("objects_with_serializers", len(work))
    cov.extra("objects_paired", n_pairs)
    for k, v in buckets.items():
        cov.extra("bucket_" + k, v)
    cov.extra("bl_count_differs_on_identical_trace", n_bl_diff_on_identical)
    cov.extra("findings", len(findings))

    findings.sort(key=lambda f: (f["bucket"], f["unit"], f["symbol"]))

    print(f"SERIALIZER FIELD-TRACE DIVERGENCES: {len(findings)}")
    print(f"  compared (ours vs target body) : {sum(buckets.values())}")
    print(f"  IDENTICAL                      : {buckets[BUCKET_IDENTICAL]}")
    print(f"  FIELD_SET_DIFF                 : {buckets[BUCKET_SET]}"
          f"   (a field missing, duplicated or at a different offset)")
    print(f"  FIELD_ORDER_DIFF               : {buckets[BUCKET_ORDER]}"
          f"   (same field multiset, different order)")
    print(f"  bl-count differs on an otherwise IDENTICAL trace : "
          f"{n_bl_diff_on_identical}")
    print("      (a LEAD only -- an inlining difference looks identical to a "
          "dropped stream op)")
    print()
    shown = findings if args.limit <= 0 else findings[: args.limit]
    if args.limit > 0 and len(shown) < len(findings):
        print(f"  showing {len(shown)} of {len(findings)}")
    for f in shown:
        print(f"  [{f['bucket']}] {f['unit']}  {f['symbol']}")
        print(f"      ours   {len(f['our_offsets'])} refs, target "
              f"{len(f['target_offsets'])}; first divergence at index "
              f"{f['first_divergence_index']}")
        if f["shape"]:
            weak = f["shape"] == "adjacent-transposition-no-call-between"
            print(f"      shape  : {f['shape']}"
                  + ("   <- WEAK: two references the compiler may schedule "
                     "either way" if weak else ""))
        if f["derived_nonvol_ours"] != f["derived_nonvol_target"]:
            print(f"      note   : callee-saved field pointers differ "
                  f"({f['derived_nonvol_ours']} ours / "
                  f"{f['derived_nonvol_target']} target) -- weaker evidence")
        if f["only_ours"]:
            print("      only ours   : "
                  + ", ".join(f"{fmt_off(o)}x{c}" for o, c in f["only_ours"]))
        if f["only_target"]:
            print("      only target : "
                  + ", ".join(f"{fmt_off(o)}x{c}" for o, c in f["only_target"]))
    if bl_diff_rows and args.limit <= 0:
        print("\n  bl-count leads (trace identical, call count differs):")
        for r in bl_diff_rows:
            print(f"      {r['unit']}  {r['symbol']}  ours={r['our_bl']} "
                  f"target={r['target_bl']}")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump({"findings": findings, "bl_count_leads": bl_diff_rows,
                       "_coverage": cov.as_dict()}, fh, indent=2)

    rc = cov.emit()
    if rc:
        return rc
    if args.fail_on and len(findings) >= args.fail_on:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
