#!/usr/bin/env python3
"""
fake_impl_scan.py — hunt for FAKE / STUB FUNCTION BODIES: functions whose
*compiled* body is trivial (an empty `blr`, a single field load, a constant
return) where the TARGET body is substantial real code (wave-14 Lane A).

Background (the bug class):
  These are not register-allocation or scheduling residuals — they are MISSING
  IMPLEMENTATIONS. The source compiles to a tiny stub (often literally `return;`
  / `return 0;` / `return mField;`) while the original function does real work.
  Wave-13 recovered five of these BY HAND:
      Voice::IsPlaying                 20.5 -> 98.0   (real "is voice playing" logic)
      Synth360::SetupHeadsetSubmixes   21.0 -> 84.6
      FxSendSynapse360::SyncEffectParams 1.1 -> 89.7
      FxSendSynapse360::CreateFx       11.1 -> 100
      (+ HeadsetXferEffect ctor)
  This scanner makes finding the rest systematic.

The signal (per function, from objdiff-cli --include-instructions JSON):
  objdiff aligns the two instruction streams. A row is:
      target-only  (has 'target', no 'base')  => an instruction the TARGET has
                                                  that OUR body LACKS (= missing)
      base-only    (has 'base', no 'target')  => an instruction WE emit that the
                                                  target lacks (= spurious)
      paired       (has both)                  => aligned (equal / diff / replace)
  NOTE: the match_type LABEL ('insert' vs 'delete') flips with objdiff's chosen
  alignment direction, so we classify by SIDE PRESENCE, never by the label.

  A FAKE IMPL is the asymmetric case:
      missing_ratio  = target_only / target_total       (target has lots we lack)
      our_body_insns = rows with a base side            (how big OUR body is)
      our_real_insns = our_body_insns minus prologue/epilogue boilerplate
                       (savegprlr/restgprlr, stwu/addi sp, mflr/mtlr, blr, nops)
  We flag when missing_ratio is HIGH and our_real_insns is LOW: the target is
  big, our body is a stub. The lower our_pct and the bigger target_size, the
  stronger the candidate.

  We also surface the trivial-body shape so triage knows what kind of stub it is:
      empty        our body is only prologue/epilogue (`blr`)            -> return;
      const        our body is a couple of li/blr                        -> return K;
      field-load   our body is a single lwz/lfs off this + return        -> return mX;
      small        a handful of real insns                               -> partial

TWO TIERS, AND THE ONE THAT WAS INVISIBLE UNTIL 2026-08-19:
  Tier 1 "we wrote a WRONG/SHORT body"  — our object defines the symbol, objdiff
      scores it, the row carries `fuzzy_match_percent`. This is what the scanner
      measured from wave 14 through wave 23, and this tier really is exhausted
      (broad sweeps re-find only Synth360::PreInit, MemAlloc, NgEnviron::Select).
  Tier 2 "we wrote NO body at all"      — the target defines the symbol, our
      object does not. objdiff emits `match_percent_normalized: 0.0` and OMITS
      `fuzzy_match_percent` entirely. `gather_candidates` used to `continue` on
      the missing key, so **1030 authorable rows (849 >= 24B) never entered the
      candidate set**, and four consecutive broad sweeps reported the pool
      "EXHAUSTED" while never having looked at them. Fixed; see gather_candidates.
      Triage note: tier 2 is dominated by non-authorable noise — per-TU template
      instantiations, MSVC COMDAT copies of header inlines, `merged_`/`fn_` ICF
      and EH artifacts, and the xdk/binkxenon SDK trees. Filter those before
      reading a count. The residue that IS actionable is the COMDAT-PLACEMENT
      class: a function we define out-of-line in a .cpp that the original had
      `inline` in a header, so the target's single folded copy lands in some
      other TU's address range and we score 0 there (fixed for MakeRotMatrixX/
      Y/Z in Rot.h, 3 x 144B, 0 -> 100%).

DISTINGUISHED FROM (deliberately NOT flagged):
  - hard divergences where OUR body is *substantial but wrong* (e.g. RndShader
    CalcShaderOpts at 36% with hundreds of our-side instructions): those are
    real-code-vs-real-code mismatches, not stubs. our_real_insns is large there,
    so missing_ratio stays low and they fall below --min-missing-ratio.
  - deliberate platform/dependency stubs that we KEEP (e.g. MemAlloc routing to
    malloc until MemHeap is decompiled, or Synth360::PreInit which needs XAudio2).
    The scanner still surfaces these (they ARE fake bodies); triage decides which
    are recoverable vs intentionally-stubbed. Verdict is a guess, not a mandate.

Read-only: diffs already-built .obj files (NO --build). Safe alongside the
build/permuter fleet. Never writes decomp.db.

Usage:
    python3 scripts/analysis/fake_impl_scan.py --project .
    python3 scripts/analysis/fake_impl_scan.py --project . --units-grep synth_xbox
    python3 scripts/analysis/fake_impl_scan.py --project . --max-pct 60 --min-target-size 120
    python3 scripts/analysis/fake_impl_scan.py --project . --validate-wave13   # recall check
"""
import argparse
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Prologue/epilogue + framing instructions that every function carries; these do
# NOT count as "real body". Used to decide if OUR body is a trivial stub.
BOILERPLATE_OPCODES = {
    "blr", "nop", "mflr", "mtlr",
    "stwu", "stw", "lwz",          # only when off sp/r1 (filtered below)
    "stmw", "lmw", "stfd", "lfd",  # callee-save spills (off sp/r1)
    "addi", "subi", "addic",       # only when adjusting sp (filtered below)
    "b",                           # tail-branch to __restgprlr_* etc (filtered below)
}
# Calls into the MSVC PPC save/restore runtime helpers = pure prologue/epilogue.
SAVE_RESTORE_RE = re.compile(r"__(save|rest)(gprlr|fpr|gpr|vmx)")
# Units that are not source-authorable by us. We exclude these from the candidate
# set (they are the XEX/thirdparty side we don't own).
NON_AUTHORABLE_PREFIXES = ("xdk/", "default/xdk/", "thirdparty/", "default/thirdparty/")


def is_framing_instruction(op, args, sp_only):
    """Is this base-side instruction pure prologue/epilogue / framing boilerplate
    (i.e. NOT real work)? `args` is the textual operand string; `sp_only` filters
    loads/stores/adds so only the sp/r1-relative framing ones are dropped."""
    if op in ("nop", "blr", "mflr", "mtlr"):
        return True
    if SAVE_RESTORE_RE.search(args or ""):
        return True  # bl __savegprlr_29 / b __restgprlr_29
    if op in ("stmw", "lmw", "stfd", "lfd", "stw", "lwz"):
        # callee-save spill/reload through sp/r1 only
        return bool(re.search(r"\br1\b|\bsp\b", args or ""))
    if op in ("stwu", "addi", "subi", "addic"):
        # stack-pointer adjustment only (stwu r1,..,r1 ; subi r31,r1,K ; addi r1,r31,K)
        return bool(re.search(r"\br1\b|\bsp\b", args or ""))
    return False


def classify_body(base_real_ops):
    """Given the list of (opcode,args) of OUR non-framing body instructions,
    name the stub shape."""
    n = len(base_real_ops)
    if n == 0:
        return "empty"      # body is just blr
    ops = [o for o, _ in base_real_ops]
    if n <= 2 and all(o in ("li", "lis", "ori", "addi") for o in ops):
        return "const"      # return K;
    if n <= 2 and all(o in ("lwz", "lfs", "lha", "lbz", "lhz", "ld", "lwa") for o in ops):
        return "field-load"  # return mField;
    if n <= 5:
        return "small"      # tiny partial body
    return "substantial"    # NOT a stub (real-but-wrong code)


def scan_one(objdiff, project, symbol, unit, our_pct, target_size):
    cmd = [objdiff, "diff", symbol, "-p", project, "-u", unit,
           "--include-instructions", "-f", "json", "-o", "/dev/stdout"]
    try:
        r = subprocess.run(cmd, cwd=project, capture_output=True, text=True, timeout=180)
        if r.returncode != 0:
            return {"symbol": symbol, "unit": unit, "error": (r.stderr or "")[-160:]}
        d = json.loads(r.stdout)
    except Exception as e:  # noqa: BLE001
        return {"symbol": symbol, "unit": unit, "error": str(e)[:160]}

    ins = d.get("instructions", [])
    if not ins:
        return None
    target_only = 0          # target has it, we lack it  (= missing real code)
    base_only = 0            # we have it, target lacks it (= spurious)
    target_total = 0
    base_real_ops = []       # OUR non-framing body instructions
    for x in ins:
        t = x.get("target")
        b = x.get("base")
        if t:
            target_total += 1
        if t and not b:
            target_only += 1
        elif b and not t:
            base_only += 1
        if b:
            op = b.get("opcode", "")
            args = b.get("args", "")
            if not is_framing_instruction(op, args, sp_only=True):
                base_real_ops.append((op, args))

    if target_total == 0:
        return None
    missing_ratio = target_only / target_total
    our_real = len(base_real_ops)
    body_shape = classify_body(base_real_ops)

    # Verdict guess. Two distinct fake-impl shapes, separated by HOW MUCH of the
    # target we are missing:
    #   * a TRIVIAL stub (empty/const/field-load/handful) — obvious fake body.
    #   * an INCOMPLETE impl — our body has real code but the target has a LOT
    #     more (high missing-ratio). Wave-13's Voice::IsPlaying (our_real=25,
    #     miss=68%) and Synth360::SetupHeadsetSubmixes (our_real=72, miss=67%)
    #     were THIS class: not empty, but a partial/old hack against a much
    #     larger real body. This is the tier hand-recovery keeps finding, so the
    #     scanner must surface it too.
    if our_real == 0:
        verdict = "empty-stub"
    elif body_shape in ("const", "field-load"):
        verdict = "trivial-stub"
    elif body_shape == "small":
        verdict = "partial-stub"
    else:
        # substantial body — distinguish incomplete-impl (we're way short of the
        # target) from a real-but-wrong divergence (sizes are comparable).
        verdict = "incomplete-impl" if missing_ratio >= 0.5 else "real-code-divergence"

    return {
        "symbol": symbol,
        "unit": unit,
        "demangled": d.get("demangled"),
        "our_pct": our_pct,
        "norm": d.get("normalized_match_percent"),
        "target_size": target_size,
        "target_insns": target_total,
        "missing_insns": target_only,
        "spurious_insns": base_only,
        "our_real_insns": our_real,
        "missing_ratio": round(missing_ratio, 4),
        "body_shape": body_shape,
        "verdict": verdict,
        "body_preview": [f"{o} {a}".strip() for o, a in base_real_ops[:6]],
    }


def gather_candidates(report_path, max_pct, min_target_size, units_grep):
    """Candidate rows from report.json.

    BIT-ROT FIX (2026-08-19): the report does NOT always carry
    `fuzzy_match_percent`.  For a function whose symbol has **no base body at
    all** — the target defines it, our object does not — objdiff emits only
    `match_percent_normalized` (0.0) and omits the fuzzy key entirely.  The old
    code did `if pct is None: continue`, which silently discarded **1030
    authorable rows (849 of them >= 24B)** — and those are precisely the most
    extreme fake-impl class: functions we never wrote.  `DxShaderMgr::
    SetVConstant(VShaderConstant, const Vector4&)` (22 insns, "Stub (High)")
    sat in that hole from wave-14 through wave-23, across four broad sweeps
    that all reported the pool "EXHAUSTED".  Fall back to the normalized key,
    and never drop a row without counting it.
    """
    rep = json.load(open(report_path))
    out = []
    skipped_no_pct = []
    for u in rep["units"]:
        un = u.get("name") or ""
        if un.startswith(NON_AUTHORABLE_PREFIXES):
            continue
        if units_grep and units_grep not in un:
            continue
        for f in (u.get("functions") or []):
            pct = f.get("fuzzy_match_percent")
            if pct is None:
                # No fuzzy score => no base body was diffed. The normalized key
                # is still present and is the honest score (0.0 = absent).
                pct = f.get("match_percent_normalized")
            sz = int(f.get("size") or 0)
            if pct is None:
                skipped_no_pct.append((f.get("name"), un))
                continue
            if pct > max_pct:
                continue
            if sz < min_target_size:
                continue
            out.append((f["name"], un, pct, sz))
    # Tie-break on symbol so the candidate order (and every downstream sort that
    # inherits it) is byte-stable across runs.
    out.sort(key=lambda r: (r[2], -r[3], r[0]))  # lowest pct, largest target first
    return out, skipped_no_pct


# Wave-13 known positives — the scanner MUST re-find these (recall validation).
# (These were recovered in wave-13 so they are no longer stubs on the current
#  tree; validate-wave13 re-checks them against the PARENT commit of the wave-13
#  recovery so the original fake bodies are present.)
WAVE13_POSITIVES = [
    ("?IsPlaying@Voice@@QAA_NXZ", "synth_xbox/Voice"),
    ("?SetupHeadsetSubmixes@Synth360@@", "synth_xbox/Synth"),
    ("?SyncEffectParams@FxSendSynapse360@@", "synth_xbox/FxSendSynapse"),
    ("?CreateFx@FxSendSynapse360@@", "synth_xbox/FxSendSynapse"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--project", default=REPO)
    ap.add_argument("--objdiff", default=os.path.join(REPO, "bin", "objdiff-cli"))
    ap.add_argument("--report", default=None)
    ap.add_argument("--max-pct", type=float, default=70.0,
                    help="only consider fns with fuzzy match <= this (default 70)")
    ap.add_argument("--min-target-size", type=int, default=80,
                    help="only consider fns whose TARGET is >= this many bytes (default 80)")
    ap.add_argument("--min-missing-ratio", type=float, default=0.55,
                    help="flag when this fraction of the target is MISSING from our body (default 0.55)")
    ap.add_argument("--max-our-real", type=int, default=6,
                    help="our non-framing body must be <= this many insns to count as a TRIVIAL stub (default 6)")
    ap.add_argument("--min-incomplete-ratio", type=float, default=0.5,
                    help="flag a SUBSTANTIAL body as incomplete-impl when this fraction of the "
                         "target is missing (catches partial bodies like Voice::IsPlaying; default 0.5)")
    ap.add_argument("--max-incomplete-real", type=int, default=80,
                    help="an incomplete-impl's body must be <= this many real insns; above this it is a "
                         "heavily-inlined real-code divergence, not a fake (e.g. SpotlightDrawer our_real=261); "
                         "default 80. Recall note: wave-13 SetupHeadsetSubmixes had our_real=72.")
    ap.add_argument("--units-grep", default=None, help="restrict to units containing this substring")
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--limit", type=int, default=0, help="0 = no cap (honest)")
    ap.add_argument("--include-divergences", action="store_true",
                    help="also list real-code-vs-real-code divergences (not stubs)")
    ap.add_argument("--validate-wave13", action="store_true",
                    help="report recall on the wave-13 known positives, then continue scan")
    ap.add_argument("--out", default=os.path.join(
        os.environ.get("TMPDIR", "/tmp"), "fake_impl_scan.json"))
    args = ap.parse_args()

    project = os.path.abspath(args.project)
    report = args.report or os.path.join(project, "build", "373307D9", "report.json")
    objdiff = args.objdiff if os.path.isfile(args.objdiff) else os.path.join(REPO, "bin", "objdiff-cli")

    cands, skipped_no_pct = gather_candidates(
        report, args.max_pct, args.min_target_size, args.units_grep)
    if skipped_no_pct:
        print(f"[fake_impl_scan] WARNING: {len(skipped_no_pct)} authorable rows carried "
              f"neither fuzzy_match_percent nor match_percent_normalized and were skipped",
              file=sys.stderr)
    capped = False
    if args.limit and len(cands) > args.limit:
        cands = cands[:args.limit]
        capped = True
    print(f"[fake_impl_scan] project={project}", file=sys.stderr)
    print(f"[fake_impl_scan] candidate set (pct<={args.max_pct}, target>={args.min_target_size}B"
          f"{', unit~'+args.units_grep if args.units_grep else ''}): {len(cands)}"
          f"{' (CAPPED)' if capped else ''}", file=sys.stderr)

    results = []
    unscannable = []          # objdiff produced no instruction rows at all
    with ThreadPoolExecutor(max_workers=args.workers) as ex:
        futs = {ex.submit(scan_one, objdiff, project, sym, un, pct, sz): (sym, un)
                for sym, un, pct, sz in cands}
        done = 0
        for fut in as_completed(futs):
            r = fut.result()
            done += 1
            if done % 50 == 0:
                print(f"  scanned {done}/{len(cands)}", file=sys.stderr)
            if r:
                results.append(r)
            else:
                # scan_one returns None when objdiff emitted no instructions (or
                # no target side). Do NOT let these vanish: a dropped row is
                # indistinguishable from a clean one in the summary.
                sym, un = futs[fut]
                unscannable.append({"symbol": sym, "unit": un})

    errors = [r for r in results if r.get("error")]
    scanned = [r for r in results if not r.get("error")]

    # A FAKE IMPL is either:
    #   (a) a TRIVIAL body (empty/const/field-load/small) with a high missing-ratio, OR
    #   (b) an INCOMPLETE-IMPL: a substantial body that is still missing a large
    #       fraction of the target (Voice::IsPlaying class).
    def is_fake(r):
        if r["verdict"] == "real-code-divergence":
            return False
        if r["verdict"] == "incomplete-impl":
            # Genuinely short-of-target AND not just heavily-inlined real code.
            return (r["missing_ratio"] >= args.min_incomplete_ratio
                    and r["our_real_insns"] <= args.max_incomplete_real)
        # trivial tiers
        return (r["missing_ratio"] >= args.min_missing_ratio
                and r["our_real_insns"] <= args.max_our_real)

    fakes = [r for r in scanned if is_fake(r)]
    # Divergences = explicit real-code divergences PLUS incomplete-impls that bust
    # the body-size cap (heavily-inlined real code, not a fake body).
    divergences = [r for r in scanned
                   if r["verdict"] == "real-code-divergence"
                   or (r["verdict"] == "incomplete-impl" and not is_fake(r))]

    order = {"empty-stub": 0, "trivial-stub": 1, "partial-stub": 2, "incomplete-impl": 3}
    # symbol last = total order, so two runs emit byte-identical lists
    fakes.sort(key=lambda r: (order.get(r["verdict"], 9), -r["target_size"],
                              r["our_pct"], r["symbol"]))

    out = {
        "scanned": len(cands),
        "capped": capped,
        "max_pct": args.max_pct,
        "min_target_size": args.min_target_size,
        "min_missing_ratio": args.min_missing_ratio,
        "max_our_real": args.max_our_real,
        "fake_count": len(fakes),
        "divergence_count": len(divergences),
        "error_count": len(errors),
        "unscannable_count": len(unscannable),
        "skipped_no_pct_count": len(skipped_no_pct),
        "unscannable": sorted(unscannable, key=lambda r: (r["unit"], r["symbol"])),
        "fakes": fakes,
        "divergences": divergences if args.include_divergences else [],
        "errors": [{"symbol": e["symbol"], "unit": e["unit"], "error": e["error"]}
                   for e in errors][:20],
    }
    json.dump(out, open(args.out, "w"), indent=1)

    if args.validate_wave13:
        # Recall: how many wave-13 positives does the scanner's signal re-find?
        # Match by symbol-prefix + unit-substring against the FULL scanned set
        # (not just the flagged fakes) and report whether each would be flagged.
        print("\n" + "=" * 72, file=sys.stderr)
        print("WAVE-13 RECALL CHECK", file=sys.stderr)
        print("=" * 72, file=sys.stderr)
        flagged_keys = {(r["symbol"], r["unit"]) for r in fakes}
        found = 0
        for sym_prefix, unit_sub in WAVE13_POSITIVES:
            hits = [r for r in scanned
                    if r["symbol"].startswith(sym_prefix.rstrip("@")) and unit_sub in r["unit"]]
            if hits:
                h = hits[0]
                would_flag = (h["symbol"], h["unit"]) in flagged_keys
                status = "FLAGGED" if would_flag else f"present (verdict={h['verdict']}, "\
                    f"miss={h['missing_ratio']}, our_real={h['our_real_insns']})"
                if would_flag:
                    found += 1
                print(f"  {sym_prefix:50s} {status}", file=sys.stderr)
            else:
                print(f"  {sym_prefix:50s} NOT IN CANDIDATE SET "
                      f"(already recovered above max_pct={args.max_pct})", file=sys.stderr)
        print(f"  recall (of present): {found}/{len([1 for s,u in WAVE13_POSITIVES])} "
              f"(NOTE: run against the wave-13 PARENT commit so the stubs are present)",
              file=sys.stderr)

    print("\n" + "=" * 72, file=sys.stderr)
    print("FAKE-IMPL SCAN", file=sys.stderr)
    print("=" * 72, file=sys.stderr)
    print(f"scanned       : {len(cands)}", file=sys.stderr)
    print(f"FAKE IMPLS    : {len(fakes)} (trivial body vs substantial target)", file=sys.stderr)
    print(f"divergences   : {len(divergences)} (real-but-wrong code, NOT stubs)", file=sys.stderr)
    print(f"unscannable   : {len(unscannable)} (objdiff emitted no instruction rows)", file=sys.stderr)
    print(f"errors        : {len(errors)}", file=sys.stderr)
    print(f"out           : {args.out}", file=sys.stderr)
    for h in fakes:
        print(f"\n[{h['verdict']}] {h['unit']}  pct=%.1f target=%dB miss=%.0f%% our_real=%d"
              % (h["our_pct"], h["target_size"], h["missing_ratio"] * 100, h["our_real_insns"]),
              file=sys.stderr)
        print(f"   {h['symbol']}", file=sys.stderr)
        if h["demangled"]:
            print(f"   {h['demangled']}", file=sys.stderr)
        if h["body_preview"]:
            print(f"   our body: {h['body_preview']}", file=sys.stderr)


if __name__ == "__main__":
    main()
