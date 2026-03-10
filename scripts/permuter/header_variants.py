"""Standalone workflow for discovering and scoring header-backed variants."""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

from .cross_unit import AffectedFunction
from .file_util import apply_file_updates
from .header_impact import HeaderImpact, estimate_header_impact
from .header_variant_scorer import HeaderVariantScore, HeaderVariantScorer
from .header_tail_call import discover_header_tail_call_variants
from .patterns import get_pattern
from .extractor import extract_function
from .types import Variant, variant_file_updates, extract_qualified_name


@dataclass(frozen=True)
class DiscoveredHeaderVariant:
    """A header-backed variant paired with its blast-radius estimate."""

    variant: Variant
    impact: HeaderImpact


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.header_variants",
        description="Discover and score cross-unit/header-backed permuter variants.",
    )
    parser.add_argument("--symbol", help="Mangled symbol or demangled substring")
    parser.add_argument("--source", type=Path, help="Path to the .cpp source file")
    parser.add_argument("--function", help="Qualified C++ function name")
    parser.add_argument(
        "--pattern",
        default="noinline_stub",
        help="Pattern to use for discovery (default: noinline_stub)",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=Path("decomp.db"),
        help="Path to decomp.db (default: ./decomp.db)",
    )
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path("."),
        help="Project root containing build/, decomp.db, and objdiff-cli",
    )
    parser.add_argument(
        "--refresh-baseline",
        action="store_true",
        help="Force fresh objdiff baseline scores instead of reusing DB percentages",
    )
    parser.add_argument(
        "--include-complete",
        action="store_true",
        help="Include COMPLETE functions in multi-symbol scoring",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply the best accepted variant after scoring",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        dest="json_output",
        help="Emit machine-readable JSON",
    )
    return parser.parse_args()


def resolve_from_db(
    db_path: Path,
    symbol_query: str,
) -> tuple[str, Path, str] | None:
    """Resolve symbol -> (symbol, source_path, qualified_name) from decomp.db."""
    import sqlite3

    if not db_path.exists():
        return None

    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    try:
        row = conn.execute(
            "SELECT symbol, demangled, unit FROM functions WHERE symbol = ? LIMIT 1",
            (symbol_query,),
        ).fetchone()
        if not row:
            row = conn.execute(
                "SELECT symbol, demangled, unit FROM functions WHERE demangled LIKE ? LIMIT 1",
                (f"%{symbol_query}%",),
            ).fetchone()
    finally:
        conn.close()

    if not row:
        return None

    qualified_name = extract_qualified_name(row["demangled"] or "")
    if not qualified_name:
        return None
    return row["symbol"], Path(row["unit"]), qualified_name


def discover_header_variants(
    source_path: Path,
    function_name: str,
    pattern_name: str = "noinline_stub",
) -> list[DiscoveredHeaderVariant]:
    """Generate only the variants that edit auxiliary/header files."""
    if pattern_name == "header_tail_call":
        return [
            DiscoveredHeaderVariant(variant=item.variant, impact=item.impact)
            for item in discover_header_tail_call_variants(source_path, function_name)
        ]

    ctx = extract_function(source_path, function_name)
    pattern = get_pattern(pattern_name)
    project_root = source_path.resolve().parent
    for parent in source_path.resolve().parents:
        if (parent / ".git").exists():
            project_root = parent
            break

    discovered: list[DiscoveredHeaderVariant] = []
    for variant in pattern.generate(ctx):
        if not variant.auxiliary_files:
            continue
        header = variant.auxiliary_files[0].path
        impact = estimate_header_impact(project_root, header)
        discovered.append(DiscoveredHeaderVariant(variant=variant, impact=impact))
    return discovered


def score_discovered_variants(
    source_path: Path,
    discovered: list[DiscoveredHeaderVariant],
    scorer: HeaderVariantScorer,
    refresh_baseline: bool = False,
    include_complete: bool = False,
) -> list[HeaderVariantScore]:
    """Evaluate discovered header variants and return sorted scores."""
    scores = [
        scorer.evaluate_variant(
            source_path,
            item.impact,
            item.variant,
            exclude_complete=not include_complete,
            refresh_baseline=refresh_baseline,
        )
        for item in discovered
    ]
    scores.sort(
        key=lambda score: (
            0 if score.accepted else 1,
            score.perfect_lost,
            -score.total_delta,
            -score.improved_count,
            score.regressed_count,
        )
    )
    return scores


def select_best_variant(scores: list[HeaderVariantScore]) -> HeaderVariantScore | None:
    """Return the best accepted scored variant, if any."""
    for score in scores:
        if score.accepted:
            return score
    return None


def apply_header_variant(primary_source_path: Path, variant: Variant) -> None:
    """Apply a header-backed variant to disk."""
    originals: dict[Path, bytes | None] = {}
    apply_file_updates(variant_file_updates(primary_source_path, variant), originals)


def _score_to_dict(score: HeaderVariantScore) -> dict[str, object]:
    """Serialize a header variant score to JSON-friendly data."""
    return {
        "variant": score.variant.name,
        "pattern": score.variant.pattern_name,
        "description": score.variant.description,
        "build_success": score.build_success,
        "build_error": score.build_error,
        "accepted": score.accepted,
        "total_delta": score.total_delta,
        "improved_count": score.improved_count,
        "regressed_count": score.regressed_count,
        "unchanged_count": score.unchanged_count,
        "perfect_gained": score.perfect_gained,
        "perfect_lost": score.perfect_lost,
        "changed_objects": [str(path) for path in score.changed_objects],
        "build_targets": [str(path) for path in score.build_targets],
        "functions": [
            {
                "symbol": item.function.symbol,
                "function_name": item.function.function_name,
                "source_path": str(item.function.source_path),
                "baseline_percent": item.baseline_percent,
                "variant_percent": item.variant_percent,
                "delta": item.delta,
            }
            for item in score.functions
        ],
    }


def _print_scores(scores: list[HeaderVariantScore]) -> None:
    """Print a compact human-readable summary of scored variants."""
    for idx, score in enumerate(scores):
        status = "ACCEPT" if score.accepted else "reject"
        changed = len(score.changed_objects)
        print(
            f"[{idx}] {status} {score.variant.name}: "
            f"delta={score.total_delta:+.2f} "
            f"improved={score.improved_count} "
            f"regressed={score.regressed_count} "
            f"perfect_lost={score.perfect_lost} "
            f"changed_objs={changed}"
        )


def main() -> int:
    args = parse_args()
    project_root = args.project_root.resolve()
    db_path = args.db.resolve()

    if args.symbol and (not args.source or not args.function):
        resolved = resolve_from_db(db_path, args.symbol)
        if resolved is None:
            print(f"Could not resolve '{args.symbol}' from {db_path}", file=sys.stderr)
            return 1
        mangled, source_path, function_name = resolved
        args.symbol = mangled
        if not args.source:
            args.source = project_root / source_path
        if not args.function:
            args.function = function_name

    missing = []
    if not args.source:
        missing.append("--source")
    if not args.function:
        missing.append("--function")
    if missing:
        print(f"Error: required arguments: {', '.join(missing)}", file=sys.stderr)
        return 1

    discovered = discover_header_variants(
        args.source.resolve(),
        args.function,
        pattern_name=args.pattern,
    )
    if not discovered:
        if args.json_output:
            print(json.dumps({"variants": []}, indent=2))
        else:
            print("No header-backed variants found.")
        return 0

    scorer = HeaderVariantScorer(project_root=project_root, db_path=db_path)
    scores = score_discovered_variants(
        args.source.resolve(),
        discovered,
        scorer,
        refresh_baseline=args.refresh_baseline,
        include_complete=args.include_complete,
    )

    if args.apply:
        best = select_best_variant(scores)
        if best is None:
            print("No accepted variant to apply.", file=sys.stderr)
            return 2
        apply_header_variant(args.source.resolve(), best.variant)

    if args.json_output:
        print(json.dumps({"variants": [_score_to_dict(score) for score in scores]}, indent=2))
    else:
        _print_scores(scores)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
