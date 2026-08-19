#!/usr/bin/env python3
"""Neutralize the MSVC prologue/epilogue register-save helpers for emulation.

SUPERSEDED (2026-08-19) by scripts/unicorn_runner/save_helpers.py, which
installs the helpers' REAL bodies in production. Do NOT call install() any
more: the harness already emulates the helpers, so the monkeypatch now
*removes* fidelity — it drops the r14-r31 / f14-f31 spill this module never
modelled — instead of adding it, and --neutralize-helpers is a no-op at best.
Kept, with its recorded mining output under docs/analysis/, because
`uses_helpers()` is still the static predicate cap_blastradius.py
cross-tabulates with.

The unicorn harness stubbed every external REL24 target with `li r3,0; blr`
(memory_map.TRAMPOLINE_STUB). MSVC's `__savegprlr_N` / `__restgprlr_N` /
`__savefpr_N` / `__restfpr_N` helpers are external REL24 targets, so they get
that stub too — which is catastrophic for two reasons:

  1. `li r3,0` destroys the incoming `this` / first argument at the SECOND
     instruction of the function, before the body runs.
  2. `__restgprlr_N` is tail-branched to (`b`), and the real helper reloads LR
     from -0x8(r1) before its `blr`. The stub's `blr` therefore returns to
     whatever LR happens to hold, which after any `bl` in the body is an
     address inside the function -> an infinite loop.

Whether a function uses the helpers or open-codes the saves is an MSVC
frame-size heuristic, so the decomp and the original frequently disagree.
When they do, ANY unicorn verdict for that function is meaningless.

This module installs monkeypatches that rewrite the helper call sites into
the equivalent open-coded sequence, matching what the original emits:

    bl __savegprlr_N   ->  stw r12, -0x8(r1)          (LR was mflr'd into r12)
    b  __restgprlr_N   ->  b <appendix>, appendix = lwz r12,-0x8(r1)
                                                     mtlr r12
                                                     blr

The r29-r31 spill/reload the helpers also perform is dropped: it is symmetric
(nothing reads the caller's values here) and costs no instruction slots.

Import and call install() before using scripts.unicorn_runner.run.
"""
import struct

HELPER_PREFIXES = ("__savegprlr_", "__restgprlr_", "__savefpr_", "__restfpr_",
                   "__savevmx_", "__restvmx_")

STW_R12_LR_SLOT = 0x9181FFF8   # stw   r12, -0x8(r1)
LWZ_R12_LR_SLOT = 0x8181FFF8   # lwz   r12, -0x8(r1)
MTLR_R12        = 0x7D8803A6   # mtlr  r12
BLR             = 0x4E800020   # blr
NOP             = 0x60000000   # ori r0,r0,0


def uses_helpers(relocs):
    return any(r["type_name"] == "REL24"
               and any(r["symbol_name"].startswith(p) for p in HELPER_PREFIXES)
               for r in relocs)


class UnsupportedShape(Exception):
    """The function's helper usage is a shape this rewriter cannot emulate."""


def _kind(sym):
    if sym.startswith("__savegprlr_"):
        return "save_gpr"
    if sym.startswith("__restgprlr_"):
        return "rest_gpr"
    if sym.startswith(("__savefpr_", "__savevmx_")):
        return "save_other"
    if sym.startswith(("__restfpr_", "__restvmx_")):
        return "rest_other"
    return None


def neutralize(code, relocs):
    """Rewrite helper call sites. Returns (new_code, kept_relocs).

    Classification is by SYMBOL, not by the link bit: MSVC emits
    `bl __restfpr_28` immediately followed by `b __restgprlr_27`, so keying on
    LK misreads the FPR restore as a save and corrupts the LR slot. That bug
    manufactured decomp-only UC_ERR_EXCEPTION faults in exactly the functions
    that spill FPRs (CharBonesSamples::Relativize et al).

      __savegprlr_N (bl)  -> stw r12, -0x8(r1)     the LR spill; r12 = mflr'd LR
      __savefpr_N   (bl)  -> nop                   FPR spill only, no LR
      __restfpr_N   (bl)  -> nop                   ditto
      __restgprlr_N (b)   -> b <appendix>          lwz r12,-0x8(r1); mtlr r12; blr

    -0x8(r1) is the top word of the frame the callee just released, which MSVC
    reserves for this save area, so nothing else can be living there.

    A tail-branched FPR restore in a function with no GPR save helper has no
    LR spill to recover and raises UnsupportedShape rather than emitting
    something that silently returns to a garbage address.
    """
    if code is None or not uses_helpers(relocs):
        return code, relocs
    buf = bytearray(code)
    kept = []
    restore_sites = []
    saw_save_gpr = any(_kind(r["symbol_name"]) == "save_gpr"
                       for r in relocs if r["type_name"] == "REL24")

    for r in relocs:
        sym, off = r["symbol_name"], r["offset"]
        kind = _kind(sym) if r["type_name"] == "REL24" else None
        if kind is None:
            kept.append(r)
            continue
        insn = struct.unpack_from(">I", buf, off)[0]
        tail = not (insn & 1)          # `b` (no link) = tail branch
        if kind == "save_gpr":
            struct.pack_into(">I", buf, off, STW_R12_LR_SLOT)
        elif kind in ("save_other",) or (kind == "rest_other" and not tail):
            struct.pack_into(">I", buf, off, NOP)
        elif kind in ("rest_gpr", "rest_other"):
            if tail:
                if not saw_save_gpr:
                    raise UnsupportedShape(f"tail {sym} with no GPR save helper")
                restore_sites.append(off)
            else:
                struct.pack_into(">I", buf, off, NOP)

    if restore_sites:
        appendix = len(buf)
        for w in (LWZ_R12_LR_SLOT, MTLR_R12, BLR):
            buf.extend(struct.pack(">I", w))
        for off in restore_sites:
            delta = appendix - off
            struct.pack_into(">I", buf, off, 0x48000000 | (delta & 0x03FFFFFC))
    return bytes(buf), kept


def install():
    """REMOVED. The harness emulates the helpers in production; see the header.

    This used to monkeypatch the extractors so both sides got helper-free code.
    Applied on top of scripts/unicorn_runner/save_helpers.py it would strip the
    helper relocations back out and hand the sides the open-coded rewrite,
    which drops the r14-r31 / f14-f31 spill this module never modelled -- so it
    now REDUCES fidelity, and any triage run that used it would be measuring a
    worse harness than production while believing it was measuring a better
    one. Raise rather than no-op: a control condition that is silently false is
    the failure mode this whole lane exists to remove.
    """
    raise RuntimeError(
        "cap_helpers.install() is superseded by "
        "scripts/unicorn_runner/save_helpers.py, which installs the real "
        "helper bodies in production. Re-run the probe without it; if you "
        "need the old behaviour for a historical comparison, check out "
        "8fe9d04a7 (fix/cap-exhausted-decomp-mining) in a worktree.")


def _install_disabled():                      # pragma: no cover — kept for the record
    from scripts.unicorn_runner import extractor
    import scripts.unicorn_runner.run as run_mod

    if getattr(extractor, "_cap_helpers_installed", False):
        return
    orig_d = extractor.extract_from_decomp
    orig_o = extractor.extract_from_original

    def wrap(fn):
        def inner(coff, symbol):
            code, relocs = fn(coff, symbol)
            return neutralize(code, relocs)
        return inner

    extractor._cap_orig_decomp = orig_d
    extractor._cap_orig_original = orig_o
    for mod in (extractor, run_mod):
        mod.extract_from_decomp = wrap(orig_d)
        mod.extract_from_original = wrap(orig_o)
    extractor._cap_helpers_installed = True
