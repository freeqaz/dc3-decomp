"""Climb history — tracks completed hill-climb runs to avoid redundant work
and to feed the variant outcome predictor (roadmap B4).

Two tables:

- ``climb_history``: one row per completed hill-climb / beam run. Used by
  ``should_skip`` to avoid re-running plateaued/perfect functions. B4 added
  per-climb predictor features (diagnosis fingerprint, function size, beam
  depth) here.
- ``climb_variant``: one row per *individual* variant tried during a climb,
  carrying the per-variant pattern label and whether that variant improved
  the score. This is the per-variant granularity the predictor trains on —
  the older ``patterns_csv`` on ``climb_history`` is only a pattern *set* for
  the whole climb, which can't distinguish a winning pattern from a losing one.

Both live in permuter_cache.db alongside score_cache and pattern_runs.

Schema evolution is **backward-compatible**: new climb_history columns are
added with ``ALTER TABLE`` only when missing, and all reads tolerate
NULL/absent values so records written by the pre-B4 schema still load.
"""

from __future__ import annotations

import hashlib
import sqlite3
import time
from pathlib import Path

from .repo_paths import get_cache_db_path
from .types import VariantOutcome

_CACHE_DB = get_cache_db_path()

# Base schema. climb_history's B4 columns are added by _migrate() so that
# databases created by older code pick them up in place without a rebuild.
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

CREATE TABLE IF NOT EXISTS climb_variant (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    climb_id         INTEGER NOT NULL,
    timestamp        REAL NOT NULL,
    symbol           TEXT NOT NULL,
    pattern_label    TEXT NOT NULL,
    diag_fingerprint TEXT,
    func_loc         INTEGER,
    func_stmts       INTEGER,
    beam_depth       INTEGER,
    delta            REAL NOT NULL,
    won              INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_climb_variant_pattern
ON climb_variant (pattern_label);
"""

# B4 columns added to climb_history. (column_name, SQL type)
_HISTORY_NEW_COLUMNS = [
    ("diag_fingerprint", "TEXT"),   # diagnosis category, e.g. "regswap+offset"
    ("func_loc", "INTEGER"),        # function size in source lines
    ("func_stmts", "INTEGER"),      # top-level statement count
    ("beam_depth", "INTEGER"),      # rounds for hill-climb / depth for beam
]


def _patterns_hash(pattern_names: list[str]) -> str:
    """Stable hash of sorted pattern names."""
    key = ",".join(sorted(pattern_names))
    return hashlib.md5(key.encode()).hexdigest()


def _migrate(conn: sqlite3.Connection) -> None:
    """Add B4 columns to climb_history if an older schema is in place.

    ALTER TABLE ADD COLUMN is metadata-only in SQLite and the added columns
    default to NULL, so pre-B4 rows stay readable.
    """
    existing = {row[1] for row in conn.execute("PRAGMA table_info(climb_history)")}
    for name, decl in _HISTORY_NEW_COLUMNS:
        if name not in existing:
            conn.execute(f"ALTER TABLE climb_history ADD COLUMN {name} {decl}")


def _get_conn(db_path: Path | None = None) -> sqlite3.Connection:
    conn = sqlite3.connect(str(db_path or _CACHE_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.executescript(_SCHEMA)
    _migrate(conn)
    return conn


def should_skip(
    symbol: str,
    source_md5: str,
    pattern_names: list[str],
    db_path: Path | None = None,
) -> str | None:
    """Check if this (symbol, source, patterns) combo was already tried.

    Returns a skip reason string if we should skip, None if we should run.

    Skip conditions:
    - Same source was climbed with a superset of the requested patterns
      AND the result was plateau/no_variants/noise_only/unfixable (no improvement possible)
    - Same source reached 100% (already perfect)
    """
    conn = _get_conn(db_path)
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
    diag_fingerprint: str | None = None,
    func_loc: int | None = None,
    func_stmts: int | None = None,
    beam_depth: int | None = None,
    variant_outcomes: list[VariantOutcome] | None = None,
    db_path: Path | None = None,
) -> int | None:
    """Record a completed hill-climb run.

    B4 features (all optional, default None so older callers keep working):
    - ``diag_fingerprint``: diagnosis category string (see
      ``diagnosis_fingerprint``).
    - ``func_loc`` / ``func_stmts``: function size (source lines / statement count).
    - ``beam_depth``: search depth (rounds for hill-climb, beam depth for beam).
    - ``variant_outcomes``: per-variant labels+deltas to populate ``climb_variant``.

    Returns the climb_history row id, or None if the run wasn't recorded.
    """
    # Don't record interrupted runs — they're incomplete
    if stopped_reason == "interrupted":
        return None

    conn = _get_conn(db_path)
    ts = time.time()
    cur = conn.execute(
        "INSERT INTO climb_history "
        "(timestamp, symbol, source_md5, patterns_hash, patterns_csv, "
        "initial_pct, final_pct, delta, stopped_reason, rounds_used, elapsed_seconds, "
        "diag_fingerprint, func_loc, func_stmts, beam_depth) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            ts, symbol, source_md5,
            _patterns_hash(pattern_names),
            ",".join(sorted(pattern_names)),
            initial_pct, final_pct, final_pct - initial_pct,
            stopped_reason, rounds_used, elapsed_seconds,
            diag_fingerprint, func_loc, func_stmts, beam_depth,
        ),
    )
    climb_id = cur.lastrowid

    if variant_outcomes:
        conn.executemany(
            "INSERT INTO climb_variant "
            "(climb_id, timestamp, symbol, pattern_label, diag_fingerprint, "
            "func_loc, func_stmts, beam_depth, delta, won) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            [
                (
                    climb_id, ts, symbol, vo.pattern_label,
                    diag_fingerprint, func_loc, func_stmts, beam_depth,
                    vo.delta, 1 if vo.won else 0,
                )
                for vo in variant_outcomes
            ],
        )

    conn.commit()
    conn.close()
    return climb_id


def load_variant_training_data(db_path: Path | None = None) -> list[dict]:
    """Return all recorded per-variant outcomes as feature dicts.

    Each dict carries the predictor's features plus the ``won`` label. Pre-B4
    runs contributed no climb_variant rows (the table is new), so this returns
    only data captured under the instrumented schema.
    """
    conn = _get_conn(db_path)
    rows = conn.execute(
        "SELECT pattern_label, diag_fingerprint, func_loc, func_stmts, "
        "beam_depth, delta, won FROM climb_variant"
    ).fetchall()
    conn.close()
    return [
        {
            "pattern_label": r[0],
            "diag_fingerprint": r[1],
            "func_loc": r[2],
            "func_stmts": r[3],
            "beam_depth": r[4],
            "delta": r[5],
            "won": bool(r[6]),
        }
        for r in rows
    ]


def clear_symbol(symbol: str, db_path: Path | None = None) -> int:
    """Clear history for a symbol (e.g., after manual source edits)."""
    conn = _get_conn(db_path)
    cursor = conn.execute(
        "DELETE FROM climb_history WHERE symbol = ?", (symbol,),
    )
    conn.execute("DELETE FROM climb_variant WHERE symbol = ?", (symbol,))
    conn.commit()
    deleted = cursor.rowcount
    conn.close()
    return deleted


def clear_all(db_path: Path | None = None) -> int:
    """Clear all climb history."""
    conn = _get_conn(db_path)
    cursor = conn.execute("DELETE FROM climb_history")
    conn.execute("DELETE FROM climb_variant")
    conn.commit()
    deleted = cursor.rowcount
    conn.close()
    return deleted


def stats(db_path: Path | None = None) -> dict:
    """Return summary stats."""
    conn = _get_conn(db_path)
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
        variants = conn.execute("SELECT COUNT(*) FROM climb_variant").fetchone()[0]
        return {
            "total_runs": total,
            "unique_symbols": symbols,
            "skippable": skippable,
            "variant_outcomes": variants,
        }
    finally:
        conn.close()


def diagnosis_fingerprint(diagnosis) -> str | None:
    """Collapse a Diagnosis into a stable category string for the predictor.

    Mirrors strategy_db.classify_diagnosis_category but takes a Diagnosis
    object directly (the form available at climb-record time in both the
    hill-climber and beam search). Returns None when no diagnosis is present.
    """
    if diagnosis is None:
        return None
    try:
        from .strategy_db import classify_diagnosis_category
        info = {
            "has_regswap": bool(getattr(diagnosis, "reg_swap_pairs", None)),
            "has_structural": (getattr(diagnosis, "replace_real", 0) or 0) > 0
                              or bool(getattr(diagnosis, "clusters", None)),
            "has_prologue": bool(getattr(diagnosis, "has_prologue_mismatch", False)),
            "has_offset": bool(getattr(diagnosis, "offset_deltas", None)),
        }
        return classify_diagnosis_category(info)
    except Exception:
        return None


def _fmt_time(ts: float) -> str:
    """Format a timestamp as a short relative or absolute string."""
    import datetime
    dt = datetime.datetime.fromtimestamp(ts)
    return dt.strftime("%m-%d %H:%M")
