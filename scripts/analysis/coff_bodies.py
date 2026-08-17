#!/usr/bin/env python3
"""Function-COMDAT body reader that does NOT drop EH-bearing functions.

Ported into dc3-decomp from rb3-xenon's `tools/coff_bodies_ext.py` (lanes CW-2 /
STLPORT-1 / EHFIX-BODIES) on 2026-08-17.  Rebased onto `scripts/analysis/coffx.py`
so it carries no dependency on rb3-xenon's `tools/icf_fold_evidence.py`.

WHY A SPECIAL READER IS NEEDED
------------------------------
The obvious gate -- "accept a code section only when it holds EXACTLY ONE
defining function symbol, AT OFFSET 0" -- misfires on every EH-bearing function.
MSVC X360 lays such a COMDAT out as::

    +0    8-byte EH prefix (unnamed)
    +8    the function entry             <- value != 0, so an offset-0 gate fails
    +N    __unwind$NNNNN                 <- ALSO storage 2/3, type 0x20, so a
                                            "exactly one def" gate drops the
                                            whole section

So instead: slice each code section by CONSECUTIVE defining-symbol values.
Symbol i owns ``[value_i, value_{i+1})``; the last owns ``[value_i, end)``.
Relocations are rebased into the owning slice.

THE INTERIOR-EH-PREFIX ARTIFACT (rb3-xenon lane STLPORT-1, 2026-08-16)
---------------------------------------------------------------------
EVERY EH-bearing region carries that 8-byte prefix -- the entry AND each funclet
-- so a section laid out::

    +0    8-byte EH prefix       <- excluded: the slice starts at value == 8
    +8    the function body, 96 B
    +104  8-byte EH prefix       <- BILLED TO THE FUNCTION ABOVE
    +112  __catch$NNNNN

yields a 104-byte body for a 96-byte function.  dtk's split objs carve the
target side by .pdata extents and carry no prefixes, so a target-vs-ours SIZE
test reads a uniform +8 on every EH-bearing function that has a successor.  On
rb3-xenon that artifact was written into the record as a *source* defect and a
whole lane was commissioned to fix a bug that did not exist.

...AND THE FIX MUST BE GATED ON THE MARKER, NOT THE NAME (rb3-xenon commit
913a9623, lane EHFIX-BODIES, 2026-08-17)
--------------------------------------------------------------------------
The first cut of the trim asked "is the SUCCESSOR named ``__unwind$`` /
``__catch$`` / ...?".  That is a PROXY for the question that matters ("is there
an 8-byte EH prefix at ``end - 8``?") and it is not equivalent: an interior EH
prefix can precede an ORDINARY function packed into the same ``.text``, and then
the successor carries no funclet name and the trim never fires.  Measured on
rb3-xenon build 45410914: 3 of 6,395 interior prefixes are of that shape.

This port therefore ships the corrected, MARKER-FIRST / NAME-NEVER rule:

  1. PRIMARY -- a ``$EH*`` boundary symbol (class 3, type 0) injected at every
     interior prefix by an obj patcher.  **dc3-decomp has no such patcher today**
     (there is no ``eh_boundary_patched.stamp`` in ``build/373307D9/``), so on
     this tree rule 1 never fires and rule 2 does all the work.  The code path is
     kept so the two trees stay one implementation.
  2. FALLBACK -- the prefix's own signature: eight zero bytes whose two words
     relocate to ``__CxxFrameHandler`` and ``__ehfuncinfo$...``.  Those are data
     pointers and cannot be instructions, so it cannot fire on a real body.
  3. The successor's NAME is never consulted.

The trim is a bounded ``end - 8`` and never empties a slice, so a marker in the
wrong place can cost 8 bytes but can never truncate a body.

⚠ Note the DIRECTION of the failure this guards against.  A byte-length test
against the target returns "NO -- size %d vs %d" the moment lengths differ, so
the artifact made target-byte fold proof IMPOSSIBLE for a whole population,
while the within-build test (ours(S) vs ours(F), which is what
``fold_proof.py`` runs) was immune because the artifact cancels on both sides.
A one-sided instrument error is invisible to the two-sided control.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent))

from scripts.analysis.coffx import read_coff  # noqa: E402

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SYM_DTYPE_FUNCTION = 0x20

#: Every EH-bearing region MSVC X360 emits is preceded by this unnamed prefix.
EH_PREFIX_BYTES = 8
#: Boundary marker an obj patcher may inject at every interior prefix.  Keyed on
#: the NAME plus class 3 / type 0, never on storage class alone: class-6
#: ``$M#####`` debug labels sit INSIDE bodies, and a class-2/3 rule truncates a
#: target function on dc3 (rb3-xenon lane EHFIX-SYNTH).
EH_MARKER_PREFIX = "$EH"
EH_PERSONALITY = "__CxxFrameHandler"
EH_FUNCINFO_PREFIX = "__ehfuncinfo$"
#: A type-18 PAIR record's "VirtualAddress" is a DISPLACEMENT, not an address, so
#: it must never claim an offset in a reloc-by-offset map.
IMAGE_REL_PPC_PAIR = 0x12


def is_aux_code_symbol(name: str) -> bool:
    """EH/unwind data emitted as type-0x20 symbols.

    Real code, but never a C++ callee, so it delimits slices without becoming a
    comparison target.
    """
    return name.startswith("__unwind$") or name.startswith("__ehhandler$")


def eh_boundaries(section_symbols) -> set:
    """Offsets of the ``$EH*`` EH-prefix markers among one section's symbols."""
    return {s.value for s in section_symbols
            if s.cls == IMAGE_SYM_CLASS_STATIC and s.typ == 0
            and s.name.startswith(EH_MARKER_PREFIX)}


def eh_prefix_end(end, v, marks, raw, rel):
    """``end`` with the SUCCESSOR's 8-byte EH prefix handed back, or ``end``.

    Marker first, then the prefix's own byte+relocation signature; the
    successor's NAME is never consulted -- see the module docstring for why a
    name predicate misses interior prefixes that precede an ordinary function.
    """
    lo = end - EH_PREFIX_BYTES
    if lo <= v:
        return end
    if lo in marks:
        return lo
    if end <= len(raw) and raw[lo:end] == b"\0" * EH_PREFIX_BYTES \
            and rel.get(lo) == EH_PERSONALITY \
            and str(rel.get(lo + 4, "")).startswith(EH_FUNCINFO_PREFIX):
        return lo
    return end


def function_bodies(path, stats=None):
    """Yield ``(name, body_bytes, relocs, entry_off)`` for each function slice.

    ``relocs`` is a list of ``(offset_within_body, target_symbol_name, type)``.
    """
    data = Path(path).read_bytes()
    secs, syms = read_coff(data)
    if not secs:
        return
    idx_name = {}
    by_sec = collections.defaultdict(list)
    for s in syms:
        idx_name[s.index] = s.name
        if s.sec > 0:
            by_sec[s.sec - 1].append(s)
    for si, sec in enumerate(secs):
        if not sec.is_code:
            continue
        defs = [s for s in by_sec.get(si, [])
                if s.name != sec.name
                and s.cls in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC)
                and s.typ == IMAGE_SYM_DTYPE_FUNCTION]
        if not defs:
            continue
        raw = sec.data
        # De-duplicate identical offsets (aliases at the same address), keeping a
        # stable order, then slice by consecutive values.
        pts = sorted({(s.value, s.name) for s in defs})
        bounds = sorted({v for v, _n in pts} | {len(raw)})
        nxt = {v: bounds[i + 1] for i, v in enumerate(bounds[:-1])}
        marks = eh_boundaries(by_sec.get(si, []))
        rel = {}
        for (o, i, t) in sec.relocs:
            if t != IMAGE_REL_PPC_PAIR:
                rel.setdefault(o, idx_name.get(i, "?"))
        for v, name in pts:
            if v % 4 or v >= len(raw):
                if stats is not None:
                    stats["skip_bad_off"] += 1
                continue
            end = nxt.get(v, len(raw))
            trimmed = eh_prefix_end(end, v, marks, raw, rel)
            if trimmed != end:
                end = trimmed
                if stats is not None:
                    stats["eh_prefix_suffix_stripped"] += 1
            if stats is not None:
                stats["entry_off_%s" % (v if v <= 8 else "gt8")] += 1
                stats["slices"] += 1
            if is_aux_code_symbol(name):
                if stats is not None:
                    stats["aux_skipped"] += 1
                continue
            rl = [(o - v, idx_name.get(i, "?"), t)
                  for (o, i, t) in sec.relocs if v <= o < end]
            yield name, raw[v:end], rl, v


def data_bodies(path, stats=None):
    """Yield ``(name, bytes, relocs, offset)`` for each DATA-section slice.

    /OPT:ICF folds identical read-only DATA COMDATs as well as functions, and a
    large share of `name_check`'s relocation-name charges name data symbols --
    string literals (``??_C@``), fp constants (``__real@``), vtables (``??_7``),
    local-static guards (``??_B``) and function-local statics.  Those cannot be
    adjudicated from a code-only index, so the same consecutive-value slicing is
    applied to non-code sections.

    No EH-prefix trim here: the 8-byte prefix is a `.text` layout artifact.
    """
    data = Path(path).read_bytes()
    secs, syms = read_coff(data)
    if not secs:
        return
    idx_name = {}
    by_sec = collections.defaultdict(list)
    for s in syms:
        idx_name[s.index] = s.name
        if s.sec > 0:
            by_sec[s.sec - 1].append(s)
    for si, sec in enumerate(secs):
        if sec.is_code or not sec.data:
            continue
        defs = [s for s in by_sec.get(si, [])
                if s.name != sec.name
                and s.cls in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC)]
        if not defs:
            continue
        raw = sec.data
        pts = sorted({(s.value, s.name) for s in defs})
        bounds = sorted({v for v, _n in pts} | {len(raw)})
        nxt = {v: bounds[i + 1] for i, v in enumerate(bounds[:-1])}
        for v, name in pts:
            if v >= len(raw):
                if stats is not None:
                    stats["data_skip_bad_off"] += 1
                continue
            end = nxt.get(v, len(raw))
            if stats is not None:
                stats["data_slices"] += 1
            rl = [(o - v, idx_name.get(i, "?"), t)
                  for (o, i, t) in sec.relocs if v <= o < end]
            yield name, raw[v:end], rl, v


def iter_objects(roots):
    """Yield every ``*.obj`` under each root (a file root yields itself)."""
    for root in roots:
        p = Path(root)
        if p.is_file():
            yield p
        else:
            for dirpath, _dirnames, filenames in os.walk(p):
                for fn in sorted(filenames):
                    if fn.endswith(".obj"):
                        yield Path(dirpath) / fn


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("objects", nargs="+",
                    help="object files or directories to walk")
    ap.add_argument("--symbol", action="append", default=[],
                    help="only report these symbols (repeatable)")
    ap.add_argument("--json", dest="json_out",
                    help="write {symbol: {obj,size,relocs}} JSON here")
    ap.add_argument("--stats", action="store_true",
                    help="print slice statistics")
    args = ap.parse_args(argv)

    want = set(args.symbol)
    stats = collections.Counter()
    out = {}
    for obj in iter_objects(args.objects):
        for name, body, relocs, entry in function_bodies(obj, stats):
            if want and name not in want:
                continue
            out.setdefault(name, []).append({
                "obj": str(obj), "size": len(body), "entry": entry,
                "relocs": [[o, t, ty] for (o, t, ty) in relocs],
            })
    for name in sorted(out):
        for hit in out[name]:
            print(f"{hit['size']:>7} B  {len(hit['relocs']):>3} rel  "
                  f"{name[:80]:<80}  {hit['obj']}")
    if args.stats:
        print()
        for k in sorted(stats):
            print(f"  {k}: {stats[k]}")
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
