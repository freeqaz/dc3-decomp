"""CLI entry point: python -m scripts.permuter"""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path

from .diagnosis import format_diagnosis_summary, is_all_noise
from .extractor import extract_function
from .generator import generate_variants
from .scorer import Scorer
from .patterns import get_all_patterns, get_pattern, list_patterns
from .repo_paths import get_decomp_db_path
from .types import ScoreResult


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter",
        description="Generate and score source variations for decomp matching.",
    )
    parser.add_argument(
        "--symbol", help="Mangled symbol name for objdiff"
    )
    parser.add_argument(
        "--source", type=Path, help="Path to .cpp source file"
    )
    parser.add_argument(
        "--function", help="Qualified C++ function name (e.g. RndMesh::BurnXfm)"
    )
    parser.add_argument(
        "--patterns",
        default="all",
        help="Comma-separated pattern names, or 'all' (default: all)",
    )
    parser.add_argument(
        "--workers", type=int, default=1,
        help="Parallel workers for variant scoring (default: 1)",
    )
    parser.add_argument(
        "--max-variants",
        type=int,
        default=100,
        help="Maximum variants to generate (default: 100)",
    )
    parser.add_argument(
        "--no-stop-on-perfect",
        action="store_true",
        help="Continue scoring even after a 100%% match is found",
    )
    parser.add_argument(
        "--json", action="store_true", dest="json_output", help="Output results as JSON"
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Generate and list variants without building/scoring",
    )
    parser.add_argument(
        "--no-apply",
        action="store_true",
        help="Do not apply the best-improving variant (default: apply)",
    )
    parser.add_argument(
        "--unit",
        help="Unit name for unicorn execution guard rail (e.g. system/gesture/Skeleton)",
    )
    parser.add_argument(
        "--no-guided",
        action="store_true",
        help="Disable diagnosis-guided filtering (try all patterns blindly)",
    )
    parser.add_argument(
        "--no-compose",
        action="store_true",
        help="Disable two-step pattern composition",
    )
    parser.add_argument(
        "--no-bsf-guided",
        action="store_true",
        help="Disable BSF-guided declaration reordering",
    )
    parser.add_argument(
        "--bsf-required",
        action="store_true",
        help="Fail if BSF tracing/guidance fails (no fallback to unguided)",
    )
    parser.add_argument(
        "--compiler",
        choices=("mwcc", "msvc"),
        default=None,
        help="Compiler dialect target (mwcc=C++98 CodeWarrior, msvc=modern). "
             "Patterns emitting C++11+ syntax check this. "
             "Overrides permuter.json (default: mwcc).",
    )
    parser.add_argument(
        "--list-patterns",
        action="store_true",
        help="List available patterns and exit",
    )
    return parser.parse_args()


def resolve_from_db(symbol: str) -> tuple[str, Path, str] | None:
    """Resolve symbol -> (symbol, source_path, qualified_name) from decomp.db + objdiff.json."""
    import re
    import sqlite3

    db_path = get_decomp_db_path()
    if not db_path.exists():
        return None

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row

    # Try exact symbol match, then demangled LIKE match
    row = conn.execute(
        "SELECT symbol, demangled, unit FROM functions WHERE symbol = ? LIMIT 1",
        (symbol,),
    ).fetchone()
    if not row:
        row = conn.execute(
            "SELECT symbol, demangled, unit FROM functions WHERE demangled LIKE ? LIMIT 1",
            (f"%{symbol}%",),
        ).fetchone()
    if not row:
        return None

    mangled = row["symbol"]
    demangled = row["demangled"]
    unit = row["unit"]

    # Extract qualified C++ name from demangled signature
    from .types import extract_qualified_name
    qualified_name = extract_qualified_name(demangled)
    if not qualified_name:
        return None

    # Look up source_path from objdiff.json
    objdiff_path = Path("objdiff.json")
    if not objdiff_path.exists():
        return None

    data = json.load(open(objdiff_path))
    source_path = None
    for u in data["units"]:
        if u["name"] == unit:
            source_path = u.get("metadata", {}).get("source_path")
            break

    if not source_path:
        return None

    return mangled, Path(source_path), qualified_name


def main():
    args = parse_args()

    if args.list_patterns:
        for name in list_patterns():
            print(f"  {name}")
        return

    # Auto-resolve from DB if only --symbol is provided
    if args.symbol and (not args.source or not args.function):
        resolved = resolve_from_db(args.symbol)
        if resolved:
            mangled, source_path, qualified_name = resolved
            if not args.source:
                args.source = source_path
            if not args.function:
                args.function = qualified_name
            args.symbol = mangled
            print(f"Resolved: {args.symbol} -> {args.function} in {args.source}", file=sys.stderr)
        else:
            print(f"Could not resolve '{args.symbol}' from decomp.db", file=sys.stderr)

    # Validate required args
    missing = []
    if not args.symbol:
        missing.append("--symbol")
    if not args.source:
        missing.append("--source")
    if not args.function:
        missing.append("--function")
    if missing:
        print(f"Error: required arguments: {', '.join(missing)}", file=sys.stderr)
        sys.exit(1)

    # Resolve patterns
    if args.patterns == "all":
        patterns = get_all_patterns()
    else:
        pattern_names = [p.strip() for p in args.patterns.split(",")]
        patterns = [get_pattern(name) for name in pattern_names]

    # BSF-guided is now default-on. --no-bsf-guided disables it.
    if args.no_bsf_guided:
        for p in patterns:
            if p.name == "declaration_reorder":
                p.bsf_guided = False
    else:
        for p in patterns:
            if p.name == "declaration_reorder":
                p.bsf_required = getattr(args, "bsf_required", False)

    # Extract function
    print(f"Extracting {args.function} from {args.source}...", file=sys.stderr)
    ctx = extract_function(args.source, args.function)

    # Resolve compiler dialect: --compiler flag > permuter.json > "mwcc"
    from .project_config import get_compiler
    ctx.compiler_dialect = args.compiler or get_compiler()

    print(
        f"Found function with {len(ctx.statements)} statements "
        f"({ctx.func_byte_range[1] - ctx.func_byte_range[0]} bytes) "
        f"[dialect={ctx.compiler_dialect}]",
        file=sys.stderr,
    )

    # Score variants (need baseline for diagnosis before generating)
    original_source = args.source.read_bytes()
    guided = not args.no_guided

    results: list[ScoreResult] = []
    with Scorer(args.source, args.symbol, unit=args.unit) as scorer:
        baseline = scorer.get_baseline(guided=guided)
        baseline_exec = scorer._baseline_equivalent

        # Wire symbol and diagnosis into context
        ctx.symbol = args.symbol
        if scorer.diagnosis:
            ctx.diagnosis = scorer.diagnosis
            print(format_diagnosis_summary(scorer.diagnosis), file=sys.stderr)

            # Early skip: if all mismatches are noise, nothing to permute
            if is_all_noise(scorer.diagnosis) and not args.no_guided:
                print(
                    "All mismatches are noise (offset/symbol/branch reloc). "
                    "Nothing to permute.",
                    file=sys.stderr,
                )
                if args.json_output:
                    _print_json(baseline, [], scorer.diagnosis)
                return

        exec_label = ""
        if baseline_exec is True:
            exec_label = " [EXEC OK]"
        elif baseline_exec is False:
            exec_label = " [EXEC DIVERGENT]"
        print(f"Baseline: {baseline:.2f}%{exec_label}", file=sys.stderr)

        # Generate variants (after diagnosis so filtering can use it)
        from .composer import _DEFAULT_PAIRS
        compose_pairs = None if args.no_compose else _DEFAULT_PAIRS
        variants = list(generate_variants(ctx, patterns, args.max_variants, compose_pairs=compose_pairs))
        print(f"Generated {len(variants)} variants", file=sys.stderr)

        if args.dry_run:
            _print_dry_run(variants, args.json_output)
            return

        if args.workers > 1:
            print(f"Scoring {len(variants)} variants ({args.workers} workers)...", file=sys.stderr)
            batch_results = scorer.score_batch(variants, workers=args.workers)
            for i, result in enumerate(batch_results):
                results.append(result)
                marker = ""
                if not result.build_success:
                    marker = " BUILD FAILED"
                elif result.error and "equivalence" in result.error.lower():
                    marker = " EXEC BROKEN"
                elif result.match_percent > baseline:
                    marker = " IMPROVED"
                elif result.match_percent == baseline:
                    marker = " same"

                exec_tag = ""
                if result.execution_equivalent is True:
                    exec_tag = " [EXEC OK]"
                elif result.execution_equivalent is False:
                    exec_tag = " [EXEC BROKEN]"
                    
                dedup_tag = f" [{result.error}]" if result.error in ("source_dedup", "cache_hit", "obj_dedup") else ""
                print(f"[{i + 1}/{len(variants)}] {result.variant.name}: {result.match_percent:.2f}%{marker}{exec_tag}{dedup_tag}", file=sys.stderr)
                
                if not args.no_stop_on_perfect and result.match_percent >= 100.0:
                    print("Perfect match found! (Remaining batch variants may have been evaluated)", file=sys.stderr)
                    # We don't break early here because the batch is already processed, but we can stop subsequent ones if there was another batch.
        else:
            for i, variant in enumerate(variants):
                print(
                    f"[{i + 1}/{len(variants)}] {variant.name}: {variant.description}... ",
                    end="",
                    flush=True,
                    file=sys.stderr,
                )
                result = scorer.score(variant)
                results.append(result)

                marker = ""
                if not result.build_success:
                    marker = " BUILD FAILED"
                elif result.error and "equivalence" in result.error.lower():
                    marker = " EXEC BROKEN"
                elif result.match_percent > baseline:
                    marker = " IMPROVED"
                elif result.match_percent == baseline:
                    marker = " same"

                exec_tag = ""
                if result.execution_equivalent is True:
                    exec_tag = " [EXEC OK]"
                elif result.execution_equivalent is False:
                    exec_tag = " [EXEC BROKEN]"

                print(f"{result.match_percent:.2f}%{marker}{exec_tag}", file=sys.stderr)

                if not args.no_stop_on_perfect and result.match_percent >= 100.0:
                    print("Perfect match found!", file=sys.stderr)
                    break

    # Sort by match percentage descending
    results.sort(key=lambda r: r.match_percent, reverse=True)

    if args.json_output:
        _print_json(baseline, results, ctx.diagnosis)
    else:
        _print_table(baseline, results, original_source)

    # Apply best improvement (default behavior, opt out with --no-apply)
    if not args.no_apply:
        improved = [r for r in results if r.build_success and r.match_percent > baseline]
        if improved:
            best = improved[0]
            args.source.write_bytes(best.variant.source)
            print(f"\nApplied: {best.variant.name} ({best.variant.description})", file=sys.stderr)
            print(f"New match: {best.match_percent:.2f}% (was {baseline:.2f}%)", file=sys.stderr)
            _print_diff(original_source, best.variant.source, args.source)
        else:
            print("\nNo improvements to apply.", file=sys.stderr)


def _print_diff(original: bytes, variant: bytes, source_path: Path, file=None):
    """Print a unified diff between original and variant source."""
    if file is None:
        file = sys.stderr
    orig_lines = original.decode("utf-8", errors="replace").splitlines(keepends=True)
    var_lines = variant.decode("utf-8", errors="replace").splitlines(keepends=True)
    diff = difflib.unified_diff(
        orig_lines, var_lines,
        fromfile=f"a/{source_path}",
        tofile=f"b/{source_path}",
        n=3,
    )
    diff_text = "".join(diff)
    if diff_text:
        print(diff_text, file=file)


def _print_dry_run(variants, json_output: bool):
    if json_output:
        data = [
            {
                "name": v.name,
                "pattern": v.pattern_name,
                "description": v.description,
            }
            for v in variants
        ]
        print(json.dumps(data, indent=2))
    else:
        for v in variants:
            print(f"  [{v.pattern_name}] {v.name}: {v.description}")


def _print_json(baseline: float, results: list[ScoreResult], diagnosis=None):
    from .types import Diagnosis

    # Compute headline summary fields BEFORE the results array so downstream
    # consumers don't have to filter. `results` is sorted by match_percent
    # desc, so results[0] can be a regression when no variant improved —
    # that is, results[0].match_percent < baseline. Always check
    # `best_improvement` (None if nothing beat baseline) instead.
    improvements = [
        r for r in results
        if r.build_success and r.match_percent > baseline + 1e-6
    ]
    build_failures = sum(1 for r in results if not r.build_success)

    if improvements:
        best = max(improvements, key=lambda r: r.match_percent)
        best_summary = {
            "name": best.variant.name,
            "pattern": best.variant.pattern_name,
            "description": best.variant.description,
            "match_percent": best.match_percent,
            "delta": best.match_percent - baseline,
        }
    else:
        best_summary = None

    data = {
        "baseline": baseline,
        "best_improvement": best_summary,
        "improvements_count": len(improvements),
        "variants_total": len(results),
        "build_failures": build_failures,
        "results": [
            {
                "name": r.variant.name,
                "pattern": r.variant.pattern_name,
                "description": r.variant.description,
                "match_percent": r.match_percent,
                "build_success": r.build_success,
                "error": r.error,
                "delta": r.match_percent - baseline,
                "execution_equivalent": r.execution_equivalent,
            }
            for r in results
        ],
    }
    if diagnosis is not None:
        data["diagnosis"] = {
            "total_instructions": diagnosis.total_instructions,
            "match_counts": diagnosis.match_counts,
            "diff_ops": [
                {"index": d.index, "target": d.target_opcode, "base": d.base_opcode}
                for d in diagnosis.diff_ops
            ],
            "reg_swap_pairs": {
                f"{k[0]}<->{k[1]}": {"count": v.count, "first": v.first_idx, "last": v.last_idx}
                for k, v in diagnosis.reg_swap_pairs.items()
            },
            "clusters": [
                {"start": c.start_idx, "end": c.end_idx, "size": c.size,
                 "inserts": c.inserts, "deletes": c.deletes}
                for c in diagnosis.clusters
            ],
            "noise_explained": diagnosis.noise_explained,
            "noise_total": diagnosis.noise_total,
        }
    print(json.dumps(data, indent=2))


def _print_table(baseline: float, results: list[ScoreResult], original_source: bytes | None = None):
    print(f"\n{'=' * 70}")
    print(f"RESULTS (baseline: {baseline:.2f}%)")
    print(f"{'=' * 70}")
    for r in results:
        delta = r.match_percent - baseline
        marker = ""
        if not r.build_success:
            marker = " BUILD FAILED"
        elif delta > 0:
            marker = f" +{delta:.2f}%"
        elif delta == 0:
            marker = " (same)"
        else:
            marker = f" {delta:.2f}%"

        exec_info = ""
        if r.execution_equivalent is True:
            exec_info = " EXEC OK"
        elif r.execution_equivalent is False:
            exec_info = " EXEC BROKEN"
        print(f"  {r.variant.name:25s} {r.match_percent:6.2f}%{marker}{exec_info}")
        print(f"    {r.variant.description}")

        # Show diff for improvements
        if original_source and r.build_success and r.match_percent > baseline:
            _print_diff(original_source, r.variant.source, Path("source"))

    # Summary
    improved = [r for r in results if r.build_success and r.match_percent > baseline]
    if improved:
        best = improved[0]
        print(f"\nBest improvement: {best.variant.name} at {best.match_percent:.2f}%")
        print(f"  {best.variant.description}")
    else:
        print("\nNo improvements found.")


if __name__ == "__main__":
    main()
