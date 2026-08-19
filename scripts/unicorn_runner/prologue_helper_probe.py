#!/usr/bin/env python3
"""Diagnostic: re-run one unicorn comparison with the Xenon MSVC
prologue/epilogue helpers emulated instead of stubbed.

THIS IS NOT PART OF THE ORACLE.  It changes nothing in run.py / engine.py and
is never invoked by the frontier sweep.  It exists to *classify* a divergence:
if a function's verdict flips DIVERGENT -> EQUIVALENT under this probe, the
recorded divergence is a harness artifact, not a decomp bug.

------------------------------------------------------------------
The artifact it isolates (found 2026-08-19)
------------------------------------------------------------------
engine.py stubs every external call with `li r3,0; blr` (TRAMPOLINE_STUB).
Xenon MSVC does not open-code its register saves; it calls helpers:

    prologue:  mflr r12
               bl   __savefpr_N          ; clobbers LR
               bl   __savegprlr_N        ; stores the real LR (r12) at -8(r1)
                                         ; -- the stub stores nothing and
                                         ;    clobbers r3 (`this` / sret)
    epilogue:  addi r1, r1, frame
               subi r12, r1, N
               bl   __restfpr_N          ; clobbers LR again
               b    __restgprlr_N        ; the real helper reloads LR from
                                         ; -8(r1), mtlr, blr

Because the tail `b` is not a call, the stub's `blr` returns to whatever LR
happens to hold -- the address just past the *last stubbed call in the body*.
The function therefore re-enters its own tail and spins until the 50k
instruction cap.  Observed directly: IsValidSwipePosition executes +0x23C
16,616 times, one three-instruction cycle (`b` -> `li r3,0` -> `blr`).

Consequences for the recorded database:
  * every helper-using function burns its whole instruction budget in that
    spin, so the logged call count is "however many stub hits fit in 50k",
    a number set by the *length of the spin cycle* -- which differs between
    the two sides whenever their epilogues differ by even one instruction.
    That is the entire content of most `call_count` rows.
  * when only one side uses __savegprlr_N (register pressure differs), the
    extra `bl` shifts the whole call log by one position, so the comparator
    reports a `call_arg` mismatch at the first real call.
  * r3 is dead after the first helper call on both sides, so `this` is 0 for
    the rest of the body and the field-access map degenerates to "READ 0x000".

------------------------------------------------------------------
What the probe does
------------------------------------------------------------------
Rewrites, in the prepared code buffer only:

    bl __savegprlr_N              -> stw  r12, -0x8(r1)
    bl __savefpr_N / __savevmx_N  -> nop
    bl __restfpr_N / __restvmx_N  -> nop
    b  __restgprlr_N              -> b <thunk>, where <thunk> is
                                     `lwz r12, -0x8(r1); mtlr r12; blr`
                                     appended past the end of the code buffer

This reproduces exactly the inline prologue/epilogue MSVC emits when it does
not use the helpers, so both sides return through LR normally.  The thunk is
appended rather than written over the instructions preceding the tail branch:
in the common `addi r1, r1, frame ; b __restgprlr_N` epilogue those slots are
live, and overwriting them corrupts the run ("Unexpected fetch from unmapped
0x00000000").

Usage:
    python3 -m scripts.unicorn_runner.prologue_helper_probe <unit> <symbol>
    python3 -m scripts.unicorn_runner.prologue_helper_probe <unit> <symbol> --plain
        (--plain = no rewrites, i.e. the production behaviour, for A/B)
"""
import os
import struct
import sys

from .coff import COFFParser
from .memory_map import CODE_BASE
from . import run as R

NOP = 0x60000000
BLR = 0x4E800020
STW_R12_M8_R1 = 0x9181FFF8
LWZ_R12_M8_R1 = 0x8181FFF8
MTLR_R12 = 0x7D8803A6

_SAVE_GPR = ("savegprlr",)
_SAVE_OTHER = ("savefpr", "savevmx")
_REST_OTHER = ("restfpr", "restvmx")
_REST_GPR = ("restgprlr",)


def _decode_branch(code, off):
    """Return (target_addr, link_bit) for a b/bl at off, else (None, None)."""
    word = struct.unpack_from(">I", code, off)[0]
    if (word >> 26) != 18:
        return None, None
    disp = word & 0x03FFFFFC
    if disp & 0x02000000:
        disp -= 0x04000000
    absolute = (word >> 1) & 1
    target = (disp if absolute else (CODE_BASE + off + disp)) & 0xFFFFFFFF
    return target, (word & 1)


def _helper_kind(trampolines_by_addr, addr):
    for sym in trampolines_by_addr.get(addr, ()):
        for group, name in ((_SAVE_GPR, "savegpr"), (_REST_GPR, "restgpr"),
                            (_SAVE_OTHER, "savefpr"), (_REST_OTHER, "restfpr")):
            if any(tag in sym for tag in group):
                return name
    return None


def rewrite_helpers(side, label, log):
    """Rewrite helper calls in a PreparedSide in place. Returns the side."""
    code = bytearray(side.code)
    by_addr = {}
    for sym, addr in side.trampolines.items():
        by_addr.setdefault(addr, []).append(sym)

    original_len = len(code)
    thunk_off = [None]

    def get_thunk():
        if thunk_off[0] is None:
            thunk_off[0] = len(code)
            code.extend(struct.pack(">III", LWZ_R12_M8_R1, MTLR_R12, BLR))
            log.append(f"{label}: epilogue thunk appended at +0x{thunk_off[0]:X}")
        return thunk_off[0]

    for off in range(0, original_len - 3, 4):
        target, link = _decode_branch(code, off)
        if target is None:
            continue
        kind = _helper_kind(by_addr, target)
        if kind is None:
            continue
        if kind == "savegpr" and link:
            struct.pack_into(">I", code, off, STW_R12_M8_R1)
            log.append(f"{label} +0x{off:X}: bl __savegprlr -> stw r12,-8(r1)")
        elif kind in ("savefpr", "restfpr"):
            struct.pack_into(">I", code, off, NOP)
            log.append(f"{label} +0x{off:X}: bl __{kind} -> nop")
        elif kind == "restgpr":
            if link:
                struct.pack_into(">I", code, off, NOP)
                log.append(f"{label} +0x{off:X}: bl __restgprlr -> nop")
                continue
            dest = get_thunk()
            disp = (dest - off) & 0x03FFFFFC
            struct.pack_into(">I", code, off, 0x48000000 | disp)
            log.append(f"{label} +0x{off:X}: b __restgprlr -> b thunk+0x{dest:X}")

    side.code = code
    return side


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    plain = "--plain" in argv
    positional = [a for a in argv if not a.startswith("--")]
    if len(positional) < 2:
        print(__doc__)
        return 2
    unit, symbol = positional[0], positional[1]

    root = os.environ.get("DC3_ROOT") or os.path.dirname(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    log = []
    orig_prepare_side = R.prepare_side
    orig_prepare_coloaded = R.prepare_coloaded_side
    state = {"n": 0}

    def label():
        state["n"] += 1
        return "decomp" if state["n"] == 1 else "orig"

    if not plain:
        R.prepare_side = lambda *a, **k: rewrite_helpers(
            orig_prepare_side(*a, **k), label(), log)
        R.prepare_coloaded_side = lambda *a, **k: rewrite_helpers(
            orig_prepare_coloaded(*a, **k), label(), log)

    try:
        decomp_path, orig_path = R.resolve_unit(unit, root)
        code, output = R.run_comparison_inner(
            symbol, COFFParser(decomp_path), COFFParser(orig_path), verbose=True)
    finally:
        R.prepare_side = orig_prepare_side
        R.prepare_coloaded_side = orig_prepare_coloaded

    if log:
        print("Helper rewrites:")
        for line in log:
            print("  " + line)
    print(output)
    return code


if __name__ == "__main__":
    sys.exit(main())
