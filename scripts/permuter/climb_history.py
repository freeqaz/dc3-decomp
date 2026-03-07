"""Climb history — tracks completed hill-climb runs to avoid redundant work.

Records (symbol, source_md5, patterns_hash) after each hill-climb so that
subsequent runs can skip functions whose source hasn't changed and whose
pattern set is a subset of what was already tried.

Stored in permuter_cache.db alongside score_cache and pattern_runs.
"""

from __future__ import annotations

import hashlib
import sqlite3
import time
from pathlib import Path

_CACHE_DB = Path(__file__).resolve().parent.parent.parent / "permuter_cache.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS climb_history (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp       REAL NOT NULL,
    symbol          TEXT NOT NULL,
    source_md5      TEXT NOT NULL,
    patterns_hash   TEXT NOT NULL,
    patterns_csv    TEXT NOT NULL,
    initial_pct     REAL NOT NULL,
    final_pct       REAL NOT NULL,
    delta           REAL NOT NULL,
    stopped_reason  TEXT NOT NULL,
    rounds_used     INTEGER NOT NULL,
    elapsed_seconds REAL
);

CREATE INDEX IF NOT EXISTS idx_climb_history_lookup
ON climb_history (symbol, source_md5);
"""


def _patterns_hash(pattern_names: list[str]) -> str:
    """Stable hash of sorted pattern names."""
    key = ",".join(sorted(pattern_names))
    return hashlib.md5(key.encode()).hexdigest()


def _get_conn() -> sqlite3.Connection:
    conn = sqlite3.connect(str(_CACHE_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(_SCHEMA)
    return conn


def should_skip(
    symbol: str,
    source_md5: str,
    pattern_names: list[str],
) -> str | None:
    """Check if this (symbol, source, patterns) combo was already tried.

    Returns a skip reason string if we should skip, None if we should run.

    Skip conditions:
    - Same source was climbed with a superset of the requested patterns
      AND the result was plateau/no_variants/noise_only/unfixable (no improvement possible)
    - Same source reached 100% (already perfect)
    """
    conn = _get_conn()
    rows = conn.execute(
        "SELECT patterns_csv, final_pct, stopped_reason, delta, timestamp "
        "FROM climb_history "
        "WHERE symbol = ? AND source_md5 = ? "
        "ORDER BY timestamp DESC",
        (symbol, source_md5),
    ).fetchall()
    conn.close()

    if not rows:
        return None

    requested = set(pattern_names)

    for patterns_csv, final_pct, stopped_reason, delta, ts in rows:
        previous_patterns = set(patterns_csv.split(","))

        # Already at 100%
        if final_pct >= 100.0:
            return f"already 100% (run {_fmt_time(ts)})"

        # Previous run used a superset of requested patterns and plateaued
        if requested <= previous_patterns:
            if stopped_reason in ("plateau", "no_variants", "noise_only", "unfixable"):
                return (
                    f"{stopped_reason} at {final_pct:.1f}% with "
                    f"{len(previous_patterns)} patterns (run {_fmt_time(ts)})"
                )

    return None


def record_climb(
    symbol: str,
    source_md5: str,
    pattern_names: list[str],
    initial_pct: float,
    final_pct: float,
    stopped_reason: str,
    rounds_used: int,
    elapsed_seconds: float,
) -> None:
    """Record a completed hill-climb run."""
    # Don't record interrupted runs — they're incomplete
    if stopped_reason == "interrupted":
        return

    conn = _get_conn()
    conn.execute(
        "INSERT INTO climb_history "
        "(timestamp, symbol, source_md5, patterns_hash, patterns_csv, "
        "initial_pct, final_pct, delta, stopped_reason, rounds_used, elapsed_seconds) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            time.time(), symbol, source_md5,
            _patterns_hash(pattern_names),
            ",".join(sorted(pattern_names)),
            initial_pct, final_pct, final_pct - initial_pct,
            stopped_reason, rounds_used, elapsed_seconds,
        ),
    )
    conn.commit()
    conn.close()


def clear_symbol(symbol: str) -> int:
    """Clear history for a symbol (e.g., after manual source edits)."""
    conn = _get_conn()
    cursor = conn.execute(
        "DELETE FROM climb_history WHERE symbol = ?", (symbol,),
    )
    conn.commit()
    deleted = cursor.rowcount
    conn.close()
    return deleted


def clear_all() -> int:
    """Clear all climb history."""
    conn = _get_conn()
    cursor = conn.execute("DELETE FROM climb_history")
    conn.commit()
    deleted = cursor.rowcount
    conn.close()
    return deleted


def stats() -> dict:
    """Return summary stats."""
    conn = _get_conn()
    try:
        total = conn.execute("SELECT COUNT(*) FROM climb_history").fetchone()[0]
        symbols = conn.execute(
            "SELECT COUNT(DISTINCT symbol) FROM climb_history"
        ).fetchone()[0]
        skippable = conn.execute(
            "SELECT COUNT(*) FROM climb_history "
            "WHERE stopped_reason IN ('plateau', 'no_variants', 'noise_only', 'unfixable') "
            "OR final_pct >= 100.0"
        ).fetchone()[0]
        return {"total_runs": total, "unique_symbols": symbols, "skippable": skippable}
    finally:
        conn.close()


def _fmt_time(ts: float) -> str:
    """Format a timestamp as a short relative or absolute string."""
    import datetime
    dt = datetime.datetime.fromtimestamp(ts)
    return dt.strftime("%m-%d %H:%M")
