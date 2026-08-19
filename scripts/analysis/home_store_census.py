#!/usr/bin/env python3
"""Census of the "dead home-slot store" signature, target .obj vs base .obj.

Signature (docs/decomp/patterns/fixable-inline-boundary.md):

    addi rD, rBase, <field-offset>      # computed sub-object address
    ...
    stw  rD, <frame-offset>(r1|r31)     # dead store into a local temp slot
    <frame-offset is never reloaded in this function>

That store materialises the `this` of an *inlined member function* whose
receiver is a sub-object of some other object.  It is NOT an ABI home slot and
NOT the parameter home area (corrected 2026-08-06): it is the first local-temp
slot, which the stack packer also reuses for unrelated temps.  The store is
emitted only when ALL of: the caller carries a C++ EH state
(__CxxFrameHandler), the inlined callee's `this` is a computed sub-object
address, and that callee references `this` at least twice.  Where it is
emitted, the count is a direct read of how many inline levels the original
source had at that point.  Counting them on both sides gives a per-function
delta:

    delta = target_home_stores - base_home_stores

    delta > 0  -> the target had MORE inline levels than we wrote (add a wrapper)
    delta < 0  -> we have MORE inline levels than the target  (flatten a wrapper)
    delta == 0 -> lever does not apply here (a NORMAL result, not a failed audit)

The lever is a COUNT, not a direction: `RndMesh::SetVolume` regressed when the
wrapper was removed entirely, because the target wanted exactly one level.

Population note: the strict form is RARE.  The 90-99.99% band yields 4 rows and
the whole binary yields 14 with a nonzero delta.  The productive sub-case is
constructors whose adjacent same-typed scalar fields are really an aggregate
member (HamAudio, NgPostProc -- both reached 100%).

Reads COFF objects directly -- no objdiff run needed.

COVERAGE (scripts/analysis/coverage.py)
=======================================
This tool compares TARGET function bodies against OUR bodies of the same name,
and most target bodies have no counterpart of ours.  Until 2026-08-19 it said
so nowhere: `# N rows` on stderr, with no denominator anywhere.  Measured here:

    1,245 of 2,224 objdiff.json units have no target/base pair on disk
    23,479 target bodies have no body of ours to compare against
    the `# 30046 rows` of an --all run is drawn from ~44% of the declared units

Every one of those skips is now a counted drop, so the row count arrives with
the population it came from.

Usage:
    python3 scripts/analysis/home_store_census.py --min 90 --max 99.9
    python3 scripts/analysis/home_store_census.py --json /tmp/home.json --all
"""

import argparse
import json
import os
import struct
import sys
from collections import defaultdict

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

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


def home_stores(words, frame_regs=(1, 31)):
    """Return list of (addi_idx, stw_idx, base_reg, field_off, frame_off).

    A `stw rD, F(rFrame)` counts only when
      * rD was last defined by `addi rD, rBase, imm` with rBase not a frame reg
        and imm > 0 (a computed sub-object address), and
      * no load in the whole function reads displacement F off the same frame
        register (the slot is dead).
    """
    # displacements read back off a frame register anywhere in the body.  An
    # `addi rX, rFrame, D` counts too: taking the slot's address (an sret
    # buffer, an outgoing const-ref argument) makes the slot LIVE even though
    # no load names it.  Missing this was worth several false candidates.
    loaded = set()
    for w in words:
        o = op(w)
        if o in (OP_LWZ, OP_LWZU, OP_LFS, OP_LFD, OP_LHZ, OP_LBZ, OP_ADDI):
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

#: Which ruler the `%` column is on.  A percentage without its ruler is not a
#: measurement (scripts/analysis/ruler.py); `match_percent_normalized` is the
#: LOOSE ruler and this tree grades on functionRelocDiffs=name_check.
PERCENT_RULER_LABEL = ("percent column = report.json `match_percent_normalized` "
                       "(the LOOSE ruler; this tree GRADES on "
                       "functionRelocDiffs=name_check == `fuzzy_match_percent`), "
                       "falling back to fuzzy_match_percent, then 0.0")


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
    ap.add_argument("--limit", type=int, default=60,
                    help="shorten the PRINTOUT to N rows (default 60; 0 = all). "
                         "The row COUNT below the list, and --json, are always "
                         "the full set -- the list now says 'showing N of M'.")
    add_coverage_args(ap)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    cfg = json.load(open(os.path.join(pd, "objdiff.json")))
    pct = load_report(pd)

    # Stage 1 denominator: units.  1,245 of 2,224 have no pair on disk.
    ucov = CoverageReport("home_store_census/units", allow_truncation=True)
    ucov.universe(len(cfg["units"]), "units in objdiff.json")

    rows = []
    n_bodies = 0
    drops = defaultdict(int)
    for unit in sorted(cfg["units"], key=lambda u: u.get("name", "")):
        if "target_path" not in unit or "base_path" not in unit:
            ucov.drop("no-target-or-base-path",
                      note="objdiff.json unit declares only one side")
            continue
        tp = os.path.join(pd, unit["target_path"])
        bp = os.path.join(pd, unit["base_path"])
        if not (os.path.isfile(tp) and os.path.isfile(bp)):
            ucov.drop("object-file-missing",
                      note="declared path is not on disk (unbuilt unit)")
            continue
        try:
            tb = function_bodies(tp)
            bb = function_bodies(bp)
        except Exception as e:
            ucov.drop("coff-parse-error", note="function_bodies raised")
            print(f"# skip {unit['name']}: {e}", file=sys.stderr)
            continue
        ucov.examine()
        n_bodies += len(tb)
        for name in sorted(tb):
            twords = tb[name]
            if name not in bb:
                drops["no-body-of-ours"] += 1
                continue
            p, unm, size, dem = pct.get(name, (None, unit["name"], 0, name))
            if p is None:
                drops["no-percent-in-report"] += 1
                continue
            if not args.all and not (args.min <= p <= args.max):
                drops["outside-percent-window"] += 1
                continue
            ths = home_stores(twords)
            bhs = home_stores(bb[name])
            if len(ths) == len(bhs) and not args.all:
                drops["delta-zero"] += 1
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

    # Stage 2 denominator: target function bodies in the units we could read.
    cov = CoverageReport("home_store_census", args=args)
    cov.universe(n_bodies,
                 "TARGET function bodies in the unit pairs we read")
    cov.examine(len(rows))
    for reason, n in sorted(drops.items()):
        cov.drop(reason, n, note={
            "no-body-of-ours": "the target defines it; our object does not -- "
                               "nothing to diff (structurally necessary)",
            "no-percent-in-report": "report.json carries no percent for this "
                                    "symbol (the fake_impl_scan shape)",
            "outside-percent-window": "excluded by --min/--max; pass --all",
            "delta-zero": "same home-store count on both sides; pass --all to keep",
        }.get(reason, ""))
    cov.note(PERCENT_RULER_LABEL)
    cov.extra("units_coverage", ucov.as_dict())

    # symbol as the final tie-break: -abs(delta) and -percent tie constantly and
    # a stable sort then leaks COFF enumeration order into the printed head.
    rows.sort(key=lambda r: (-abs(r["delta"]), -r["percent"], r["symbol"]))
    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1)

    # DISPLAY-ONLY: `rows` is complete here and stays complete.
    shown = rows[:args.limit] if args.limit else rows
    print(f"# {PERCENT_RULER_LABEL}")
    if len(shown) < len(rows):
        print(f"# showing {len(shown)} of {len(rows)} rows (--limit {args.limit}); "
              f"--json and the count below are the FULL set")
    for r in shown:
        print(f"{r['percent']:6.2f}%  d={r['delta']:+d} (t={r['tgt']} b={r['base']}) "
              f"{r['size']:5d}B  {r['unit']:38s} {r['demangled'][:80]}")
    print(f"# {len(rows)} rows of {n_bodies} target bodies examined "
          f"({ucov.as_dict()['examined']} of {len(cfg['units'])} units read)",
          file=sys.stderr)

    ucov.emit()
    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
