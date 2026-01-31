"""Reporting module for DC3 Decomp Orchestrator.

Provides progress reports, batch summaries, and attempt tracking for
the master agent and orchestrator dashboard.
"""

from datetime import datetime, timedelta
from pathlib import Path
from typing import Any

from .database import get_connection, get_stats, DEFAULT_DB_PATH


def generate_progress_report(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """
    Generate real-time progress report for master agent.

    Returns:
        Dict with:
        - active_agents: Count of currently locked functions
        - recent_completions: Attempts completed in last 10 minutes
        - total_functions: Total tracked functions
        - complete: Functions with verdict COMPLETE
        - at_limit: Functions with verdict AT_LIMIT
        - in_progress: Functions currently being worked on
        - avg_percent: Average match percentage
    """
    conn = get_connection(db_path)

    # Get basic stats
    stats = get_stats(db_path)

    # Count active agents (functions currently locked)
    active_agents = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE locked_by IS NOT NULL"
    ).fetchone()[0]

    # Get recent completions (attempts in last 10 minutes)
    cutoff = (datetime.now() - timedelta(minutes=10)).strftime("%Y-%m-%d %H:%M:%S")
    recent_completions = conn.execute(
        """
        SELECT COUNT(*) FROM attempts
        WHERE finished_at >= ?
        """,
        (cutoff,),
    ).fetchone()[0]

    # Get recent successful completions specifically
    recent_successes = conn.execute(
        """
        SELECT COUNT(*) FROM attempts
        WHERE finished_at >= ?
          AND exit_status IN ('complete', 'success')
        """,
        (cutoff,),
    ).fetchone()[0]

    # Get recent improvements (end_percent > start_percent)
    recent_improvements = conn.execute(
        """
        SELECT COUNT(*) FROM attempts
        WHERE finished_at >= ?
          AND end_percent > start_percent
        """,
        (cutoff,),
    ).fetchone()[0]

    return {
        "active_agents": active_agents,
        "recent_completions": recent_completions,
        "recent_successes": recent_successes,
        "recent_improvements": recent_improvements,
        "total_functions": stats["total_functions"],
        "complete": stats["complete"],
        "at_limit": stats["at_limit"],
        "in_progress": active_agents,
        "avg_percent": stats["avg_percent"],
        "total_attempts": stats["total_attempts"],
        "timestamp": datetime.now().isoformat(),
    }


def generate_batch_summary(
    db_path: str | Path = DEFAULT_DB_PATH,
    pattern: str = "*",
    since_minutes: int = 60,
) -> str:
    """
    Generate markdown summary of batch results.

    Args:
        db_path: Path to database
        pattern: Unit glob pattern to filter by
        since_minutes: Only include attempts from last N minutes

    Returns:
        Formatted markdown summary
    """
    conn = get_connection(db_path)
    cutoff = (datetime.now() - timedelta(minutes=since_minutes)).strftime(
        "%Y-%m-%d %H:%M:%S"
    )

    # Query attempts for the pattern
    rows = conn.execute(
        """
        SELECT
            a.id,
            a.session_id,
            a.model,
            a.started_at,
            a.finished_at,
            a.start_percent,
            a.end_percent,
            a.exit_status,
            a.verdict,
            a.notes,
            f.symbol,
            f.demangled,
            f.unit
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.finished_at >= ?
          AND f.unit GLOB ?
        ORDER BY a.finished_at DESC
        """,
        (cutoff, pattern),
    ).fetchall()

    if not rows:
        return f"# Batch Summary\n\nNo attempts found for pattern `{pattern}` in last {since_minutes} minutes.\n"

    # Calculate improvements
    total_attempts = len(rows)
    successful = sum(1 for r in rows if r["exit_status"] in ("complete", "success"))
    improved = sum(
        1
        for r in rows
        if r["end_percent"] is not None
        and r["start_percent"] is not None
        and r["end_percent"] > r["start_percent"]
    )
    at_limit = sum(1 for r in rows if r["verdict"] == "AT_LIMIT")
    errors = sum(1 for r in rows if r["exit_status"] == "error")

    # Calculate total percentage gain
    total_gain = sum(
        (r["end_percent"] or 0) - (r["start_percent"] or 0)
        for r in rows
        if r["end_percent"] is not None and r["start_percent"] is not None
    )

    # Find top improvements
    improvements = [
        {
            "demangled": r["demangled"] or r["symbol"],
            "unit": r["unit"],
            "start": r["start_percent"],
            "end": r["end_percent"],
            "gain": (r["end_percent"] or 0) - (r["start_percent"] or 0),
            "model": r["model"],
        }
        for r in rows
        if r["end_percent"] is not None
        and r["start_percent"] is not None
        and r["end_percent"] > r["start_percent"]
    ]
    improvements.sort(key=lambda x: x["gain"], reverse=True)
    top_improvements = improvements[:10]

    # Identify patterns that worked (group by model and exit_status)
    model_stats = {}
    for r in rows:
        model = r["model"] or "unknown"
        if model not in model_stats:
            model_stats[model] = {"total": 0, "improved": 0, "complete": 0}
        model_stats[model]["total"] += 1
        if (
            r["end_percent"] is not None
            and r["start_percent"] is not None
            and r["end_percent"] > r["start_percent"]
        ):
            model_stats[model]["improved"] += 1
        if r["exit_status"] in ("complete", "success"):
            model_stats[model]["complete"] += 1

    # Build markdown
    lines = [
        "# Batch Summary",
        "",
        f"**Pattern:** `{pattern}`",
        f"**Time range:** Last {since_minutes} minutes",
        f"**Generated:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "## Overview",
        "",
        f"| Metric | Value |",
        f"|--------|-------|",
        f"| Total attempts | {total_attempts} |",
        f"| Successful completions | {successful} |",
        f"| Improved | {improved} |",
        f"| At limit | {at_limit} |",
        f"| Errors | {errors} |",
        f"| Total gain | +{total_gain:.1f}% |",
        "",
    ]

    if top_improvements:
        lines.extend(
            [
                "## Top Improvements",
                "",
                "| Function | Unit | Start | End | Gain | Model |",
                "|----------|------|-------|-----|------|-------|",
            ]
        )
        for imp in top_improvements:
            name = imp["demangled"]
            if len(name) > 40:
                name = name[:37] + "..."
            unit = imp["unit"] or ""
            if len(unit) > 30:
                unit = "..." + unit[-27:]
            lines.append(
                f"| {name} | {unit} | {imp['start']:.1f}% | {imp['end']:.1f}% | +{imp['gain']:.1f}% | {imp['model']} |"
            )
        lines.append("")

    if model_stats:
        lines.extend(
            [
                "## Model Performance",
                "",
                "| Model | Attempts | Improved | Complete | Success Rate |",
                "|-------|----------|----------|----------|--------------|",
            ]
        )
        for model, stats in sorted(model_stats.items()):
            rate = (
                (stats["improved"] / stats["total"] * 100) if stats["total"] > 0 else 0
            )
            lines.append(
                f"| {model} | {stats['total']} | {stats['improved']} | {stats['complete']} | {rate:.1f}% |"
            )
        lines.append("")

    return "\n".join(lines)


def get_recent_attempts(
    db_path: str | Path = DEFAULT_DB_PATH,
    minutes: int = 10,
    limit: int = 50,
) -> list[dict[str, Any]]:
    """
    Get attempts from last N minutes.

    Args:
        db_path: Path to database
        minutes: Time window in minutes
        limit: Maximum number of attempts to return

    Returns:
        List of attempt dicts with function info
    """
    conn = get_connection(db_path)
    cutoff = (datetime.now() - timedelta(minutes=minutes)).strftime(
        "%Y-%m-%d %H:%M:%S"
    )

    rows = conn.execute(
        """
        SELECT
            a.id,
            a.session_id,
            a.model,
            a.started_at,
            a.finished_at,
            a.start_percent,
            a.end_percent,
            a.exit_status,
            a.verdict,
            a.notes,
            a.iterations,
            f.symbol,
            f.demangled,
            f.unit,
            f.size
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.finished_at >= ?
        ORDER BY a.finished_at DESC
        LIMIT ?
        """,
        (cutoff, limit),
    ).fetchall()

    return [dict(row) for row in rows]


def get_active_sessions(db_path: str | Path = DEFAULT_DB_PATH) -> list[dict[str, Any]]:
    """
    Get currently active (locked) functions with session info.

    Returns:
        List of function dicts that are currently locked
    """
    conn = get_connection(db_path)

    rows = conn.execute(
        """
        SELECT
            id,
            symbol,
            demangled,
            unit,
            current_percent,
            locked_by,
            locked_at,
            attempt_count,
            last_model
        FROM functions
        WHERE locked_by IS NOT NULL
        ORDER BY locked_at DESC
        """
    ).fetchall()

    return [dict(row) for row in rows]


def get_unit_summary(
    db_path: str | Path = DEFAULT_DB_PATH,
    pattern: str = "*",
) -> list[dict[str, Any]]:
    """
    Get summary statistics by compilation unit.

    Args:
        db_path: Path to database
        pattern: Glob pattern for units

    Returns:
        List of dicts with unit stats
    """
    conn = get_connection(db_path)

    rows = conn.execute(
        """
        SELECT
            unit,
            COUNT(*) as total,
            SUM(CASE WHEN verdict = 'COMPLETE' THEN 1 ELSE 0 END) as complete,
            SUM(CASE WHEN verdict = 'AT_LIMIT' THEN 1 ELSE 0 END) as at_limit,
            SUM(CASE WHEN locked_by IS NOT NULL THEN 1 ELSE 0 END) as in_progress,
            AVG(current_percent) as avg_percent,
            SUM(attempt_count) as total_attempts
        FROM functions
        WHERE unit GLOB ?
        GROUP BY unit
        ORDER BY total DESC
        """,
        (pattern,),
    ).fetchall()

    return [dict(row) for row in rows]


def get_model_effectiveness(
    db_path: str | Path = DEFAULT_DB_PATH,
    hours: int = 0,
    exclude_unknown: bool = True,
    model: str | None = None,
) -> dict[str, dict[str, Any]]:
    """
    Query model effectiveness statistics.

    Args:
        db_path: Path to database
        hours: Only analyze attempts from last N hours (0 = all time)
        exclude_unknown: Exclude attempts with unknown model
        model: Filter to specific model (None = all models)

    Returns:
        Dict mapping model name to stats:
        {
            model: {
                attempts: int,
                improved: int,
                avg_gain: float,
                complete: int,
                stuck: int,
                at_limit: int,
                # Actual cost tracking (v2 schema)
                total_actual_cost: float | None,  # Sum of actual_cost_usd
                avg_actual_cost: float | None,    # Average cost per attempt
                with_cost_data: int,              # Count of attempts with actual cost
                total_input_tokens: int | None,
                total_output_tokens: int | None,
                avg_duration_ms: float | None,
            }
        }
    """
    conn = get_connection(db_path)

    # Build WHERE clause
    conditions = ["start_percent IS NOT NULL", "end_percent IS NOT NULL"]
    params = []

    if exclude_unknown:
        conditions.append("model != 'unknown'")

    if hours > 0:
        conditions.append("created_at > datetime('now', '-' || ? || ' hours')")
        params.append(str(hours))

    if model:
        conditions.append("model = ?")
        params.append(model)

    where_clause = " AND ".join(conditions)

    rows = conn.execute(
        f"""
        SELECT
            model,
            COUNT(*) as attempts,
            SUM(CASE WHEN end_percent > start_percent THEN 1 ELSE 0 END) as improved,
            AVG(CASE WHEN end_percent > start_percent
                THEN end_percent - start_percent END) as avg_gain,
            SUM(CASE WHEN exit_status = 'complete' THEN 1 ELSE 0 END) as complete,
            SUM(CASE WHEN exit_status = 'stuck' THEN 1 ELSE 0 END) as stuck,
            SUM(CASE WHEN exit_status = 'at_limit' THEN 1 ELSE 0 END) as at_limit,
            -- Actual cost tracking (v2 schema columns)
            SUM(actual_cost_usd) as total_actual_cost,
            AVG(actual_cost_usd) as avg_actual_cost,
            SUM(CASE WHEN actual_cost_usd IS NOT NULL THEN 1 ELSE 0 END) as with_cost_data,
            SUM(input_tokens) as total_input_tokens,
            SUM(output_tokens) as total_output_tokens,
            AVG(duration_ms) as avg_duration_ms
        FROM attempts
        WHERE {where_clause}
        GROUP BY model
        ORDER BY attempts DESC
        """,
        params,
    ).fetchall()

    return {
        row["model"]: {
            "attempts": row["attempts"],
            "improved": row["improved"] or 0,
            "avg_gain": row["avg_gain"] or 0.0,
            "complete": row["complete"] or 0,
            "stuck": row["stuck"] or 0,
            "at_limit": row["at_limit"] or 0,
            # Actual cost tracking
            "total_actual_cost": row["total_actual_cost"],
            "avg_actual_cost": row["avg_actual_cost"],
            "with_cost_data": row["with_cost_data"] or 0,
            "total_input_tokens": row["total_input_tokens"],
            "total_output_tokens": row["total_output_tokens"],
            "avg_duration_ms": row["avg_duration_ms"],
        }
        for row in rows
    }


def get_effectiveness_by_range(
    db_path: str | Path = DEFAULT_DB_PATH,
    ranges: list[tuple[int, int]] | None = None,
    hours: int = 0,
    exclude_unknown: bool = True,
    model: str | None = None,
) -> dict[str, dict[str, dict[str, Any]]]:
    """
    Get effectiveness broken down by start_percent ranges.

    Args:
        db_path: Path to database
        ranges: List of (min, max) tuples for ranges. Default: [(0,25), (25,50), (50,75), (75,100)]
        hours: Only analyze attempts from last N hours (0 = all time)
        exclude_unknown: Exclude attempts with unknown model
        model: Filter to specific model (None = all models)

    Returns:
        Dict mapping range label to model stats:
        {
            range_label: {
                model: {avg_gain: float, count: int}
            }
        }
    """
    if ranges is None:
        ranges = [(0, 25), (25, 50), (50, 75), (75, 100)]

    conn = get_connection(db_path)

    # Build WHERE clause
    conditions = ["start_percent IS NOT NULL", "end_percent IS NOT NULL"]
    params = []

    if exclude_unknown:
        conditions.append("model != 'unknown'")

    if hours > 0:
        conditions.append("created_at > datetime('now', '-' || ? || ' hours')")
        params.append(str(hours))

    if model:
        conditions.append("model = ?")
        params.append(model)

    where_clause = " AND ".join(conditions)

    rows = conn.execute(
        f"""
        SELECT
            model,
            CASE
                WHEN start_percent < 25 THEN '0-25%'
                WHEN start_percent < 50 THEN '25-50%'
                WHEN start_percent < 75 THEN '50-75%'
                ELSE '75-100%'
            END as start_range,
            AVG(end_percent - start_percent) as avg_gain,
            COUNT(*) as n
        FROM attempts
        WHERE {where_clause}
        GROUP BY model, start_range
        ORDER BY start_range, model
        """,
        params,
    ).fetchall()

    # Organize by range
    result: dict[str, dict[str, dict[str, Any]]] = {}
    for row in rows:
        range_label = row["start_range"]
        model = row["model"]

        if range_label not in result:
            result[range_label] = {}

        result[range_label][model] = {
            "avg_gain": row["avg_gain"] or 0.0,
            "count": row["n"],
        }

    return result


def get_gain_distribution(
    db_path: str | Path = DEFAULT_DB_PATH,
    model: str | None = None,
    hours: int = 0,
    exclude_unknown: bool = True,
) -> list[tuple[str, int]]:
    """
    Get histogram of improvement amounts.

    Args:
        db_path: Path to database
        model: Filter to specific model (None = all models)
        hours: Only analyze attempts from last N hours (0 = all time)
        exclude_unknown: Exclude attempts with unknown model

    Returns:
        List of (bucket_label, count) tuples
    """
    conn = get_connection(db_path)

    # Build WHERE clause
    conditions = ["start_percent IS NOT NULL", "end_percent IS NOT NULL"]
    params = []

    if exclude_unknown:
        conditions.append("model != 'unknown'")

    if model:
        conditions.append("model = ?")
        params.append(model)

    if hours > 0:
        conditions.append("created_at > datetime('now', '-' || ? || ' hours')")
        params.append(str(hours))

    where_clause = " AND ".join(conditions)

    rows = conn.execute(
        f"""
        SELECT
            CASE
                WHEN end_percent <= start_percent THEN 'No change'
                WHEN end_percent - start_percent < 10 THEN '0-10% gain'
                WHEN end_percent - start_percent < 25 THEN '10-25% gain'
                WHEN end_percent - start_percent < 50 THEN '25-50% gain'
                ELSE '50%+ gain'
            END as bucket,
            COUNT(*) as n
        FROM attempts
        WHERE {where_clause}
        GROUP BY bucket
        ORDER BY
            CASE bucket
                WHEN 'No change' THEN 1
                WHEN '0-10% gain' THEN 2
                WHEN '10-25% gain' THEN 3
                WHEN '25-50% gain' THEN 4
                WHEN '50%+ gain' THEN 5
            END
        """,
        params,
    ).fetchall()

    return [(row["bucket"], row["n"]) for row in rows]


def format_model_analysis(
    effectiveness: dict[str, dict[str, Any]],
    by_range: dict[str, dict[str, dict[str, Any]]],
    distribution: list[tuple[str, int]],
    hours: int = 0,
    cost_table: dict[str, float] | None = None,
) -> str:
    """
    Format model analysis as text for terminal display.

    Args:
        effectiveness: Output from get_model_effectiveness()
        by_range: Output from get_effectiveness_by_range()
        distribution: Output from get_gain_distribution()
        hours: Hours filter used (for display)
        cost_table: Optional dict of model -> cost_per_function for estimated $/% gain calc

    Returns:
        Formatted string for terminal output
    """
    lines = []

    # Header
    time_str = f"last {hours} hours" if hours > 0 else "all time"
    lines.append("=" * 70)
    lines.append(f"Model Effectiveness Analysis ({time_str})")
    lines.append("=" * 70)
    lines.append("")

    # Check if we have any actual cost data
    has_actual_costs = any(
        stats.get("with_cost_data", 0) > 0
        for stats in effectiveness.values()
    )

    # Overall performance by model
    if effectiveness:
        lines.append("Overall Performance by Model:")

        # Build header based on available data
        if has_actual_costs:
            lines.append(
                "  Model           | Attempts | Improved | Avg Gain | Complete | Actual $/fn | $/% gain*"
            )
            lines.append(
                "  ----------------|----------|----------|----------|----------|-------------|----------"
            )
        elif cost_table:
            lines.append(
                "  Model           | Attempts | Improved | Avg Gain | Complete | Stuck | Est $/% gain"
            )
            lines.append(
                "  ----------------|----------|----------|----------|----------|-------|-------------"
            )
        else:
            lines.append(
                "  Model           | Attempts | Improved | Avg Gain | Complete | Stuck"
            )
            lines.append(
                "  ----------------|----------|----------|----------|----------|------"
            )

        for model, stats in sorted(effectiveness.items(), key=lambda x: -x[1]["attempts"]):
            attempts = stats["attempts"]
            improved = stats["improved"]
            avg_gain = stats["avg_gain"]
            complete = stats["complete"]
            stuck = stats["stuck"]

            # Use actual cost data if available, otherwise use estimates
            with_cost = stats.get("with_cost_data", 0)
            avg_actual_cost = stats.get("avg_actual_cost")

            if has_actual_costs:
                # Display actual costs when available
                if avg_actual_cost is not None and with_cost > 0:
                    cost_str = f"${avg_actual_cost:.4f}"
                    # Calculate actual $/% gain
                    if avg_gain > 0:
                        actual_cost_per_gain = avg_actual_cost / avg_gain
                        cost_per_gain_str = f"${actual_cost_per_gain:.4f}"
                    else:
                        cost_per_gain_str = "N/A"
                    lines.append(
                        f"  {model:15s} | {attempts:8d} | {improved:8d} | {avg_gain:+7.1f}% | {complete:8d} | {cost_str:>11s} | {cost_per_gain_str}"
                    )
                else:
                    # No actual cost data for this model
                    lines.append(
                        f"  {model:15s} | {attempts:8d} | {improved:8d} | {avg_gain:+7.1f}% | {complete:8d} | {'N/A':>11s} | N/A"
                    )
            elif cost_table:
                # Fall back to estimated costs
                cost_per_gain = ""
                if model in cost_table and avg_gain > 0:
                    cost = cost_table.get(model, 0)
                    cost_per_gain = f"  ${cost / avg_gain:.3f}"
                lines.append(
                    f"  {model:15s} | {attempts:8d} | {improved:8d} | {avg_gain:+7.1f}% | {complete:8d} | {stuck:5d} |{cost_per_gain}"
                )
            else:
                lines.append(
                    f"  {model:15s} | {attempts:8d} | {improved:8d} | {avg_gain:+7.1f}% | {complete:8d} | {stuck:5d}"
                )

        # Add note about cost data source
        if has_actual_costs:
            total_with_cost = sum(s.get("with_cost_data", 0) for s in effectiveness.values())
            total_attempts = sum(s["attempts"] for s in effectiveness.values())
            lines.append(f"  (* $/% gain from actual costs: {total_with_cost}/{total_attempts} attempts have cost data)")
        lines.append("")

    # Effectiveness by starting percentage
    if by_range:
        lines.append("Effectiveness by Starting Percentage:")

        # Get all models across all ranges
        all_models = set()
        for range_stats in by_range.values():
            all_models.update(range_stats.keys())
        models = sorted(all_models)

        # Header
        header = "  Range      |"
        for m in models:
            header += f" {m:14s}|"
        lines.append(header)
        lines.append("  " + "-" * (11 + 16 * len(models)))

        # Rows by range (in order)
        range_order = ["0-25%", "25-50%", "50-75%", "75-100%"]
        for range_label in range_order:
            if range_label not in by_range:
                continue

            range_stats = by_range[range_label]
            row = f"  {range_label:10s} |"
            for m in models:
                if m in range_stats:
                    avg = range_stats[m]["avg_gain"]
                    n = range_stats[m]["count"]
                    row += f" {avg:+5.1f}% (n={n:3d})|"
                else:
                    row += f" {'--':^14s}|"
            lines.append(row)
        lines.append("")

    # Gain distribution
    if distribution:
        lines.append("Gain Distribution (all models):")

        # Find max count for bar scaling
        max_count = max(count for _, count in distribution) if distribution else 1
        bar_width = 40

        for bucket, count in distribution:
            bar_len = int((count / max_count) * bar_width) if max_count > 0 else 0
            bar = "█" * bar_len
            lines.append(f"  {bucket:14s} {bar:40s} {count}")
        lines.append("")

    # Key insights
    if effectiveness:
        lines.append("Key Insights:")

        # Find best model by avg_gain
        best_by_gain = max(
            ((m, s) for m, s in effectiveness.items() if s["improved"] > 0),
            key=lambda x: x[1]["avg_gain"],
            default=None,
        )
        if best_by_gain:
            lines.append(f"  - Highest avg gain: {best_by_gain[0]} ({best_by_gain[1]['avg_gain']:+.1f}%)")

        # Find most cost-effective - prefer actual cost data when available
        if has_actual_costs:
            # Use actual cost data
            valid_models = [
                (m, s, s["avg_actual_cost"] / s["avg_gain"])
                for m, s in effectiveness.items()
                if s["avg_gain"] > 0 and s.get("avg_actual_cost") is not None and s.get("with_cost_data", 0) > 0
            ]
            if valid_models:
                best_cost = min(valid_models, key=lambda x: x[2])
                lines.append(f"  - Most cost-effective: {best_cost[0]} (${best_cost[2]:.4f} per % gained, actual)")
        elif cost_table:
            # Fall back to estimated cost data
            valid_models = [
                (m, s, cost_table.get(m, 0) / s["avg_gain"])
                for m, s in effectiveness.items()
                if s["avg_gain"] > 0 and m in cost_table
            ]
            if valid_models:
                best_cost = min(valid_models, key=lambda x: x[2])
                lines.append(f"  - Most cost-effective: {best_cost[0]} (${best_cost[2]:.3f} per % gained, estimated)")

        # Find model with highest completion rate
        best_completion = max(
            ((m, s) for m, s in effectiveness.items() if s["attempts"] > 5),
            key=lambda x: x[1]["complete"] / x[1]["attempts"] if x[1]["attempts"] > 0 else 0,
            default=None,
        )
        if best_completion and best_completion[1]["attempts"] > 0:
            rate = best_completion[1]["complete"] / best_completion[1]["attempts"] * 100
            lines.append(f"  - Best completion rate: {best_completion[0]} ({rate:.1f}%)")

        # Add total actual cost if available
        if has_actual_costs:
            total_actual = sum(
                s.get("total_actual_cost", 0) or 0
                for s in effectiveness.values()
            )
            if total_actual > 0:
                total_gain = sum(
                    s["avg_gain"] * s["improved"]
                    for s in effectiveness.values()
                    if s["avg_gain"] > 0
                )
                lines.append(f"  - Total actual spend: ${total_actual:.2f}")
                if total_gain > 0:
                    lines.append(f"  - Overall cost efficiency: ${total_actual / total_gain:.4f} per % gained")

        lines.append("")

    return "\n".join(lines)
