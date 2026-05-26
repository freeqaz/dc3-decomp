"""Re-runnable permuter benchmark harness (roadmap item A0).

Runs the permuter over the pinned bench set (``bench_set.json``) with fixed
seeds and a fixed pattern set, then emits:

  * a machine-readable results file (``results-<timestamp>.json``) so every
    later PR can post a delta against the baseline, and
  * a human summary on stdout.

It also drives the env-gated per-variant profiler (``PERMUTER_PROFILE=1``) so a
single run attributes wall-clock across
``{generate, compile-spawn, compile-run, objdiff-spawn, objdiff-run,
python-overhead}``.

Usage::

    PYTHONHASHSEED=0 ./venv/bin/python -m scripts.permuter.bench.run
    ./venv/bin/python -m scripts.permuter.bench.run --limit 5 --bands high,mid
    ./venv/bin/python -m scripts.permuter.bench.run --out results.json --profile

The three Headline Metrics (per the roadmap) are computed over the whole set:

  * variants / second   — total variants scored / total scoring wall-clock
  * wins / 100 attempts  — functions that improved at all, per 100 functions
  * wall-clock to first 100% — mean wall time across functions that reached 100%
                               (None when none did)
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent.parent  # scripts/permuter/bench -> repo root


def _load_bench_set() -> dict:
    return json.loads((BENCH_DIR / "bench_set.json").read_text())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m scripts.permuter.bench.run")
    parser.add_argument("--limit", type=int, default=0,
                        help="Cap number of functions (0 = all)")
    parser.add_argument("--bands", default="",
                        help="Comma-separated bands to include (high,mid,low). Default: all")
    parser.add_argument("--only", default="",
                        help="Comma-separated qualified-name substrings to include")
    parser.add_argument("--out", type=Path, default=None,
                        help="Results JSON path (default: bench/results-<timestamp>.json)")
    parser.add_argument("--profile", action="store_true",
                        help="Force PERMUTER_PROFILE=1 for the per-variant breakdown")
    parser.add_argument("--no-profile", action="store_true",
                        help="Disable profiling even if PERMUTER_PROFILE is set")
    parser.add_argument("--warm-cache", action="store_true",
                        help="Use the shared persistent score cache (default: a "
                             "fresh temp cache so every variant exercises the "
                             "real compile+objdiff path — the only honest input "
                             "to the profiling breakdown)")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    # Reproducibility: pin hash seed before anything that might rely on dict/set
    # ordering. (The permuter is pattern-driven and has no random.* calls, but
    # we pin anyway so cross-week runs are bit-stable.)
    bench = _load_bench_set()
    seed_cfg = bench["seed_config"]
    if os.environ.get("PYTHONHASHSEED") != str(seed_cfg["pythonhashseed"]):
        # Re-exec once with a pinned hash seed so the run is deterministic.
        if os.environ.get("_BENCH_REEXEC") != "1":
            os.environ["PYTHONHASHSEED"] = str(seed_cfg["pythonhashseed"])
            os.environ["_BENCH_REEXEC"] = "1"
            os.execv(sys.executable, [sys.executable, "-m",
                                      "scripts.permuter.bench.run", *sys.argv[1:]])

    if args.profile:
        os.environ["PERMUTER_PROFILE"] = "1"
    if args.no_profile:
        os.environ["PERMUTER_PROFILE"] = "0"

    # Imports after the env is pinned so profiling_enabled() sees the flag.
    sys.path.insert(0, str(REPO_ROOT))
    from scripts.permuter.hill_climber import hill_climb
    from scripts.permuter.patterns import get_all_patterns
    from scripts.permuter.profiling import reset_profiler, profiling_enabled

    # Cache isolation. The shared permuter_cache.db has months of prior runs;
    # replaying it turns the inner loop into pure-Python cache lookups and the
    # compile/objdiff breakdown becomes meaningless. Point the score cache at a
    # throwaway temp DB so a baseline run measures the real cold path.
    fresh_cache_dir = None
    if not args.warm_cache:
        import tempfile
        from scripts.permuter import score_cache
        fresh_cache_dir = Path(tempfile.mkdtemp(prefix="permuter_bench_cache_"))
        fresh_db = fresh_cache_dir / "permuter_cache.db"
        score_cache._CACHE_DB = fresh_db
        # ScoreCache.__init__ binds db_path=_CACHE_DB as a *default* at class
        # definition, so reassigning the module global isn't enough — rebind
        # the function default too.
        score_cache.ScoreCache.__init__.__defaults__ = (fresh_db,)
        print(f"[bench] fresh score cache: {fresh_db}", file=sys.stderr)

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

    print(f"[bench] {len(functions)} functions, profiling="
          f"{profiling_enabled()}, patterns={len(patterns)}", file=sys.stderr)

    per_function: list[dict] = []
    # Aggregate profiling across the whole run (one merged breakdown).
    merged_profile: dict[str, dict] = {}

    overall_start = time.perf_counter()
    for i, fn in enumerate(functions):
        source = REPO_ROOT / fn["source_path"]
        print(f"\n[{i + 1}/{len(functions)}] {fn['qualified_name']} "
              f"({fn['band']}, baseline {fn['baseline_percent']:.4f}%)",
              file=sys.stderr)

        profiler = reset_profiler()
        profiler.start_wall()
        t0 = time.perf_counter()
        error = None
        try:
            result = hill_climb(
                symbol=fn["objdiff_key"],
                source_path=source,
                function_name=fn["qualified_name"],
                patterns=patterns,
                max_rounds=seed_cfg["max_rounds"],
                max_variants=seed_cfg["max_variants"],
                plateau_limit=seed_cfg["plateau_limit"],
                compose=seed_cfg["compose"],
                apply=False,  # never mutate source — bench must be repeatable
                unit=fn["unit"],
                workers=seed_cfg["workers"],
                validate=False,
            )
        except Exception as exc:  # keep the sweep alive
            error = f"{type(exc).__name__}: {exc}"
            result = None
        elapsed = time.perf_counter() - t0
        profiler.stop_wall()

        prof_summary = profiler.summary() if profiling_enabled() else {}
        _merge_profile(merged_profile, profiler)

        variants_scored = 0
        best_discovered = None
        if result is not None:
            variants_scored = sum(r.num_variants for r in result.rounds)
            # We run with apply=False for repeatability, so total_delta is
            # always 0. A "win" is therefore whether the climb *discovered* a
            # variant above baseline — read the best round score, not the
            # applied delta.
            round_bests = [r.best_score for r in result.rounds if r.best_score is not None]
            best_discovered = max([result.initial_percent, *round_bests]) \
                if round_bests else result.initial_percent

        rec = {
            "id": fn["id"],
            "qualified_name": fn["qualified_name"],
            "unit": fn["unit"],
            "band": fn["band"],
            "baseline_percent": fn["baseline_percent"],
            "elapsed_seconds": round(elapsed, 3),
            "error": error,
        }
        if result is not None:
            discovered_delta = best_discovered - result.initial_percent
            rec.update({
                "initial_percent": round(result.initial_percent, 4),
                "final_percent": round(result.final_percent, 4),
                "best_discovered_percent": round(best_discovered, 4),
                "discovered_delta": round(discovered_delta, 4),
                "delta": round(result.total_delta, 4),
                "rounds": len(result.rounds),
                "variants_scored": variants_scored,
                "stopped_reason": result.stopped_reason,
                "winning_pattern": result.winning_pattern,
                "reached_100": best_discovered >= 99.999,
                "improved": discovered_delta > 1e-6,
            })
            print(f"    -> {result.initial_percent:.4f}% -> "
                  f"best {best_discovered:.4f}% (+{discovered_delta:.4f}% discovered) "
                  f"{variants_scored} variants, {elapsed:.1f}s, "
                  f"{result.stopped_reason}", file=sys.stderr)
        else:
            rec.update({"improved": False, "reached_100": False,
                        "variants_scored": 0})
            print(f"    -> ERROR: {error}", file=sys.stderr)
        if prof_summary:
            rec["profile"] = prof_summary
        per_function.append(rec)

    overall_wall = time.perf_counter() - overall_start

    metrics = _headline_metrics(per_function, overall_wall)
    profile_breakdown = _finalize_merged_profile(merged_profile)

    results = {
        "schema": "permuter-bench/1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "git_head": _git_head(),
        "seed_config": seed_cfg,
        "n_functions": len(functions),
        "overall_wall_seconds": round(overall_wall, 2),
        "headline_metrics": metrics,
        "profile_breakdown": profile_breakdown,
        "functions": per_function,
    }

    out_path = args.out or (BENCH_DIR /
                            f"results-{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    out_path.write_text(json.dumps(results, indent=2))

    _print_summary(results, out_path)

    if fresh_cache_dir is not None:
        import shutil
        shutil.rmtree(fresh_cache_dir, ignore_errors=True)
    return 0


def _merge_profile(merged: dict, profiler) -> None:
    """Fold one function's raw buckets into the run-wide accumulator."""
    if not profiler.enabled:
        return
    for name, bucket in profiler.buckets.items():
        slot = merged.setdefault(name, {"seconds": 0.0, "count": 0})
        slot["seconds"] += bucket.seconds
        slot["count"] += bucket.count
    wall = merged.setdefault("_wall", {"seconds": 0.0, "count": 0})
    wall["seconds"] += max(0.0, profiler.wall_end - profiler.wall_start)
    floors = merged.setdefault("_floors", {})
    if profiler._compile_spawn_floor is not None:
        floors["compile"] = profiler._compile_spawn_floor
    if profiler._objdiff_spawn_floor is not None:
        floors["objdiff"] = profiler._objdiff_spawn_floor


def _finalize_merged_profile(merged: dict) -> dict:
    """Turn the run-wide accumulator into a percentage breakdown.

    Note on percentages: with parallel compile workers the summed compile-run
    seconds can exceed wall-clock (N threads each accruing time). We therefore
    report two denominators: ``percent_of_wall`` (sums >100% when parallel —
    shows where *thread*-time goes) and ``percent_of_attributed`` (always sums
    to ~100% over the instrumented buckets — the honest 'where does the work
    go' view). A reader deciding A2/A4 should look at percent_of_attributed.
    """
    if not merged:
        return {"enabled": False}
    wall = merged.pop("_wall", {"seconds": 0.0})["seconds"]
    floors = merged.pop("_floors", {})
    bucket_counts = {k: v["count"] for k, v in merged.items()}
    bucket_seconds = {k: v["seconds"] for k, v in merged.items()}
    attributed = sum(bucket_seconds.values())
    # python-overhead is the wall residual not captured by any subprocess timer.
    residual = max(0.0, wall - attributed)
    bucket_seconds["python-overhead"] = residual

    total_attr = attributed + residual
    pct_attr = {}
    pct_wall = {}
    for name, secs in bucket_seconds.items():
        pct_attr[name] = round(100.0 * secs / total_attr, 2) if total_attr else 0.0
        pct_wall[name] = round(100.0 * secs / wall, 2) if wall else 0.0

    # Per-call millisecond cost — the number that decides A2/A4. A daemon
    # (A2) only helps if objdiff-spawn ms is a large share of (spawn+run);
    # a compile worker (A4) only helps if compile-spawn ms is large vs run.
    per_call_ms = {}
    for kind in ("compile", "objdiff"):
        n = bucket_counts.get(f"{kind}-run", 0)
        if n:
            spawn_ms = 1000.0 * bucket_seconds.get(f"{kind}-spawn", 0.0) / n
            run_ms = 1000.0 * bucket_seconds.get(f"{kind}-run", 0.0) / n
            per_call_ms[kind] = {
                "calls": n,
                "spawn_ms": round(spawn_ms, 3),
                "run_ms": round(run_ms, 3),
                "spawn_share_pct": round(100.0 * spawn_ms / (spawn_ms + run_ms), 2)
                if (spawn_ms + run_ms) else 0.0,
            }

    return {
        "enabled": True,
        "wall_seconds": round(wall, 3),
        "attributed_seconds": round(attributed, 3),
        "spawn_floors_ms": {k: round(v * 1000, 3) for k, v in floors.items()},
        "seconds": {k: round(v, 4) for k, v in sorted(bucket_seconds.items())},
        "counts": dict(sorted(bucket_counts.items())),
        "per_call_ms": per_call_ms,
        "percent_of_attributed": dict(sorted(pct_attr.items())),
        "percent_of_wall": dict(sorted(pct_wall.items())),
    }


def _headline_metrics(per_function: list[dict], overall_wall: float) -> dict:
    total_variants = sum(f.get("variants_scored", 0) for f in per_function)
    scoring_wall = sum(f["elapsed_seconds"] for f in per_function)
    improved = [f for f in per_function if f.get("improved")]
    reached_100 = [f for f in per_function if f.get("reached_100")]
    attempts = len(per_function)

    first_100_times = [f["elapsed_seconds"] for f in reached_100]
    mean_to_100 = (round(sum(first_100_times) / len(first_100_times), 2)
                   if first_100_times else None)

    return {
        "variants_per_second": round(total_variants / scoring_wall, 2) if scoring_wall else 0.0,
        "total_variants_scored": total_variants,
        "scoring_wall_seconds": round(scoring_wall, 2),
        "wins_per_100_attempts": round(100.0 * len(improved) / attempts, 2) if attempts else 0.0,
        "functions_improved": len(improved),
        "functions_reached_100": len(reached_100),
        "wall_clock_to_first_100_seconds": mean_to_100,
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
    m = results["headline_metrics"]
    p = results["profile_breakdown"]
    line = "=" * 64
    print(f"\n{line}")
    print("PERMUTER BENCH RESULTS")
    print(line)
    print(f"  git HEAD:            {results['git_head']}")
    print(f"  functions:           {results['n_functions']}")
    print(f"  overall wall:        {results['overall_wall_seconds']}s")
    print()
    print("  Headline Metrics")
    print(f"    variants / second:          {m['variants_per_second']}")
    print(f"    wins / 100 attempts:        {m['wins_per_100_attempts']}  "
          f"({m['functions_improved']}/{results['n_functions']} improved)")
    w100 = m["wall_clock_to_first_100_seconds"]
    print(f"    wall-clock to first 100%:   "
          f"{w100 if w100 is not None else 'n/a'} s  "
          f"({m['functions_reached_100']} reached 100%)")
    print(f"    total variants scored:      {m['total_variants_scored']}")
    if p.get("enabled"):
        print()
        print("  Per-variant profile (percent_of_attributed)")
        for name, pct in sorted(p["percent_of_attributed"].items(),
                                key=lambda kv: -kv[1]):
            print(f"    {name:18s} {pct:6.2f}%   ({p['seconds'][name]}s)")
        print(f"    spawn floors (ms): {p['spawn_floors_ms']}")
        print()
        print("  Per-call cost (decides A2 daemon / A4 compile worker)")
        for kind, d in p.get("per_call_ms", {}).items():
            print(f"    {kind:8s} {d['calls']:5d} calls  "
                  f"spawn {d['spawn_ms']:.2f}ms + run {d['run_ms']:.2f}ms  "
                  f"(spawn = {d['spawn_share_pct']}% of subprocess time)")
    print()
    print(f"  results written: {out_path}")
    print(line)


if __name__ == "__main__":
    raise SystemExit(main())
