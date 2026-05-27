"""A/B harness: measure BUILD-FAILED rate, total compiles, and win count.

Drives the permuter over a representative subset of the pinned bench set and
tallies — by wrapping ``Scorer.score_batch`` — the *real* compile outcomes
(dedup / cache / obj-dedup pseudo-results are excluded, since they never
exercise a compiler). Reports:

  * total real compiles
  * BUILD-FAILED count + rate
  * wins (variants that scored strictly above their round baseline)
  * wall-clock

Run it twice (probe/filter OFF vs ON) to prove the optimization:

    # before (both off)
    PERMUTER_SYNTAX_PROBE=0 PERMUTER_VAREXT_MACRO_FILTER=0 \
      ./venv/bin/python -m scripts.permuter.bench.measure_wasted_compiles --limit 8

    # after (defaults: both on)
    ./venv/bin/python -m scripts.permuter.bench.measure_wasted_compiles --limit 8

Sandbox MUST be disabled (wibo/cl.exe write objects the sandbox blocks).
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent.parent


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        prog="python -m scripts.permuter.bench.measure_wasted_compiles"
    )
    p.add_argument("--limit", type=int, default=8,
                   help="Number of functions from the bench set (0 = all)")
    p.add_argument("--bands", default="",
                   help="Comma-separated bands (high,mid,low). Default: all")
    p.add_argument("--max-variants", type=int, default=40)
    p.add_argument("--max-rounds", type=int, default=3)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--set", type=Path, default=None,
                   help="Custom function-set JSON (same shape as bench_set.json). "
                        "Default: the pinned bench_set.json.")
    return p.parse_args()


def main() -> int:
    args = parse_args()
    # Pin hash seed so the two A/B runs generate the identical variant stream.
    if os.environ.get("PYTHONHASHSEED") != "0":
        if os.environ.get("_MWC_REEXEC") != "1":
            os.environ["PYTHONHASHSEED"] = "0"
            os.environ["_MWC_REEXEC"] = "1"
            os.execv(sys.executable, [sys.executable, "-m",
                     "scripts.permuter.bench.measure_wasted_compiles", *sys.argv[1:]])

    sys.path.insert(0, str(REPO_ROOT))
    from scripts.permuter import score_cache
    from scripts.permuter.scorer import Scorer
    from scripts.permuter.hill_climber import hill_climb
    from scripts.permuter.patterns import get_all_patterns

    # Fresh, isolated cache so every variant exercises the real compile path.
    fresh_dir = Path(tempfile.mkdtemp(prefix="mwc_cache_"))
    fresh_db = fresh_dir / "permuter_cache.db"
    score_cache._CACHE_DB = fresh_db
    score_cache.ScoreCache.__init__.__defaults__ = (fresh_db,)

    set_path = args.set or (BENCH_DIR / "bench_set.json")
    bench = json.loads(set_path.read_text())
    functions = bench["functions"]
    bands = {b.strip() for b in args.bands.split(",") if b.strip()}
    if bands:
        functions = [f for f in functions if f["band"] in bands]
    if args.limit > 0:
        functions = functions[: args.limit]

    # --- Counters, populated by the score_batch wrapper. ---
    stats = {
        "real_compiles": 0,   # variants that actually invoked the compiler
        "build_failed": 0,    # of those, failed to build
        "build_ok": 0,
        "pseudo": 0,          # dedup / cache / obj-dedup (no compile)
        "wins": 0,            # build_ok and match > round baseline
    }

    _orig_score_batch = Scorer.score_batch

    def _counting_score_batch(self, variants, workers=6):
        results = _orig_score_batch(self, variants, workers=workers)
        # Scorer caches the function's baseline match% as _baseline_pct after
        # establish_baseline(); a win is a build_ok variant above it.
        baseline = getattr(self, "_baseline_pct", None)
        for r in results:
            # Pseudo-results never touched the compiler.
            if r.error in ("source_dedup", "cache_hit", "obj_dedup"):
                stats["pseudo"] += 1
                continue
            stats["real_compiles"] += 1
            if r.build_success:
                stats["build_ok"] += 1
                if baseline is not None and r.match_percent > baseline + 1e-9:
                    stats["wins"] += 1
            else:
                stats["build_failed"] += 1
        return results

    Scorer.score_batch = _counting_score_batch

    patterns = get_all_patterns()
    cfg = (
        f"SYNTAX_PROBE={os.environ.get('PERMUTER_SYNTAX_PROBE', '1')} "
        f"VAREXT_MACRO_FILTER={os.environ.get('PERMUTER_VAREXT_MACRO_FILTER', '1')}"
    )
    print(f"[mwc] {len(functions)} functions  [{cfg}]", file=sys.stderr)

    # Per-function best-discovered score — the authoritative win invariant:
    # the filters must not lower the best match% reached on ANY function.
    per_func_best: dict[str, float] = {}

    t0 = time.perf_counter()
    for i, fn in enumerate(functions):
        source = REPO_ROOT / fn["source_path"]
        print(f"[{i+1}/{len(functions)}] {fn['qualified_name']}", file=sys.stderr)
        try:
            res = hill_climb(
                symbol=fn["objdiff_key"],
                source_path=source,
                function_name=fn["qualified_name"],
                patterns=patterns,
                max_rounds=args.max_rounds,
                max_variants=args.max_variants,
                plateau_limit=bench["seed_config"]["plateau_limit"],
                compose=bench["seed_config"]["compose"],
                apply=False,
                unit=fn["unit"],
                workers=bench["seed_config"]["workers"],
                validate=False,
                adaptive=False,
            )
            bests = [r.best_score for r in res.rounds if r.best_score is not None]
            per_func_best[fn["qualified_name"]] = round(
                max([res.initial_percent, *bests]) if bests else res.initial_percent, 4
            )
        except Exception as exc:
            print(f"    ERROR: {type(exc).__name__}: {exc}", file=sys.stderr)
    wall = time.perf_counter() - t0

    Scorer.score_batch = _orig_score_batch

    rc = stats["real_compiles"]
    bf_rate = (100.0 * stats["build_failed"] / rc) if rc else 0.0
    summary = {
        "config": cfg,
        "n_functions": len(functions),
        "wall_seconds": round(wall, 2),
        "real_compiles": rc,
        "build_failed": stats["build_failed"],
        "build_failed_rate_pct": round(bf_rate, 2),
        "build_ok": stats["build_ok"],
        "pseudo_results": stats["pseudo"],
        "wins": stats["wins"],
        "per_function_best": per_func_best,
    }

    print("\n" + "=" * 60)
    print("WASTED-COMPILE MEASUREMENT")
    print("=" * 60)
    for k, v in summary.items():
        print(f"  {k:22s} {v}")
    print("=" * 60)

    if args.out:
        args.out.write_text(json.dumps(summary, indent=2))

    import shutil
    shutil.rmtree(fresh_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
