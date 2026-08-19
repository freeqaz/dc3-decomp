#!/usr/bin/env python3
"""
vtable_dispatch_scan.py — hunt for WRONG-TARGET VIRTUAL DISPATCH bugs that the
normalized match metric hides (wave-10 Lane A).

Background (the bug class):
  Splash::Suspend / Splash::Resume both read 100.0% NORMALIZED, yet the source
  dispatched the WRONG DxRnd virtual: the target loads vtable slot +0x13c
  (DxRnd::Resume) where our source loaded +0x138 (Suspend), and vice-versa.
  The bug lived ENTIRELY in the relocation/immediate plane that normalized
  scoring (functionRelocDiffs=none) discards. Only a RAW diff shows
  `lwz r11, 0x13c(r12)` vs `lwz r11, 0x138(r12)` on the vtable slot load.
  This is a real behavioral inversion on Xbox AND a native-port correctness risk.

Signal we hunt:
  A function whose NORMALIZED match is high (>= --min-norm, default 98) but whose
  RAW match is < normalized (raw-vs-norm GAP), AND whose RAW diff contains a
  *virtual-call slot load* whose IMMEDIATE OFFSET differs between target and base:

      lwz rDST, 0xNN(rVT)   target
      lwz rDST, 0xMM(rVT)   base       (NN != MM)

  where rVT is a register that was loaded as a vtable pointer (the classic
  vcall chain is `lwz r12, 0x0(r3); lwz r12, 0xNN(r12); mtctr r12; bctrl`, but
  MSVC PPC also loads the slot into r11/r10/etc. and the vtable base is often
  this+subobject). We require the differing operand to be a *pure immediate*
  (no relocation symbol) so this is genuinely a different SLOT, not a benign
  pooled-string/EH-record/address reloc (which is what most raw-vs-norm gaps are).

This is COMPLEMENTARY to:
  - reloc_strict_classify.py — finds wrong *symbol-name* targets (NameOnly). It
    does NOT catch a same-vtable-symbol-but-different-slot-offset bug, because the
    reloc target symbol is identical; only the immediate displacement differs.
  - audit_normalized_masking.py — buckets gaps broadly into member/reloc/const;
    it DROPS the immediate whenever a reloc symbol is present on the instruction,
    so it can miss the slot-load (which has NO reloc, just an immediate). This
    scanner is the focused vtable-dispatch lens.

Output: JSON list of candidate functions, each with the exact lwz offset-diff
rows and the vcall-chain context, so the triage step can run
`scripts/dump_vtable.py <Class> --diff-pair 0xTGT 0xSRC` to resolve which
virtual each slot is.

Read-only: diffs already-built .obj files (NO --build). Safe to run alongside
the build/permuter fleet. Never writes decomp.db.

Usage:
    python3 scripts/analysis/vtable_dispatch_scan.py [--project DIR]
    python3 scripts/analysis/vtable_dispatch_scan.py --min-norm 98 --workers 8 --out /tmp/scan.json
    python3 scripts/analysis/vtable_dispatch_scan.py --all   # don't bound by norm>=min (every fn with raw<norm)

COVERAGE (see scripts/analysis/coverage.py)
  This scanner's CAP handling was already honest before the coverage retrofit:
  `--limit` defaults to 0 ("no cap"), `capped` was surfaced in stderr AND in the
  JSON, and objdiff errors were counted.  What it did NOT do was state its
  DENOMINATOR: `scanned : 1642` was the size of the post-filter candidate set,
  and the run behind docs/analysis/dispatch-data-rescan-20260818.md concluded
  "the wrong-target-dispatch class looks exhausted" without ever printing the
  48,344 rows in report.json or the 16,920 of them it discarded.  Both numbers
  are now in the COVERAGE block, along with the justification for the discard.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO)
from scripts.analysis.coverage import CoverageReport, add_coverage_args  # noqa: E402

# PPC indexed/word loads that show up in a vcall chain. The slot load is the one
# whose displacement we care about (the vtable pointer load is usually 0x0).
WORD_LOADS = {"lwz", "lwzu"}
# Registers that are NOT a vtable pointer: stack/frame pointer + TOC. Loads off
# these are stack-locals / globals, never a virtual slot.
FRAME_REGS = {"r1", "sp", "r2"}


def base_reg_after_imm(typed_args):
    """For a D-form load `op rDST, imm, rBASE`, return rBASE (the reg after the imm)."""
    seen_imm = False
    for a in typed_args:
        t = a.get("type")
        if t in ("Signed", "Unsigned"):
            seen_imm = True
        elif t == "Register" and seen_imm:
            return a.get("value")
    return None


def imm_of(typed_args):
    for a in typed_args:
        if a.get("type") in ("Signed", "Unsigned"):
            return int(a.get("value"))
    return None


def has_reloc_symbol(typed_args):
    """True if the instruction carries a relocation symbol operand (so the imm
    is an addend into a pooled section, NOT a struct/vtable member offset)."""
    for a in typed_args:
        t = a.get("type")
        if t not in ("Register", "Signed", "Unsigned"):
            v = str(a.get("value", ""))
            # Bare hex/dec address label = intra-fn branch target, not a reloc.
            if re.fullmatch(r"-?0x[0-9a-fA-F]+", v) or re.fullmatch(r"-?\d+", v):
                continue
            return True
    return False


def detect_frame_ptr_regs(instructions, limit=24):
    """MSVC PPC reserves a frame pointer (`subi/addi rN, r1, K`) on large frames.
    Loads through it are stack-locals, not vtable slots. Scan both sides."""
    fp = set()
    for x in instructions[:limit]:
        for side in ("target", "base"):
            ins = x.get(side) or {}
            if ins.get("opcode") in ("subi", "addi"):
                regs = [a.get("value") for a in (ins.get("typed_args") or [])
                        if a.get("type") == "Register"]
                if len(regs) >= 2 and regs[0] not in ("r1", "sp") and regs[1] in ("r1", "sp"):
                    fp.add(regs[0])
    return fp


def reg_set_as_vtable(instructions, idx, vt_reg, frame_regs, lookback=6):
    """Heuristic: was `vt_reg` loaded as a vtable pointer shortly before idx?
    The classic load is `lwz vt_reg, 0x0(rX)` (vtable ptr is at object offset 0)
    or `lwz vt_reg, 0xNN(rX)` for a sub-object vtable. We accept any prior
    `lwz vt_reg, <small-imm>(rOBJ)` where rOBJ is not a frame/TOC reg, i.e. a
    load through `this` or a sub-object. Returns (True, src_imm) if found."""
    for j in range(idx - 1, max(-1, idx - 1 - lookback), -1):
        ins = (instructions[j].get("target") or {})
        op = ins.get("opcode", "")
        targs = ins.get("typed_args") or []
        if op not in WORD_LOADS:
            continue
        regs = [a.get("value") for a in targs if a.get("type") == "Register"]
        if not regs or regs[0] != vt_reg:
            continue
        base = base_reg_after_imm(targs)
        # vtable pointer is loaded from an object pointer (this / sub-object),
        # not from the stack/TOC.
        if base and base not in frame_regs and base not in FRAME_REGS:
            return True, imm_of(targs)
    return False, None


def followed_by_indirect_call(instructions, idx, vt_reg, lookahead=6):
    """Is the slot loaded into a register that is shortly `mtctr`'d then `bctrl`?
    We don't know which dest reg yet, so this is called with the SLOT-load dest.
    Returns True if a `bctr`/`bctrl`/`mtctr` appears within lookahead."""
    for j in range(idx + 1, min(len(instructions), idx + 1 + lookahead)):
        ins = (instructions[j].get("target") or {})
        op = ins.get("opcode", "")
        if op in ("bctrl", "bctr"):
            return True
        # mtctr on the dest reg is a strong vcall signal too.
        if op == "mtctr":
            return True
    return False


def dest_reg(typed_args):
    for a in typed_args:
        if a.get("type") == "Register":
            return a.get("value")
    return None


def scan_one(objdiff, project, symbol, unit):
    """Run a RAW (relocations-counted) read-only objdiff and look for vtable
    slot-load offset diffs. Returns a candidate dict or None."""
    fd, path = tempfile.mkstemp(suffix=".json", prefix="vtscan_")
    os.close(fd)
    try:
        # NO -c functionRelocDiffs=none  => raw_match_percent counts relocations
        # AND raw immediates (the slot offsets we hunt).
        cmd = [objdiff, "diff", "-p", project, symbol, "-u", unit,
               "--include-instructions", "-f", "json", "-o", path]
        r = subprocess.run(cmd, cwd=project, capture_output=True, text=True, timeout=180)
        if r.returncode != 0:
            return {"symbol": symbol, "unit": unit, "error": (r.stderr or "")[-200:]}
        with open(path) as f:
            d = json.load(f)
    except Exception as e:  # noqa: BLE001
        return {"symbol": symbol, "unit": unit, "error": str(e)[:200]}
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass

    ins = d.get("instructions", [])
    frame_regs = detect_frame_ptr_regs(ins)
    hits = []
    for i, x in enumerate(ins):
        if x.get("match_type") != "diff_arg":
            continue
        t = x.get("target") or {}
        b = x.get("base") or {}
        if t.get("opcode") not in WORD_LOADS or b.get("opcode") not in WORD_LOADS:
            continue
        targs = t.get("typed_args") or []
        bargs = b.get("typed_args") or []
        # Must be a pure-immediate offset diff (no reloc symbol) => a real slot,
        # not a pooled-string/EH-record/global address reloc.
        if has_reloc_symbol(targs) or has_reloc_symbol(bargs):
            continue
        t_imm = imm_of(targs)
        b_imm = imm_of(bargs)
        if t_imm is None or b_imm is None or t_imm == b_imm:
            continue
        t_base = base_reg_after_imm(targs)
        b_base = base_reg_after_imm(bargs)
        # base reg must be a general (object/vtable) register, not frame/TOC.
        if (t_base in FRAME_REGS or t_base in frame_regs or
                b_base in FRAME_REGS or b_base in frame_regs):
            continue
        # Confirm this looks like a vtable slot: the base reg was loaded as a
        # vtable pointer earlier, OR the slot is consumed by an indirect call.
        is_vt, vt_src = reg_set_as_vtable(ins, i, t_base, frame_regs)
        dst = dest_reg(targs)
        is_call = followed_by_indirect_call(ins, i, dst)
        confidence = "strong" if (is_vt and is_call) else (
            "medium" if (is_vt or is_call) else "weak")
        hits.append({
            "idx": i,
            "tgt_offset": t_imm, "src_offset": b_imm,
            "tgt_offset_hex": hex(t_imm & 0xffffffff),
            "src_offset_hex": hex(b_imm & 0xffffffff),
            "tgt": f"{t.get('opcode')} {t.get('args')}",
            "src": f"{b.get('opcode')} {b.get('args')}",
            "vt_base_reg": t_base,
            "loaded_as_vtable": is_vt,
            "vtable_src_offset": (hex(vt_src & 0xffffffff) if vt_src is not None else None),
            "consumed_by_indirect_call": is_call,
            "confidence": confidence,
        })

    if not hits:
        return None
    return {
        "symbol": symbol,
        "unit": unit,
        "demangled": d.get("demangled"),
        "raw": d.get("raw_match_percent"),
        "norm": d.get("normalized_match_percent"),
        "hits": hits,
        "best_confidence": ("strong" if any(h["confidence"] == "strong" for h in hits)
                            else "medium" if any(h["confidence"] == "medium" for h in hits)
                            else "weak"),
    }


def gather_candidates(report_path, min_norm, all_gap, cov=None):
    """Bound the candidate set from report.json: functions where normalized >= min_norm
    and raw < normalized (the raw-vs-norm GAP). No silent cap — returns ALL.

    DENOMINATOR NOTE — the `raw is None` skip, justified and counted.
    `fuzzy_match_percent` (here: `raw`) is a key objdiff only emits for
    functions WE DEFINE; 16,920 of the 48,344 rows in this tree lack it.  That
    is the same population the `fake_impl_scan` bare `continue` hid, so the skip
    is written down rather than left implicit.

    Why the skip is SUBSTANTIVELY DEFENSIBLE here (unlike in fake_impl_scan):
    this scanner hunts a raw-vs-norm GAP, and a row with no raw score cannot
    exhibit one.  16,919 of the 16,920 also carry `match_percent_normalized ==
    0.0` — there is no body on our side, so there is no vtable slot load to
    compare and nothing for this lens to see.

    ...but the justification is NOT universal, and the code says so.  Exactly
    one row breaks the "all of them are norm == 0.0" claim:
    `RndShaderDepthVolume::CalcShaderOpts` (default/system/rndobj/Shader) has
    `match_percent_normalized == 3.59375` and no `fuzzy_match_percent`.  It gets
    its own drop slug so that a future reader can see the exception exists
    instead of inheriting a rounded-off generalisation.
    """
    rep = json.load(open(report_path))
    out = []
    for u in rep["units"]:
        un = u.get("name")
        for f in (u.get("functions") or []):
            n = f.get("match_percent_normalized")
            raw = f.get("fuzzy_match_percent")
            if n is None:
                if cov:
                    cov.drop("no-normalized-percent", 1,
                             note="row carries no match_percent_normalized")
                continue
            if raw is None:
                if cov:
                    if n:
                        cov.drop("no-fuzzy-percent-but-nonzero-norm", 1,
                                 note="NOT covered by the norm==0 justification — a "
                                      "gap is unmeasurable here, not impossible")
                    else:
                        cov.drop("no-fuzzy-percent-norm-zero", 1,
                                 note="objdiff emits no fuzzy_match_percent for functions "
                                      "we do not define; norm==0 so no raw-vs-norm gap "
                                      "can exist")
                continue
            if raw >= n:  # no gap
                if cov:
                    cov.drop("no-raw-vs-norm-gap", 1,
                             note="raw >= normalized: nothing hidden by normalization")
                continue
            if not all_gap and n < min_norm:
                if cov:
                    cov.drop("below---min-norm", 1,
                             note=f"normalized < {min_norm}; use --all to include")
                continue
            out.append((f["name"], un, raw, n))
    # Full tie-break on the symbol: rows sharing a raw% must not be able to swap
    # places between runs, or a `--limit` cut becomes nondeterministic.
    out.sort(key=lambda r: (r[2], r[0]))  # lowest raw first
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", default=REPO)
    ap.add_argument("--objdiff", default=os.path.join(REPO, "bin", "objdiff-cli"))
    ap.add_argument("--report", default=None)
    ap.add_argument("--min-norm", type=float, default=98.0,
                    help="only consider fns with normalized >= this (default 98)")
    ap.add_argument("--all", dest="all_gap", action="store_true",
                    help="consider EVERY fn where raw<norm regardless of norm")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--limit", type=int, default=0, help="0 = no cap (honest)")
    ap.add_argument("--out", default=os.path.join(
        os.environ.get("TMPDIR", "/tmp"), "vtable_dispatch_scan.json"))
    add_coverage_args(ap)
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    report = args.report or os.path.join(project, "build", "373307D9", "report.json")
    objdiff = args.objdiff if os.path.isfile(args.objdiff) else os.path.join(REPO, "bin", "objdiff-cli")

    cov = CoverageReport("vtable_dispatch_scan", args=args)
    with open(report) as _f:
        cov.universe(sum(len(u.get("functions") or [])
                         for u in json.load(_f).get("units", [])),
                     "function rows in report.json")
    cov.extra("report", report)
    cov.extra("project", project)

    cands = gather_candidates(report, args.min_norm, args.all_gap, cov=cov)
    capped = False
    n_cands = len(cands)
    if args.limit and len(cands) > args.limit:
        cands = cands[:args.limit]
        capped = True
        cov.cap("--limit", args.limit, before=n_cands, after=len(cands),
                note="never examined; the cut is off the TOP of a raw%-ascending sort")
    cov.extra("candidates", n_cands)
    print(f"[vtable_dispatch_scan] project={project}", file=sys.stderr)
    print(f"[vtable_dispatch_scan] candidate gap set (raw<norm, "
          f"{'all' if args.all_gap else f'norm>={args.min_norm}'}): {len(cands)}"
          f"{' (CAPPED)' if capped else ''}", file=sys.stderr)

    results = []
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(scan_one, objdiff, project, sym, un): sym
                for sym, un, *_ in cands}
        done = 0
        for fut in as_completed(futs):
            r = fut.result()
            done += 1
            if done % 50 == 0:
                print(f"  scanned {done}/{len(cands)}", file=sys.stderr)
            if r:
                results.append(r)

    errors = [r for r in results if r.get("error")]
    hits = [r for r in results if r.get("hits")]
    # An objdiff failure means the function was NOT inspected — it is a drop,
    # not a silent clean. The remainder really were diffed.
    cov.drop("objdiff-failed", len(errors), note="function was NOT inspected")
    cov.examine(len(cands) - len(errors))

    # DETERMINISM: `results` is appended in as_completed() order, which is thread
    # scheduling. Every collection below therefore needs a TOTAL order —
    # (confidence, raw) alone ties constantly, and `errors[:20]` used to slice a
    # completion-ordered list, so the 20 errors you saw depended on the weather.
    hits.sort(key=lambda r: ({"strong": 0, "medium": 1, "weak": 2}[r["best_confidence"]],
                             r["raw"] if r["raw"] is not None else 0,
                             r["symbol"]))
    errors.sort(key=lambda r: (r["symbol"], r.get("unit") or ""))

    out = {
        "scanned": len(cands),
        "capped": capped,
        "min_norm": args.min_norm,
        "all_gap": args.all_gap,
        "hit_count": len(hits),
        "error_count": len(errors),
        "hits": hits,
        "errors": [{"symbol": e["symbol"], "unit": e["unit"], "error": e["error"]}
                   for e in errors][:20],
        "errors_truncated_in_this_listing": max(0, len(errors) - 20),
        "_coverage": cov.as_dict(),
    }
    json.dump(out, open(args.out, "w"), indent=1)

    print("\n" + "=" * 72, file=sys.stderr)
    print("VTABLE-DISPATCH OFFSET-DIFF SCAN", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    # `scanned` is the POST-FILTER candidate count. Print the whole chain so it
    # can never again be read as "we looked at everything".
    _d = cov.as_dict()
    print(f"report rows : {_d['universe']} (the denominator)", file=sys.stderr)
    print(f"candidates  : {n_cands} (raw<norm"
          f"{'' if args.all_gap else f' and norm>={args.min_norm}'})", file=sys.stderr)
    print(f"scanned     : {len(cands)} (candidates after --limit)", file=sys.stderr)
    print(f"inspected   : {_d['examined']} (candidates objdiff actually diffed)", file=sys.stderr)
    print(f"hits        : {len(hits)} (fns with a pure-immediate word-load offset diff)", file=sys.stderr)
    print(f"errors      : {len(errors)} (NOT inspected)", file=sys.stderr)
    print(f"out         : {args.out}", file=sys.stderr)
    for h in hits:
        print(f"\n[{h['best_confidence']}] {h['unit']}  raw=%.2f norm=%.2f" % (h["raw"], h["norm"]),
              file=sys.stderr)
        print(f"   {h['symbol']}", file=sys.stderr)
        for hh in h["hits"]:
            print(f"   slot {hh['tgt_offset_hex']} (tgt) vs {hh['src_offset_hex']} (src)"
                  f"  vt_reg={hh['vt_base_reg']} loaded_as_vt={hh['loaded_as_vtable']}"
                  f" call={hh['consumed_by_indirect_call']} [{hh['confidence']}]",
                  file=sys.stderr)
            print(f"      TGT: {hh['tgt']}", file=sys.stderr)
            print(f"      SRC: {hh['src']}", file=sys.stderr)

    sys.exit(cov.emit())


if __name__ == "__main__":
    main()
