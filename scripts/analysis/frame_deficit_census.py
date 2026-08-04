#!/usr/bin/env python3
"""Bulk stack-frame-size census: target .obj vs our .obj, every paired unit.

Motivation (dc3-decomp, 2026-08-04): `LabelShrinkWrapper::UpdateAndDrawWrapper`
went 68.1% -> 99.9% once its four unnamed `Vector3(...)` const-ref temporaries
were given names.  Unnamed aggregate temps passed by const-ref die at the end of
their own full-expression, so N of them share ONE stack slot; the target had
three.  The frame therefore came out 0x10 SHORT (`stwu r1,-0xb0` vs `-0xc0`) and
every downstream FPR/GPR was permuted.

That "our frame is smaller than the target's" reading is the single cheapest,
most mechanical discriminator for the bug -- and it does NOT need objdiff.  The
prologue instruction `stwu r1, -N(r1)` encodes to 0x9421____ with N in the
signed 16-bit displacement, so we can read both sides straight out of the COFF
objects and diff N for every function in the build in one pass.

Emits one row per function where a frame size could be read on both sides:

    delta = base_frame - target_frame      (negative == WE ARE SHORT == Lever-5 shape)

Usage:
    python3 scripts/analysis/frame_deficit_census.py            # short frames only
    python3 scripts/analysis/frame_deficit_census.py --all      # every delta
    python3 scripts/analysis/frame_deficit_census.py --json out.json
"""

import argparse
import json
import os
import struct
import sys
from collections import defaultdict

# ---------------------------------------------------------------- COFF reader
# (same layout logic as scripts/analyze_funclets.py; headers are little-endian,
#  the PowerPC instruction stream inside .text is big-endian)

IMAGE_SYM_CLASS_EXTERNAL = 2
IMAGE_SYM_CLASS_STATIC = 3
IMAGE_SYM_DTYPE_FUNCTION = 0x20


def read_coff(filepath):
    with open(filepath, "rb") as f:
        data = f.read()
    machine, nsections, timestamp, sym_ptr, num_syms, opt_size, chars = \
        struct.unpack_from("<HHiIIHH", data, 0)
    str_table_offset = sym_ptr + num_syms * 18

    section_offset = 20 + opt_size
    sections = []
    for i in range(nsections):
        sh = data[section_offset + i * 40: section_offset + i * 40 + 40]
        if len(sh) < 40:
            break
        name_raw = sh[:8]
        if name_raw[0:1] == b"/":
            try:
                str_off = int(name_raw.lstrip(b"/").rstrip(b"\x00").decode("ascii"))
                end_idx = data.index(b"\x00", str_table_offset + str_off)
                name = data[str_table_offset + str_off:end_idx].decode("ascii", "replace")
            except Exception:
                name = name_raw.decode("ascii", "replace")
        else:
            name = name_raw.rstrip(b"\x00").decode("ascii", "replace")
        (vsize, vaddr, raw_size, raw_offset, reloc_offset,
         lineno_offset, n_relocs, n_linenos, flags) = struct.unpack_from("<IIIIIIHHI", sh, 8)
        sections.append({
            "idx": i + 1, "name": name, "raw_size": raw_size,
            "raw_offset": raw_offset, "flags": flags,
        })

    symbols = []
    i = 0
    sym_offset = sym_ptr
    while i < num_syms:
        entry = data[sym_offset:sym_offset + 18]
        if len(entry) < 18:
            break
        name_bytes = entry[:8]
        value = struct.unpack_from("<I", entry, 8)[0]
        section_num = struct.unpack_from("<h", entry, 12)[0]
        sym_type = struct.unpack_from("<H", entry, 14)[0]
        storage_class = entry[16]
        aux_count = entry[17]
        if name_bytes[:4] == b"\x00\x00\x00\x00":
            str_off_v = struct.unpack_from("<I", name_bytes, 4)[0]
            try:
                end_idx = data.index(b"\x00", str_table_offset + str_off_v)
                name = data[str_table_offset + str_off_v:end_idx].decode("ascii", "replace")
            except ValueError:
                name = "<bad>"
        else:
            name = name_bytes.rstrip(b"\x00").decode("ascii", "replace")
        symbols.append({
            "name": name, "value": value, "section": section_num,
            "class": storage_class, "type": sym_type,
        })
        sym_offset += 18 * (1 + aux_count)
        i += 1 + aux_count
    return data, sections, symbols


# ------------------------------------------------------- prologue frame reader

MAX_PROLOGUE_WORDS = 48


def frame_size_at(data, sections, sym):
    """Read `stwu r1, -N(r1)` out of a function prologue.  Returns N or None.

    Encoding: stwu rS,D(rA)  ->  primary opcode 37, so the word is
    0b100101 rS rA D  == 0x9421____ for rS=r1, rA=r1.  D is a signed 16-bit
    displacement, always negative for a frame allocation.

    Big frames (>= 32 KiB) use `lis r12 / ori r12 / stwux r1,r1,r12` instead and
    are reported as None ('unknown') rather than guessed at.
    """
    sec_num = sym["section"]
    if sec_num <= 0 or sec_num > len(sections):
        return None
    sec = sections[sec_num - 1]
    if not (sec["flags"] & 0x20):  # IMAGE_SCN_CNT_CODE
        return None
    start = sec["raw_offset"] + sym["value"]
    # A COMDAT section is one function; a split .text holds many.  Either way we
    # only ever look at the first MAX_PROLOGUE_WORDS instructions.
    limit = min(len(data), start + MAX_PROLOGUE_WORDS * 4,
                sec["raw_offset"] + sec["raw_size"])
    for off in range(start, limit - 3, 4):
        word = struct.unpack_from(">I", data, off)[0]
        if (word & 0xFFFF0000) == 0x94210000:
            disp = word & 0xFFFF
            if disp & 0x8000:
                disp -= 0x10000
            return -disp if disp < 0 else None
        # stwux r1,r1,rN  -> big frame, bail out rather than guess
        if (word & 0xFC0007FE) == 0x7C00016E and ((word >> 21) & 0x1F) == 1:
            return None
        # a terminator before any stwu means a leaf/frameless function
        if word == 0x4E800020:  # blr
            return 0
    return None


def code_functions(data, sections, symbols):
    """name -> frame size, for every function-typed symbol with a readable frame."""
    out = {}
    for sym in symbols:
        if sym["class"] not in (IMAGE_SYM_CLASS_EXTERNAL, IMAGE_SYM_CLASS_STATIC):
            continue
        if sym["section"] <= 0 or sym["section"] > len(sections):
            continue
        sec = sections[sym["section"] - 1]
        if not (sec["flags"] & 0x20):
            continue
        if sym["name"].startswith((".text", ".data", ".rdata", ".bss", ".pdata",
                                   ".xdata", ".rodata", ".drectve", ".debug")):
            continue
        fs = frame_size_at(data, sections, sym)
        if fs is None:
            continue
        # keep the first definition; COFF can list a symbol more than once
        out.setdefault(sym["name"], fs)
    return out


# ------------------------------------------------------------------- main scan

def load_report(project_dir):
    """symbol -> (match_percent_normalized, unit name)."""
    path = os.path.join(project_dir, "build/373307D9/report.json")
    pct = {}
    try:
        rep = json.load(open(path))
    except Exception:
        return pct
    for unit in rep.get("units", []):
        uname = unit.get("name", "")
        for fn in unit.get("functions", []) or []:
            p = fn.get("match_percent_normalized")
            if p is None:
                p = fn.get("fuzzy_match_percent")
            prev = pct.get(fn["name"])
            if prev is None or (p is not None and p > prev[0]):
                pct[fn["name"]] = (p, uname, int(fn.get("size", 0) or 0))
    return pct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-dir", default=os.getcwd())
    ap.add_argument("--all", action="store_true",
                    help="report every frame delta, not just deficits")
    ap.add_argument("--max-percent", type=float, default=100.0,
                    help="skip functions already at/above this match%%")
    ap.add_argument("--min-percent", type=float, default=0.0)
    ap.add_argument("--json", default=None)
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    units = json.load(open(os.path.join(pd, "objdiff.json")))["units"]
    pct = load_report(pd)

    rows = []
    n_units = 0
    for u in units:
        tp, bp = u.get("target_path"), u.get("base_path")
        if not tp or not bp:
            continue
        tp, bp = os.path.join(pd, tp), os.path.join(pd, bp)
        if not (os.path.exists(tp) and os.path.exists(bp)):
            continue
        try:
            tgt = code_functions(*read_coff(tp))
            base = code_functions(*read_coff(bp))
        except Exception as exc:
            print(f"  [skip] {u['name']}: {exc}", file=sys.stderr)
            continue
        n_units += 1
        for name, bframe in base.items():
            if name not in tgt:
                continue
            tframe = tgt[name]
            if tframe == 0 and bframe == 0:
                continue
            delta = bframe - tframe
            if not args.all and delta >= 0:
                continue
            p, punit, size = pct.get(name, (None, u["name"], 0))
            if p is not None and not (args.min_percent <= p < args.max_percent):
                continue
            rows.append({
                "symbol": name, "unit": u["name"],
                "source": (u.get("metadata") or {}).get("source_path", ""),
                "target_frame": tframe, "base_frame": bframe,
                "delta": delta, "percent": p, "size": size,
            })

    # rank: biggest payoff first == lowest current match%, then largest deficit
    rows.sort(key=lambda r: (r["percent"] if r["percent"] is not None else 0,
                             r["delta"]))
    if args.limit:
        rows = rows[:args.limit]

    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1)

    print(f"# scanned {n_units} unit pairs; {len(rows)} rows")
    print(f"# {'match%':>7}  {'tgt':>6} {'base':>6} {'delta':>6}  symbol")
    for r in rows:
        p = f"{r['percent']:.1f}" if r["percent"] is not None else "  n/a"
        print(f"{p:>9}  0x{r['target_frame']:04x} 0x{r['base_frame']:04x} "
              f"{r['delta']:>+6d}  {r['symbol']}   [{r['source'] or r['unit']}]")

    if args.all:
        hist = defaultdict(int)
        for r in rows:
            hist[r["delta"]] += 1
        print("\n# delta histogram")
        for d in sorted(hist):
            print(f"  {d:>+6d}: {hist[d]}")


if __name__ == "__main__":
    main()
