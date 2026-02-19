#!/usr/bin/env python3
"""
Enrichment experiment analysis script.

Analyzes A/B test results for context enrichment experiments.
Compares control vs treatment groups and computes statistical significance.

Usage:
    python scripts/analysis/analyze_enrichment.py --experiment diff_patterns
    python scripts/analysis/analyze_enrichment.py --all --output docs/context-enrichment/
"""

import argparse
import json
import sqlite3
from pathlib import Path
from typing import Any, Dict, List, Optional
import math

# Database path
DEFAULT_DB_PATH = "decomp.db"


def get_connection(db_path: str = DEFAULT_DB_PATH) -> sqlite3.Connection:
    """Get a database connection."""
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row
    return conn


def analyze_experiment(
    experiment: str,
    db_path: str = DEFAULT_DB_PATH,
    min_samples: int = 20,
) -> Dict[str, Any]:
    """
    Analyze a single enrichment experiment.

    Args:
        experiment: Experiment name (e.g., "diff_patterns")
        db_path: Path to database
        min_samples: Minimum samples required for significance

    Returns:
        Dict with analysis results
    """
    conn = get_connection(db_path)

    # Query attempts with enrichment flags
    query = """
        SELECT
            a.id,
            a.start_percent,
            a.end_percent,
            a.exit_status,
            a.model,
            a.enrichment_flags,
            a.iterations,
            a.input_tokens,
            a.output_tokens,
            a.duration_ms
        FROM attempts a
        WHERE a.enrichment_flags IS NOT NULL
          AND a.start_percent IS NOT NULL
    """

    rows = conn.execute(query).fetchall()

    # Separate control and treatment groups
    control = []
    treatment = []

    for row in rows:
        flags = json.loads(row["enrichment_flags"])
        if experiment not in flags:
            continue

        record = {
            "id": row["id"],
            "start_percent": row["start_percent"],
            "end_percent": row["end_percent"],
            "exit_status": row["exit_status"],
            "model": row["model"],
            "gain": (row["end_percent"] or 0) - (row["start_percent"] or 0),
            "improved": (row["end_percent"] or 0) > (row["start_percent"] or 0),
            "iterations": row["iterations"],
            "tokens": (row["input_tokens"] or 0) + (row["output_tokens"] or 0),
            "duration_ms": row["duration_ms"],
        }

        if flags[experiment]:
            treatment.append(record)
        else:
            control.append(record)

    # Compute metrics
    result = {
        "experiment": experiment,
        "control_n": len(control),
        "treatment_n": len(treatment),
        "sufficient_data": len(control) >= min_samples and len(treatment) >= min_samples,
    }

    if not result["sufficient_data"]:
        result["message"] = f"Insufficient data: need {min_samples} samples per group"
        return result

    # Success rate (improved %)
    control_success = sum(1 for r in control if r["improved"]) / len(control)
    treatment_success = sum(1 for r in treatment if r["improved"]) / len(treatment)

    # Average gain
    control_gain = sum(r["gain"] for r in control) / len(control)
    treatment_gain = sum(r["gain"] for r in treatment) / len(treatment)

    # AT_LIMIT rate
    control_at_limit = sum(1 for r in control if r["exit_status"] == "at_limit") / len(control)
    treatment_at_limit = sum(1 for r in treatment if r["exit_status"] == "at_limit") / len(treatment)

    # Populate results
    result["control"] = {
        "success_rate": round(control_success * 100, 1),
        "avg_gain": round(control_gain, 2),
        "at_limit_rate": round(control_at_limit * 100, 1),
    }
    result["treatment"] = {
        "success_rate": round(treatment_success * 100, 1),
        "avg_gain": round(treatment_gain, 2),
        "at_limit_rate": round(treatment_at_limit * 100, 1),
    }

    # Relative improvements
    result["improvement"] = {
        "success_rate_delta": round(treatment_success - control_success, 3) * 100,
        "avg_gain_delta": round(treatment_gain - control_gain, 2),
        "at_limit_delta": round(treatment_at_limit - control_at_limit, 3) * 100,
    }

    # Statistical significance (simple z-test for proportions)
    p1, n1 = control_success, len(control)
    p2, n2 = treatment_success, len(treatment)
    p_pooled = (p1 * n1 + p2 * n2) / (n1 + n2)

    if p_pooled > 0 and p_pooled < 1:
        se = math.sqrt(p_pooled * (1 - p_pooled) * (1/n1 + 1/n2))
        if se > 0:
            z = (p2 - p1) / se
            # Two-tailed p-value approximation
            p_value = 2 * (1 - _normal_cdf(abs(z)))
            result["significance"] = {
                "z_score": round(z, 2),
                "p_value": round(p_value, 4),
                "significant_05": p_value < 0.05,
                "significant_01": p_value < 0.01,
            }

    # Recommendation
    if result.get("significance", {}).get("significant_05"):
        if result["improvement"]["success_rate_delta"] > 0:
            result["recommendation"] = "ENABLE - statistically significant improvement"
        else:
            result["recommendation"] = "DISABLE - statistically significant regression"
    else:
        result["recommendation"] = "CONTINUE TESTING - not yet significant"

    return result


def _normal_cdf(x: float) -> float:
    """Approximate normal CDF using error function approximation."""
    # Abramowitz and Stegun approximation
    a1 = 0.254829592
    a2 = -0.284496736
    a3 = 1.421413741
    a4 = -1.453152027
    a5 = 1.061405429
    p = 0.3275911

    sign = 1 if x >= 0 else -1
    x = abs(x) / math.sqrt(2)

    t = 1.0 / (1.0 + p * x)
    y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * math.exp(-x * x)

    return 0.5 * (1.0 + sign * y)


def generate_report(results: Dict[str, Any]) -> str:
    """Generate markdown report for experiment results."""
    exp = results["experiment"]

    lines = [
        f"# Experiment Results: {exp}",
        "",
        f"**Status**: {'Sufficient data' if results['sufficient_data'] else 'Insufficient data'}",
        f"**Control group**: {results['control_n']} attempts",
        f"**Treatment group**: {results['treatment_n']} attempts",
        "",
    ]

    if not results["sufficient_data"]:
        lines.extend([
            "## Status",
            "",
            results.get("message", "Need more data"),
            "",
            "Continue running experiments to collect more samples.",
        ])
        return "\n".join(lines)

    # Metrics comparison table
    lines.extend([
        "## Results Comparison",
        "",
        "| Metric | Control | Treatment | Delta |",
        "|--------|---------|-----------|-------|",
        f"| Success Rate | {results['control']['success_rate']}% | {results['treatment']['success_rate']}% | {results['improvement']['success_rate_delta']:+.1f}% |",
        f"| Avg Gain | {results['control']['avg_gain']}% | {results['treatment']['avg_gain']}% | {results['improvement']['avg_gain_delta']:+.2f}% |",
        f"| AT_LIMIT Rate | {results['control']['at_limit_rate']}% | {results['treatment']['at_limit_rate']}% | {results['improvement']['at_limit_delta']:+.1f}% |",
        "",
    ])

    # Significance
    if "significance" in results:
        sig = results["significance"]
        lines.extend([
            "## Statistical Significance",
            "",
            f"- Z-score: {sig['z_score']}",
            f"- P-value: {sig['p_value']}",
            f"- Significant at 0.05: {'Yes' if sig['significant_05'] else 'No'}",
            f"- Significant at 0.01: {'Yes' if sig['significant_01'] else 'No'}",
            "",
        ])

    # Recommendation
    lines.extend([
        "## Recommendation",
        "",
        f"**{results['recommendation']}**",
        "",
    ])

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Analyze enrichment experiment results"
    )
    parser.add_argument(
        "--experiment",
        help="Specific experiment to analyze",
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="Analyze all experiments",
    )
    parser.add_argument(
        "--output",
        help="Output directory for reports",
    )
    parser.add_argument(
        "--db",
        default=DEFAULT_DB_PATH,
        help="Database path",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output raw JSON",
    )

    args = parser.parse_args()

    experiments = [
        "diff_patterns",
        "function_types",
        "rb2_layouts",
        "attempt_diffs",
        "matched_siblings",
        "callee_signatures",
    ]

    if args.experiment:
        experiments = [args.experiment]
    elif not args.all:
        parser.print_help()
        return

    for exp in experiments:
        print(f"\n{'='*60}")
        print(f"Analyzing: {exp}")
        print("=" * 60)

        results = analyze_experiment(exp, db_path=args.db)

        if args.json:
            print(json.dumps(results, indent=2))
        else:
            report = generate_report(results)
            print(report)

            if args.output:
                output_dir = Path(args.output)
                output_dir.mkdir(parents=True, exist_ok=True)
                output_file = output_dir / f"{exp.replace('_', '-')}-results.md"
                with open(output_file, "w") as f:
                    f.write(report)
                print(f"\nReport written to: {output_file}")


if __name__ == "__main__":
    main()
