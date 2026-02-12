"""Intra-TU callee co-loading for the Unicorn Function Runner.

When comparing a function, also extracts its intra-TU callees and loads them
sequentially in the CODE region. Patches the caller's bl instructions to jump
to the real callee code instead of a trampoline. External calls still hit
trampolines as before.
"""

import struct
from collections import deque
from dataclasses import dataclass, field

from .memory_map import REGION_SIZE


@dataclass
class ColoadResult:
    """Result of building a co-load layout."""
    symbol_offsets: dict  # symbol_name -> byte offset within combined buffer
    coloaded_symbols: list  # ordered list of callee symbols (not including root)
    total_size: int  # total size of combined buffer


def is_intra_tu_callee(coff, symbol_name):
    """Check if a REL24 target is defined in this .obj's .text section.

    Returns True if the symbol exists, has a positive section number,
    and its section name starts with '.text'.
    """
    sym = coff.symbol_map.get(symbol_name)
    if sym is None or sym['section'] <= 0:
        return False
    sec = coff.sections[sym['section'] - 1]
    return sec['name'].startswith('.text')


def _get_rel24_targets(relocs):
    """Extract unique REL24 target symbol names from relocations."""
    return {r['symbol_name'] for r in relocs if r['type_name'] == 'REL24'}


def collect_intra_tu_callees(coff, root_symbol, extract_fn, max_depth=None):
    """BFS from root's REL24 relocs, collecting transitive intra-TU callees.

    Args:
        coff: COFFParser instance
        root_symbol: mangled symbol name of the root function
        extract_fn: extract_from_decomp or extract_from_original
        max_depth: caps recursion depth (None = unlimited)

    Returns:
        dict of symbol_name -> (bytes, relocs) for each intra-TU callee
        (does NOT include the root symbol itself)
    """
    callees = {}  # symbol_name -> (bytes, relocs)
    visited = {root_symbol}
    queue = deque()

    # Get root's relocs to find initial targets
    root_bytes, root_relocs = extract_fn(coff, root_symbol)
    if root_bytes is None:
        return callees

    # Seed BFS with root's REL24 targets
    for target in _get_rel24_targets(root_relocs):
        if target not in visited and is_intra_tu_callee(coff, target):
            queue.append((target, 1))
            visited.add(target)

    while queue:
        sym_name, depth = queue.popleft()

        callee_bytes, callee_relocs = extract_fn(coff, sym_name)
        if callee_bytes is None or len(callee_bytes) == 0:
            continue

        callees[sym_name] = (callee_bytes, callee_relocs)

        # Continue BFS if depth allows
        if max_depth is not None and depth >= max_depth:
            continue

        for target in _get_rel24_targets(callee_relocs):
            if target not in visited and is_intra_tu_callee(coff, target):
                queue.append((target, depth + 1))
                visited.add(target)

    return callees


def _has_bctr_switch(code_bytes, relocs, coff):
    """Check if a function uses bctr + .rdata references (switch table)."""
    has_bctr = False
    for i in range(0, len(code_bytes), 4):
        insn = struct.unpack_from(">I", code_bytes, i)[0]
        if insn == 0x4E800420:  # bctr
            has_bctr = True
            break

    if not has_bctr:
        return False

    # Check for .rdata references (indicates switch table)
    for reloc in relocs:
        if reloc['type_name'] in ('REFHI', 'REFLO'):
            sym = coff.symbol_map.get(reloc['symbol_name'])
            if sym and sym['section'] > 0:
                sec = coff.sections[sym['section'] - 1]
                if sec['name'].startswith('.rdata'):
                    return True

    return False


def build_coload_layout(root_symbol, root_bytes,
                        common_callees, decomp_callees, orig_callees,
                        decomp_coff, orig_coff):
    """Build sequential layout for root + co-loaded callees.

    Args:
        root_symbol: mangled symbol name of the root function
        root_bytes: bytes of the root function (used for sizing the root slot)
        common_callees: set of symbol names present in both decomp and orig
        decomp_callees: dict of symbol_name -> (bytes, relocs) from decomp
        orig_callees: dict of symbol_name -> (bytes, relocs) from orig
        decomp_coff: COFFParser for decomp
        orig_coff: COFFParser for orig

    Returns:
        ColoadResult or None if combined size exceeds 64KB
    """
    # Filter out problematic callees
    eligible = []
    for sym_name in sorted(common_callees):
        d_bytes, d_relocs = decomp_callees[sym_name]
        o_bytes, o_relocs = orig_callees[sym_name]

        # Skip callees with bctr_switch (switch table rdata handling is complex)
        if _has_bctr_switch(d_bytes, d_relocs, decomp_coff):
            continue
        if _has_bctr_switch(o_bytes, o_relocs, orig_coff):
            continue

        eligible.append(sym_name)

    if not eligible:
        return None

    # Build layout: root at offset 0, callees after (4-byte aligned)
    root_size = len(root_bytes)
    symbol_offsets = {root_symbol: 0}

    offset = (root_size + 3) & ~3  # 4-byte align after root

    coloaded_symbols = []
    for sym_name in eligible:
        d_bytes, _ = decomp_callees[sym_name]
        o_bytes, _ = orig_callees[sym_name]
        # Use max size for consistent offsets between decomp and orig
        callee_size = max(len(d_bytes), len(o_bytes))

        symbol_offsets[sym_name] = offset
        coloaded_symbols.append(sym_name)
        offset = (offset + callee_size + 3) & ~3  # 4-byte align

    total_size = offset

    # Check 64KB limit (CODE region size)
    if total_size > REGION_SIZE:
        return None

    return ColoadResult(
        symbol_offsets=symbol_offsets,
        coloaded_symbols=coloaded_symbols,
        total_size=total_size,
    )


def partition_relocs(relocs, intra_tu_symbols):
    """Filter out REL24 relocs targeting intra-TU symbols.

    Non-REL24 relocs (REFHI/REFLO/ADDR32/PAIR) pass through unchanged.
    Result is passed to assign_addresses so intra-TU symbols don't get
    trampoline stubs.

    Args:
        relocs: list of relocation dicts
        intra_tu_symbols: set of symbol names that are co-loaded

    Returns:
        list of relocs with intra-TU REL24 entries removed
    """
    return [r for r in relocs
            if not (r['type_name'] == 'REL24' and r['symbol_name'] in intra_tu_symbols)]


def adjust_relocs_to_layout(relocs, base_offset):
    """Adjust relocation offsets by adding base_offset.

    Used to shift callee relocs to their position in the combined buffer.

    Returns new list of adjusted reloc dicts (original list is not modified).
    """
    adjusted = []
    for r in relocs:
        adj = dict(r)
        adj['offset'] = r['offset'] + base_offset
        adjusted.append(adj)
    return adjusted
