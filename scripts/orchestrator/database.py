"""Database module for DC3 Decomp Orchestrator.

Handles SQLite database for persistent state tracking of functions,
attempts, and worktrees.
"""

import json
import sqlite3
from datetime import datetime
from pathlib import Path
from typing import Any

# Database path (relative to repo root)
DEFAULT_DB_PATH = "decomp.db"

# Schema version for migrations
SCHEMA_VERSION = 4

SCHEMA = """
-- Schema version tracking
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER PRIMARY KEY
);

-- Core function tracking
CREATE TABLE IF NOT EXISTS functions (
    id INTEGER PRIMARY KEY,
    symbol TEXT NOT NULL UNIQUE,        -- Mangled name
    demangled TEXT,                     -- Human-readable
    unit TEXT,                          -- "src/system/char/Char.cpp"
    size INTEGER,

    current_percent REAL,               -- Latest match %
    best_percent REAL,                  -- Best ever match %
    verdict TEXT,                       -- COMPLETE, AT_LIMIT, etc.

    locked_by TEXT,                     -- Session ID (prevents conflicts)
    locked_at TIMESTAMP,

    attempt_count INTEGER DEFAULT 0,
    last_model TEXT,                    -- haiku, sonnet, opus
    next_model TEXT,                    -- What to try next

    source_patch TEXT,                  -- Successful diff

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Attempt history (learning + debugging)
CREATE TABLE IF NOT EXISTS attempts (
    id INTEGER PRIMARY KEY,
    function_id INTEGER REFERENCES functions(id),
    session_id TEXT,
    model TEXT,

    started_at TIMESTAMP,
    finished_at TIMESTAMP,

    exit_status TEXT,                   -- success, stuck, error
    start_percent REAL,
    end_percent REAL,
    verdict TEXT,

    patch TEXT,                         -- What was tried
    notes TEXT,                         -- Agent's summary
    iterations INTEGER,                 -- How many tool calls

    -- Token usage tracking (v2 schema)
    input_tokens INTEGER,               -- API input tokens
    output_tokens INTEGER,              -- API output tokens
    cache_read_tokens INTEGER,          -- Cache read tokens
    cache_creation_tokens INTEGER,      -- Cache creation tokens
    actual_cost_usd REAL,               -- Actual cost from SDK
    duration_ms INTEGER,                -- Total duration in ms

    -- A/B testing enrichment tracking (v4 schema)
    enrichment_flags TEXT,              -- JSON: {"diff_patterns": true, "function_types": false, ...}

    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Worktree pool tracking
CREATE TABLE IF NOT EXISTS worktrees (
    id INTEGER PRIMARY KEY,
    path TEXT NOT NULL UNIQUE,
    session_id TEXT,
    status TEXT,                        -- available, in_use, dirty
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Indexes for common queries
CREATE INDEX IF NOT EXISTS idx_functions_verdict ON functions(verdict);
CREATE INDEX IF NOT EXISTS idx_functions_locked ON functions(locked_by);
CREATE INDEX IF NOT EXISTS idx_functions_unit ON functions(unit);
CREATE INDEX IF NOT EXISTS idx_functions_percent ON functions(current_percent);
CREATE INDEX IF NOT EXISTS idx_attempts_function ON attempts(function_id);
CREATE INDEX IF NOT EXISTS idx_attempts_session ON attempts(session_id);
CREATE INDEX IF NOT EXISTS idx_worktrees_status ON worktrees(status);

-- RB3 file pairing for cross-reference assistance
CREATE TABLE IF NOT EXISTS file_pairs (
    id INTEGER PRIMARY KEY,
    dc3_unit TEXT NOT NULL UNIQUE,         -- DC3 unit path (e.g., "default/system/char/CharBones")
    rb3_file TEXT,                          -- RB3 source file path (absolute)
    compatibility_score REAL,               -- Overlapping functions / max(dc3, rb3) functions
    function_overlap INTEGER,               -- Number of functions with matching names
    dc3_function_count INTEGER,             -- Total functions in DC3 unit
    rb3_function_count INTEGER,             -- Total functions in RB3 file
    has_rb2_dwarf BOOLEAN DEFAULT 0,        -- Has class info in RB2 DWARF dump
    last_synced TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_file_pairs_compat ON file_pairs(compatibility_score DESC);
CREATE INDEX IF NOT EXISTS idx_file_pairs_dc3_unit ON file_pairs(dc3_unit);
"""


def get_connection(db_path: str | Path = DEFAULT_DB_PATH) -> sqlite3.Connection:
    """Get a database connection with row factory enabled."""
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def init_database(db_path: str | Path = DEFAULT_DB_PATH) -> sqlite3.Connection:
    """Initialize database with schema. Safe to call multiple times."""
    conn = get_connection(db_path)

    # Check if already initialized
    cursor = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
    )
    if cursor.fetchone() is None:
        # Fresh database - create schema
        conn.executescript(SCHEMA)
        conn.execute("INSERT INTO schema_version (version) VALUES (?)", (SCHEMA_VERSION,))
        conn.commit()
        print(f"Initialized database at {db_path}")
    else:
        # Check version for migrations
        version = conn.execute("SELECT version FROM schema_version").fetchone()[0]
        if version < SCHEMA_VERSION:
            _run_migrations(conn, version, SCHEMA_VERSION)

    return conn


def _run_migrations(conn: sqlite3.Connection, from_version: int, to_version: int) -> None:
    """Run database migrations from from_version to to_version."""
    print(f"Running database migrations: v{from_version} -> v{to_version}")

    if from_version < 2 <= to_version:
        # Migration v1 -> v2: Add token tracking columns to attempts table
        print("  Migration v2: Adding token usage tracking columns...")
        migrations = [
            "ALTER TABLE attempts ADD COLUMN input_tokens INTEGER",
            "ALTER TABLE attempts ADD COLUMN output_tokens INTEGER",
            "ALTER TABLE attempts ADD COLUMN cache_read_tokens INTEGER",
            "ALTER TABLE attempts ADD COLUMN cache_creation_tokens INTEGER",
            "ALTER TABLE attempts ADD COLUMN actual_cost_usd REAL",
            "ALTER TABLE attempts ADD COLUMN duration_ms INTEGER",
        ]
        for sql in migrations:
            try:
                conn.execute(sql)
            except sqlite3.OperationalError as e:
                # Column may already exist (partial migration)
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 3 <= to_version:
        # Migration v2 -> v3: Add file_pairs table for RB3 cross-reference
        print("  Migration v3: Adding file_pairs table for RB3 integration...")
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS file_pairs (
                id INTEGER PRIMARY KEY,
                dc3_unit TEXT NOT NULL UNIQUE,
                rb3_file TEXT,
                compatibility_score REAL,
                function_overlap INTEGER,
                dc3_function_count INTEGER,
                rb3_function_count INTEGER,
                has_rb2_dwarf BOOLEAN DEFAULT 0,
                last_synced TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_file_pairs_compat ON file_pairs(compatibility_score DESC);
            CREATE INDEX IF NOT EXISTS idx_file_pairs_dc3_unit ON file_pairs(dc3_unit);
        """)

    if from_version < 4 <= to_version:
        # Migration v3 -> v4: Add enrichment_flags for A/B testing
        print("  Migration v4: Adding enrichment_flags column for A/B testing...")
        try:
            conn.execute("ALTER TABLE attempts ADD COLUMN enrichment_flags TEXT")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise
        # Add index for querying by enrichment experiment
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_attempts_enrichment
            ON attempts(enrichment_flags)
        """)

    # Update schema version
    conn.execute("UPDATE schema_version SET version = ?", (to_version,))
    conn.commit()
    print(f"  Migration complete. Database at v{to_version}")


def ingest_report(
    report_path: str | Path,
    db_path: str | Path = DEFAULT_DB_PATH,
    update_existing: bool = True,
) -> dict[str, int]:
    """
    Parse report.json and populate/update the functions table.

    Args:
        report_path: Path to build/373307D9/report.json
        db_path: Path to SQLite database
        update_existing: If True, update existing functions. If False, skip them.

    Returns:
        Dict with counts: inserted, updated, skipped
    """
    conn = init_database(db_path)

    with open(report_path) as f:
        report = json.load(f)

    inserted = 0
    updated = 0
    skipped = 0

    # report.json structure:
    # { "units": [ { "name": "...", "functions": [ { ... } ] } ] }
    for unit in report.get("units", []):
        unit_name = unit.get("name", "")

        for func in unit.get("functions", []):
            symbol = func.get("symbol", func.get("name", ""))
            if not symbol:
                continue

            demangled = func.get("demangled", func.get("name", ""))
            size = func.get("size", 0)

            # Calculate match percentage from fuzzy_match_percent or match_percent
            percent = func.get("fuzzy_match_percent")
            if percent is None:
                percent = func.get("match_percent")

            # Determine verdict based on matching
            if percent == 100.0:
                verdict = "COMPLETE"
            elif percent is not None and percent >= 99.0:
                verdict = "NEAR_COMPLETE"
            else:
                verdict = None

            # Check if function exists
            existing = conn.execute(
                "SELECT id, current_percent, best_percent FROM functions WHERE symbol = ?",
                (symbol,),
            ).fetchone()

            if existing:
                if update_existing:
                    # Update if we have better data
                    best = existing["best_percent"] or 0
                    if percent is not None and percent > best:
                        best = percent

                    conn.execute(
                        """
                        UPDATE functions SET
                            demangled = COALESCE(?, demangled),
                            unit = COALESCE(?, unit),
                            size = COALESCE(?, size),
                            current_percent = ?,
                            best_percent = ?,
                            verdict = COALESCE(?, verdict),
                            updated_at = CURRENT_TIMESTAMP
                        WHERE id = ?
                        """,
                        (demangled, unit_name, size, percent, best, verdict, existing["id"]),
                    )
                    updated += 1
                else:
                    skipped += 1
            else:
                # Insert new function
                conn.execute(
                    """
                    INSERT INTO functions
                        (symbol, demangled, unit, size, current_percent, best_percent, verdict)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    (symbol, demangled, unit_name, size, percent, percent, verdict),
                )
                inserted += 1

    conn.commit()
    return {"inserted": inserted, "updated": updated, "skipped": skipped}


def get_function_by_symbol(
    symbol: str, db_path: str | Path = DEFAULT_DB_PATH
) -> dict[str, Any] | None:
    """Get function by symbol name."""
    conn = get_connection(db_path)
    row = conn.execute(
        """
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, locked_by, locked_at, attempt_count, last_model, next_model
        FROM functions
        WHERE symbol = ?
        """,
        (symbol,),
    ).fetchone()

    if row:
        return dict(row)
    return None


def get_next_function(
    pattern: str | list[str] = "*",
    min_percent: float = 0,
    max_percent: float = 100,
    exclude_locked: bool = True,
    exclude_complete: bool = True,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> dict[str, Any] | None:
    """
    Get next function to work on based on criteria.

    Args:
        pattern: Glob pattern(s) for unit (e.g., "src/system/char/*" or list of patterns)
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage
        exclude_locked: Skip functions locked by other agents
        exclude_complete: Skip functions with verdict COMPLETE or AT_LIMIT
        db_path: Database path

    Returns:
        Function dict or None if no matches
    """
    conn = get_connection(db_path)

    glob_clause, glob_params = _build_unit_glob_clause(pattern)

    query = f"""
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, locked_by, attempt_count, last_model
        FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
    """
    params: list[Any] = glob_params + [min_percent, max_percent]

    if exclude_locked:
        query += " AND locked_by IS NULL"

    if exclude_complete:
        query += " AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"

    # Order by: non-null percent first, then by descending percent (near-matches first)
    query += """
        ORDER BY
            CASE WHEN current_percent IS NULL THEN 1 ELSE 0 END,
            current_percent DESC
        LIMIT 1
    """

    row = conn.execute(query, params).fetchone()
    if row:
        return dict(row)
    return None


def normalize_unit_pattern(pattern: str) -> str:
    """
    Normalize a unit pattern to match database unit paths.

    Database units use "default/" prefix (e.g., "default/system/char/Char").
    Users may specify:
      - "src/system/char/*" -> "default/system/char/*"
      - "*char*" -> "*char*" (unchanged, wildcards match anywhere)
      - "default/system/*" -> "default/system/*" (unchanged)
    """
    # If pattern starts with "src/", replace with "default/"
    if pattern.startswith("src/"):
        return "default/" + pattern[4:]
    return pattern


def _build_unit_glob_clause(
    patterns: str | list[str],
) -> tuple[str, list[str]]:
    """
    Build a SQL WHERE clause fragment matching one or more unit GLOB patterns.

    Args:
        patterns: Single pattern string or list of pattern strings.

    Returns:
        Tuple of (sql_fragment, params) where sql_fragment is like
        "(unit GLOB ? OR unit GLOB ?)" and params is the normalized patterns.
    """
    if isinstance(patterns, str):
        patterns = [patterns]

    normalized = [normalize_unit_pattern(p) for p in patterns]

    if len(normalized) == 1:
        return "unit GLOB ?", normalized

    clauses = " OR ".join("unit GLOB ?" for _ in normalized)
    return f"({clauses})", normalized


def query_functions(
    pattern: str | list[str] = "*",
    min_percent: float = 0,
    max_percent: float = 100,
    exclude_locked: bool = True,
    exclude_complete: bool = True,
    limit: int = 20,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Query multiple functions matching criteria.

    Returns list of function dicts.
    """
    conn = get_connection(db_path)

    glob_clause, glob_params = _build_unit_glob_clause(pattern)

    query = f"""
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, locked_by, attempt_count
        FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
    """
    params: list[Any] = glob_params + [min_percent, max_percent]

    if exclude_locked:
        query += " AND locked_by IS NULL"

    if exclude_complete:
        query += " AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))"

    query += """
        ORDER BY
            CASE WHEN current_percent IS NULL THEN 1 ELSE 0 END,
            current_percent DESC
        LIMIT ?
    """
    params.append(limit)

    rows = conn.execute(query, params).fetchall()
    return [dict(row) for row in rows]


def lock_function(
    function_id: int, session_id: str, db_path: str | Path = DEFAULT_DB_PATH
) -> bool:
    """
    Lock a function for exclusive work by a session.

    Returns True if lock acquired, False if already locked.
    """
    conn = get_connection(db_path)

    # Check if already locked
    row = conn.execute(
        "SELECT locked_by FROM functions WHERE id = ?", (function_id,)
    ).fetchone()

    if row is None:
        return False  # Function doesn't exist

    if row["locked_by"] is not None and row["locked_by"] != session_id:
        return False  # Locked by someone else

    conn.execute(
        """
        UPDATE functions
        SET locked_by = ?, locked_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (session_id, function_id),
    )
    conn.commit()
    return True


def unlock_function(
    function_id: int, db_path: str | Path = DEFAULT_DB_PATH
) -> None:
    """Release lock on a function."""
    conn = get_connection(db_path)
    conn.execute(
        """
        UPDATE functions
        SET locked_by = NULL, locked_at = NULL, updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (function_id,),
    )
    conn.commit()


def unlock_session(session_id: str, db_path: str | Path = DEFAULT_DB_PATH) -> int:
    """Release all locks held by a session. Returns count of unlocked functions."""
    conn = get_connection(db_path)
    cursor = conn.execute(
        """
        UPDATE functions
        SET locked_by = NULL, locked_at = NULL, updated_at = CURRENT_TIMESTAMP
        WHERE locked_by = ?
        """,
        (session_id,),
    )
    conn.commit()
    return cursor.rowcount


def record_attempt(
    function_id: int,
    session_id: str,
    model: str,
    start_percent: float | None,
    end_percent: float | None,
    exit_status: str,
    verdict: str | None = None,
    patch: str | None = None,
    notes: str | None = None,
    iterations: int | None = None,
    input_tokens: int | None = None,
    output_tokens: int | None = None,
    cache_read_tokens: int | None = None,
    cache_creation_tokens: int | None = None,
    actual_cost_usd: float | None = None,
    duration_ms: int | None = None,
    enrichment_flags: dict | None = None,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> int:
    """
    Record an attempt on a function.

    Args:
        function_id: Database ID of the function
        session_id: Unique session identifier
        model: Model used (haiku, sonnet, opus, etc.)
        start_percent: Match percentage before attempt
        end_percent: Match percentage after attempt
        exit_status: Result status (complete, stuck, error, at_limit)
        verdict: Analysis verdict (COMPLETE, AT_LIMIT, etc.)
        patch: Git diff of changes made
        notes: Agent's summary notes
        iterations: Number of tool calls made
        input_tokens: API input tokens used
        output_tokens: API output tokens used
        cache_read_tokens: Cache read tokens used
        cache_creation_tokens: Cache creation tokens used
        actual_cost_usd: Actual cost from SDK (None for MCP direct calls)
        duration_ms: Total duration in milliseconds
        enrichment_flags: Dict of enrichment experiment assignments
                         e.g., {"diff_patterns": true, "function_types": false}

    Returns the attempt ID.
    """
    conn = get_connection(db_path)

    # Serialize enrichment_flags to JSON if provided
    enrichment_json = json.dumps(enrichment_flags) if enrichment_flags else None

    cursor = conn.execute(
        """
        INSERT INTO attempts
            (function_id, session_id, model, started_at, finished_at,
             start_percent, end_percent, exit_status, verdict, patch, notes, iterations,
             input_tokens, output_tokens, cache_read_tokens, cache_creation_tokens,
             actual_cost_usd, duration_ms, enrichment_flags)
        VALUES (?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            function_id,
            session_id,
            model,
            start_percent,
            end_percent,
            exit_status,
            verdict,
            patch,
            notes,
            iterations,
            input_tokens,
            output_tokens,
            cache_read_tokens,
            cache_creation_tokens,
            actual_cost_usd,
            duration_ms,
            enrichment_json,
        ),
    )

    # Update function's attempt count and last model
    conn.execute(
        """
        UPDATE functions
        SET attempt_count = attempt_count + 1,
            last_model = ?,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (model, function_id),
    )

    conn.commit()
    return cursor.lastrowid


def update_function_status(
    function_id: int,
    current_percent: float | None = None,
    verdict: str | None = None,
    source_patch: str | None = None,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> None:
    """Update function status after an attempt."""
    conn = get_connection(db_path)

    updates = ["updated_at = CURRENT_TIMESTAMP"]
    params: list[Any] = []

    if current_percent is not None:
        updates.append("current_percent = ?")
        params.append(current_percent)

        # Update best_percent if this is better
        updates.append("best_percent = MAX(COALESCE(best_percent, 0), ?)")
        params.append(current_percent)

    if verdict is not None:
        updates.append("verdict = ?")
        params.append(verdict)

    if source_patch is not None:
        updates.append("source_patch = ?")
        params.append(source_patch)

    params.append(function_id)

    conn.execute(
        f"UPDATE functions SET {', '.join(updates)} WHERE id = ?",
        params,
    )
    conn.commit()


def get_last_attempt(
    function_id: int, db_path: str | Path = DEFAULT_DB_PATH
) -> dict[str, Any] | None:
    """Get the most recent attempt for a function."""
    conn = get_connection(db_path)

    row = conn.execute(
        """
        SELECT id, session_id, model, started_at, finished_at,
               start_percent, end_percent, exit_status, verdict, patch, notes, iterations
        FROM attempts
        WHERE function_id = ?
        ORDER BY created_at DESC
        LIMIT 1
        """,
        (function_id,),
    ).fetchone()

    if row:
        return dict(row)
    return None


def get_attempts_for_function(
    function_id: int, limit: int = 10, db_path: str | Path = DEFAULT_DB_PATH
) -> list[dict[str, Any]]:
    """Get attempt history for a function."""
    conn = get_connection(db_path)

    rows = conn.execute(
        """
        SELECT id, session_id, model, started_at, finished_at,
               start_percent, end_percent, exit_status, verdict, notes, iterations
        FROM attempts
        WHERE function_id = ?
        ORDER BY created_at DESC
        LIMIT ?
        """,
        (function_id, limit),
    ).fetchall()

    return [dict(row) for row in rows]


def query_batch_stats(
    pattern: str | list[str] = "*",
    min_percent: float = 0,
    max_percent: float = 100,
    limit: int = 0,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> dict[str, Any]:
    """
    Get statistics about functions that would be targeted by a batch run.

    Returns breakdown of:
    - Total functions matching pattern in database
    - Functions in match percentage range
    - Functions available (not locked, not complete)
    - First-try functions (no previous attempts)
    - Retry functions (have previous attempts)
    - Functions that would be selected (respecting limit)

    Args:
        pattern: Glob pattern(s) for unit (e.g., "src/system/char/*" or list of patterns)
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage
        limit: Max functions to process (0 = unlimited)
        db_path: Database path

    Returns:
        Dict with counts and breakdowns
    """
    conn = get_connection(db_path)

    glob_clause, glob_params = _build_unit_glob_clause(pattern)

    # Count functions matching pattern (total in scope)
    total_matching = conn.execute(
        f"SELECT COUNT(*) FROM functions WHERE {glob_clause}",
        glob_params,
    ).fetchone()[0]

    # Count functions in match percentage range
    in_range = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
        """,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count locked functions in range
    locked = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NOT NULL
        """,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count complete/at_limit functions in range
    excluded_verdict = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND verdict IN ('COMPLETE', 'AT_LIMIT')
        """,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count available functions (not locked, not complete)
    available = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NULL
          AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
        """,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count first-try functions (no attempts yet) among available
    first_tries = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NULL
          AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
          AND attempt_count = 0
        """,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Retries = available - first_tries
    retries = available - first_tries

    # How many will be selected (respecting limit)
    selected = available if limit == 0 else min(available, limit)
    more_available = available > selected if limit > 0 else False

    # Format pattern for display
    display_pattern = pattern if isinstance(pattern, str) else ", ".join(pattern)

    return {
        "pattern": display_pattern,
        "min_percent": min_percent,
        "max_percent": max_percent,
        "limit": limit,
        "total_matching_pattern": total_matching,
        "in_match_range": in_range,
        "locked": locked,
        "excluded_complete": excluded_verdict,
        "available": available,
        "first_tries": first_tries,
        "retries": retries,
        "selected": selected,
        "more_available": more_available,
    }


def get_stats(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """Get overall statistics."""
    conn = get_connection(db_path)

    total = conn.execute("SELECT COUNT(*) FROM functions").fetchone()[0]
    complete = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict = 'COMPLETE'"
    ).fetchone()[0]
    at_limit = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE verdict = 'AT_LIMIT'"
    ).fetchone()[0]
    locked = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE locked_by IS NOT NULL"
    ).fetchone()[0]
    with_percent = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE current_percent IS NOT NULL"
    ).fetchone()[0]
    total_attempts = conn.execute("SELECT COUNT(*) FROM attempts").fetchone()[0]

    # Average match percent (for non-null)
    avg_percent = conn.execute(
        "SELECT AVG(current_percent) FROM functions WHERE current_percent IS NOT NULL"
    ).fetchone()[0]

    return {
        "total_functions": total,
        "complete": complete,
        "at_limit": at_limit,
        "locked": locked,
        "with_percent": with_percent,
        "total_attempts": total_attempts,
        "avg_percent": round(avg_percent, 2) if avg_percent else None,
    }


# ============================================================================
# RB3 File Pairing Functions
# ============================================================================


def upsert_file_pair(
    dc3_unit: str,
    rb3_file: str | None = None,
    compatibility_score: float | None = None,
    function_overlap: int | None = None,
    dc3_function_count: int | None = None,
    rb3_function_count: int | None = None,
    has_rb2_dwarf: bool = False,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> int:
    """
    Insert or update a file pairing record.

    Args:
        dc3_unit: DC3 unit path (e.g., "default/system/char/CharBones")
        rb3_file: Full path to RB3 source file
        compatibility_score: Function overlap ratio (0.0 - 1.0)
        function_overlap: Number of matching function names
        dc3_function_count: Total DC3 functions
        rb3_function_count: Total RB3 functions
        has_rb2_dwarf: Whether RB2 DWARF info is available
        db_path: Database path

    Returns:
        Row ID of the inserted/updated record
    """
    conn = get_connection(db_path)

    # Check if exists
    existing = conn.execute(
        "SELECT id FROM file_pairs WHERE dc3_unit = ?", (dc3_unit,)
    ).fetchone()

    if existing:
        conn.execute(
            """
            UPDATE file_pairs SET
                rb3_file = COALESCE(?, rb3_file),
                compatibility_score = COALESCE(?, compatibility_score),
                function_overlap = COALESCE(?, function_overlap),
                dc3_function_count = COALESCE(?, dc3_function_count),
                rb3_function_count = COALESCE(?, rb3_function_count),
                has_rb2_dwarf = ?,
                last_synced = CURRENT_TIMESTAMP
            WHERE id = ?
            """,
            (
                rb3_file,
                compatibility_score,
                function_overlap,
                dc3_function_count,
                rb3_function_count,
                has_rb2_dwarf,
                existing["id"],
            ),
        )
        conn.commit()
        return existing["id"]
    else:
        cursor = conn.execute(
            """
            INSERT INTO file_pairs
                (dc3_unit, rb3_file, compatibility_score, function_overlap,
                 dc3_function_count, rb3_function_count, has_rb2_dwarf)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                dc3_unit,
                rb3_file,
                compatibility_score,
                function_overlap,
                dc3_function_count,
                rb3_function_count,
                has_rb2_dwarf,
            ),
        )
        conn.commit()
        return cursor.lastrowid


def get_file_pair(dc3_unit: str, db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any] | None:
    """Get file pairing for a DC3 unit."""
    conn = get_connection(db_path)
    row = conn.execute(
        """
        SELECT id, dc3_unit, rb3_file, compatibility_score, function_overlap,
               dc3_function_count, rb3_function_count, has_rb2_dwarf, last_synced
        FROM file_pairs
        WHERE dc3_unit = ?
        """,
        (dc3_unit,),
    ).fetchone()
    return dict(row) if row else None


def query_file_pairs(
    min_compat: float = 0.0,
    pattern: str = "*",
    limit: int = 100,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Query file pairs by compatibility score and pattern.

    Args:
        min_compat: Minimum compatibility score (0.0 - 1.0)
        pattern: Glob pattern for dc3_unit
        limit: Maximum results
        db_path: Database path

    Returns:
        List of file pair dicts, sorted by compatibility descending
    """
    conn = get_connection(db_path)
    normalized_pattern = normalize_unit_pattern(pattern)

    rows = conn.execute(
        """
        SELECT id, dc3_unit, rb3_file, compatibility_score, function_overlap,
               dc3_function_count, rb3_function_count, has_rb2_dwarf, last_synced
        FROM file_pairs
        WHERE dc3_unit GLOB ?
          AND (compatibility_score IS NULL OR compatibility_score >= ?)
        ORDER BY compatibility_score DESC NULLS LAST
        LIMIT ?
        """,
        (normalized_pattern, min_compat, limit),
    ).fetchall()

    return [dict(row) for row in rows]


def get_file_pairs_stats(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """Get statistics about file pairings."""
    conn = get_connection(db_path)

    total = conn.execute("SELECT COUNT(*) FROM file_pairs").fetchone()[0]
    with_rb3 = conn.execute(
        "SELECT COUNT(*) FROM file_pairs WHERE rb3_file IS NOT NULL"
    ).fetchone()[0]
    high_compat = conn.execute(
        "SELECT COUNT(*) FROM file_pairs WHERE compatibility_score >= 0.8"
    ).fetchone()[0]
    has_dwarf = conn.execute(
        "SELECT COUNT(*) FROM file_pairs WHERE has_rb2_dwarf = 1"
    ).fetchone()[0]
    avg_compat = conn.execute(
        "SELECT AVG(compatibility_score) FROM file_pairs WHERE compatibility_score IS NOT NULL"
    ).fetchone()[0]

    return {
        "total_pairs": total,
        "with_rb3_match": with_rb3,
        "high_compatibility": high_compat,  # >= 80%
        "has_rb2_dwarf": has_dwarf,
        "avg_compatibility": round(avg_compat, 3) if avg_compat else None,
    }


# ============================================================================
# Priority-Based Selection (Phase 2 Scoring Infrastructure)
# ============================================================================


def query_functions_by_priority(
    min_priority: float = 0,
    min_percent: float = 0,
    max_percent: float = 100,
    reachable_only: bool = False,
    exclude_locked: bool = True,
    limit: int = 20,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Query functions ordered by priority score from Phase 2 infrastructure.

    Uses the ease × impact × confidence scoring model with pattern-based
    fixability analysis.

    Args:
        min_priority: Minimum priority score (0-100+)
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage (capped at 99.99 to exclude 100%)
        reachable_only: If True, only return functions that can reach 100%
        exclude_locked: Skip functions locked by other agents
        limit: Max results to return
        db_path: Database path

    Returns:
        List of function dicts with priority metadata, sorted by priority desc
    """
    conn = get_connection(db_path)

    # Cap max_percent to exclude 100% functions (those are complete)
    effective_max = min(max_percent, 99.99)

    query = """
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, locked_by, attempt_count,
               priority_score, ease_score, impact_score, confidence_score,
               reachable_100, primary_pattern, has_linker_merged, has_bool_mask
        FROM functions
        WHERE excluded = 0
          AND priority_score >= ?
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent < ?))
          AND (verdict IS NULL OR verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
    """
    params: list[Any] = [min_priority, min_percent, effective_max]

    if reachable_only:
        query += " AND reachable_100 = 1"

    if exclude_locked:
        query += " AND locked_by IS NULL"

    query += """
        ORDER BY priority_score DESC, current_percent DESC
        LIMIT ?
    """
    params.append(limit)

    rows = conn.execute(query, params).fetchall()
    return [dict(row) for row in rows]


def query_functions_for_unit_completion(
    min_completion_pct: float = 70,
    max_completion_pct: float = 100,
    reachable_only: bool = False,
    exclude_locked: bool = True,
    limit: int = 20,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Query incomplete functions from nearly-complete units.

    Targets units that are 70-99% complete to push them to 100%.

    Args:
        min_completion_pct: Minimum unit completion percentage
        max_completion_pct: Maximum unit completion percentage
        reachable_only: If True, only return functions that can reach 100%
        exclude_locked: Skip functions locked by other agents
        limit: Max results to return
        db_path: Database path

    Returns:
        List of function dicts from near-complete units, sorted by unit
        completion then priority
    """
    conn = get_connection(db_path)

    # Find near-complete units
    units_query = """
        SELECT unit,
               COUNT(*) as total,
               SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched,
               ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 1) as pct
        FROM functions
        WHERE excluded = 0 AND unit IS NOT NULL
        GROUP BY unit
        HAVING pct >= ? AND pct < ?
        ORDER BY pct DESC
    """
    units = conn.execute(units_query, (min_completion_pct, max_completion_pct)).fetchall()

    if not units:
        return []

    # Get incomplete functions from these units
    unit_names = [u["unit"] for u in units]
    placeholders = ",".join("?" * len(unit_names))

    query = f"""
        SELECT f.id, f.symbol, f.demangled, f.unit, f.size, f.current_percent,
               f.best_percent, f.verdict, f.locked_by, f.attempt_count,
               f.priority_score, f.ease_score, f.impact_score, f.confidence_score,
               f.reachable_100, f.primary_pattern
        FROM functions f
        WHERE f.excluded = 0
          AND f.unit IN ({placeholders})
          AND f.current_percent < 100
          AND (f.verdict IS NULL OR f.verdict NOT IN ('COMPLETE', 'AT_LIMIT'))
    """
    params: list[Any] = list(unit_names)

    if reachable_only:
        query += " AND f.reachable_100 = 1"

    if exclude_locked:
        query += " AND f.locked_by IS NULL"

    query += """
        ORDER BY f.priority_score DESC
        LIMIT ?
    """
    params.append(limit)

    rows = conn.execute(query, params).fetchall()
    return [dict(row) for row in rows]


def get_priority_stats(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """Get statistics about the priority scoring infrastructure."""
    conn = get_connection(db_path)

    # Check if priority columns are populated
    has_scores = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE priority_score > 0"
    ).fetchone()[0]

    if has_scores == 0:
        return {
            "populated": False,
            "message": "Run compute_scores.py to populate priority data",
        }

    # Priority distribution
    high_priority = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE priority_score >= 50 AND excluded = 0"
    ).fetchone()[0]
    medium_priority = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE priority_score >= 20 AND priority_score < 50 AND excluded = 0"
    ).fetchone()[0]
    low_priority = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE priority_score > 0 AND priority_score < 20 AND excluded = 0"
    ).fetchone()[0]

    # Reachable 100% stats (80%+ functions)
    reachable = conn.execute(
        """SELECT COUNT(*) FROM functions
           WHERE reachable_100 = 1 AND current_percent >= 80 AND current_percent < 100 AND excluded = 0"""
    ).fetchone()[0]
    unreachable = conn.execute(
        """SELECT COUNT(*) FROM functions
           WHERE reachable_100 = 0 AND current_percent >= 80 AND current_percent < 100 AND excluded = 0"""
    ).fetchone()[0]

    # Pattern breakdown
    linker_merged = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_linker_merged = 1 AND excluded = 0"
    ).fetchone()[0]
    bool_mask = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_bool_mask = 1 AND excluded = 0"
    ).fetchone()[0]

    return {
        "populated": True,
        "with_scores": has_scores,
        "high_priority": high_priority,
        "medium_priority": medium_priority,
        "low_priority": low_priority,
        "reachable_100_80plus": reachable,
        "unreachable_80plus": unreachable,
        "linker_merged_count": linker_merged,
        "bool_mask_count": bool_mask,
    }
