#!/usr/bin/env python3
"""Compare the __real@ constant SET a function loads: TARGET obj vs OUR BUILT obj.

Why this exists
---------------
`float_oracle.py` (and its `float_oracle2.py` predecessor) probe the *source text*
for float literals and then look their big-endian bytes up in the target object.
That probe OVER-FIRES, measured 2026-09-13 on this repo:

  Rnd::DrawTimers            "target loads 0.02891, source lacks it" -- MSVC had
                             folded the source's `0.025f + 0.00391f`.
  CharBones::ScaleAdd        "target loads 0.0396741, source lacks it" -- the
                             source spells it `0.039674062`, same float32.
  CharLipSync::PlayBack::Poll"target loads 1/255, source lacks it" -- the source
                             computes `1.0f / 255.0f`, folded at compile time.

All three read MATCH here, because this tool compares what the two COMPILERS
emitted rather than what the programmer typed.  It also has no endianness footgun
and no "did I remember the unary minus" footgun: `-0.5f` is simply `__real@bf000000`
on both sides, whoever wrote it.

Method
------
For a function symbol, walk the *relocation table* of the code section that
defines it (never a linear disassembly -- capstone's PPC decoder halts at the
first undecodable word), keep the relocations whose target symbol is named
`__real@<hex>`, and report the multiset of decoded constants for each side.

Limits, stated rather than hidden
---------------------------------
  * A constant reached through an internal-linkage `lbl_<addr>` symbol is NOT
    named `__real@` and is invisible here.  That is the same class every objdiff
    ruler exempts; ClipDistMap::Draw's `sLargeFloat` and ChatReceiver's pooled
    +32767.0f both live there and both looked like findings until resolved by
    hand out of the defining object's .rdata.
  * A count difference on an otherwise-present constant is usually REGALLOC, not
    a missing source use: the target may reload the literal inside a loop where
    our build parks it in a callee-saved FPR.  Check whether the extra load sits
    in a loop body before calling it a defect.

Usage
-----
  python3 scripts/analysis/float_const_setdiff.py <rel-cpp-under-src/system> <mangled-symbol>
  python3 scripts/analysis/float_const_setdiff.py --project-dir <worktree> ...
"""
import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coffx

REAL32 = re.compile(r"__real@([0-9a-f]{8})$")
REAL64 = re.compile(r"__real@([0-9a-f]{16})$")


def _read(path):
    if not os.path.exists(path):
        return None
    secs, syms = coffx.read_coff(open(path, "rb").read())
    if not secs:
        return None
    coffx.infer_sizes(secs, syms)
    return secs, syms


def constants(obj_path, symbol):
    """{hex_bits: relocation_count} for the __real@ constants `symbol` loads."""
    parsed = _read(obj_path)
    if parsed is None:
        return None
    secs, syms = parsed
    by_index = {s.index: s for s in syms}
    found = {}
    defs = [
        s for s in syms
        if s.name == symbol and s.sec > 0 and secs[s.sec - 1].is_code
    ]
    if not defs:
        return None
    for fn in defs:
        sec = secs[fn.sec - 1]
        lo, hi = fn.value, fn.value + max(fn.size, 4)
        for off, sym_index, _type in sec.relocs:
            if not (lo <= off < hi):
                continue
            target = by_index.get(sym_index)
            if target is None:
                continue
            m = REAL32.match(target.name) or REAL64.match(target.name)
            if m:
                found[m.group(1)] = found.get(m.group(1), 0) + 1
    return found


def decode(bits):
    raw = bytes.fromhex(bits)
    return struct.unpack(">f" if len(raw) == 4 else ">d", raw)[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="path under src/system, e.g. char/CharEyes.cpp")
    ap.add_argument("symbol", help="mangled function symbol")
    ap.add_argument("--project-dir", default=".",
                    help="worktree to read OUR built objects from (default: cwd)")
    args = ap.parse_args()

    rel = args.source.lstrip("./")
    if rel.startswith("src/system/"):
        rel = rel[len("src/system/"):]
    obj = rel.replace(".cpp", ".obj")

    root = os.path.abspath(args.project_dir)
    target_obj = os.path.join(root, "build/373307D9/obj/system", obj)
    base_obj = os.path.join(root, "build/373307D9/src/system", obj)

    target = constants(target_obj, args.symbol)
    base = constants(base_obj, args.symbol)
    if target is None:
        print(f"no target definition of {args.symbol} in {target_obj}")
        return 2
    if base is None:
        print(f"no built definition of {args.symbol} in {base_obj}")
        return 2

    def render(d):
        return ", ".join(f"{decode(h):g} x{c}" for h, c in sorted(d.items())) or "(none)"

    differ = target != base
    print(f"{args.symbol} [{'DIFFER' if differ else 'MATCH'}]")
    print(f"   target: {render(target)}")
    print(f"   ours  : {render(base)}")
    if not differ:
        return 0
    for h in sorted(set(target) | set(base)):
        t, b = target.get(h, 0), base.get(h, 0)
        if t != b:
            print(f"   __real@{h} = {decode(h):.9g}: target x{t}, ours x{b}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
