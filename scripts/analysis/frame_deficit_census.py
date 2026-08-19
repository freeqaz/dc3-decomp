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

COVERAGE (scripts/analysis/coverage.py)
=======================================
"where a frame size could be read on both sides" is a MUCH smaller population
than "every function", and until 2026-08-19 the tool never said by how much.
`frame_size_at()` returns None for a big frame (`stwux`, >= 32 KiB) and for any
prologue whose `stwu` lands past MAX_PROLOGUE_WORDS, and every such symbol left
through a bare `continue`.  Measured on this tree:

    65,661 of 114,857 target code symbols (57.2%) have no readable frame
    17,033 of  67,178 of ours likewise
   148,197 of our symbols have no target counterpart at all

None of those are bugs -- the docstring at `frame_size_at` already says big
frames are reported "as unknown rather than guessed at" -- but a scanner whose
denominator is 43% of the population must SAY 43%, or its "N rows" reads as a
census of the binary.  Every one of those skips is now a counted drop.

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

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

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


def code_functions(data, sections, symbols, stats=None, candidates=None):
    """name -> frame size, for every function-typed symbol with a readable frame.

    `stats` (optional dict) accumulates the denominator this function used to
    throw away:

        code_symbols      symbols that passed the class/section/name filters
        frame_unreadable  ...whose prologue `frame_size_at` could not read
                          (big `stwux` frame, or `stwu` past MAX_PROLOGUE_WORDS)
        readable          ...that yielded a frame size

    `candidates` (optional set) collects the NAMES of every code symbol,
    readable or not, so a caller can tell "the target has no such symbol" apart
    from "the target has it but its frame is unreadable" -- two very different
    reasons for a row to be absent, indistinguishable before 2026-08-19.
    """
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
        if stats is not None:
            stats["code_symbols"] = stats.get("code_symbols", 0) + 1
        if candidates is not None:
            candidates.add(sym["name"])
        fs = frame_size_at(data, sections, sym)
        if fs is None:
            if stats is not None:
                stats["frame_unreadable"] = stats.get("frame_unreadable", 0) + 1
            continue
        if stats is not None:
            stats["readable"] = stats.get("readable", 0) + 1
        # keep the first definition; COFF can list a symbol more than once
        out.setdefault(sym["name"], fs)
    return out


# ------------------------------------------------------------------- main scan

def load_report(project_dir, allow_missing=False):
    """symbol -> (match_percent_normalized, unit name, size).

    The percentages here are `match_percent_normalized` -- the LOOSE ruler, the
    one `provenance.diff_config` shows this tree does NOT grade on -- falling
    back to `fuzzy_match_percent` only when normalized is absent.  Callers must
    label the column; see `PERCENT_RULER_LABEL`.

    A missing or corrupt report.json used to be swallowed (`except Exception:
    return pct`), which silently degraded --min-percent/--max-percent to "no
    filter at all" while every printed `n/a` looked like an ordinary unmatched
    function.  It is now fatal unless --allow-missing-report says otherwise.
    """
    path = os.path.join(project_dir, "build/373307D9/report.json")
    pct = {}
    try:
        rep = json.load(open(path))
    except Exception as exc:
        msg = (f"could not read {path}: {exc!r}. Without it --min-percent/"
               f"--max-percent silently become no filter at all and every row "
               f"prints match% = n/a.")
        if not allow_missing:
            raise SystemExit(f"FATAL: {msg}\n(pass --allow-missing-report to "
                             f"proceed with NO percent filter, knowingly.)")
        print(f"!! {msg}", file=sys.stderr)
        return pct
    for unit in rep.get("units", []):
        uname = unit.get("name", "")
        for fn in unit.get("functions", []) or []:
            p = fn.get("match_percent_normalized")
            if p is None:
                p = fn.get("fuzzy_match_percent")
            prev = pct.get(fn["name"])
            # `p > prev[0]` raised TypeError whenever a duplicate symbol's FIRST
            # occurrence carried no percent (prev[0] is None) -- a latent crash
            # that depended on report.json ordering.  A real number always wins
            # over None; two Nones keep the first.
            if prev is None:
                pct[fn["name"]] = (p, uname, int(fn.get("size", 0) or 0))
            elif p is not None and (prev[0] is None or p > prev[0]):
                pct[fn["name"]] = (p, uname, int(fn.get("size", 0) or 0))
    return pct


#: Which ruler the `match%` column is on.  A percentage without its ruler is not
#: a measurement (scripts/analysis/ruler.py).
PERCENT_RULER_LABEL = ("match% column = report.json `match_percent_normalized` "
                       "(the LOOSE ruler; this tree GRADES on "
                       "functionRelocDiffs=name_check, i.e. `fuzzy_match_percent`), "
                       "falling back to fuzzy_match_percent when normalized is absent")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project-dir", default=os.getcwd())
    ap.add_argument("--all", action="store_true",
                    help="report every frame delta, not just deficits")
    # NOTE: report.json is a *build artifact of whatever was last compiled*, so a
    # function you just edited still carries its old percentage.  Default the
    # ceiling above 100 so the filter never silently drops the function under
    # test -- this bit us while validating the tool against the known-good
    # LabelShrinkWrapper case, where a stale 100.0 hid a live -0x10 deficit.
    ap.add_argument("--max-percent", type=float, default=101.0,
                    help="skip functions already at/above this match%% "
                         "(pass 99.999 to hide matched functions; default 101 = no filter)")
    ap.add_argument("--min-percent", type=float, default=0.0)
    ap.add_argument("--json", default=None)
    ap.add_argument("--limit", type=int, default=0,
                    help="shorten the PRINTOUT to N rows (default 0 = print all). "
                         "Since 2026-08-19 the slice is applied AFTER the "
                         "'N rows' summary, the histogram and --json, so it can "
                         "no longer rewrite the counts it appears above; before "
                         "that it silently did.")
    ap.add_argument("--allow-missing-report", action="store_true",
                    help="proceed when build/373307D9/report.json cannot be read. "
                         "Every match%% then prints n/a and the percent filters "
                         "do nothing; previously this was the SILENT default.")
    add_coverage_args(ap)
    args = ap.parse_args()

    pd = os.path.abspath(args.project_dir)
    units = json.load(open(os.path.join(pd, "objdiff.json")))["units"]
    pct = load_report(pd, allow_missing=args.allow_missing_report)

    # Stage 1 denominator: units.  1,245 of 2,224 objdiff.json units have no
    # target/base pair on disk and were dropped without a word before 2026-08-19.
    ucov = CoverageReport("frame_deficit_census/units", allow_truncation=True)
    ucov.universe(len(units), "units in objdiff.json")

    tstats, bstats = {}, {}
    tgt_candidates = set()
    rows = []
    n_units = 0
    n_base_syms = 0
    drops = defaultdict(int)
    for u in sorted(units, key=lambda x: x.get("name", "")):
        tp, bp = u.get("target_path"), u.get("base_path")
        if not tp or not bp:
            ucov.drop("no-target-or-base-path", note="objdiff.json unit declares "
                                                     "only one side")
            continue
        tp, bp = os.path.join(pd, tp), os.path.join(pd, bp)
        if not (os.path.exists(tp) and os.path.exists(bp)):
            ucov.drop("object-file-missing", note="declared path is not on disk "
                                                  "(unbuilt unit)")
            continue
        try:
            cand, base_cand = set(), set()
            tgt = code_functions(*read_coff(tp), stats=tstats, candidates=cand)
            base = code_functions(*read_coff(bp), stats=bstats,
                                  candidates=base_cand)
        except Exception as exc:
            ucov.drop("coff-parse-error", note="read_coff/code_functions raised")
            print(f"  [skip] {u['name']}: {exc}", file=sys.stderr)
            continue
        ucov.examine()
        tgt_candidates |= cand
        n_units += 1
        # The universe is every code symbol of OURS in this unit, readable frame
        # or not.  The unreadable ones are the population `frame_size_at`
        # returns None for; they were the largest silent skip in this tool.
        n_base_syms += len(base_cand)
        drops["our-frame-unreadable"] += len(base_cand - set(base))
        for name in sorted(base):
            bframe = base[name]
            if name not in tgt:
                # Two very different absences, no longer conflated:
                if name in cand:
                    drops["target-frame-unreadable"] += 1
                else:
                    drops["no-target-counterpart"] += 1
                continue
            tframe = tgt[name]
            if tframe == 0 and bframe == 0:
                drops["both-frames-zero"] += 1
                continue
            delta = bframe - tframe
            if not args.all and delta >= 0:
                drops["not-a-deficit"] += 1
                continue
            p, punit, size = pct.get(name, (None, u["name"], 0))
            if p is not None and not (args.min_percent <= p < args.max_percent):
                drops["outside-percent-window"] += 1
                continue
            rows.append({
                "symbol": name, "unit": u["name"],
                "source": (u.get("metadata") or {}).get("source_path", ""),
                "target_frame": tframe, "base_frame": bframe,
                "delta": delta, "percent": p, "size": size,
            })

    # Stage 2 denominator: our code symbols in the units we could read.
    cov = CoverageReport("frame_deficit_census", args=args)
    cov.universe(n_base_syms,
                 "distinct code symbols in OUR objects, across the unit pairs "
                 "we read (readable frame or not)")
    cov.examine(len(rows))
    for reason, n in sorted(drops.items()):
        cov.drop(reason, n, note={
            "target-frame-unreadable":
                "the target defines it but its prologue frame is unreadable "
                "(big stwux frame, or stwu past MAX_PROLOGUE_WORDS)",
            "no-target-counterpart": "no symbol of this name in the target object",
            "our-frame-unreadable":
                "OUR object defines it but its prologue frame is unreadable -- "
                "this tool can never compare it, on either side",
            "both-frames-zero": "leaf/frameless on both sides -- nothing to diff",
            "not-a-deficit": "delta >= 0; pass --all to include",
            "outside-percent-window": "excluded by --min-percent/--max-percent",
        }.get(reason, ""))
    cov.note(PERCENT_RULER_LABEL)
    cov.note(f"target side: {tstats.get('code_symbols', 0)} code symbols, "
             f"{tstats.get('frame_unreadable', 0)} with an UNREADABLE frame "
             f"(never comparable by this tool)")
    cov.note(f"our side   : {bstats.get('code_symbols', 0)} code symbols, "
             f"{bstats.get('frame_unreadable', 0)} with an UNREADABLE frame")
    cov.extra("units_coverage", ucov.as_dict())
    cov.extra("target_symbol_stats", dict(sorted(tstats.items())))
    cov.extra("base_symbol_stats", dict(sorted(bstats.items())))

    # rank: biggest payoff first == lowest current match%, then largest deficit,
    # then symbol name so equal keys cannot reorder between runs
    rows.sort(key=lambda r: (r["percent"] if r["percent"] is not None else 0,
                             r["delta"], r["symbol"]))

    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1)

    print(f"# scanned {n_units} unit pairs of {len(units)} declared; "
          f"{len(rows)} rows of {n_base_syms} of our code symbols examined")
    print(f"# {PERCENT_RULER_LABEL}")
    if args.all:
        hist = defaultdict(int)
        for r in rows:
            hist[r["delta"]] += 1
        print("\n# delta histogram (all %d rows, before any --limit)" % len(rows))
        for d in sorted(hist):
            print(f"  {d:>+6d}: {hist[d]}")
        print()

    # DISPLAY-ONLY from here down: every count above is computed on the full set.
    shown = rows[:args.limit] if args.limit else rows
    if args.limit and len(shown) < len(rows):
        print(f"# showing {len(shown)} of {len(rows)} rows (--limit {args.limit}); "
              f"the counts and histogram above are the FULL set")
    print(f"# {'match%':>7}  {'tgt':>6} {'base':>6} {'delta':>6}  symbol")
    for r in shown:
        p = f"{r['percent']:.1f}" if r["percent"] is not None else "  n/a"
        print(f"{p:>9}  0x{r['target_frame']:04x} 0x{r['base_frame']:04x} "
              f"{r['delta']:>+6d}  {r['symbol']}   [{r['source'] or r['unit']}]")

    ucov.emit()
    return cov.emit()


if __name__ == "__main__":
    sys.exit(main())
