"""Pattern effectiveness tracking — persistent stats for permuter patterns.

Stored in permuter_cache.db. Records per-run pattern outcomes so we can
answer: which patterns actually win, how often, and by how much?

Query examples:
    -- Overall win rate per pattern
    SELECT pattern, COUNT(*) as runs,
           SUM(won) as wins,
           ROUND(100.0 * SUM(won) / COUNT(*), 1) as win_rate,
           ROUND(AVG(CASE WHEN won THEN best_delta END), 2) as avg_win_delta
    FROM pattern_runs
    GROUP BY pattern
    ORDER BY wins DESC;

    -- Best patterns by total improvement contributed
    SELECT pattern,
           SUM(CASE WHEN won THEN best_delta ELSE 0 END) as total_delta,
           SUM(won) as wins
    FROM pattern_runs
    GROUP BY pattern
    ORDER BY total_delta DESC;

    -- Pattern performance on specific mismatch types
    SELECT pattern, diagnosis_category, COUNT(*) as runs, SUM(won) as wins
    FROM pattern_runs
    WHERE diagnosis_category IS NOT NULL
    GROUP BY pattern, diagnosis_category
    ORDER BY pattern, wins DESC;
"""

from __future__ import annotations

import sqlite3
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

_CACHE_DB = Path(__file__).resolve().parent.parent.parent / "permuter_cache.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS pattern_runs (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp           REAL NOT NULL,
    symbol              TEXT NOT NULL,
    function_name       TEXT,
    source_path         TEXT,

    -- What was tried
    pattern             TEXT NOT NULL,
    variants_generated  INTEGER NOT NULL DEFAULT 0,
    variants_built      INTEGER NOT NULL DEFAULT 0,
    build_failures      INTEGER NOT NULL DEFAULT 0,

    -- Outcome
    won                 INTEGER NOT NULL DEFAULT 0,
    best_delta          REAL NOT NULL DEFAULT 0,
    best_variant        TEXT,

    -- Context (for correlating patterns with mismatch types)
    initial_pct         REAL,
    final_pct           REAL,
    diagnosis_category  TEXT,

    -- Caller context
    unit                TEXT,
    caller              TEXT NOT NULL DEFAULT 'hill_climber'
);

CREATE INDEX IF NOT EXISTS idx_pattern_runs_pattern
ON pattern_runs (pattern);

CREATE INDEX IF NOT EXISTS idx_pattern_runs_symbol
ON pattern_runs (symbol);

CREATE INDEX IF NOT EXISTS idx_pattern_runs_won
ON pattern_runs (won) WHERE won = 1;
"""


@dataclass
class PatternRunStats:
    """Per-pattern stats accumulated during a single hill_climb run."""

    pattern: str
    variants_generated: int = 0
    variants_built: int = 0
    build_failures: int = 0
    best_delta: float = 0.0
    best_variant: str | None = None
    won: bool = False  # Was this pattern's variant the overall winner?


@dataclass
class RunStatsAccumulator:
    """Collects per-pattern stats during a hill_climb run."""

    by_pattern: dict[str, PatternRunStats] = field(default_factory=dict)

    def record_variant(
        self,
        pattern_name: str,
        variant_name: str,
        match_pct: float,
        baseline: float,
        build_success: bool,
    ):
        """Record a scored variant."""
        if pattern_name not in self.by_pattern:
            self.by_pattern[pattern_name] = PatternRunStats(pattern=pattern_name)

        stats = self.by_pattern[pattern_name]
        stats.variants_generated += 1

        if not build_success:
            stats.build_failures += 1
        else:
            stats.variants_built += 1
            delta = match_pct - baseline
            if delta > stats.best_delta:
                stats.best_delta = delta
                stats.best_variant = variant_name

    def mark_winner(self, pattern_name: str):
        """Mark a pattern as the round winner.

        For composed/chained patterns (compose:a+b, chain:a+b+c), also
        credit each component pattern if tracked.
        """
        if pattern_name in self.by_pattern:
            self.by_pattern[pattern_name].won = True
        # Credit individual components of compose/chain winners
        from .types import _split_pattern_name
        components = _split_pattern_name(pattern_name)
        if len(components) > 1:
            for comp in components:
                if comp in self.by_pattern and comp != pattern_name:
                    self.by_pattern[comp].won = True


def store_run(
    accumulator: RunStatsAccumulator,
    symbol: str,
    function_name: str | None,
    source_path: str | None,
    initial_pct: float,
    final_pct: float,
    diagnosis_category: str | None = None,
    unit: str | None = None,
    caller: str = "hill_climber",
) -> None:
    """Persist pattern stats from a hill_climb run to permuter_cache.db."""
    if not accumulator.by_pattern:
        return

    conn = sqlite3.connect(str(_CACHE_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(_SCHEMA)

    now = time.time()
    rows = []
    for stats in accumulator.by_pattern.values():
        rows.append((
            now, symbol, function_name, source_path,
            stats.pattern, stats.variants_generated,
            stats.variants_built, stats.build_failures,
            int(stats.won), stats.best_delta, stats.best_variant,
            initial_pct, final_pct, diagnosis_category, unit, caller,
        ))

    conn.executemany(
        "INSERT INTO pattern_runs "
        "(timestamp, symbol, function_name, source_path, "
        "pattern, variants_generated, variants_built, build_failures, "
        "won, best_delta, best_variant, "
        "initial_pct, final_pct, diagnosis_category, unit, caller) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        rows,
    )
    conn.commit()
    conn.close()


def query_pattern_summary() -> list[dict]:
    """Query aggregate pattern effectiveness.

    Returns list of dicts sorted by win count descending:
        pattern, runs, wins, win_rate, avg_win_delta, total_delta
    """
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.row_factory = sqlite3.Row

        # Check table exists
        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            conn.close()
            return []

        rows = conn.execute("""
            SELECT
                pattern,
                COUNT(*) as runs,
                SUM(won) as wins,
                ROUND(100.0 * SUM(won) / COUNT(*), 1) as win_rate,
                ROUND(AVG(CASE WHEN won THEN best_delta END), 3) as avg_win_delta,
                ROUND(SUM(CASE WHEN won THEN best_delta ELSE 0 END), 3) as total_delta,
                SUM(variants_generated) as total_variants,
                SUM(build_failures) as total_build_failures
            FROM pattern_runs
            GROUP BY pattern
            ORDER BY wins DESC, total_delta DESC
        """).fetchall()
        conn.close()

        return [dict(r) for r in rows]
    except Exception:
        return []


def query_pattern_by_diagnosis() -> list[dict]:
    """Query pattern win rates broken down by diagnosis category.

    Returns list of dicts:
        pattern, diagnosis_category, runs, wins, win_rate
    """
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.row_factory = sqlite3.Row

        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            conn.close()
            return []

        rows = conn.execute("""
            SELECT
                pattern,
                diagnosis_category,
                COUNT(*) as runs,
                SUM(won) as wins,
                ROUND(100.0 * SUM(won) / COUNT(*), 1) as win_rate
            FROM pattern_runs
            WHERE diagnosis_category IS NOT NULL
            GROUP BY pattern, diagnosis_category
            HAVING runs >= 3
            ORDER BY pattern, wins DESC
        """).fetchall()
        conn.close()

        return [dict(r) for r in rows]
    except Exception:
        return []


def print_summary():
    """Print a formatted summary of pattern effectiveness to stdout."""
    rows = query_pattern_summary()
    if not rows:
        print("No pattern stats recorded yet.")
        print("Run the permuter to start collecting data.")
        return

    total_runs = sum(r["runs"] for r in rows)
    total_wins = sum(r["wins"] or 0 for r in rows)

    # Column widths
    pat_w = max(len(r["pattern"]) for r in rows)
    pat_w = max(pat_w, 7)  # "Pattern"

    print(f"\n{'=' * (pat_w + 65)}")
    print(f"  PATTERN EFFECTIVENESS ({total_runs:,} runs, {total_wins:,} wins)")
    print(f"{'=' * (pat_w + 65)}")
    print(
        f"  {'Pattern':<{pat_w}} | {'Runs':>5} | {'Wins':>5} | {'Rate':>6} | "
        f"{'AvgΔ':>7} | {'TotalΔ':>8} | {'Variants':>8} | {'Fails':>5}"
    )
    print(
        f"  {'─' * pat_w}─┼─{'─' * 5}─┼─{'─' * 5}─┼─{'─' * 6}─┼─"
        f"{'─' * 7}─┼─{'─' * 8}─┼─{'─' * 8}─┼─{'─' * 5}"
    )

    for r in rows:
        wins = r["wins"] or 0
        win_rate = f"{r['win_rate']}%" if r["win_rate"] else "—"
        avg_delta = f"+{r['avg_win_delta']:.2f}" if r["avg_win_delta"] else "—"
        total_delta = f"+{r['total_delta']:.2f}" if r["total_delta"] else "—"
        print(
            f"  {r['pattern']:<{pat_w}} | {r['runs']:>5,} | {wins:>5,} | "
            f"{win_rate:>6} | {avg_delta:>7} | {total_delta:>8} | "
            f"{r['total_variants']:>8,} | {r['total_build_failures']:>5,}"
        )

    print(
        f"  {'─' * pat_w}─┴─{'─' * 5}─┴─{'─' * 5}─┴─{'─' * 6}─┴─"
        f"{'─' * 7}─┴─{'─' * 8}─┴─{'─' * 8}─┴─{'─' * 5}"
    )

    # Top winners
    winners = [r for r in rows if r["wins"] and r["wins"] > 0]
    if winners:
        top = winners[0]
        print(f"\n  Most wins: {top['pattern']} ({top['wins']} wins, "
              f"{top['win_rate']}% rate, +{top['total_delta']:.2f}% total)")


def _print_unit_breakdown():
    """Show pattern wins broken down by unit."""
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.row_factory = sqlite3.Row

        exists = conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            print("No pattern stats recorded yet.")
            return

        rows = conn.execute("""
            SELECT
                unit,
                pattern,
                SUM(won) as wins,
                ROUND(SUM(CASE WHEN won THEN best_delta ELSE 0 END), 2) as total_delta
            FROM pattern_runs
            WHERE won = 1 AND unit IS NOT NULL
            GROUP BY unit, pattern
            ORDER BY unit, wins DESC
        """).fetchall()
        conn.close()

        if not rows:
            print("No winning patterns recorded yet.")
            return

        by_unit: dict[str, list] = {}
        for r in rows:
            by_unit.setdefault(r["unit"], []).append(dict(r))

        print(f"\n  Pattern wins by unit ({len(by_unit)} units)")
        print(f"  {'─' * 60}")
        for unit, entries in sorted(by_unit.items()):
            print(f"\n  {unit}")
            for e in entries:
                print(f"    {e['pattern']:30s} {e['wins']:>3} wins (+{e['total_delta']:.2f}%)")

    except Exception as e:
        print(f"Error: {e}")


def main():
    """CLI entry point for querying pattern stats."""
    import argparse
    parser = argparse.ArgumentParser(
        prog="python -m scripts.permuter.pattern_stats",
        description="Query permuter pattern effectiveness stats.",
    )
    parser.add_argument(
        "--json", action="store_true",
        help="Output as JSON",
    )
    parser.add_argument(
        "--by-unit", action="store_true",
        help="Show per-unit breakdown for winning patterns",
    )
    args = parser.parse_args()

    if args.json:
        import json
        data = {
            "patterns": query_pattern_summary(),
        }
        print(json.dumps(data, indent=2))
    elif args.by_unit:
        _print_unit_breakdown()
    else:
        print_summary()


if __name__ == "__main__":
    main()
