"""Cross-function strategy database.

Mines historical improvement patterns from baseline reports and commit diffs,
stores structured strategy records, and provides lookup for beam search
priority boosting.

The key insight: when a pattern historically worked for functions with a
specific diagnosis profile in a specific unit category, it should be
prioritized for similar functions.

Usage:
    # Build/update the database
    python -m scripts.permuter.strategy_db build

    # Query strategies for a function
    python -m scripts.permuter.strategy_db query --unit system/rndobj --diagnosis regswap

    # Show statistics
    python -m scripts.permuter.strategy_db stats
"""

from __future__ import annotations

import json
import math
import sqlite3
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

from .repo_paths import get_cache_db_path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DB_PATH = REPO_ROOT / "strategy.db"
CACHE_DB_PATH = get_cache_db_path()
BASELINES_DIR = REPO_ROOT / "build" / "373307D9" / "baselines"


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class StrategyRecord:
    """A learned strategy: pattern X works for diagnosis Y in unit category Z."""

    pattern: str
    unit_category: str  # e.g. "system/rndobj", "system/ui"
    diagnosis_category: str  # e.g. "regswap", "structural", "prologue", "mixed"
    win_count: int
    total_count: int
    avg_delta: float
    to_100_count: int
    example_symbols: list[str] = field(default_factory=list)

    @property
    def win_rate(self) -> float:
        return self.win_count / self.total_count if self.total_count > 0 else 0.0

    @property
    def to_100_rate(self) -> float:
        return self.to_100_count / self.total_count if self.total_count > 0 else 0.0


@dataclass
class StrategyRecommendation:
    """Prioritized pattern recommendation for a specific function context."""

    pattern: str
    priority_boost: float  # Multiplier for pattern priority (1.0 = neutral)
    confidence: float  # 0.0-1.0
    reason: str
    historical_win_rate: float
    historical_count: int


# ---------------------------------------------------------------------------
# Mining classifiers (maps mined pattern names to permuter pattern names)
# ---------------------------------------------------------------------------

# Map mine_patterns.py classifications → permuter pattern names.
# Only include patterns the permuter can actually apply automatically.
# Excluded: scope_change (manual body writing), header_include_change (manual),
#   milo_macro (manual MILO_ASSERT edits), native_guard (manual #ifdef),
#   field_rename (manual), body_removal (manual), struct_type_fix (manual),
#   default_value_fix (manual), milo_fail_simplify (manual),
#   symbol_inline (manual), iterator_to_index (manual)
MINED_TO_PERMUTER = {
    "signed_unsigned": "signed_unsigned",
    "int_cast": "cast_insertion",
    "comparison_flip": "comparison_flip",
    "branch_polarity": "branch_polarity",
    "ternary_swap": "ternary_swap",
    "variable_extraction": "variable_extraction",
    "declaration_reorder": "declaration_reorder",
    "member_ref_bind": "member_ref_bind",
    "early_return_merge": "early_return_merge",
    "statement_reorder": "statement_reorder",
    "and_split": "and_split",
    "single_return": "single_return",
    "float_double": "float_double_literal",
    "temp_elimination": "temp_elimination",
    "bool_cast": "bool_cast",
    "fma_reorder": "fma_reorder",
    "null_guard_insert": "null_guard_insert",
    "condition_rewrite": "guard_to_nested",
    "conditional_split": "comparison_equivalence",
    "empty_size_swap": "comparison_equivalence",
    "assert_line_fix": "assert_line_fix",
    "push_back_move": "commutative_swap",
    "noinline_or_pragma": "noinline_stub",
}

# Patterns that represent manual work, not automatable by the permuter.
# These are filtered out during bulk_load_from_mining.
_MANUAL_ONLY_PATTERNS = frozenset({
    "scope_change", "header_include_change", "milo_macro", "native_guard",
    "field_rename", "body_removal", "struct_type_fix", "default_value_fix",
    "milo_fail_simplify", "symbol_inline", "iterator_to_index",
    "accessor_change", "member_access_extraction",
})

# Diagnosis category from mismatch profile
def classify_diagnosis_category(diagnosis_info: dict) -> str:
    """Classify a diagnosis profile into a category string."""
    has_regswap = diagnosis_info.get("has_regswap", False)
    has_structural = diagnosis_info.get("has_structural", False)
    has_prologue = diagnosis_info.get("has_prologue", False)
    has_offset = diagnosis_info.get("has_offset", False)

    categories = []
    if has_regswap:
        categories.append("regswap")
    if has_structural:
        categories.append("structural")
    if has_prologue:
        categories.append("prologue")
    if has_offset:
        categories.append("offset")

    if not categories:
        return "clean"
    if len(categories) == 1:
        return categories[0]
    return "mixed"


def unit_category(unit_name: str) -> str:
    """Extract unit category from full unit path.

    'default/system/rndobj/Trans' -> 'system/rndobj'
    """
    name = unit_name
    if name.startswith("default/"):
        name = name[len("default/"):]
    parts = name.split("/")
    if len(parts) >= 2:
        return "/".join(parts[:2])
    return parts[0] if parts else "unknown"


# ---------------------------------------------------------------------------
# Database schema and operations
# ---------------------------------------------------------------------------

_SCHEMA = """
CREATE TABLE IF NOT EXISTS strategy (
    pattern TEXT NOT NULL,
    unit_category TEXT NOT NULL,
    diagnosis_category TEXT NOT NULL DEFAULT 'unknown',
    win_count INTEGER NOT NULL DEFAULT 0,
    total_count INTEGER NOT NULL DEFAULT 0,
    avg_delta REAL NOT NULL DEFAULT 0.0,
    to_100_count INTEGER NOT NULL DEFAULT 0,
    examples TEXT NOT NULL DEFAULT '[]',
    PRIMARY KEY (pattern, unit_category, diagnosis_category)
);

CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_strategy_pattern ON strategy(pattern);
CREATE INDEX IF NOT EXISTS idx_strategy_unit ON strategy(unit_category);
CREATE INDEX IF NOT EXISTS idx_strategy_diag ON strategy(diagnosis_category);
"""


class StrategyDB:
    """SQLite-backed strategy database."""

    def __init__(self, db_path: Path = DB_PATH):
        self.db_path = db_path
        self._conn: Optional[sqlite3.Connection] = None

    def _connect(self) -> sqlite3.Connection:
        if self._conn is None:
            self._conn = sqlite3.connect(str(self.db_path))
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.executescript(_SCHEMA)
        return self._conn

    def close(self):
        if self._conn:
            self._conn.close()
            self._conn = None

    def __enter__(self):
        self._connect()
        return self

    def __exit__(self, *args):
        self.close()

    # -- Write operations --

    def upsert_strategy(
        self,
        pattern: str,
        unit_cat: str,
        diag_cat: str,
        delta: float,
        reached_100: bool,
        symbol: str = "",
    ):
        """Record one pattern application outcome."""
        conn = self._connect()
        conn.execute("""
            INSERT INTO strategy (pattern, unit_category, diagnosis_category,
                                  win_count, total_count, avg_delta, to_100_count, examples)
            VALUES (?, ?, ?, 1, 1, ?, ?, ?)
            ON CONFLICT(pattern, unit_category, diagnosis_category) DO UPDATE SET
                win_count = win_count + 1,
                total_count = total_count + 1,
                avg_delta = (avg_delta * total_count + ?) / (total_count + 1),
                to_100_count = to_100_count + ?,
                examples = CASE
                    WHEN length(examples) < 500
                    THEN json_insert(examples, '$[#]', ?)
                    ELSE examples
                END
        """, (
            pattern, unit_cat, diag_cat,
            delta, 1 if reached_100 else 0,
            json.dumps([symbol]) if symbol else "[]",
            # ON CONFLICT params:
            delta, 1 if reached_100 else 0, symbol,
        ))
        conn.commit()

    def bulk_load_from_mining(self, mining_records: list[dict]):
        """Load strategy records from mine_patterns.py JSON output.

        Each record has: symbol, unit, old_pct, new_pct, delta, patterns[{pattern, confidence}]
        """
        conn = self._connect()

        # Clear existing data for fresh rebuild
        conn.execute("DELETE FROM strategy")
        conn.execute("DELETE FROM metadata")

        # Aggregate: (permuter_pattern, unit_cat, diag_cat) -> stats
        agg: dict[tuple[str, str, str], dict] = defaultdict(
            lambda: {"wins": 0, "total": 0, "deltas": [], "to_100": 0, "examples": []}
        )

        for rec in mining_records:
            unit_cat = unit_category(rec.get("unit", ""))
            reached_100 = rec.get("new_pct", 0) >= 100.0
            delta = rec.get("delta", 0.0)

            for p in rec.get("patterns", []):
                mined_name = p["pattern"]

                # Skip manual-only patterns the permuter can't replicate
                if mined_name in _MANUAL_ONLY_PATTERNS:
                    continue

                # Map to permuter pattern name
                permuter_name = MINED_TO_PERMUTER.get(mined_name, mined_name)

                # Use 'unknown' for diagnosis since mining doesn't have diagnosis data
                key = (permuter_name, unit_cat, "unknown")
                stats = agg[key]
                stats["wins"] += 1
                stats["total"] += 1
                stats["deltas"].append(delta)
                if reached_100:
                    stats["to_100"] += 1
                sym = rec.get("symbol", "")
                if sym and len(stats["examples"]) < 5:
                    stats["examples"].append(sym)

        # Write aggregated records
        for (pattern, unit_cat, diag_cat), stats in agg.items():
            avg_delta = sum(stats["deltas"]) / len(stats["deltas"]) if stats["deltas"] else 0.0
            conn.execute("""
                INSERT OR REPLACE INTO strategy
                    (pattern, unit_category, diagnosis_category,
                     win_count, total_count, avg_delta, to_100_count, examples)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                pattern, unit_cat, diag_cat,
                stats["wins"], stats["total"], avg_delta,
                stats["to_100"], json.dumps(stats["examples"][:5]),
            ))

        # Record metadata
        conn.execute("""
            INSERT OR REPLACE INTO metadata (key, value)
            VALUES ('mining_records_loaded', ?)
        """, (str(len(mining_records)),))
        conn.execute("""
            INSERT OR REPLACE INTO metadata (key, value)
            VALUES ('strategy_count', ?)
        """, (str(len(agg)),))

        conn.commit()
        return len(agg)

    def bulk_load_from_pattern_runs(self, cache_db_path: Path) -> int:
        """Load strategy records by mining permuter_cache.db's pattern_runs table.

        Aggregates historical pattern outcomes grouped by (pattern, unit_category,
        diagnosis_category), computing win rates, average deltas, and to-100 counts.
        Unit paths are categorized via unit_category() before grouping, so
        different source paths within the same subsystem are merged.

        Args:
            cache_db_path: Path to permuter_cache.db (opened read-only).

        Returns:
            Number of strategy records inserted/updated.
        """
        if not cache_db_path.exists():
            return 0

        # Open cache DB read-only
        cache_uri = f"file:{cache_db_path}?mode=ro"
        cache_conn = sqlite3.connect(cache_uri, uri=True)
        cache_conn.row_factory = sqlite3.Row

        # Verify table exists
        exists = cache_conn.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='pattern_runs'"
        ).fetchone()
        if not exists:
            cache_conn.close()
            return 0

        # Fetch all rows — aggregation happens in Python so unit_category()
        # can normalise the raw unit paths before grouping.
        rows = cache_conn.execute("""
            SELECT
                pattern,
                COALESCE(unit, '') as unit,
                COALESCE(diagnosis_category, 'unknown') as diag_cat,
                won,
                best_delta,
                final_pct,
                symbol
            FROM pattern_runs
        """).fetchall()
        cache_conn.close()

        if not rows:
            return 0

        # Aggregate in Python: (pattern, unit_cat, diag_cat) -> stats
        agg: dict[tuple[str, str, str], dict] = defaultdict(
            lambda: {
                "wins": 0, "total": 0, "deltas": [],
                "to_100": 0, "examples": [],
            }
        )

        for row in rows:
            pattern = row["pattern"]
            unit_cat = unit_category(row["unit"]) if row["unit"] else "unknown"
            diag_cat = row["diag_cat"]
            key = (pattern, unit_cat, diag_cat)
            stats = agg[key]
            stats["total"] += 1
            if row["won"]:
                stats["wins"] += 1
                sym = row["symbol"]
                if sym and len(stats["examples"]) < 5:
                    stats["examples"].append(sym)
            stats["deltas"].append(row["best_delta"] or 0.0)
            if (row["final_pct"] or 0.0) >= 100.0:
                stats["to_100"] += 1

        conn = self._connect()
        count = 0

        for (pattern, unit_cat, diag_cat), stats in agg.items():
            avg_delta = (
                sum(stats["deltas"]) / len(stats["deltas"])
                if stats["deltas"] else 0.0
            )
            conn.execute("""
                INSERT INTO strategy
                    (pattern, unit_category, diagnosis_category,
                     win_count, total_count, avg_delta, to_100_count, examples)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(pattern, unit_category, diagnosis_category) DO UPDATE SET
                    win_count = excluded.win_count,
                    total_count = excluded.total_count,
                    avg_delta = excluded.avg_delta,
                    to_100_count = excluded.to_100_count,
                    examples = excluded.examples
            """, (
                pattern, unit_cat, diag_cat,
                stats["wins"], stats["total"], avg_delta,
                stats["to_100"], json.dumps(stats["examples"]),
            ))
            count += 1

        # Record metadata
        total_runs = len(rows)
        conn.execute("""
            INSERT OR REPLACE INTO metadata (key, value)
            VALUES ('pattern_runs_loaded', ?)
        """, (str(total_runs),))
        conn.execute("""
            INSERT OR REPLACE INTO metadata (key, value)
            VALUES ('strategy_count_from_runs', ?)
        """, (str(count),))

        conn.commit()
        return count

    # -- Read operations --

    def lookup(
        self,
        unit_cat: str | None = None,
        diag_cat: str | None = None,
        min_wins: int = 2,
    ) -> list[StrategyRecord]:
        """Look up strategies matching the given criteria."""
        conn = self._connect()
        conditions = ["win_count >= ?"]
        params: list = [min_wins]

        if unit_cat:
            conditions.append("unit_category = ?")
            params.append(unit_cat)
        if diag_cat:
            conditions.append("diagnosis_category = ?")
            params.append(diag_cat)

        where = " AND ".join(conditions)
        rows = conn.execute(f"""
            SELECT pattern, unit_category, diagnosis_category,
                   win_count, total_count, avg_delta, to_100_count, examples
            FROM strategy
            WHERE {where}
            ORDER BY win_count DESC
        """, params).fetchall()

        results = []
        for row in rows:
            examples = json.loads(row[7]) if row[7] else []
            results.append(StrategyRecord(
                pattern=row[0],
                unit_category=row[1],
                diagnosis_category=row[2],
                win_count=row[3],
                total_count=row[4],
                avg_delta=row[5],
                to_100_count=row[6],
                example_symbols=examples,
            ))
        return results

    def recommend_patterns(
        self,
        unit_cat: str,
        diag_cat: str | None = None,
        top_k: int = 10,
    ) -> list[StrategyRecommendation]:
        """Get prioritized pattern recommendations for a function context.

        Returns boost multipliers based on historical win rates.
        """
        # Get strategies for this unit category
        strategies = self.lookup(unit_cat=unit_cat, diag_cat=diag_cat, min_wins=1)

        # Also get cross-unit strategies (patterns that work everywhere)
        all_strategies = self.lookup(min_wins=5)

        # Build pattern scores.
        # Note: mining data only has positive examples (improvements), so
        # win_rate is always 1.0. We rank by COUNT instead — more historical
        # wins in this unit = stronger signal to try this pattern.
        pattern_scores: dict[str, dict] = {}

        # Cross-unit baseline (weak signal, saturates quickly)
        for s in all_strategies:
            if s.pattern not in pattern_scores:
                pattern_scores[s.pattern] = {
                    "unit_count": 0,
                    "cross_count": 0,
                    "reasons": [],
                    "to_100": 0,
                    "avg_delta": 0.0,
                }
            score = pattern_scores[s.pattern]
            score["cross_count"] += s.win_count

        # Unit-specific strategies (strong signal)
        for s in strategies:
            if s.pattern not in pattern_scores:
                pattern_scores[s.pattern] = {
                    "unit_count": 0,
                    "cross_count": 0,
                    "reasons": [],
                    "to_100": 0,
                    "avg_delta": 0.0,
                }
            score = pattern_scores[s.pattern]
            score["unit_count"] = s.win_count
            score["to_100"] = s.to_100_count
            score["avg_delta"] = s.avg_delta
            score["reasons"].append(
                f"unit {unit_cat}: {s.win_count} wins, "
                f"avg_delta={s.avg_delta:+.1f}%, to_100={s.to_100_count}"
            )

        # Convert to recommendations
        recs = []
        for pattern, score in pattern_scores.items():
            unit_ct = score["unit_count"]
            cross_ct = score["cross_count"]
            # Confidence: unit-specific count matters most (capped at 0.9)
            # Cross-unit gives a small baseline (capped at 0.3)
            confidence = min(0.9, unit_ct / 20) + min(0.1, cross_ct / 500)
            # Priority boost: 1.0 (neutral) to 1.5 (strong historical signal)
            # Based on unit-specific count, log-scaled to avoid runaway boosts
            boost = 1.0 + min(0.5, 0.15 * math.log1p(unit_ct))
            recs.append(StrategyRecommendation(
                pattern=pattern,
                priority_boost=boost,
                confidence=confidence,
                reason="; ".join(score["reasons"]) if score["reasons"] else
                       f"cross-unit only: {cross_ct} wins",
                historical_win_rate=1.0,  # mining only has positives
                historical_count=unit_ct or cross_ct,
            ))

        recs.sort(key=lambda r: (-r.priority_boost, -r.historical_count))
        return recs[:top_k]

    def get_stats(self) -> dict:
        """Get database statistics."""
        conn = self._connect()
        total = conn.execute("SELECT COUNT(*) FROM strategy").fetchone()[0]
        patterns = conn.execute(
            "SELECT COUNT(DISTINCT pattern) FROM strategy"
        ).fetchone()[0]
        units = conn.execute(
            "SELECT COUNT(DISTINCT unit_category) FROM strategy"
        ).fetchone()[0]
        total_wins = conn.execute(
            "SELECT SUM(win_count) FROM strategy"
        ).fetchone()[0] or 0

        # Top patterns by total wins
        top_patterns = conn.execute("""
            SELECT pattern, SUM(win_count) as wins, SUM(total_count) as total,
                   AVG(avg_delta) as avg_d, SUM(to_100_count) as to100
            FROM strategy
            GROUP BY pattern
            ORDER BY wins DESC
            LIMIT 15
        """).fetchall()

        # Top unit categories
        top_units = conn.execute("""
            SELECT unit_category, SUM(win_count) as wins,
                   COUNT(DISTINCT pattern) as pattern_count
            FROM strategy
            GROUP BY unit_category
            ORDER BY wins DESC
            LIMIT 10
        """).fetchall()

        return {
            "total_records": total,
            "unique_patterns": patterns,
            "unique_units": units,
            "total_wins": total_wins,
            "top_patterns": [
                {"pattern": r[0], "wins": r[1], "total": r[2],
                 "avg_delta": round(r[3], 1), "to_100": r[4]}
                for r in top_patterns
            ],
            "top_units": [
                {"unit": r[0], "wins": r[1], "patterns": r[2]}
                for r in top_units
            ],
        }


# ---------------------------------------------------------------------------
# Integration with beam search
# ---------------------------------------------------------------------------

def apply_strategy_boosts(
    round_hints: "RoundHints",
    unit_cat: str,
    diag_cat: str | None = None,
    db_path: Path = DB_PATH,
) -> list[StrategyRecommendation]:
    """Apply strategy database recommendations to RoundHints.

    Call this before pattern generation in beam search or hill climber.
    Returns the recommendations applied for logging.
    """
    if not db_path.exists():
        return []

    try:
        db = StrategyDB(db_path)
        recs = db.recommend_patterns(unit_cat, diag_cat)
        db.close()
    except Exception:
        return []

    for rec in recs:
        if rec.priority_boost > 1.2:
            round_hints.atlas_boost_patterns.add(rec.pattern)

    return recs


def record_permuter_result(
    result: "HillClimbResult",
    unit: str | None = None,
    db_path: Path = DB_PATH,
):
    """Record a permuter run result into the strategy database.

    Creates a feedback loop: the permuter's own wins improve future
    recommendations. Call this after each successful hill_climb/beam_search.
    """
    if result.total_delta <= 0.0:
        return  # Only record improvements

    winning = result.winning_pattern
    if not winning:
        return

    unit_cat = unit_category(unit) if unit else "unknown"
    reached_100 = result.final_percent >= 100.0

    # Split composed pattern names
    pattern_names = []
    for prefix in ("compose:", "chain:", "crosscompose:", "merge:", "evo_cross:", "evo_mut:"):
        if winning.startswith(prefix):
            _, parts = winning.split(":", 1)
            pattern_names = parts.split("+")
            break
    if not pattern_names:
        pattern_names = [winning]

    try:
        db = StrategyDB(db_path)
        for pname in pattern_names:
            db.upsert_strategy(
                pattern=pname,
                unit_cat=unit_cat,
                diag_cat="permuter_win",  # Distinct from mining data
                delta=result.total_delta,
                reached_100=reached_100,
                symbol=result.symbol,
            )
        db.close()
    except Exception:
        pass  # Recording is best-effort


def bulk_load_from_pattern_runs(
    cache_db_path: Path = CACHE_DB_PATH,
    strategy_db_path: Path = DB_PATH,
) -> int:
    """Mine permuter_cache.db pattern_runs into strategy.db.

    Convenience wrapper around StrategyDB.bulk_load_from_pattern_runs().

    Args:
        cache_db_path: Path to permuter_cache.db.
        strategy_db_path: Path to strategy.db (created if needed).

    Returns:
        Number of strategy records inserted/updated.
    """
    db = StrategyDB(strategy_db_path)
    try:
        return db.bulk_load_from_pattern_runs(cache_db_path)
    finally:
        db.close()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def cmd_build(args):
    """Build strategy database from mined patterns."""
    import subprocess

    print("Mining patterns from baseline history...")
    result = subprocess.run(
        [sys.executable, str(REPO_ROOT / "scripts" / "analysis" / "mine_patterns.py"),
         "--json"],
        capture_output=True, text=True, timeout=300
    )
    if result.returncode != 0:
        print(f"Error running mine_patterns: {result.stderr[:500]}", file=sys.stderr)
        sys.exit(1)

    records = json.loads(result.stdout)
    print(f"Loaded {len(records)} improvement records from history")

    db = StrategyDB()
    count = db.bulk_load_from_mining(records)
    db.close()

    print(f"Created {count} strategy records in {DB_PATH}")
    print()

    # Show stats
    db = StrategyDB()
    stats = db.get_stats()
    db.close()
    _print_stats(stats)


def cmd_build_from_runs(args):
    """Build strategy database by mining permuter_cache.db pattern_runs."""
    cache_path = Path(args.cache_db) if hasattr(args, "cache_db") and args.cache_db else CACHE_DB_PATH
    if not cache_path.exists():
        print(f"Cache database not found: {cache_path}", file=sys.stderr)
        sys.exit(1)

    print(f"Mining pattern_runs from {cache_path}...")
    count = bulk_load_from_pattern_runs(cache_path)
    print(f"Loaded {count} strategy records into {DB_PATH}")
    print()

    # Show stats
    db = StrategyDB()
    stats = db.get_stats()
    db.close()
    _print_stats(stats)


def cmd_query(args):
    """Query strategies for a function context."""
    db = StrategyDB()
    recs = db.recommend_patterns(
        unit_cat=args.unit,
        diag_cat=args.diagnosis if hasattr(args, "diagnosis") else None,
        top_k=args.top_k if hasattr(args, "top_k") else 10,
    )
    db.close()

    if not recs:
        print(f"No strategies found for unit={args.unit}")
        return

    print(f"Strategy recommendations for unit={args.unit}:")
    print(f"{'Pattern':30s} {'Boost':>6s} {'Conf':>5s} {'WinRate':>7s} {'Count':>6s} Reason")
    print("-" * 100)
    for r in recs:
        print(
            f"{r.pattern:30s} {r.priority_boost:6.2f} {r.confidence:5.2f} "
            f"{r.historical_win_rate:6.1%} {r.historical_count:6d} {r.reason[:50]}"
        )


def cmd_stats(args):
    """Show database statistics."""
    db = StrategyDB()
    stats = db.get_stats()
    db.close()
    _print_stats(stats)


def _print_stats(stats: dict):
    """Pretty-print database statistics."""
    print(f"Strategy Database: {stats['total_records']} records, "
          f"{stats['unique_patterns']} patterns, {stats['unique_units']} units, "
          f"{stats['total_wins']} total wins")
    print()

    print("Top patterns by historical wins:")
    print(f"  {'Pattern':30s} {'Wins':>6s} {'Total':>6s} {'AvgΔ':>7s} {'To100':>6s}")
    for p in stats["top_patterns"]:
        print(f"  {p['pattern']:30s} {p['wins']:6d} {p['total']:6d} "
              f"{p['avg_delta']:+6.1f}% {p['to_100']:6d}")
    print()

    print("Top unit categories:")
    for u in stats["top_units"]:
        print(f"  {u['unit']:35s} wins={u['wins']:5d} patterns={u['patterns']:3d}")


def main():
    import argparse

    parser = argparse.ArgumentParser(description="Cross-function strategy database")
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("build", help="Build database from mined patterns")

    bfr = sub.add_parser("build-from-runs", help="Build database from permuter_cache.db pattern_runs")
    bfr.add_argument("--cache-db", help="Path to permuter_cache.db (default: repo root)")

    q = sub.add_parser("query", help="Query strategies for a function")
    q.add_argument("--unit", required=True, help="Unit category (e.g. system/rndobj)")
    q.add_argument("--diagnosis", help="Diagnosis category")
    q.add_argument("--top-k", type=int, default=10)

    sub.add_parser("stats", help="Show database statistics")

    args = parser.parse_args()
    if args.command == "build":
        cmd_build(args)
    elif args.command == "build-from-runs":
        cmd_build_from_runs(args)
    elif args.command == "query":
        cmd_query(args)
    elif args.command == "stats":
        cmd_stats(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
