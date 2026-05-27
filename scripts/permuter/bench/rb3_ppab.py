"""RB3 preprocess-cache A/B with three modes: OFF, ON-strict, ON-nonstrict.

This is the RB3 counterpart to ``preprocess_cache_ab.py``. The stock harness
forces ``PERMUTER_PREPROCESS_CACHE_STRICT=1`` (byte-identical .o oracle), which
on the mwcceppc/RB3 toolchain rejects nearly every splice because splicing
shifts debug-info line numbers (the .o is never byte-identical even when the
objdiff score is identical). That oracle is correct for catching *logic*
divergence, but it is NOT the path a default-on cache would use.

So we run three modes against the SAME variant list per function:

  OFF          PERMUTER_PREPROCESS_CACHE unset (baseline).
  ON-strict    cache ON + STRICT=1 (divergence oracle; near-zero hits on RB3).
  ON-nonstrict cache ON, STRICT off — the cache validates by objdiff-score
               equivalence (its own non-strict default) and the real default-on
               path. This is where the speedup actually shows up.

Divergence requirement (zero) is checked OFF-vs-ON for BOTH on-modes:
per-variant objdiff match% must be identical to 4 decimals.

Speedup is the pooled per-call compile-run ms (OFF / ON), the contention-robust
signal. Reports per-mode fast-hit / fallback counts and per-function detail.

Run from the RB3 repo root via /tmp/claude/rb3_bench_driver.py-style chdir, or
directly: this module assumes CWD == RB3 and the worktree is on sys.path[0].
"""
from __future__ import annotations

import argparse
import json
import os
import statistics
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

WT = Path("/tmp/claude/wt-rb3-sweep")
RB3 = Path("/home/free/code/milohax/rb3")
BENCH_SET = WT / "scripts/permuter/bench/bench_set_rb3.json"
SCORE_DECIMALS = 4

_CACHE_DIRS: list[Path] = []


def _isolate_score_cache():
    from scripts.permuter import score_cache
    d = Path(tempfile.mkdtemp(prefix="rb3_ppab_cache_"))
    fresh = d / "permuter_cache.db"
    score_cache._CACHE_DB = fresh
    score_cache.ScoreCache.__init__.__defaults__ = (fresh,)
    _CACHE_DIRS.append(d)
    return d


def _teardown():
    import shutil
    for d in _CACHE_DIRS:
        shutil.rmtree(d, ignore_errors=True)
    _CACHE_DIRS.clear()


def _generate_variants(fn, patterns, max_variants):
    from scripts.permuter.scorer import Scorer
    from scripts.permuter.extractor import extract_function
    from scripts.permuter.generator import generate_variants
    from scripts.permuter.composer import get_compose_pairs, available_context_keys

    src = RB3 / fn["source_path"]
    ctx = extract_function(src, fn["qualified_name"])
    os.environ.pop("PERMUTER_PREPROCESS_CACHE", None)
    os.environ.pop("PERMUTER_PREPROCESS_CACHE_STRICT", None)
    _isolate_score_cache()
    baseline = 0.0
    with Scorer(src, fn["symbol"], unit=fn["unit"]) as sc:
        baseline = sc.get_baseline(guided=True)
        if sc.diagnosis:
            ctx.diagnosis = sc.diagnosis
    compose_pairs = get_compose_pairs(
        diagnosis=ctx.diagnosis, patterns=patterns, hints=None,
        available_context=available_context_keys(ctx),
    )
    variants = list(generate_variants(
        ctx, patterns, max_variants, compose_pairs=compose_pairs,
        chains=None, round_hints=None, failed_patterns=None,
    ))
    return variants, baseline


def _score_mode(fn, variants, workers, mode):
    from scripts.permuter.scorer import Scorer
    from scripts.permuter.profiling import reset_profiler

    src = RB3 / fn["source_path"]
    if mode == "off":
        os.environ.pop("PERMUTER_PREPROCESS_CACHE", None)
        os.environ.pop("PERMUTER_PREPROCESS_CACHE_STRICT", None)
    elif mode == "on-strict":
        os.environ["PERMUTER_PREPROCESS_CACHE"] = "1"
        os.environ["PERMUTER_PREPROCESS_CACHE_STRICT"] = "1"
    elif mode == "on-nonstrict":
        os.environ["PERMUTER_PREPROCESS_CACHE"] = "1"
        os.environ.pop("PERMUTER_PREPROCESS_CACHE_STRICT", None)

    _isolate_score_cache()
    prof = reset_profiler()
    prof.start_wall()
    t0 = time.perf_counter()
    fast_hits = fallbacks = 0
    cache_used = False
    with Scorer(src, fn["symbol"], unit=fn["unit"]) as sc:
        sc.get_baseline(guided=True)
        results = sc.score_batch(variants, workers=workers)
        if mode != "off" and sc._pp_cache is not None and not sc._pp_cache.disabled:
            cache_used = True
            fast_hits = sc._pp_cache.fast_hits
            fallbacks = sc._pp_cache.fallbacks
    wall = time.perf_counter() - t0
    prof.stop_wall()
    cb = prof.buckets.get("compile-run")
    cr_s = cb.seconds if cb else 0.0
    cr_n = cb.count if cb else 0
    return {
        "scores": [round(r.match_percent, SCORE_DECIMALS) for r in results],
        "build_success": [bool(r.build_success) for r in results],
        "wall_seconds": round(wall, 3),
        "compile_run_seconds": round(cr_s, 4),
        "compile_run_calls": cr_n,
        "compile_run_ms_per_call": round(1000.0 * cr_s / cr_n, 3) if cr_n else None,
        "fast_hits": fast_hits, "fallbacks": fallbacks, "cache_used": cache_used,
    }


def _divergences(off, on):
    n = min(len(off["scores"]), len(on["scores"]))
    divs = []
    for i in range(n):
        if off["scores"][i] != on["scores"][i]:
            divs.append({"i": i, "off": off["scores"][i], "on": on["scores"][i]})
    return n, divs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--start", type=int, default=0,
                    help="Skip the first N bench functions (slice with --count).")
    ap.add_argument("--count", type=int, default=0,
                    help="Run at most this many functions from --start (0 = all).")
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--checkpoint", type=Path, default=None,
                    help="Flush per-function records here after EACH function so a "
                         "kill never loses completed work.")
    args = ap.parse_args()

    os.environ["PERMUTER_PROFILE"] = "1"
    from scripts.permuter.patterns import get_all_patterns
    patterns = get_all_patterns()

    bench = json.loads(BENCH_SET.read_text())
    seed = bench["seed_config"]
    workers = seed["workers"]
    functions = bench["functions"]
    if args.limit:
        functions = functions[: args.limit]
    if args.start:
        functions = functions[args.start:]
    if args.count:
        functions = functions[: args.count]

    print(f"[rb3-ppab] {len(functions)} functions, workers={workers}, "
          f"patterns={len(patterns)}", file=sys.stderr)

    def _flush_checkpoint(records):
        if args.checkpoint is None:
            return
        args.checkpoint.write_text(json.dumps(
            {"schema": "rb3-preprocess-cache-ab-checkpoint/1",
             "timestamp": datetime.now(timezone.utc).isoformat(),
             "functions": records}, indent=2))

    per_fn = []
    t_start = time.perf_counter()
    for i, fn in enumerate(functions):
        print(f"\n[{i+1}/{len(functions)}] {fn['qualified_name']}", file=sys.stderr)
        rec = {"qualified_name": fn["qualified_name"], "unit": fn["unit"],
               "band": fn["band"]}
        try:
            variants, baseline = _generate_variants(fn, patterns, seed["max_variants"])
            rec["baseline_percent"] = round(baseline, SCORE_DECIMALS)
            rec["n_variants"] = len(variants)
            if not variants:
                rec["note"] = "no variants"
                per_fn.append(rec); print("   -> no variants", file=sys.stderr); continue
            off = _score_mode(fn, variants, workers, "off")
            on_s = _score_mode(fn, variants, workers, "on-strict")
            on_n = _score_mode(fn, variants, workers, "on-nonstrict")
            n_s, div_s = _divergences(off, on_s)
            n_n, div_n = _divergences(off, on_n)
            rec.update({"off": off, "on_strict": on_s, "on_nonstrict": on_n,
                        "div_strict": div_s, "div_nonstrict": div_n,
                        "n_compared": n_n})
            sp_s = (off["compile_run_ms_per_call"] / on_s["compile_run_ms_per_call"]
                    if off["compile_run_ms_per_call"] and on_s["compile_run_ms_per_call"] else None)
            sp_n = (off["compile_run_ms_per_call"] / on_n["compile_run_ms_per_call"]
                    if off["compile_run_ms_per_call"] and on_n["compile_run_ms_per_call"] else None)
            rec["speedup_strict"] = round(sp_s, 3) if sp_s else None
            rec["speedup_nonstrict"] = round(sp_n, 3) if sp_n else None
            print(f"   -> {n_n}v  div(strict/nonstrict)={len(div_s)}/{len(div_n)}  "
                  f"compile-run {off['compile_run_ms_per_call']}ms -> "
                  f"strict {on_s['compile_run_ms_per_call']}ms ({rec['speedup_strict']}x) / "
                  f"nonstrict {on_n['compile_run_ms_per_call']}ms ({rec['speedup_nonstrict']}x)  "
                  f"hits S={on_s['fast_hits']}/F{on_s['fallbacks']} "
                  f"N={on_n['fast_hits']}/F{on_n['fallbacks']}", file=sys.stderr)
        except Exception as exc:
            import traceback; traceback.print_exc()
            rec["error"] = f"{type(exc).__name__}: {exc}"
            print(f"   -> ERROR {rec['error']}", file=sys.stderr)
        per_fn.append(rec)
        _flush_checkpoint(per_fn)

    wall = time.perf_counter() - t_start
    scored = [f for f in per_fn if "off" in f]

    def pooled(mode_key):
        s = sum(f[mode_key]["compile_run_seconds"] for f in scored)
        n = sum(f[mode_key]["compile_run_calls"] for f in scored)
        return (1000.0 * s / n) if n else None

    off_ms = pooled("off"); s_ms = pooled("on_strict"); n_ms = pooled("on_nonstrict")
    tot_div_s = sum(len(f["div_strict"]) for f in scored)
    tot_div_n = sum(len(f["div_nonstrict"]) for f in scored)
    tot_v = sum(f["n_compared"] for f in scored)
    hits_s = sum(f["on_strict"]["fast_hits"] for f in scored)
    fb_s = sum(f["on_strict"]["fallbacks"] for f in scored)
    hits_n = sum(f["on_nonstrict"]["fast_hits"] for f in scored)
    fb_n = sum(f["on_nonstrict"]["fallbacks"] for f in scored)
    sp_list_n = [f["speedup_nonstrict"] for f in scored if f.get("speedup_nonstrict")]
    sp_list_s = [f["speedup_strict"] for f in scored if f.get("speedup_strict")]
    cache_built_n = sum(1 for f in scored if f["on_nonstrict"]["cache_used"])
    cache_built_s = sum(1 for f in scored if f["on_strict"]["cache_used"])

    summary = {
        "n_functions_scored": len(scored),
        "total_variants_compared": tot_v,
        "divergences_strict": tot_div_s,
        "divergences_nonstrict": tot_div_n,
        "pooled_compile_run_ms_off": round(off_ms, 3) if off_ms else None,
        "pooled_compile_run_ms_strict": round(s_ms, 3) if s_ms else None,
        "pooled_compile_run_ms_nonstrict": round(n_ms, 3) if n_ms else None,
        "pooled_speedup_strict": round(off_ms / s_ms, 3) if (off_ms and s_ms) else None,
        "pooled_speedup_nonstrict": round(off_ms / n_ms, 3) if (off_ms and n_ms) else None,
        "median_speedup_strict": round(statistics.median(sp_list_s), 3) if sp_list_s else None,
        "median_speedup_nonstrict": round(statistics.median(sp_list_n), 3) if sp_list_n else None,
        "fast_hits_strict": hits_s, "fallbacks_strict": fb_s,
        "fast_hits_nonstrict": hits_n, "fallbacks_nonstrict": fb_n,
        "hit_rate_nonstrict": round(hits_n / (hits_n + fb_n), 4) if (hits_n + fb_n) else None,
        "cache_built_strict": f"{cache_built_s}/{len(scored)}",
        "cache_built_nonstrict": f"{cache_built_n}/{len(scored)}",
    }
    results = {
        "schema": "rb3-preprocess-cache-ab/1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "overall_wall_seconds": round(wall, 2),
        "n_functions": len(functions),
        "summary": summary,
        "functions": per_fn,
    }
    out = args.out or (WT / "scripts/permuter/bench" /
                       f"rb3_ppab-results-{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    out.write_text(json.dumps(results, indent=2))

    L = "=" * 66
    print(f"\n{L}\nRB3 PREPROCESS-CACHE A/B (3-mode)\n{L}")
    print(f"  functions scored:     {summary['n_functions_scored']}/{len(functions)}")
    print(f"  variants compared:    {summary['total_variants_compared']}")
    print(f"  overall wall:         {results['overall_wall_seconds']}s")
    print("\n  Correctness (divergences vs OFF; must be 0)")
    print(f"    strict:             {summary['divergences_strict']}")
    print(f"    nonstrict:          {summary['divergences_nonstrict']}")
    print("\n  Cache built (validation passed)")
    print(f"    strict:             {summary['cache_built_strict']}")
    print(f"    nonstrict:          {summary['cache_built_nonstrict']}")
    print("\n  Fast-path coverage")
    print(f"    strict   hits/fb:   {summary['fast_hits_strict']}/{summary['fallbacks_strict']}")
    print(f"    nonstrict hits/fb:  {summary['fast_hits_nonstrict']}/{summary['fallbacks_nonstrict']}  "
          f"(hit rate {summary['hit_rate_nonstrict']})")
    print("\n  Per-call compile-run speedup (pooled / median)")
    print(f"    OFF ms/call:        {summary['pooled_compile_run_ms_off']}")
    print(f"    strict:    {summary['pooled_compile_run_ms_strict']}ms  "
          f"pooled {summary['pooled_speedup_strict']}x  median {summary['median_speedup_strict']}x")
    print(f"    nonstrict: {summary['pooled_compile_run_ms_nonstrict']}ms  "
          f"pooled {summary['pooled_speedup_nonstrict']}x  median {summary['median_speedup_nonstrict']}x")
    gate = (summary["divergences_nonstrict"] == 0
            and summary["pooled_speedup_nonstrict"] is not None
            and summary["pooled_speedup_nonstrict"] >= 1.5)
    print(f"\n  GATE (nonstrict, >=1.5x, zero divergence): "
          f"{'PASS' if gate else 'FAIL'}")
    print(f"\n  results: {out}\n{L}")
    _teardown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
