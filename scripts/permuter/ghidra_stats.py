"""Ghidra-guided permuter analytics — track when Ghidra guidance helps.

Stored in permuter_cache.db alongside the score cache. Records per-function
outcomes: whether Ghidra data was available, whether guided variants were
generated, whether a guided variant won (improved match%).

Query examples:
    -- Hit rate: how often Ghidra guidance produces the winning variant
    SELECT
        COUNT(*) as total,
        SUM(ghidra_available) as available,
        SUM(ghidra_variants_generated > 0) as generated,
        SUM(ghidra_winner) as wins
    FROM ghidra_stats;

    -- Best improvements from Ghidra-guided variants
    SELECT symbol, delta, winning_variant
    FROM ghidra_stats WHERE ghidra_winner = 1
    ORDER BY delta DESC LIMIT 20;
"""

from __future__ import annotations

import sqlite3
import time
from dataclasses import dataclass, field
from pathlib import Path

_CACHE_DB = Path(__file__).resolve().parent.parent.parent / "permuter_cache.db"

_GHIDRA_SCHEMA = """
CREATE TABLE IF NOT EXISTS ghidra_stats (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    symbol                  TEXT NOT NULL,
    function_name           TEXT,
    timestamp               REAL NOT NULL,
    -- Ghidra data availability
    ghidra_available        INTEGER NOT NULL DEFAULT 0,
    ghidra_code_bytes       INTEGER DEFAULT 0,
    ghidra_vars_count       INTEGER DEFAULT 0,
    ghidra_gpr_saves        INTEGER,
    -- Variant generation
    ghidra_variants_generated INTEGER NOT NULL DEFAULT 0,
    total_variants          INTEGER NOT NULL DEFAULT 0,
    -- Outcome
    ghidra_winner           INTEGER NOT NULL DEFAULT 0,
    winning_variant         TEXT,
    winning_pattern         TEXT,
    initial_pct             REAL,
    final_pct               REAL,
    delta                   REAL NOT NULL DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_ghidra_stats_symbol
ON ghidra_stats (symbol);
"""


@dataclass
class GhidraRunStats:
    """Accumulates Ghidra stats for a single hill_climb run."""

    ghidra_available: bool = False
    ghidra_code_bytes: int = 0
    ghidra_vars_count: int = 0
    ghidra_gpr_saves: int | None = None
    ghidra_variants_generated: int = 0
    total_variants: int = 0
    ghidra_winner: bool = False
    winning_variant: str | None = None
    winning_pattern: str | None = None
    # Preflight fields
    preflight_flagged: bool = False
    preflight_reason: str | None = None
    preflight_confidence: float = 0.0


@dataclass
class GhidraBatchStats:
    """Aggregated Ghidra stats across a batch run."""

    functions_total: int = 0
    functions_with_ghidra: int = 0
    functions_with_ghidra_variants: int = 0
    functions_with_ghidra_wins: int = 0
    total_ghidra_variants: int = 0
    total_variants: int = 0
    total_delta_ghidra: float = 0.0
    total_delta_other: float = 0.0
    preflight_flagged: int = 0
    preflight_skipped: int = 0

    def accumulate(self, run: GhidraRunStats, delta: float) -> None:
        self.functions_total += 1
        if run.ghidra_available:
            self.functions_with_ghidra += 1
        if run.ghidra_variants_generated > 0:
            self.functions_with_ghidra_variants += 1
            self.total_ghidra_variants += run.ghidra_variants_generated
        self.total_variants += run.total_variants
        if run.ghidra_winner and delta > 0:
            self.functions_with_ghidra_wins += 1
            self.total_delta_ghidra += delta
        elif delta > 0:
            self.total_delta_other += delta
        if run.preflight_flagged:
            self.preflight_flagged += 1
            if run.preflight_confidence >= 0.8:
                self.preflight_skipped += 1

    def summary_lines(self) -> list[str]:
        """Return formatted summary lines for stderr output."""
        if self.functions_with_ghidra == 0:
            return ["  Ghidra: not used (no --ghidra or no cached decompilations)"]

        lines = []
        lines.append(
            f"  Ghidra cache: {self.functions_with_ghidra}/"
            f"{self.functions_total} functions had decompilations"
        )
        if self.total_ghidra_variants > 0:
            lines.append(
                f"  Ghidra variants: {self.total_ghidra_variants} generated "
                f"(of {self.total_variants} total)"
            )
        if self.functions_with_ghidra_variants > 0:
            hit_rate = (
                self.functions_with_ghidra_wins / self.functions_with_ghidra_variants * 100
                if self.functions_with_ghidra_variants > 0 else 0
            )
            lines.append(
                f"  Ghidra wins: {self.functions_with_ghidra_wins}/"
                f"{self.functions_with_ghidra_variants} functions with guided variants "
                f"({hit_rate:.0f}% hit rate)"
            )
        if self.total_delta_ghidra > 0:
            lines.append(
                f"  Ghidra delta: +{self.total_delta_ghidra:.2f}% "
                f"(vs +{self.total_delta_other:.2f}% from other patterns)"
            )
        if self.preflight_flagged > 0:
            lines.append(
                f"  Ghidra preflight: {self.preflight_flagged} flagged, "
                f"{self.preflight_skipped} high-confidence skips"
            )
        return lines


def store_run(
    symbol: str,
    function_name: str | None,
    run: GhidraRunStats,
    initial_pct: float,
    final_pct: float,
) -> None:
    """Store a single Ghidra run's stats to permuter_cache.db."""
    conn = sqlite3.connect(str(_CACHE_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(_GHIDRA_SCHEMA)
    conn.execute(
        "INSERT INTO ghidra_stats "
        "(symbol, function_name, timestamp, ghidra_available, ghidra_code_bytes, "
        "ghidra_vars_count, ghidra_gpr_saves, ghidra_variants_generated, "
        "total_variants, ghidra_winner, winning_variant, winning_pattern, "
        "initial_pct, final_pct, delta) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            symbol,
            function_name,
            time.time(),
            int(run.ghidra_available),
            run.ghidra_code_bytes,
            run.ghidra_vars_count,
            run.ghidra_gpr_saves,
            run.ghidra_variants_generated,
            run.total_variants,
            int(run.ghidra_winner),
            run.winning_variant,
            run.winning_pattern,
            initial_pct,
            final_pct,
            final_pct - initial_pct,
        ),
    )
    conn.commit()
    conn.close()


def query_summary() -> dict:
    """Query aggregate Ghidra stats from permuter_cache.db.

    Returns a dict with totals and rates, or empty dict if no data.
    """
    try:
        conn = sqlite3.connect(str(_CACHE_DB))
        conn.execute("PRAGMA journal_mode=WAL")
        # Check table exists
        tables = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='ghidra_stats'"
        ).fetchone()
        if not tables:
            conn.close()
            return {}

        row = conn.execute("""
            SELECT
                COUNT(*) as total_runs,
                SUM(ghidra_available) as available,
                SUM(ghidra_variants_generated > 0) as had_variants,
                SUM(ghidra_winner) as wins,
                SUM(CASE WHEN ghidra_winner = 1 THEN delta ELSE 0 END) as ghidra_delta,
                SUM(CASE WHEN ghidra_winner = 0 AND delta > 0 THEN delta ELSE 0 END) as other_delta,
                AVG(CASE WHEN ghidra_winner = 1 THEN delta END) as avg_ghidra_delta,
                SUM(ghidra_variants_generated) as total_ghidra_variants,
                SUM(total_variants) as total_variants
            FROM ghidra_stats
        """).fetchone()
        conn.close()

        if not row or row[0] == 0:
            return {}

        return {
            "total_runs": row[0],
            "ghidra_available": row[1] or 0,
            "had_variants": row[2] or 0,
            "wins": row[3] or 0,
            "ghidra_delta": row[4] or 0.0,
            "other_delta": row[5] or 0.0,
            "avg_ghidra_delta": row[6] or 0.0,
            "total_ghidra_variants": row[7] or 0,
            "total_variants": row[8] or 0,
        }
    except Exception:
        return {}
