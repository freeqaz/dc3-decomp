"""WP1: Beam vs Greedy benchmark on a fixed 30-function slice.

Runs each target function through both beam search and hill_climb (greedy),
captures metrics (delta, elapsed, rounds, stopped_reason), and emits a
JSON results artifact + summary to stderr.

Usage:
    # Full benchmark (both strategies, all 30 targets)
    python -m scripts.permuter.tests.benchmark_beam

    # Quick smoke test (first 3 targets, greedy only)
    python -m scripts.permuter.tests.benchmark_beam --limit 3 --strategy hill_climb

    # Beam only, specific bracket
    python -m scripts.permuter.tests.benchmark_beam --bracket low --strategy beam

    # Output to file
    python -m scripts.permuter.tests.benchmark_beam --output results.json
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent.parent
TARGETS_FILE = Path(__file__).resolve().parent / "benchmark_targets.json"


# ---------------------------------------------------------------------------
# Resolution helpers
# ---------------------------------------------------------------------------

def _load_unit_to_source() -> dict[str, str]:
    """Build unit_name -> source_path from objdiff.json."""
    objdiff = REPO_ROOT / "objdiff.json"
    if not objdiff.exists():
        print("Error: objdiff.json not found", file=sys.stderr)
        return {}
    data = json.loads(objdiff.read_text())
    mapping: dict[str, str] = {}
    for u in data["units"]:
        src = u.get("metadata", {}).get("source_path")
        if src:
            mapping[u["name"]] = src
    return mapping


def _load_symbol_info() -> dict[str, dict]:
    """Build symbol -> {unit, demangled, match_pct} from report.json."""
    report = REPO_ROOT / "build" / "373307D9" / "report.json"
    if not report.exists():
        print("Error: report.json not found", file=sys.stderr)
        return {}
    data = json.loads(report.read_text())
    info: dict[str, dict] = {}
    for unit in data["units"]:
        for fn in unit.get("functions", []):
            info[fn["name"]] = {
                "unit": unit["name"],
                "demangled": fn.get("metadata", {}).get("demangled_name", ""),
                "match_pct": fn.get("fuzzy_match_percent", 0),
            }
    return info


def _extract_qualified_name(demangled: str) -> str:
    """Extract Class::Method or free function name from demangled signature."""
    import re
    # Pattern: ... Name::Method(...) or ... FreeName(...)
    m = re.search(r'(?:(\w[\w:]*::))?(\w+)\s*\(', demangled)
    if m:
        cls = m.group(1) or ""
        name = m.group(2)
        return f"{cls}{name}"
    # Plain C function name (no parens in demangled)
    if demangled and re.match(r'^[A-Za-z_]\w*$', demangled.strip()):
        return demangled.strip()
    return ""


# ---------------------------------------------------------------------------
# Single-function runner
# ---------------------------------------------------------------------------

@dataclass
class FunctionResult:
    symbol: str
    unit: str
    bracket: str
    baseline_pct: float
    strategy: str
    initial_pct: float
    final_pct: float
    delta: float
    elapsed_s: float
    rounds: int
    stopped_reason: str
    validation_tier: int = 0
    error: str | None = None


def _run_one(
    symbol: str,
    unit: str,
    source_path: str,
    func_name: str,
    bracket: str,
    strategy: str,
    max_rounds: int = 3,
    beam_width: int = 6,
    beam_depth: int = 3,
    beam_expand: int = 16,
    no_apply: bool = True,
) -> FunctionResult:
    """Run one strategy on one function, return metrics."""
    # Late imports to avoid heavy startup cost until needed
    sys.path.insert(0, str(REPO_ROOT))

    from scripts.permuter.patterns import get_all_patterns
    patterns = get_all_patterns()

    t0 = time.time()
    try:
        if strategy == "beam":
            from scripts.permuter.beam_search import beam_search
            from scripts.permuter.types import BeamConfig
            config = BeamConfig(
                width=beam_width,
                depth=beam_depth,
                expand=beam_expand,
            )
            result = beam_search(
                symbol=symbol,
                source_path=Path(REPO_ROOT / source_path),
                function_name=func_name,
                patterns=patterns,
                config=config,
                apply=not no_apply,
                unit=unit,
                ghidra=False,   # Benchmark without Ghidra for reproducibility
                m2c=False,
                constrained=False,
            )
        else:  # hill_climb
            from scripts.permuter.hill_climber import hill_climb
            result = hill_climb(
                symbol=symbol,
                source_path=Path(REPO_ROOT / source_path),
                function_name=func_name,
                patterns=patterns,
                max_rounds=max_rounds,
                max_variants=50,
                plateau_limit=2,
                compose=True,
                apply=not no_apply,
                ghidra=False,
                m2c=False,
                chain=False,
                adaptive=False,
                constrained=False,
            )
        elapsed = time.time() - t0
        return FunctionResult(
            symbol=symbol,
            unit=unit,
            bracket=bracket,
            baseline_pct=result.initial_percent,
            strategy=strategy,
            initial_pct=result.initial_percent,
            final_pct=result.final_percent,
            delta=result.final_percent - result.initial_percent,
            elapsed_s=round(elapsed, 2),
            rounds=len(result.rounds),
            stopped_reason=result.stopped_reason,
            validation_tier=result.validation_tier,
        )
    except Exception as e:
        elapsed = time.time() - t0
        return FunctionResult(
            symbol=symbol,
            unit=unit,
            bracket=bracket,
            baseline_pct=0,
            strategy=strategy,
            initial_pct=0,
            final_pct=0,
            delta=0,
            elapsed_s=round(elapsed, 2),
            rounds=0,
            stopped_reason="error",
            error=str(e),
        )


# ---------------------------------------------------------------------------
# Main benchmark loop
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Beam vs Greedy benchmark")
    parser.add_argument("--limit", type=int, default=0, help="Max targets (0=all)")
    parser.add_argument("--bracket", choices=["high", "mid", "low"],
                        help="Only run targets in this bracket")
    parser.add_argument("--strategy", choices=["beam", "hill_climb", "both"],
                        default="both", help="Strategy to benchmark (default: both)")
    parser.add_argument("--output", "-o", help="JSON output file path")
    parser.add_argument("--max-rounds", type=int, default=3,
                        help="Max rounds for hill_climb (default: 3)")
    parser.add_argument("--beam-width", type=int, default=6,
                        help="Beam width (default: 6)")
    parser.add_argument("--beam-depth", type=int, default=3,
                        help="Beam depth (default: 3)")
    parser.add_argument("--beam-expand", type=int, default=16,
                        help="Proposals per state (default: 16)")
    args = parser.parse_args()

    # Load targets
    targets_data = json.loads(TARGETS_FILE.read_text())
    targets = targets_data["targets"]

    # Filter by bracket
    if args.bracket:
        targets = [t for t in targets if t["bracket"] == args.bracket]

    # Apply limit
    if args.limit > 0:
        targets = targets[:args.limit]

    # Resolve source paths
    unit_to_source = _load_unit_to_source()
    symbol_info = _load_symbol_info()

    # Determine strategies to run
    strategies = ["beam", "hill_climb"] if args.strategy == "both" else [args.strategy]

    print(f"Benchmark: {len(targets)} targets × {len(strategies)} strategies "
          f"= {len(targets) * len(strategies)} runs", file=sys.stderr)
    print(f"Settings: rounds={args.max_rounds}, beam={args.beam_width}w/{args.beam_depth}d/{args.beam_expand}e",
          file=sys.stderr)
    print("", file=sys.stderr)

    results: list[FunctionResult] = []
    total_runs = len(targets) * len(strategies)
    run_idx = 0

    for target in targets:
        symbol = target["symbol"]
        unit = target["unit"]
        bracket = target["bracket"]
        source_path = unit_to_source.get(unit)

        if not source_path:
            print(f"  SKIP {symbol[:60]}: no source path for unit {unit}",
                  file=sys.stderr)
            continue

        # Get qualified function name from report
        info = symbol_info.get(symbol, {})
        demangled = info.get("demangled", "")
        func_name = _extract_qualified_name(demangled)
        if not func_name:
            # Fallback: use demangled as-is
            func_name = demangled

        for strategy in strategies:
            run_idx += 1
            short_name = demangled[:50] if demangled else symbol[:50]
            print(f"[{run_idx}/{total_runs}] {strategy:10s} {short_name} ({bracket})",
                  file=sys.stderr)

            r = _run_one(
                symbol=symbol,
                unit=unit,
                source_path=source_path,
                func_name=func_name,
                bracket=bracket,
                strategy=strategy,
                max_rounds=args.max_rounds,
                beam_width=args.beam_width,
                beam_depth=args.beam_depth,
                beam_expand=args.beam_expand,
            )
            results.append(r)

            status = "ERR" if r.error else ("++" if r.delta > 0 else "==")
            print(f"  {status} {r.initial_pct:.1f}→{r.final_pct:.1f}% "
                  f"(Δ{r.delta:+.2f}) {r.elapsed_s:.1f}s {r.stopped_reason}",
                  file=sys.stderr)

    # Build summary
    summary = _build_summary(results, strategies)

    # Emit JSON
    output = {
        "benchmark": "beam_vs_greedy",
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "settings": {
            "max_rounds": args.max_rounds,
            "beam_width": args.beam_width,
            "beam_depth": args.beam_depth,
            "beam_expand": args.beam_expand,
            "ghidra": False,
            "constrained": False,
        },
        "targets_count": len(targets),
        "strategies": strategies,
        "summary": summary,
        "results": [asdict(r) for r in results],
    }

    json_str = json.dumps(output, indent=2)

    if args.output:
        Path(args.output).write_text(json_str)
        print(f"\nResults written to {args.output}", file=sys.stderr)
    else:
        print(json_str)

    # Print summary to stderr
    _print_summary(summary, strategies)


def _build_summary(results: list[FunctionResult], strategies: list[str]) -> dict:
    """Aggregate results into per-strategy summary stats."""
    summary: dict = {}
    for strat in strategies:
        strat_results = [r for r in results if r.strategy == strat]
        if not strat_results:
            continue

        deltas = [r.delta for r in strat_results if not r.error]
        elapsed = [r.elapsed_s for r in strat_results if not r.error]
        improved = [r for r in strat_results if r.delta > 0]
        errors = [r for r in strat_results if r.error]

        # Per-bracket breakdown
        brackets: dict = {}
        for bracket in ("high", "mid", "low"):
            br = [r for r in strat_results if r.bracket == bracket and not r.error]
            if br:
                brackets[bracket] = {
                    "count": len(br),
                    "improved": sum(1 for r in br if r.delta > 0),
                    "mean_delta": round(sum(r.delta for r in br) / len(br), 3),
                    "max_delta": round(max(r.delta for r in br), 3) if br else 0,
                    "mean_elapsed": round(sum(r.elapsed_s for r in br) / len(br), 1),
                }

        summary[strat] = {
            "total": len(strat_results),
            "improved": len(improved),
            "errors": len(errors),
            "mean_delta": round(sum(deltas) / len(deltas), 3) if deltas else 0,
            "max_delta": round(max(deltas), 3) if deltas else 0,
            "sum_delta": round(sum(deltas), 3) if deltas else 0,
            "mean_elapsed_s": round(sum(elapsed) / len(elapsed), 1) if elapsed else 0,
            "total_elapsed_s": round(sum(elapsed), 1) if elapsed else 0,
            "stopped_reasons": _count_reasons(strat_results),
            "brackets": brackets,
        }

    # Head-to-head comparison (if both strategies ran)
    if len(strategies) == 2:
        s1, s2 = strategies
        r1 = {r.symbol: r for r in results if r.strategy == s1 and not r.error}
        r2 = {r.symbol: r for r in results if r.strategy == s2 and not r.error}
        common = set(r1.keys()) & set(r2.keys())
        if common:
            wins_1 = sum(1 for s in common if r1[s].delta > r2[s].delta)
            wins_2 = sum(1 for s in common if r2[s].delta > r1[s].delta)
            ties = len(common) - wins_1 - wins_2
            summary["head_to_head"] = {
                "common_targets": len(common),
                f"{s1}_wins": wins_1,
                f"{s2}_wins": wins_2,
                "ties": ties,
                f"{s1}_mean_delta": round(
                    sum(r1[s].delta for s in common) / len(common), 3
                ),
                f"{s2}_mean_delta": round(
                    sum(r2[s].delta for s in common) / len(common), 3
                ),
            }

    return summary


def _count_reasons(results: list[FunctionResult]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for r in results:
        reason = r.stopped_reason
        counts[reason] = counts.get(reason, 0) + 1
    return counts


def _print_summary(summary: dict, strategies: list[str]):
    print("\n" + "=" * 60, file=sys.stderr)
    print("BENCHMARK SUMMARY", file=sys.stderr)
    print("=" * 60, file=sys.stderr)

    for strat in strategies:
        s = summary.get(strat, {})
        if not s:
            continue
        print(f"\n{strat.upper()}:", file=sys.stderr)
        print(f"  Targets: {s['total']}  Improved: {s['improved']}  "
              f"Errors: {s['errors']}", file=sys.stderr)
        print(f"  Delta: mean={s['mean_delta']:+.3f}%  max={s['max_delta']:+.3f}%  "
              f"sum={s['sum_delta']:+.3f}%", file=sys.stderr)
        print(f"  Time: mean={s['mean_elapsed_s']:.1f}s  "
              f"total={s['total_elapsed_s']:.1f}s", file=sys.stderr)
        print(f"  Stop reasons: {s['stopped_reasons']}", file=sys.stderr)
        for bracket, bs in s.get("brackets", {}).items():
            print(f"  [{bracket}] n={bs['count']}  improved={bs['improved']}  "
                  f"mean_Δ={bs['mean_delta']:+.3f}%  max_Δ={bs['max_delta']:+.3f}%",
                  file=sys.stderr)

    h2h = summary.get("head_to_head")
    if h2h:
        print(f"\nHEAD-TO-HEAD ({h2h['common_targets']} common targets):", file=sys.stderr)
        for k, v in h2h.items():
            if k != "common_targets":
                print(f"  {k}: {v}", file=sys.stderr)
    print("=" * 60, file=sys.stderr)


if __name__ == "__main__":
    main()
