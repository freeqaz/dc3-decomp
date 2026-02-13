"""Function extraction from both .obj flavors (decomp and original)."""

import struct


def extract_from_decomp(coff, symbol):
    """Extract function bytes and relocations from a decomp .obj (multi-symbol sections).

    Returns (bytearray, list[dict]) or (None, None) if symbol not found.
    """
    sym = coff.symbol_map.get(symbol)
    if not sym or sym['section'] <= 0:
        return None, None

    sec_idx = sym['section'] - 1
    sec = coff.sections[sec_idx]
    sec_data = coff.get_section_data(sec_idx)
    start = sym['value']

    # Find next symbol in same section to determine function size
    end = sec['raw_size']
    for s in coff.symbols:
        if s['section'] == sym['section'] and s['value'] > start:
            if s['value'] < end:
                end = s['value']

    func_bytes = bytearray(sec_data[start:end])

    # Filter and adjust relocations to be function-relative
    all_relocs = coff.get_section_relocations(sec_idx)
    relocs = []
    for r in all_relocs:
        if r['offset'] >= start and r['offset'] < end:
            adj = dict(r)
            adj['offset'] = r['offset'] - start
            relocs.append(adj)

    return func_bytes, relocs


def extract_from_original(coff, symbol):
    """Extract function bytes and relocations from an original .obj (COMDAT sections).

    Returns (bytearray, list[dict]) or (None, None) if symbol not found.
    """
    sym = coff.symbol_map.get(symbol)
    if not sym or sym['section'] <= 0:
        return None, None

    sec_idx = sym['section'] - 1
    sec = coff.sections[sec_idx]
    sec_data = coff.get_section_data(sec_idx)

    # symbol.value gives code start (skips EH header at offset 0-7 if present)
    start = sym['value']
    func_size = sec['raw_size'] - start

    if func_size <= 0:
        return None, None

    func_bytes = bytearray(sec_data[start:])

    # Filter and adjust relocations to be function-relative
    all_relocs = coff.get_section_relocations(sec_idx)
    relocs = []
    for r in all_relocs:
        if r['offset'] >= start:
            adj = dict(r)
            adj['offset'] = r['offset'] - start
            relocs.append(adj)

    return func_bytes, relocs


def has_indirect_branch(code_bytes):
    """Detect bctr (0x4E800420) and bctrl (0x4E800421) in function bytes.

    Returns "bctrl", "bctr", or None.
    """
    for i in range(0, len(code_bytes), 4):
        insn = struct.unpack_from(">I", code_bytes, i)[0]
        if insn == 0x4E800421:  # bctrl — indirect call (virtual dispatch)
            return "bctrl"
        if insn == 0x4E800420:  # bctr — indirect branch (switch table or vtable tail call)
            return "bctr"
    return None


def classify_indirect_branch(code_bytes, relocs, coff=None):
    """Classify indirect branch type with richer detail.

    Returns "bctrl", "bctr_switch", "bctr_tailcall", or None.

    bctr_switch: function has bctr + REFHI/REFLO relocs to .rdata sections (jump table)
    bctr_tailcall: function has bctr but no .rdata references (vtable tail call)
    """
    has_bctrl = False
    has_bctr = False

    for i in range(0, len(code_bytes), 4):
        insn = struct.unpack_from(">I", code_bytes, i)[0]
        if insn == 0x4E800421:
            has_bctrl = True
        elif insn == 0x4E800420:
            has_bctr = True

    if has_bctrl and has_bctr:
        # Both present — check for switch table (.rdata relocs)
        if coff is not None:
            for reloc in relocs:
                if reloc["type_name"] in ("REFHI", "REFLO"):
                    sym = coff.symbol_map.get(reloc["symbol_name"])
                    if sym and sym['section'] > 0:
                        sec = coff.sections[sym['section'] - 1]
                        if sec['name'].startswith('.rdata'):
                            return "bctrl_switch"  # vtable + switch table
        return "bctrl"  # vtable only

    if has_bctrl:
        return "bctrl"

    if not has_bctr:
        return None

    # bctr found — classify as switch table or tail call
    if coff is not None:
        for reloc in relocs:
            if reloc["type_name"] in ("REFHI", "REFLO"):
                sym = coff.symbol_map.get(reloc["symbol_name"])
                if sym and sym['section'] > 0:
                    sec = coff.sections[sym['section'] - 1]
                    if sec['name'].startswith('.rdata'):
                        return "bctr_switch"

    return "bctr_tailcall"


def has_ppc64_insns(code_bytes):
    """Detect 64-bit PPC instructions unsupported by Unicorn PPC32 mode.

    The Xbox 360 Xenon CPU is a PPC64 chip running in 32-bit compat mode,
    so MSVC uses ld/std (opcode 58/62) for callee-saved register preservation.
    Unicorn PPC32 mode doesn't support these and raises UC_ERR_EXCEPTION.

    Returns "std/ld" or None.
    """
    for i in range(0, len(code_bytes), 4):
        insn = struct.unpack_from(">I", code_bytes, i)[0]
        opcode = (insn >> 26) & 0x3F
        if opcode == 62:  # std (store doubleword)
            return "std/ld"
        if opcode == 58:  # ld (load doubleword)
            return "std/ld"
    return None
