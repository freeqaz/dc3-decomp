"""RB3 single-flag A/B driver (HARD_FILTERS / PREDICTOR / C1_SOURCE_DIFF).

Counterpart to ``c1_source_diff_ab.py`` but parameterised over which flag to
toggle, and run against the RB3 bench set (``bench_set_rb3.json``) on the
mwcceppc toolchain. For each pinned RB3 bench function it runs ``hill_climb``
twice against a FRESH isolated score cache:

  * **A** — control (flag in its production-default state)
  * **B** — treatment (flag flipped)

For each side it records: improved, reached_100, rounds-to-first-win,
best_discovered, variants_scored, wall_seconds, and ERROR (the stability
signal — any exception/crash is captured, never swallowed).

Supported flags (``--flag``):

  hard_filters   A = PERMUTER_HARD_FILTERS unset    B = =1   (needs --adaptive)
  predictor      A = PERMUTER_PREDICTOR unset        B = =1 with
                 PERMUTER_PREDICTOR_BUDGET set low enough to actually cull
                 (default 8 — well under max_variants=40)
  c1             A = PERMUTER_C1_SOURCE_DIFF=off      B = =both

Verdict per flag is win-rate + stability: B must not regress wins and must not
introduce crashes/hangs that A did not have.

NOTE: sandbox MUST be disabled — wibo/mwcceppc write objects the sandbox blocks
(SIGSYS -> silent fake-success). Driven via /tmp/claude/run_rb3_flag_ab.py
(sets sys.path + chdir RB3 + PERMUTER_PROJECT=rb3). apply=False throughout.
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

_CACHE_DIRS: list[Path] = []


def _isolate_score_cache() -> None:
    from scripts.permuter import score_cache
    d = Path(tempfile.mkdtemp(prefix="rb3_flagab_cache_"))
    fresh = d / "permuter_cache.db"
    score_cache._CACHE_DB = fresh
    score_cache.ScoreCache.__init__.__defaults__ = (fresh,)
    _CACHE_DIRS.append(d)


def _teardown() -> None:
    import shutil
    for d in _CACHE_DIRS:
        shutil.rmtree(d, ignore_errors=True)
    _CACHE_DIRS.clear()


# flag -> (A-env-dict, B-env-dict). A value of None means "pop the key".
def _flag_envs(flag: str, predictor_budget: int) -> tuple[dict, dict, bool]:
    if flag == "hard_filters":
        return ({"PERMUTER_HARD_FILTERS": None},
                {"PERMUTER_HARD_FILTERS": "1"},
                True)  # adaptive required so round_hints flow to the filter
    if flag == "predictor":
        return ({"PERMUTER_PREDICTOR": None, "PERMUTER_PREDICTOR_BUDGET": None},
                {"PERMUTER_PREDICTOR": "1",
                 "PERMUTER_PREDICTOR_BUDGET": str(predictor_budget)},
                False)
    if flag == "c1":
        return ({"PERMUTER_C1_SOURCE_DIFF": "off"},
                {"PERMUTER_C1_SOURCE_DIFF": "both"},
                False)
    raise SystemExit(f"unknown flag {flag!r}")


_ALL_FLAG_KEYS = ["PERMUTER_HARD_FILTERS", "PERMUTER_PREDICTOR",
                  "PERMUTER_PREDICTOR_BUDGET", "PERMUTER_C1_SOURCE_DIFF"]


def _apply_env(env: dict) -> None:
    for k, v in env.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v


def _run_side(fn, seed_cfg, patterns, env, adaptive):
    from scripts.permuter.hill_climber import hill_climb
    # Clear all flag keys first so nothing bleeds across sides/functions.
    for k in _ALL_FLAG_KEYS:
        os.environ.pop(k, None)
    _apply_env(env)
    _isolate_score_cache()

    source = RB3 / fn["source_path"]
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
            adaptive=adaptive,
        )
    except Exception as exc:
        import traceback
        traceback.print_exc()
        error = f"{type(exc).__name__}: {exc}"
    wall = time.perf_counter() - t0
    for k in _ALL_FLAG_KEYS:
        os.environ.pop(k, None)

    if result is None:
        return {"improved": False, "reached_100": False,
                "rounds_to_first_win": None, "best_discovered": None,
                "variants_scored": 0, "wall_seconds": round(wall, 3),
                "error": error}

    variants_scored = sum(r.num_variants for r in result.rounds)
    round_bests = [r.best_score for r in result.rounds if r.best_score is not None]
    best = max([result.initial_percent, *round_bests]) if round_bests else result.initial_percent
    improved = (best - result.initial_percent) > 1e-6
    rtw = None
    if improved:
        for rnum, rnd in enumerate(result.rounds, start=1):
            if rnd.improved:
                rtw = rnum
                break
    return {"improved": improved, "reached_100": best >= 99.999,
            "rounds_to_first_win": rtw, "best_discovered": round(best, 4),
            "initial_percent": round(result.initial_percent, 4),
            "variants_scored": variants_scored, "wall_seconds": round(wall, 3),
            "error": error}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--flag", required=True,
                    choices=["hard_filters", "predictor", "c1"])
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--predictor-budget", type=int, default=8)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()

    bench = json.loads(BENCH_SET.read_text())
    seed_cfg = bench["seed_config"]
    sys.path.insert(0, str(WT))
    from scripts.permuter.patterns import get_all_patterns
    patterns = get_all_patterns()

    functions = bench["functions"]
    if args.limit:
        functions = functions[: args.limit]

    env_a, env_b, adaptive = _flag_envs(args.flag, args.predictor_budget)
    print(f"[rb3-flag-ab] flag={args.flag} {len(functions)} functions "
          f"patterns={len(patterns)} adaptive={adaptive}", file=sys.stderr)

    per_fn = []
    t_start = time.perf_counter()
    for i, fn in enumerate(functions):
        print(f"\n[{i+1}/{len(functions)}] {fn['qualified_name']}", file=sys.stderr)
        rec = {"qualified_name": fn["qualified_name"], "unit": fn["unit"],
               "band": fn["band"], "baseline_percent": fn["baseline_percent"]}
        try:
            a = _run_side(fn, seed_cfg, patterns, env_a, adaptive)
            b = _run_side(fn, seed_cfg, patterns, env_b, adaptive)
            rec["a"] = a
            rec["b"] = b
            print(f"  A: improved={a['improved']} best={a['best_discovered']}% "
                  f"rtw={a['rounds_to_first_win']} v={a['variants_scored']} "
                  f"{a['wall_seconds']}s err={a['error']}", file=sys.stderr)
            print(f"  B: improved={b['improved']} best={b['best_discovered']}% "
                  f"rtw={b['rounds_to_first_win']} v={b['variants_scored']} "
                  f"{b['wall_seconds']}s err={b['error']}", file=sys.stderr)
        except Exception as exc:
            import traceback
            traceback.print_exc()
            rec["error"] = f"{type(exc).__name__}: {exc}"
            print(f"  -> ERROR {rec['error']}", file=sys.stderr)
        per_fn.append(rec)

    wall = time.perf_counter() - t_start
    scored = [f for f in per_fn if "a" in f and "b" in f]
    a_wins = sum(1 for f in scored if f["a"]["improved"])
    b_wins = sum(1 for f in scored if f["b"]["improved"])
    a_err = sum(1 for f in scored if f["a"]["error"]) + sum(1 for f in per_fn if "error" in f)
    b_err = sum(1 for f in scored if f["b"]["error"]) + sum(1 for f in per_fn if "error" in f)
    a_v = sum(f["a"]["variants_scored"] for f in scored)
    b_v = sum(f["b"]["variants_scored"] for f in scored)
    a_wall = sum(f["a"]["wall_seconds"] for f in scored)
    b_wall = sum(f["b"]["wall_seconds"] for f in scored)

    summary = {
        "flag": args.flag,
        "n_functions_scored": len(scored),
        "a_wins": a_wins, "b_wins": b_wins,
        "a_variants_scored": a_v, "b_variants_scored": b_v,
        "a_variants_per_sec": round(a_v / a_wall, 3) if a_wall else None,
        "b_variants_per_sec": round(b_v / b_wall, 3) if b_wall else None,
        "a_errors": a_err, "b_errors": b_err,
        "a_wall_seconds": round(a_wall, 2), "b_wall_seconds": round(b_wall, 2),
        "win_regression": b_wins < a_wins,
        "new_crashes": b_err > a_err,
    }
    results = {
        "schema": "rb3-flag-ab/1",
        "flag": args.flag,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "overall_wall_seconds": round(wall, 2),
        "n_functions": len(functions),
        "summary": summary,
        "functions": per_fn,
    }
    out = args.out or (WT / "scripts/permuter/bench" /
                       f"rb3_flag_ab-{args.flag}-{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    out.write_text(json.dumps(results, indent=2))

    L = "=" * 64
    print(f"\n{L}\nRB3 FLAG A/B — {args.flag}\n{L}")
    print(f"  functions scored:   {summary['n_functions_scored']}/{len(functions)}")
    print(f"  overall wall:       {results['overall_wall_seconds']}s")
    print(f"  A wins / B wins:    {a_wins} / {b_wins}")
    print(f"  A v/s / B v/s:      {summary['a_variants_per_sec']} / {summary['b_variants_per_sec']}")
    print(f"  A err / B err:      {a_err} / {b_err}")
    print(f"  win regression:     {summary['win_regression']}")
    print(f"  new crashes:        {summary['new_crashes']}")
    verdict = ("SAFE (no win regression, no new crashes)"
               if not summary["win_regression"] and not summary["new_crashes"]
               else "NOT SAFE")
    print(f"  VERDICT:            {verdict}")
    print(f"  results: {out}\n{L}")
    _teardown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
