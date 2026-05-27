"""A/B validation harness for the preprocessed-splice fast path (roadmap A1).

Built on top of the A0 bench harness (``run.py`` + ``bench_set.json``). For
each pinned bench function it generates ONE deterministic round of variants
(same seed config, same patterns, same compose pairs as ``hill_climb`` round
1), then scores that *identical* variant list twice:

  * **OFF** — ``PERMUTER_PREPROCESS_CACHE`` unset: every variant takes the
    normal full compile + objdiff path.
  * **ON**  — ``PERMUTER_PREPROCESS_CACHE=1``: clean variants take the
    preprocessed-splice fast path; live-macro bodies fall back.

The two runs use the SAME variant objects in the SAME order, so they can be
compared variant-for-variant. ``PERMUTER_PREPROCESS_CACHE_STRICT=1`` is forced
in the ON run as the correctness oracle: the cache self-validation hard-fails
(disabling the cache for that function) if the spliced ``.o`` is not
byte-identical to the canonical ``.o`` for the line-preserving baseline case.

The gate (roadmap A1):

  * **zero score divergence** across N >= 50 scored variants
    (per-variant objdiff match% identical to 4 decimals between OFF and ON),
    AND
  * **median per-call compile-run speedup >= 1.5x** (ON vs OFF), measured via
    the env-gated profiler's ``compile-run`` bucket — the contention-robust
    signal, not wall-clock.

If the gate passes, the caller flips the ``preprocess_cache.py`` default to ON.
If it fails, the caller leaves it OFF and reports the numbers.

Cache isolation
---------------
Each (function, mode) pair runs against a FRESH temp score cache so every
variant genuinely recompiles + re-diffs. Without this, the ON run would hit
the OFF run's obj-dedup / score cache and measure nothing.

Usage::

    PYTHONHASHSEED=0 ./venv/bin/python -m scripts.permuter.bench.preprocess_cache_ab \\
        --out scripts/permuter/bench/preprocess_cache_ab-results.json

    # quick smoke test on a few functions:
    ./venv/bin/python -m scripts.permuter.bench.preprocess_cache_ab --limit 4

NOTE: must run with the command sandbox DISABLED — wibo/cl.exe write object
files to a temp dir the sandbox blocks (SIGSYS -> silent fake-success). See
``scripts/permuter/bench/BASELINE.md``.
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

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent.parent  # scripts/permuter/bench -> repo root

# Per-variant scores are compared at this many decimals. objdiff fuzzy match%
# is reported to ~4 significant decimals; anything below this is float noise.
SCORE_DECIMALS = 4

# Gate thresholds (roadmap A1).
GATE_MIN_VARIANTS = 50
GATE_MIN_SPEEDUP = 1.5


def _load_bench_set() -> dict:
    return json.loads((BENCH_DIR / "bench_set.json").read_text())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.bench.preprocess_cache_ab"
    )
    parser.add_argument("--limit", type=int, default=0,
                        help="Cap number of functions (0 = all)")
    parser.add_argument("--bands", default="",
                        help="Comma-separated bands to include (high,mid,low). Default: all")
    parser.add_argument("--only", default="",
                        help="Comma-separated qualified-name substrings to include")
    parser.add_argument("--out", type=Path, default=None,
                        help="Results JSON path (default: bench/preprocess_cache_ab-results-<ts>.json)")
    return parser.parse_args()


def _reexec_with_pinned_seed(seed_cfg: dict) -> None:
    """Re-exec once with a pinned PYTHONHASHSEED so variant order is stable."""
    want = str(seed_cfg["pythonhashseed"])
    if os.environ.get("PYTHONHASHSEED") == want:
        return
    if os.environ.get("_PPAB_REEXEC") == "1":
        return
    os.environ["PYTHONHASHSEED"] = want
    os.environ["_PPAB_REEXEC"] = "1"
    os.execv(sys.executable, [sys.executable, "-m",
                              "scripts.permuter.bench.preprocess_cache_ab",
                              *sys.argv[1:]])


# Every isolated score-cache dir we create, torn down at the end of the run.
# We keep the rebind pointing at a LIVE dir at all times: deleting a temp dir
# while score_cache._CACHE_DB still points into it makes the next Scorer
# (which opens ScoreCache in __enter__) fail with "unable to open database".
_CACHE_DIRS: list[Path] = []


def _isolate_score_cache() -> Path:
    """Point the permuter's score cache at a fresh throwaway temp DB.

    Mirrors run.py: ScoreCache binds db_path=_CACHE_DB as a default at
    class-definition time, so reassigning the module global is not enough —
    the function default must be rebound too. The dir is registered for
    teardown at run end (NOT deleted per-call) so the rebind never dangles.
    """
    from scripts.permuter import score_cache
    cache_dir = Path(tempfile.mkdtemp(prefix="permuter_ppab_cache_"))
    fresh_db = cache_dir / "permuter_cache.db"
    score_cache._CACHE_DB = fresh_db
    score_cache.ScoreCache.__init__.__defaults__ = (fresh_db,)
    _CACHE_DIRS.append(cache_dir)
    return cache_dir


def _teardown_score_caches() -> None:
    import shutil as _sh
    for d in _CACHE_DIRS:
        _sh.rmtree(d, ignore_errors=True)
    _CACHE_DIRS.clear()


def _generate_variants_once(fn: dict, seed_cfg: dict, patterns: list) -> tuple:
    """Reproduce hill_climb round-1 variant generation, deterministically.

    Returns (variants, baseline_percent, scorer_diag_present). The variant list
    is generated ONCE and scored twice (off/on) so the two modes see identical
    inputs. We run a throwaway baseline compile to obtain the diagnosis that
    drives compose-pair selection and per-pattern budgets, exactly as round 1
    of hill_climb does.
    """
    from scripts.permuter.scorer import Scorer
    from scripts.permuter.extractor import extract_function
    from scripts.permuter.generator import generate_variants
    from scripts.permuter.composer import (
        get_compose_pairs, available_context_keys,
    )

    source = REPO_ROOT / fn["source_path"]
    symbol = fn["symbol"]
    qual = fn["qualified_name"]

    ctx = extract_function(source, qual)

    # Generation must not be perturbed by a prior function's ON-mode env leak.
    os.environ.pop("PERMUTER_PREPROCESS_CACHE", None)
    os.environ.pop("PERMUTER_PREPROCESS_CACHE_STRICT", None)

    # Isolate the score cache for the generation baseline so it doesn't read
    # the shared months-old DB (and so it doesn't dangle a deleted temp path).
    _isolate_score_cache()

    # A throwaway scorer run to get the baseline diagnosis (drives budgets +
    # compose pairs). This runs with the cache OFF — it only feeds generation.
    baseline = 0.0
    with Scorer(source, symbol, unit=fn["unit"]) as scorer:
        baseline = scorer.get_baseline(guided=True)
        if scorer.diagnosis:
            ctx.diagnosis = scorer.diagnosis

    compose_pairs = None
    if seed_cfg["compose"]:
        compose_pairs = get_compose_pairs(
            diagnosis=ctx.diagnosis,
            patterns=patterns,
            hints=None,
            available_context=available_context_keys(ctx),
        )

    variants = list(generate_variants(
        ctx, patterns, seed_cfg["max_variants"],
        compose_pairs=compose_pairs,
        chains=None,
        round_hints=None,
        failed_patterns=None,
    ))
    return variants, baseline


def _score_mode(fn: dict, variants: list, workers: int, cache_on: bool) -> dict:
    """Score the variant list in one mode (cache off/on) against a fresh cache.

    Returns a dict with per-variant scores, build successes, the profiler's
    compile-run total/count, and the preprocess cache's fast-hit/fallback
    counters (when ON).
    """
    from scripts.permuter.scorer import Scorer
    from scripts.permuter.profiling import reset_profiler

    source = REPO_ROOT / fn["source_path"]
    symbol = fn["symbol"]

    # Toggle the feature for this mode. STRICT is the oracle: in the ON run the
    # cache self-validation hard-fails (disables the cache for the function) if
    # the spliced .o is not byte-identical for the line-preserving baseline.
    if cache_on:
        os.environ["PERMUTER_PREPROCESS_CACHE"] = "1"
        os.environ["PERMUTER_PREPROCESS_CACHE_STRICT"] = "1"
    else:
        os.environ.pop("PERMUTER_PREPROCESS_CACHE", None)
        os.environ.pop("PERMUTER_PREPROCESS_CACHE_STRICT", None)

    _isolate_score_cache()
    profiler = reset_profiler()  # profiling already enabled via env (see main)
    profiler.start_wall()
    t0 = time.perf_counter()

    fast_hits = fallbacks = 0
    cache_used = False
    with Scorer(source, symbol, unit=fn["unit"]) as scorer:
        scorer.get_baseline(guided=True)
        results = scorer.score_batch(variants, workers=workers)
        if cache_on and scorer._pp_cache is not None and not scorer._pp_cache.disabled:
            cache_used = True
            fast_hits = scorer._pp_cache.fast_hits
            fallbacks = scorer._pp_cache.fallbacks

    wall = time.perf_counter() - t0
    profiler.stop_wall()

    compile_bucket = profiler.buckets.get("compile-run")
    compile_run_seconds = compile_bucket.seconds if compile_bucket else 0.0
    compile_run_calls = compile_bucket.count if compile_bucket else 0

    return {
        "scores": [round(r.match_percent, SCORE_DECIMALS) for r in results],
        "build_success": [bool(r.build_success) for r in results],
        "wall_seconds": round(wall, 3),
        "compile_run_seconds": round(compile_run_seconds, 4),
        "compile_run_calls": compile_run_calls,
        "compile_run_ms_per_call": round(
            1000.0 * compile_run_seconds / compile_run_calls, 3
        ) if compile_run_calls else None,
        "fast_hits": fast_hits,
        "fallbacks": fallbacks,
        "cache_used": cache_used,
    }


def _compare(off: dict, on: dict) -> dict:
    """Per-variant comparison of two mode results."""
    n = min(len(off["scores"]), len(on["scores"]))
    divergences: list[dict] = []
    build_parity_breaks: list[int] = []
    for i in range(n):
        if off["scores"][i] != on["scores"][i]:
            divergences.append({
                "index": i,
                "off_score": off["scores"][i],
                "on_score": on["scores"][i],
            })
        if off["build_success"][i] != on["build_success"][i]:
            build_parity_breaks.append(i)

    # Per-call compile-run speedup (the contention-robust signal).
    off_ms = off["compile_run_ms_per_call"]
    on_ms = on["compile_run_ms_per_call"]
    speedup = (off_ms / on_ms) if (off_ms and on_ms) else None

    return {
        "n_compared": n,
        "divergence_count": len(divergences),
        "divergences": divergences[:20],  # cap for readability
        "build_parity_breaks": build_parity_breaks,
        "compile_run_ms_off": off_ms,
        "compile_run_ms_on": on_ms,
        "compile_run_speedup": round(speedup, 3) if speedup else None,
        "wall_speedup": round(off["wall_seconds"] / on["wall_seconds"], 3)
        if on["wall_seconds"] else None,
    }


def _unit_cluster(unit: str) -> str:
    """Coarse cluster of a unit path, e.g. default/system/rndobj/Foo -> rndobj."""
    parts = unit.split("/")
    # Drop the 'default/system' prefix; keep the leaf subsystem dir.
    if len(parts) >= 2:
        return parts[-2]
    return unit


def main() -> int:
    args = parse_args()
    bench = _load_bench_set()
    seed_cfg = bench["seed_config"]
    _reexec_with_pinned_seed(seed_cfg)

    # The whole A/B decision rests on the profiler's compile-run bucket.
    os.environ["PERMUTER_PROFILE"] = "1"

    sys.path.insert(0, str(REPO_ROOT))
    from scripts.permuter.patterns import get_all_patterns

    patterns = get_all_patterns()
    functions = bench["functions"]
    bands = {b.strip() for b in args.bands.split(",") if b.strip()}
    if bands:
        functions = [f for f in functions if f["band"] in bands]
    if args.only:
        needles = [n.strip() for n in args.only.split(",") if n.strip()]
        functions = [f for f in functions
                     if any(n in f["qualified_name"] for n in needles)]
    if args.limit > 0:
        functions = functions[: args.limit]

    workers = seed_cfg["workers"]
    print(f"[ppab] {len(functions)} functions, workers={workers}, "
          f"patterns={len(patterns)}", file=sys.stderr)

    per_function: list[dict] = []
    overall_start = time.perf_counter()

    for i, fn in enumerate(functions):
        print(f"\n[{i + 1}/{len(functions)}] {fn['qualified_name']} "
              f"({fn['band']})", file=sys.stderr)
        rec: dict = {
            "id": fn["id"],
            "qualified_name": fn["qualified_name"],
            "unit": fn["unit"],
            "unit_cluster": _unit_cluster(fn["unit"]),
            "band": fn["band"],
        }
        try:
            variants, baseline = _generate_variants_once(fn, seed_cfg, patterns)
            rec["baseline_percent"] = round(baseline, SCORE_DECIMALS)
            rec["n_variants"] = len(variants)
            if not variants:
                rec["error"] = "no variants generated"
                per_function.append(rec)
                print("    -> no variants", file=sys.stderr)
                continue

            off = _score_mode(fn, variants, workers, cache_on=False)
            on = _score_mode(fn, variants, workers, cache_on=True)
            cmp = _compare(off, on)

            rec.update({
                "off": off,
                "on": on,
                "comparison": cmp,
            })
            print(f"    -> {cmp['n_compared']} variants, "
                  f"{cmp['divergence_count']} divergences, "
                  f"compile-run {cmp['compile_run_ms_off']}ms -> "
                  f"{cmp['compile_run_ms_on']}ms "
                  f"(speedup {cmp['compile_run_speedup']}x), "
                  f"cache fast_hits={on['fast_hits']} fallbacks={on['fallbacks']}",
                  file=sys.stderr)
        except Exception as exc:  # keep the sweep alive
            rec["error"] = f"{type(exc).__name__}: {exc}"
            import traceback
            traceback.print_exc()
            print(f"    -> ERROR: {rec['error']}", file=sys.stderr)
        per_function.append(rec)

    overall_wall = time.perf_counter() - overall_start
    summary = _summarize(per_function)

    results = {
        "schema": "permuter-preprocess-cache-ab/1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "git_head": _git_head(),
        "seed_config": seed_cfg,
        "n_functions": len(functions),
        "overall_wall_seconds": round(overall_wall, 2),
        "gate": summary["gate"],
        "summary": summary,
        "functions": per_function,
    }

    out_path = args.out or (
        BENCH_DIR / f"preprocess_cache_ab-results-"
                    f"{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    )
    out_path.write_text(json.dumps(results, indent=2))
    _print_summary(results, out_path)
    _teardown_score_caches()
    return 0


def _summarize(per_function: list[dict]) -> dict:
    scored = [f for f in per_function if "comparison" in f]
    total_variants = sum(f["comparison"]["n_compared"] for f in scored)
    total_divergences = sum(f["comparison"]["divergence_count"] for f in scored)
    build_parity_breaks = sum(
        len(f["comparison"]["build_parity_breaks"]) for f in scored
    )

    speedups = [f["comparison"]["compile_run_speedup"] for f in scored
                if f["comparison"]["compile_run_speedup"] is not None]
    wall_speedups = [f["comparison"]["wall_speedup"] for f in scored
                     if f["comparison"]["wall_speedup"] is not None]
    median_speedup = statistics.median(speedups) if speedups else None
    median_wall_speedup = statistics.median(wall_speedups) if wall_speedups else None

    # Aggregate per-call compile-run ms across all functions (pooled), which is
    # less noisy than a median of per-function ratios on tiny variant counts.
    off_secs = sum(f["off"]["compile_run_seconds"] for f in scored)
    off_calls = sum(f["off"]["compile_run_calls"] for f in scored)
    on_secs = sum(f["on"]["compile_run_seconds"] for f in scored)
    on_calls = sum(f["on"]["compile_run_calls"] for f in scored)
    pooled_off_ms = (1000.0 * off_secs / off_calls) if off_calls else None
    pooled_on_ms = (1000.0 * on_secs / on_calls) if on_calls else None
    pooled_speedup = (pooled_off_ms / pooled_on_ms) \
        if (pooled_off_ms and pooled_on_ms) else None

    total_fast_hits = sum(f["on"]["fast_hits"] for f in scored)
    total_fallbacks = sum(f["on"]["fallbacks"] for f in scored)
    fast_path_attempts = total_fast_hits + total_fallbacks
    fallback_rate = (total_fallbacks / fast_path_attempts) \
        if fast_path_attempts else None

    # Live-macro fallback rate per unit cluster.
    cluster: dict[str, dict] = {}
    for f in scored:
        c = f["unit_cluster"]
        slot = cluster.setdefault(c, {"fast_hits": 0, "fallbacks": 0,
                                      "functions": 0, "cache_used_functions": 0})
        slot["fast_hits"] += f["on"]["fast_hits"]
        slot["fallbacks"] += f["on"]["fallbacks"]
        slot["functions"] += 1
        if f["on"]["cache_used"]:
            slot["cache_used_functions"] += 1
    for c, slot in cluster.items():
        attempts = slot["fast_hits"] + slot["fallbacks"]
        slot["fallback_rate"] = round(slot["fallbacks"] / attempts, 4) \
            if attempts else None

    gate_pass = (
        total_divergences == 0
        and total_variants >= GATE_MIN_VARIANTS
        and median_speedup is not None
        and median_speedup >= GATE_MIN_SPEEDUP
    )
    gate_reasons = []
    if total_divergences != 0:
        gate_reasons.append(f"{total_divergences} score divergence(s)")
    if total_variants < GATE_MIN_VARIANTS:
        gate_reasons.append(
            f"only {total_variants} variants (< {GATE_MIN_VARIANTS})")
    if median_speedup is None:
        gate_reasons.append("no speedup measured")
    elif median_speedup < GATE_MIN_SPEEDUP:
        gate_reasons.append(
            f"median speedup {median_speedup:.3f}x (< {GATE_MIN_SPEEDUP}x)")

    return {
        "n_functions_scored": len(scored),
        "total_variants_compared": total_variants,
        "total_score_divergences": total_divergences,
        "build_parity_breaks": build_parity_breaks,
        "median_compile_run_speedup": round(median_speedup, 3)
        if median_speedup else None,
        "median_wall_speedup": round(median_wall_speedup, 3)
        if median_wall_speedup else None,
        "pooled_compile_run_ms_off": round(pooled_off_ms, 3)
        if pooled_off_ms else None,
        "pooled_compile_run_ms_on": round(pooled_on_ms, 3)
        if pooled_on_ms else None,
        "pooled_compile_run_speedup": round(pooled_speedup, 3)
        if pooled_speedup else None,
        "fast_path_attempts": fast_path_attempts,
        "total_fast_hits": total_fast_hits,
        "total_fallbacks": total_fallbacks,
        "overall_fallback_rate": round(fallback_rate, 4)
        if fallback_rate is not None else None,
        "fallback_rate_per_cluster": cluster,
        "gate": {
            "passed": gate_pass,
            "min_variants": GATE_MIN_VARIANTS,
            "min_speedup": GATE_MIN_SPEEDUP,
            "reasons_if_failed": gate_reasons,
        },
    }


def _git_head() -> str:
    import subprocess
    try:
        return subprocess.run(["git", "rev-parse", "--short", "HEAD"],
                              cwd=str(REPO_ROOT), capture_output=True,
                              text=True).stdout.strip()
    except Exception:
        return "unknown"


def _print_summary(results: dict, out_path: Path) -> None:
    s = results["summary"]
    g = s["gate"]
    line = "=" * 64
    print(f"\n{line}")
    print("PREPROCESS-CACHE A/B (roadmap A1)")
    print(line)
    print(f"  git HEAD:                  {results['git_head']}")
    print(f"  functions scored:          {s['n_functions_scored']}/{results['n_functions']}")
    print(f"  overall wall:              {results['overall_wall_seconds']}s")
    print()
    print("  Correctness")
    print(f"    variants compared:       {s['total_variants_compared']}")
    print(f"    score divergences:       {s['total_score_divergences']}  (must be 0)")
    print(f"    build-parity breaks:     {s['build_parity_breaks']}")
    print()
    print("  Speed (per-call compile-run — the contention-robust signal)")
    print(f"    median speedup:          {s['median_compile_run_speedup']}x  "
          f"(gate >= {GATE_MIN_SPEEDUP}x)")
    print(f"    pooled compile-run ms:   {s['pooled_compile_run_ms_off']} -> "
          f"{s['pooled_compile_run_ms_on']}  "
          f"(pooled {s['pooled_compile_run_speedup']}x)")
    print(f"    median wall speedup:     {s['median_wall_speedup']}x  (noisy)")
    print()
    print("  Fast-path coverage")
    print(f"    fast hits / fallbacks:   {s['total_fast_hits']} / {s['total_fallbacks']}")
    print(f"    overall fallback rate:   {s['overall_fallback_rate']}")
    for c, slot in sorted(s["fallback_rate_per_cluster"].items()):
        print(f"      {c:14s} hits={slot['fast_hits']:4d} "
              f"fallbacks={slot['fallbacks']:4d} "
              f"rate={slot['fallback_rate']} "
              f"(cache used in {slot['cache_used_functions']}/{slot['functions']} fns)")
    print()
    verdict = "PASS — flip default to ON" if g["passed"] else "FAIL — keep default OFF"
    print(f"  GATE: {verdict}")
    if not g["passed"]:
        for r in g["reasons_if_failed"]:
            print(f"    - {r}")
    print()
    print(f"  results written: {out_path}")
    print(line)


if __name__ == "__main__":
    raise SystemExit(main())
