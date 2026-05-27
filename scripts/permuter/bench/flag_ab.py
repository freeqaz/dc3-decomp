"""Generic flag A/B harness for the permuter (stress-sweep helper).

Modeled on ``c1_source_diff_ab.py`` but parameterised so any default-off
optimization flag can be A/B'd against the default without a second checkout.

For each pinned bench function it runs ``hill_climb`` twice against an
ISOLATED, fresh score cache:

  * **OFF** — the named flag(s) cleared from the environment (production
    default).
  * **ON**  — the named flag(s) set to the supplied value(s).

The "safe to default-on" gate (per the stress-sweep task):

  * zero win-rate regression  (on_wins >= off_wins), AND
  * zero new crashes          (on_errors <= off_errors).

Bonus signal reported (not gated): variants_compiled delta (HARD_FILTERS /
PREDICTOR should compile *fewer*) and rounds-to-first-win delta (C1).

Usage::

    PYTHONHASHSEED=0 ./venv/bin/python -m scripts.permuter.bench.flag_ab \
        --bands mid,low --limit 12 \
        --on PERMUTER_HARD_FILTERS=1 --adaptive \
        --label hard_filters \
        --out /tmp/claude/sweep-out/ab_hard_filters.json

NOTE: run with the command sandbox DISABLED — wibo/cl.exe write objects to a
temp dir the sandbox blocks (SIGSYS -> silent fake-success). See BASELINE.md.
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
REPO_ROOT = BENCH_DIR.parent.parent.parent


def _load_bench_set() -> dict:
    return json.loads((BENCH_DIR / "bench_set.json").read_text())


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(prog="python -m scripts.permuter.bench.flag_ab")
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--bands", default="")
    p.add_argument("--only", default="")
    p.add_argument("--on", action="append", default=[],
                   help="KEY=VAL env to set in the ON arm (repeatable)")
    p.add_argument("--off", action="append", default=[],
                   help="KEY=VAL env to set in the OFF arm (repeatable). Use "
                        "when the production default is NOT 'unset' — e.g. C1 "
                        "defaults to 'both', so OFF must explicitly set 'off'. "
                        "When omitted, the OFF arm simply clears the --on keys.")
    p.add_argument("--adaptive", action="store_true",
                   help="Pass adaptive=True to hill_climb in BOTH arms "
                        "(required for HARD_FILTERS to fire — needs round_hints)")
    p.add_argument("--chain", action="store_true",
                   help="Pass chain=True to hill_climb in BOTH arms (enables "
                        "beam search — required for the C1 source-diff ranking "
                        "signal to be consulted at all).")
    p.add_argument("--m2c", action="store_true",
                   help="Pass m2c=True to hill_climb in BOTH arms (loads m2c "
                        "decomp so the C1 source-diff signal has source to "
                        "diff against; m2c is local, no Ghidra MCP needed).")
    p.add_argument("--label", default="flag")
    p.add_argument("--out", type=Path, default=None)
    return p.parse_args()


def _reexec_with_pinned_seed(seed_cfg: dict) -> None:
    want = str(seed_cfg["pythonhashseed"])
    if os.environ.get("PYTHONHASHSEED") == want:
        return
    if os.environ.get("_FLAGAB_REEXEC") == "1":
        return
    os.environ["PYTHONHASHSEED"] = want
    os.environ["_FLAGAB_REEXEC"] = "1"
    os.execv(sys.executable, [sys.executable, "-m",
                              "scripts.permuter.bench.flag_ab", *sys.argv[1:]])


_CACHE_DIRS: list[Path] = []


def _isolate_score_cache() -> None:
    from scripts.permuter import score_cache
    cache_dir = Path(tempfile.mkdtemp(prefix="permuter_flagab_cache_"))
    fresh_db = cache_dir / "permuter_cache.db"
    score_cache._CACHE_DB = fresh_db
    score_cache.ScoreCache.__init__.__defaults__ = (fresh_db,)
    _CACHE_DIRS.append(cache_dir)


def _teardown_score_caches() -> None:
    import shutil as _sh
    for d in _CACHE_DIRS:
        _sh.rmtree(d, ignore_errors=True)
    _CACHE_DIRS.clear()


def _parse_on(pairs: list[str]) -> dict[str, str]:
    env = {}
    for kv in pairs:
        if "=" not in kv:
            raise SystemExit(f"--on expects KEY=VAL, got {kv!r}")
        k, v = kv.split("=", 1)
        env[k.strip()] = v.strip()
    return env


def _run_mode(fn: dict, seed_cfg: dict, patterns: list, arm: str,
              on_env: dict[str, str], off_env: dict[str, str],
              adaptive: bool, chain: bool = False, m2c: bool = False) -> dict:
    from scripts.permuter.hill_climber import hill_climb

    # Apply / clear the flag env for this arm.
    saved = {}
    keys = set(on_env.keys()) | set(off_env.keys())
    for k in keys:
        saved[k] = os.environ.get(k)
    if arm == "on":
        # Start from a clean slate for all keys, then set the ON values.
        for k in keys:
            os.environ.pop(k, None)
        for k, v in on_env.items():
            os.environ[k] = v
    else:  # off — clear all keys, then apply any explicit OFF values
        for k in keys:
            os.environ.pop(k, None)
        for k, v in off_env.items():
            os.environ[k] = v

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
            adaptive=adaptive,
            chain=chain,
            m2c=m2c,
        )
    except Exception as exc:  # keep sweep alive; record as a crash
        import traceback
        error = f"{type(exc).__name__}: {exc}"
        traceback.print_exc()
    wall = time.perf_counter() - t0

    # restore env
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v

    if result is None:
        return {"arm": arm, "improved": False, "reached_100": False,
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
    return {"arm": arm, "improved": improved,
            "reached_100": best >= 99.999, "rounds_to_first_win": rtw,
            "best_discovered": round(best, 4),
            "initial_percent": round(result.initial_percent, 4),
            "variants_scored": variants_scored,
            "wall_seconds": round(wall, 3), "error": None}


def main() -> int:
    args = parse_args()
    bench = _load_bench_set()
    seed_cfg = bench["seed_config"]
    _reexec_with_pinned_seed(seed_cfg)

    sys.path.insert(0, str(REPO_ROOT))
    from scripts.permuter.patterns import get_all_patterns
    patterns = get_all_patterns()
    on_env = _parse_on(args.on)
    off_env = _parse_on(args.off)

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

    print(f"[flag_ab:{args.label}] {len(functions)} functions, "
          f"on_env={on_env}, off_env={off_env}, adaptive={args.adaptive}",
          file=sys.stderr)

    rows = []
    t_all = time.perf_counter()
    for i, fn in enumerate(functions):
        print(f"\n[{i+1}/{len(functions)}] {fn['qualified_name']} "
              f"({fn['band']}, base {fn['baseline_percent']:.2f}%)", file=sys.stderr)
        off = _run_mode(fn, seed_cfg, patterns, "off", on_env, off_env,
                        args.adaptive, args.chain, args.m2c)
        print(f"    OFF: improved={off['improved']} "
              f"vars={off['variants_scored']} {off['wall_seconds']}s "
              f"err={off['error']}", file=sys.stderr)
        on = _run_mode(fn, seed_cfg, patterns, "on", on_env, off_env,
                       args.adaptive, args.chain, args.m2c)
        print(f"    ON : improved={on['improved']} "
              f"vars={on['variants_scored']} {on['wall_seconds']}s "
              f"err={on['error']}", file=sys.stderr)
        rows.append({"id": fn["id"], "qualified_name": fn["qualified_name"],
                     "band": fn["band"], "off": off, "on": on})
    wall_all = time.perf_counter() - t_all
    _teardown_score_caches()

    def agg(arm):
        wins = sum(1 for r in rows if r[arm]["improved"])
        errs = sum(1 for r in rows if r[arm]["error"])
        vars_ = sum(r[arm]["variants_scored"] for r in rows)
        wall = sum(r[arm]["wall_seconds"] for r in rows)
        rtws = [r[arm]["rounds_to_first_win"] for r in rows
                if r[arm]["rounds_to_first_win"] is not None]
        mean_rtw = round(sum(rtws) / len(rtws), 3) if rtws else None
        return {"wins": wins, "errors": errs, "variants_scored": vars_,
                "wall_seconds": round(wall, 2), "mean_rounds_to_win": mean_rtw,
                "n": len(rows),
                "wins_per_100": round(100.0 * wins / len(rows), 2) if rows else 0.0}

    off_a, on_a = agg("off"), agg("on")
    win_regression = on_a["wins"] < off_a["wins"]
    new_crashes = on_a["errors"] > off_a["errors"]
    verdict = ("NOT_SAFE" if (win_regression or new_crashes)
               else ("NO_EFFECT" if (on_a["wins"] == off_a["wins"]
                                     and on_a["variants_scored"] == off_a["variants_scored"])
                     else "SAFE"))

    results = {
        "schema": "permuter-flag-ab/1",
        "label": args.label,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "on_env": on_env, "off_env": off_env, "adaptive": args.adaptive,
        "n_functions": len(rows),
        "wall_seconds": round(wall_all, 2),
        "off": off_a, "on": on_a,
        "win_regression": win_regression, "new_crashes": new_crashes,
        "verdict": verdict,
        "rows": rows,
    }
    out = args.out or (BENCH_DIR / f"flag_ab_{args.label}_"
                       f"{datetime.now().strftime('%Y%m%d_%H%M%S')}.json")
    out.write_text(json.dumps(results, indent=2))

    line = "=" * 64
    print(f"\n{line}\nFLAG A/B: {args.label}\n{line}")
    print(f"  functions: {len(rows)}   wall: {results['wall_seconds']}s")
    print(f"  {'arm':4s} {'wins':>5s} {'wins/100':>9s} {'vars':>6s} "
          f"{'errs':>5s} {'mean_rtw':>9s}")
    for nm, a in (("OFF", off_a), ("ON", on_a)):
        print(f"  {nm:4s} {a['wins']:5d} {a['wins_per_100']:9.2f} "
              f"{a['variants_scored']:6d} {a['errors']:5d} "
              f"{str(a['mean_rounds_to_win']):>9s}")
    dvars = on_a["variants_scored"] - off_a["variants_scored"]
    print(f"  variants delta (on-off): {dvars:+d} "
          f"({'fewer compiles' if dvars < 0 else 'more compiles' if dvars > 0 else 'same'})")
    print(f"  win regression: {win_regression}   new crashes: {new_crashes}")
    print(f"  VERDICT: {verdict}")
    print(f"  written: {out}\n{line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
