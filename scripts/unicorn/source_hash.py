#!/usr/bin/env python3
"""Per-function source-hash for unicorn verdict freshness gating (wave-3 lane B).

The unicorn behavioral plane stores `unicorn_tested_at` (a DATE), so staleness
is only knowable by age. Lane B's requirement: a *source-hash freshness gate*
stored per row, so a verdict is detectably stale the moment its function's
codegen changes — not just when it gets old.

The right fingerprint for "did the thing unicorn actually executed change?" is
the decomp `.obj` function body itself: the `.text` COMDAT section bytes plus
the ordered list of reloc target symbol-names (so a changed call target counts
as a change even at identical machine bytes). This is exactly the input
`_run_comparison_core` consumes, so:

  - source unchanged  => identical .obj bytes + relocs  => identical hash
                         => the prior verdict is still valid for this source.
  - source changed    => recompiled function body       => different hash
                         => the verdict is stale, re-test.

It is independent of the original-binary side (that never changes), so it keys
purely off OUR decomp output, which is what a refresh must track.

`unicorn_signal_version` already gates on *runner semantics* changes; this gates
on *source* changes. Both together = a complete freshness story.
"""
from __future__ import annotations

import hashlib

# Reloc types whose target name affects behavior (calls + data refs). PAIR is a
# marker for the preceding REFHI/REFLO and carries the @comp.id sentinel, so we
# drop it to avoid noise.
_PAIR_TYPE = 0x12


def function_source_hash(coff, symbol: str) -> str | None:
    """Return a 16-hex-char source fingerprint for `symbol` in a decomp COFF.

    Hashes the function's own `.text` section bytes followed by the sorted
    (offset, type, target-name) reloc tuples. Returns None if the symbol has no
    section (external / not defined here).
    """
    sym = coff.symbol_map.get(symbol)
    if sym is None or sym["section"] <= 0:
        return None
    sec_idx = sym["section"]  # 1-based
    sec = coff.sections[sec_idx - 1]
    if not sec["name"].startswith(".text"):
        return None

    body = coff.get_section_data(sec_idx - 1)

    h = hashlib.sha256()
    h.update(body)
    # Reloc target names make a same-byte-but-different-callee change visible.
    relocs = coff.get_section_relocations(sec_idx - 1)
    reloc_keys = sorted(
        (r["offset"], r["type"], r["symbol_name"])
        for r in relocs
        if r["type"] != _PAIR_TYPE
    )
    for off, typ, name in reloc_keys:
        h.update(f"\n{off:08x}|{typ:04x}|{name}".encode("utf-8", "replace"))
    return h.hexdigest()[:16]
