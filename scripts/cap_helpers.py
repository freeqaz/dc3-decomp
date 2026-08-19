#!/usr/bin/env python3
"""Neutralize the MSVC prologue/epilogue register-save helpers for emulation.

The unicorn harness stubs every external REL24 target with `li r3,0; blr`
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


def uses_helpers(relocs):
    return any(r["type_name"] == "REL24"
               and any(r["symbol_name"].startswith(p) for p in HELPER_PREFIXES)
               for r in relocs)


def neutralize(code, relocs):
    """Rewrite helper call sites. Returns (new_code, kept_relocs)."""
    if code is None or not uses_helpers(relocs):
        return code, relocs
    buf = bytearray(code)
    kept = []
    restore_sites = []
    for r in relocs:
        sym, off = r["symbol_name"], r["offset"]
        if r["type_name"] == "REL24" and any(sym.startswith(p) for p in HELPER_PREFIXES):
            insn = struct.unpack_from(">I", buf, off)[0]
            if insn & 1:          # bl  -> save helper: open-code the LR spill
                struct.pack_into(">I", buf, off, STW_R12_LR_SLOT)
            else:                 # b   -> restore helper: needs 3 insns, relay
                restore_sites.append(off)
        else:
            kept.append(r)

    if restore_sites:
        appendix = len(buf)
        for w in (LWZ_R12_LR_SLOT, MTLR_R12, BLR):
            buf.extend(struct.pack(">I", w))
        for off in restore_sites:
            delta = appendix - off
            struct.pack_into(">I", buf, off, 0x48000000 | (delta & 0x03FFFFFC))
    return bytes(buf), kept


def install():
    """Monkeypatch the extractors so both sides get helper-free code."""
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
