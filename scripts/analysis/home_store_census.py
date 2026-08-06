#!/usr/bin/env python3
"""Census of the "dead home-slot store" signature, target .obj vs base .obj.

Signature (docs/decomp/patterns/fixable-inline-boundary.md):

    addi rD, rBase, <field-offset>      # computed sub-object address
    ...
    stw  rD, <frame-offset>(r1|r31)     # homed into the parameter home area
    <frame-offset is never reloaded in this function>

That store is the ABI home slot for the `this` of an *inlined member function*
whose receiver is a sub-object of some other object.  The count of such stores
is therefore a direct read of how many inline levels the original source had at
that point.  Counting them on both sides gives a per-function delta:

    delta = target_home_stores - base_home_stores

    delta > 0  -> the target had MORE inline levels than we wrote (add a wrapper)
    delta < 0  -> we have MORE inline levels than the target  (flatten a wrapper)
    delta == 0 -> lever does not apply here (a NORMAL result, not a failed audit)

The lever is a COUNT, not a direction: `RndMesh::SetVolume` regressed when the
wrapper was removed entirely, because the target wanted exactly one level.

Reads COFF objects directly -- no objdiff run needed.

Usage:
    python3 scripts/analysis/home_store_census.py --min 90 --max 99.9
    python3 scripts/analysis/home_store_census.py --json /tmp/home.json --all
"""

import argparse
import json
import os
import struct
import sys

IMAGE_SYM_DTYPE_FUNCTION = 0x20


# ------------------------------------------------------------------ COFF
def read_coff(filepath):
    """Return (sections, symbols) where each symbol has name/section/value/type."""
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
        if name_bytes[:4] == b"\x00\x00\x00\x00":
            off = struct.unpack_from("<I", name_bytes, 4)[0]
            try:
                end_idx = data.index(b"\x00", str_table_offset + off)
                name = data[str_table_offset + off:end_idx].decode("ascii", "replace")
            except Exception:
                name = ""
        else:
            name = name_bytes.rstrip(b"\x00").decode("ascii", "replace")
        value, sec_num, sym_type, storage, naux = struct.unpack_from("<IhHBB", entry, 8)
        symbols.append({"name": name, "value": value, "section": sec_num,
                        "type": sym_type, "storage": storage})
        i += 1 + naux
        sym_offset += 18 * (1 + naux)

    return data, sections, symbols


def function_bodies(filepath):
    """symbol name -> big-endian instruction word list."""
    data, sections, symbols = read_coff(filepath)
    by_sec = {s["idx"]: s for s in sections}
    # group function symbols per section, sort by value to bound each body
    per_sec = {}
    for s in symbols:
        if s["section"] <= 0:
            continue
        sec = by_sec.get(s["section"])
        if sec is None or not sec["name"].startswith(".text"):
            continue
        if s["type"] != IMAGE_SYM_DTYPE_FUNCTION:
            continue
        per_sec.setdefault(s["section"], []).append(s)

    out = {}
    for sec_idx, syms in per_sec.items():
        sec = by_sec[sec_idx]
        syms.sort(key=lambda x: x["value"])
        for n, s in enumerate(syms):
            start = s["value"]
            end = syms[n + 1]["value"] if n + 1 < len(syms) else sec["raw_size"]
            if end <= start:
                continue
            raw = data[sec["raw_offset"] + start: sec["raw_offset"] + end]
            words = [struct.unpack_from(">I", raw, o)[0]
                     for o in range(0, len(raw) - 3, 4)]
            # keep the LONGEST body seen for a name (COMDATs may repeat)
            if s["name"] not in out or len(words) > len(out[s["name"]]):
                out[s["name"]] = words
    return out


# ------------------------------------------------- minimal PPC decoding
def op(w):
    return w >> 26


def rD(w):
    return (w >> 21) & 0x1F


def rA(w):
    return (w >> 16) & 0x1F


def simm(w):
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


OP_ADDI = 14
OP_LWZ = 32
OP_STW = 36
OP_LFS = 48
OP_LFD = 50
OP_LWZU = 33
OP_LHZ = 40
OP_LBZ = 34


def home_stores(words, frame_regs=(1, 31, 30)):
    """Return list of (addi_idx, stw_idx, base_reg, field_off, frame_off).

    A `stw rD, F(rFrame)` counts only when
      * rD was last defined by `addi rD, rBase, imm` with rBase not a frame reg
        and imm > 0 (a computed sub-object address), and
      * no load in the whole function reads displacement F off the same frame
        register (the slot is dead).
    """
    # displacements loaded off each frame register anywhere in the body
    loaded = set()
    for w in words:
        o = op(w)
        if o in (OP_LWZ, OP_LWZU, OP_LFS, OP_LFD, OP_LHZ, OP_LBZ):
            if rA(w) in frame_regs:
                loaded.add((rA(w), simm(w)))

    last_addi = {}   # reg -> (idx, base, imm)
    found = []
    for i, w in enumerate(words):
        o = op(w)
        if o == OP_ADDI:
            d, a = rD(w), rA(w)
            if a != 0 and a not in frame_regs and simm(w) > 0:
                last_addi[d] = (i, a, simm(w))
            else:
                last_addi.pop(d, None)
            continue
        if o == OP_STW:
            d, a, f = rD(w), rA(w), simm(w)
            if a in frame_regs and d in last_addi and (a, f) not in loaded:
                ai, base, off = last_addi[d]
                if i - ai <= 24:
                    found.append((ai, i, base, off, f))
            continue
        # any other instruction that redefines a GPR invalidates the addi record
        # (approximate: most integer ops write rD in bits 21-25; loads write rD)
        if o in (OP_LWZ, OP_LWZU, OP_LFS, OP_LFD, OP_LHZ, OP_LBZ, 7, 8, 12, 13,
                 15, 24, 25, 26, 27, 28, 29):
            last_addi.pop(rD(w), None)
        elif o == 31:
            last_addi.pop(rD(w), None)
        elif o in (16, 18, 19):  # branches: be conservative, clear everything
            last_addi.clear()
    return found


# ------------------------------------------------------------------ main
def load_report(project_dir):
    path = os.path.join(project_dir, "build/373307D9/report.json")
    rep = json.load(open(path))
    pct = {}
    for unit in rep.get("units", []):
        for fn in unit.get("functions", []):
            name = fn.get("name")
            fp = fn.get("match_percent_normalized",
                        fn.get("fuzzy_match_percent", 0.0))
            if name:
                pct[name] = (fp, unit.get("name", ""), int(fn.get("size", 0) or 0),
                             fn.get("metadata", {}).get("demangled_name") or name)
    return pct


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-dir", default=os.getcwd())
    ap.add_argument("--min", type=float, default=90.0)
    ap.add_argument("--max", type=float, default=99.99)
    ap.add_argument("--all", action="store_true", help="ignore percent filter")
    ap.add_argument("--json")
    ap.add_argument("--limit", type=int, default=60)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    cfg = json.load(open(os.path.join(pd, "objdiff.json")))
    pct = load_report(pd)

    rows = []
    for unit in cfg["units"]:
        if "target_path" not in unit or "base_path" not in unit:
            continue
        tp = os.path.join(pd, unit["target_path"])
        bp = os.path.join(pd, unit["base_path"])
        if not (os.path.isfile(tp) and os.path.isfile(bp)):
            continue
        try:
            tb = function_bodies(tp)
            bb = function_bodies(bp)
        except Exception as e:
            print(f"# skip {unit['name']}: {e}", file=sys.stderr)
            continue
        for name, twords in tb.items():
            if name not in bb:
                continue
            p, unm, size, dem = pct.get(name, (None, unit["name"], 0, name))
            if p is None:
                continue
            if not args.all and not (args.min <= p <= args.max):
                continue
            ths = home_stores(twords)
            bhs = home_stores(bb[name])
            if len(ths) == len(bhs) and not args.all:
                continue
            rows.append({
                "symbol": name, "demangled": dem, "unit": unit["name"],
                "percent": p, "size": size,
                "tgt": len(ths), "base": len(bhs),
                "delta": len(ths) - len(bhs),
                "tgt_sites": [{"base_reg": b, "field_off": hex(o), "frame_off": hex(f)}
                              for _, _, b, o, f in ths],
                "base_sites": [{"base_reg": b, "field_off": hex(o), "frame_off": hex(f)}
                               for _, _, b, o, f in bhs],
            })

    rows.sort(key=lambda r: (-abs(r["delta"]), -r["percent"]))
    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1)
    for r in rows[:args.limit]:
        print(f"{r['percent']:6.2f}%  d={r['delta']:+d} (t={r['tgt']} b={r['base']}) "
              f"{r['size']:5d}B  {r['unit']:38s} {r['demangled'][:80]}")
    print(f"# {len(rows)} rows", file=sys.stderr)


if __name__ == "__main__":
    main()
