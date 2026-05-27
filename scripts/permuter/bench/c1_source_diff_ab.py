"""A/B validation harness for the C1 ghidra_source_diff beam-ranking signal.

Built on top of the A0 bench harness (``run.py`` + ``bench_set.json``). For
each pinned bench function it runs ``hill_climb`` twice against an isolated
score cache:

  * **OFF** — ``PERMUTER_C1_SOURCE_DIFF=off``: beam ranking ignores the
    structural diff signal (the current production default).
  * **BOTH** — ``PERMUTER_C1_SOURCE_DIFF=both``: beam ranking includes the
    averaged Ghidra+m2c structural diff as a tie-break signal.

The gate (roadmap C1):

  * **no win-rate regression** — ``both`` wins >= ``off`` wins, AND
  * **rounds-to-first-win not worse** (``both`` mean_rounds <= ``off``
    mean_rounds + 1 tolerance).

If the gate passes, the caller flips the default at ``beam_search.py:409``
from ``"off"`` to ``"both"``.  If it fails, the caller leaves the default OFF
and documents the numbers.

Cache isolation
---------------
Each (function, mode) pair runs against a FRESH temp score cache.  The A/B
uses ``apply=False`` (bench-safe) throughout.

Usage::

    # bounded smoke test — recommended first run
    PYTHONHASHSEED=0 ./venv/bin/python -m scripts.permuter.bench.c1_source_diff_ab \\
        --limit 4 --bands mid

    # write results
    PYTHONHASHSEED=0 ./venv/bin/python -m scripts.permuter.bench.c1_source_diff_ab \\
        --limit 8 --bands mid \\
        --out scripts/permuter/bench/c1_source_diff_ab-results.json

NOTE: must run with the command sandbox DISABLED — wibo/cl.exe write object
files to a temp dir the sandbox blocks (SIGSYS -> silent fake-success). See
``scripts/permuter/bench/BASELINE.md``.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

BENCH_DIR = Path(__file__).resolve().parent
REPO_ROOT = BENCH_DIR.parent.parent.parent  # scripts/permuter/bench -> repo root

# C1 gate thresholds.
# "both" must win at least as many functions as "off"  (no regression).
# Mean rounds-to-first-win may exceed "off" by at most this tolerance.
GATE_ROUNDS_TOLERANCE = 1.0


def _load_bench_set() -> dict:
    return json.loads((BENCH_DIR / "bench_set.json").read_text())


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.bench.c1_source_diff_ab"
    )
    parser.add_argument("--limit", type=int, default=0,
                        help="Cap number of functions (0 = all)")
    parser.add_argument("--bands", default="",
                        help="Comma-separated bands to include (high,mid,low). Default: all")
    parser.add_argument("--only", default="",
                        help="Comma-separated qualified-name substrings to include")
    parser.add_argument("--out", type=Path, default=None,
                        help="Results JSON path (default: bench/c1_source_diff_ab-results-<ts>.json)")
    return parser.parse_args()


def _reexec_with_pinned_seed(seed_cfg: dict) -> None:
    """Re-exec once with a pinned PYTHONHASHSEED so variant order is stable."""
    want = str(seed_cfg["pythonhashseed"])
    if os.environ.get("PYTHONHASHSEED") == want:
        return
    if os.environ.get("_C1AB_REEXEC") == "1":
        return
    os.environ["PYTHONHASHSEED"] = want
    os.environ["_C1AB_REEXEC"] = "1"
    os.execv(sys.executable, [sys.executable, "-m",
                              "scripts.permuter.bench.c1_source_diff_ab",
                              *sys.argv[1:]])


_CACHE_DIRS: list[Path] = []


def _isolate_score_cache() -> None:
    """Point the permuter's score cache at a fresh throwaway temp DB."""
    from scripts.permuter import score_cache
    cache_dir = Path(tempfile.mkdtemp(prefix="permuter_c1ab_cache_"))
    fresh_db = cache_dir / "permuter_cache.db"
    score_cache._CACHE_DB = fresh_db
    score_cache.ScoreCache.__init__.__defaults__ = (fresh_db,)
    _CACHE_DIRS.append(cache_dir)


def _teardown_score_caches() -> None:
    import shutil as _sh
    for d in _CACHE_DIRS:
        _sh.rmtree(d, ignore_errors=True)
    _CACHE_DIRS.clear()


def _run_mode(fn: dict, seed_cfg: dict, patterns: list, mode: str) -> dict:
    """Run hill_climb for one function in the given C1 mode.

    Returns a dict with: improved, reached_100, rounds_to_first_win,
    best_discovered, variants_scored, wall_seconds, error.
    """
    from scripts.permuter.hill_climber import hill_climb

    # Set the C1 mode env flag before creating any scorers.
    os.environ["PERMUTER_C1_SOURCE_DIFF"] = mode

    # Fresh isolated cache for each (function, mode) pair.
    _isolate_score_cache()

    source = REPO_ROOT / fn["source_path"]
    t0 = time.perf_counter()
    error = None
    result = None
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
            apply=False,
            unit=fn["unit"],
            workers=seed_cfg["workers"],
            validate=False,
        )
    except Exception as exc:
        error = f"{type(exc).__name__}: {exc}"
    wall = time.perf_counter() - t0

    # Clean up the env flag so it doesn't bleed across iterations.
    os.environ.pop("PERMUTER_C1_SOURCE_DIFF", None)

    if result is None:
        return {
            "mode": mode,
            "improved": False,
            "reached_100": False,
            "rounds_to_first_win": None,
            "best_discovered": None,
            "variants_scored": 0,
            "wall_seconds": round(wall, 3),
            "error": error,
        }

    variants_scored = sum(r.num_variants for r in result.rounds)
    round_bests = [r.best_score for r in result.rounds if r.best_score is not None]
    best_discovered = max([result.initial_percent, *round_bests]) if round_bests else result.initial_percent
    improved = (best_discovered - result.initial_percent) > 1e-6
    reached_100 = best_discovered >= 99.999

    # Rounds-to-first-win: the 1-based round index where we first improved.
    rounds_to_first_win = None
    if improved:
        for rnum, rnd in enumerate(result.rounds, start=1):
            if rnd.improved:
                rounds_to_first_win = rnum
                break

    return {
        "mode": mode,
        "improved": improved,
        "reached_100": reached_100,
        "rounds_to_first_win": rounds_to_first_win,
        "best_discovered": round(best_discovered, 4),
        "initial_percent": round(result.initial_percent, 4),
        "variants_scored": variants_scored,
        "wall_seconds": round(wall, 3),
        "error": error,
    }


def _summarize(per_function: list[dict]) -> dict:
    scored = [f for f in per_function if "off" in f and "both" in f
              and f["off"].get("error") is None and f["both"].get("error") is None]
    n = len(scored)
    if n == 0:
        return {"n_functions_scored": 0, "gate": {"passed": False,
                                                    "reasons_if_failed": ["no functions scored cleanly"]}}

    off_wins = sum(1 for f in scored if f["off"]["improved"])
    both_wins = sum(1 for f in scored if f["both"]["improved"])

    off_rounds = [f["off"]["rounds_to_first_win"] for f in scored
                  if f["off"]["rounds_to_first_win"] is not None]
    both_rounds = [f["both"]["rounds_to_first_win"] for f in scored
                   if f["both"]["rounds_to_first_win"] is not None]

    # wins/100 as a rate
    off_wins_per_100 = round(100.0 * off_wins / n, 2)
    both_wins_per_100 = round(100.0 * both_wins / n, 2)

    mean_off_rounds = (sum(off_rounds) / len(off_rounds)) if off_rounds else None
    mean_both_rounds = (sum(both_rounds) / len(both_rounds)) if both_rounds else None

    # Gate: both wins >= off wins AND rounds not worse (+ tolerance).
    gate_reasons = []
    if both_wins < off_wins:
        gate_reasons.append(
            f"both wins ({both_wins}) < off wins ({off_wins}) — regression"
        )
    if (mean_off_rounds is not None and mean_both_rounds is not None
            and mean_both_rounds > mean_off_rounds + GATE_ROUNDS_TOLERANCE):
        gate_reasons.append(
            f"mean rounds-to-first-win: both={mean_both_rounds:.2f} > "
            f"off={mean_off_rounds:.2f} + tolerance={GATE_ROUNDS_TOLERANCE}"
        )
    gate_pass = len(gate_reasons) == 0

    return {
        "n_functions_scored": n,
        "off_wins": off_wins,
        "both_wins": both_wins,
        "off_wins_per_100": off_wins_per_100,
        "both_wins_per_100": both_wins_per_100,
        "mean_rounds_to_first_win_off": round(mean_off_rounds, 2) if mean_off_rounds is not None else None,
        "mean_rounds_to_first_win_both": round(mean_both_rounds, 2) if mean_both_rounds is not None else None,
        "gate": {
            "passed": gate_pass,
            "rounds_tolerance": GATE_ROUNDS_TOLERANCE,
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
    print("C1 SOURCE-DIFF RANKING A/B (roadmap C1)")
    print(line)
    print(f"  git HEAD:                  {results['git_head']}")
    print(f"  functions scored:          {s['n_functions_scored']}/{results['n_functions']}")
    print(f"  overall wall:              {results['overall_wall_seconds']}s")
    print()
    print("  Win-rate comparison (improved at least once per function)")
    print(f"    off  wins / 100:         {s['off_wins_per_100']}  ({s['off_wins']}/{s['n_functions_scored']})")
    print(f"    both wins / 100:         {s['both_wins_per_100']}  ({s['both_wins']}/{s['n_functions_scored']})")
    print()
    print("  Rounds-to-first-win (lower = faster convergence)")
    print(f"    off  mean rounds:        {s['mean_rounds_to_first_win_off']}")
    print(f"    both mean rounds:        {s['mean_rounds_to_first_win_both']}")
    print()
    verdict = "PASS — flip default to both" if g["passed"] else "FAIL — keep default off"
    print(f"  GATE: {verdict}")
    if not g["passed"]:
        for r in g["reasons_if_failed"]:
            print(f"    - {r}")
    print()
    print(f"  results written: {out_path}")
    print(line)


def main() -> int:
    args = parse_args()
    bench = _load_bench_set()
    seed_cfg = bench["seed_config"]
    _reexec_with_pinned_seed(seed_cfg)

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

    print(f"[c1ab] {len(functions)} functions, patterns={len(patterns)}", file=sys.stderr)

    per_function: list[dict] = []
    overall_start = time.perf_counter()

    for i, fn in enumerate(functions):
        print(f"\n[{i + 1}/{len(functions)}] {fn['qualified_name']} "
              f"({fn['band']}, baseline {fn['baseline_percent']:.4f}%)",
              file=sys.stderr)
        rec: dict = {
            "id": fn["id"],
            "qualified_name": fn["qualified_name"],
            "unit": fn["unit"],
            "band": fn["band"],
            "baseline_percent": fn["baseline_percent"],
        }
        try:
            off = _run_mode(fn, seed_cfg, patterns, "off")
            both = _run_mode(fn, seed_cfg, patterns, "both")
            rec["off"] = off
            rec["both"] = both
            print(f"  off:  improved={off['improved']} "
                  f"best={off['best_discovered']}% "
                  f"rounds_to_win={off['rounds_to_first_win']} "
                  f"variants={off['variants_scored']} "
                  f"wall={off['wall_seconds']}s",
                  file=sys.stderr)
            print(f"  both: improved={both['improved']} "
                  f"best={both['best_discovered']}% "
                  f"rounds_to_win={both['rounds_to_first_win']} "
                  f"variants={both['variants_scored']} "
                  f"wall={both['wall_seconds']}s",
                  file=sys.stderr)
            if off.get("error"):
                print(f"    [off error] {off['error']}", file=sys.stderr)
            if both.get("error"):
                print(f"    [both error] {both['error']}", file=sys.stderr)
        except Exception as exc:
            rec["error"] = f"{type(exc).__name__}: {exc}"
            import traceback
            traceback.print_exc()
            print(f"    -> ERROR: {rec['error']}", file=sys.stderr)
        per_function.append(rec)

    overall_wall = time.perf_counter() - overall_start
    summary = _summarize(per_function)

    results = {
        "schema": "permuter-c1-source-diff-ab/1",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "git_head": _git_head(),
        "seed_config": seed_cfg,
        "n_functions": len(functions),
        "overall_wall_seconds": round(overall_wall, 2),
        "summary": summary,
        "functions": per_function,
    }

    out_path = args.out or (
        BENCH_DIR / f"c1_source_diff_ab-results-"
                    f"{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    )
    out_path.write_text(json.dumps(results, indent=2))
    _print_summary(results, out_path)
    _teardown_score_caches()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
