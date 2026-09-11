"""Database module for DC3 Decomp Orchestrator.

Handles SQLite database for persistent state tracking of functions,
attempts, and worktrees.
"""

import json
import os
import sqlite3
from datetime import datetime
from pathlib import Path
from typing import Any

# Database path (relative to repo root)
DEFAULT_DB_PATH = "decomp.db"

# Schema version for migrations
SCHEMA_VERSION = 20

# `SCHEMA` below is the v1 BASE schema, and deliberately stays that way: a fresh
# database is created from it and then walked up the migration ladder (see
# `init_database`). Do NOT "helpfully" add a v-latest column to SCHEMA -- a
# second description of the same shape is what produced the defect that fix
# documented.

# Default maximum attempts before deprioritizing a function
# Functions with >= this many attempts are excluded from normal queries
DEFAULT_MAX_ATTEMPTS = 20

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

    -- Pre-refactor backup patch (v5 schema)
    pre_refactor_patch TEXT,            -- Patch before refactor-staff pass (backup)

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

-- Merged symbol detail tracking (v6)
CREATE TABLE IF NOT EXISTS merged_symbols (
    id INTEGER PRIMARY KEY,
    function_id INTEGER NOT NULL REFERENCES functions(id) ON DELETE CASCADE,
    symbol_name TEXT NOT NULL,           -- e.g., "merged_824D1870"
    call_count INTEGER DEFAULT 1,
    category TEXT,                       -- 'addtostrings', 'makestring', 'setobjconcrete', 'destructor', 'unknown'
    resolved_symbols TEXT,               -- JSON array of demangled names
    UNIQUE(function_id, symbol_name)
);
CREATE INDEX IF NOT EXISTS idx_merged_symbols_function ON merged_symbols(function_id);
CREATE INDEX IF NOT EXISTS idx_merged_symbols_category ON merged_symbols(category);

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

-- Ghidra decompilation cache (v7)
CREATE TABLE IF NOT EXISTS decompilations (
    symbol TEXT PRIMARY KEY,
    address TEXT,
    code TEXT NOT NULL,
    signature TEXT,
    error TEXT,
    cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS xrefs (
    symbol TEXT PRIMARY KEY,
    address TEXT,
    callers_json TEXT NOT NULL,
    callees_json TEXT NOT NULL,
    callers_count INTEGER,
    callees_count INTEGER,
    error TEXT,
    cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_decompilations_address ON decompilations(address);
CREATE INDEX IF NOT EXISTS idx_xrefs_address ON xrefs(address);
"""


_migrated_dbs: set[str] = set()  # Track which DB paths have been migration-checked


# ============================================================================
# Worktree shadow-DB guard
# ============================================================================
#
# `ninja` (and anything else that opens a RELATIVE `decomp.db`) run inside a git
# worktree creates a brand-new database *there*. That is deliberately safe for
# writes -- a worktree build can never corrupt the shared DB -- but it is a trap
# for reads. The shadow carries every row and NO judgement: 48,325 rows, zero
# verdicts, zero percentages. Measured 2026-08-19, identical queries:
#
#     AT_LIMIT certs to re-audit        shadow=0   main=3796
#     near-miss finishing (>=99, <100)  shadow=0   main=89
#     80-95 band, workable              shadow=0   main=325
#
# An empty result set reads as "this class is exhausted" -- the precise failure
# mode this project has a standing rule against. Worse, `query_functions`
# treats a NULL percent as passing a range filter, so the shadow does not even
# reliably return nothing: it returned 20 rows with no percentages at all.
#
# So: refuse. Never auto-create, auto-migrate, or answer from a worktree-local
# decomp.db while the main checkout has a real one. Fail loudly, name both
# paths. Opt in with allow_shadow=True or DC3_ALLOW_SHADOW_DB=1 if you really
# do want a throwaway per-worktree database.


class ShadowDatabaseError(RuntimeError):
    """A decomp.db read would be answered by a verdict-less worktree shadow."""


def _main_checkout_for(path: Path) -> Path | None:
    """Return the main checkout if `path` lives inside a *linked* git worktree.

    Returns None for the main checkout itself and for anything not in a repo.
    A linked worktree's `.git` is a FILE holding `gitdir: <main>/.git/worktrees/<name>`.
    """
    try:
        start = path.resolve()
    except OSError:
        return None
    for d in (start, *start.parents):
        git = d / ".git"
        if git.is_dir():
            return None                      # main checkout
        if git.is_file():
            try:
                text = git.read_text(errors="replace").strip()
            except OSError:
                return None
            if not text.startswith("gitdir:"):
                return None
            gitdir = Path(text.split(":", 1)[1].strip())
            if not gitdir.is_absolute():
                gitdir = (d / gitdir).resolve()
            for parent in gitdir.parents:    # <main>/.git/worktrees/<name> -> <main>
                if parent.name == ".git":
                    return parent.parent
            return None
    return None


def shadow_target(db_path: str | Path) -> Path | None:
    """Return the main checkout's decomp.db if `db_path` is a worktree shadow.

    None means "this path is fine". Checked against the DB's OWN directory, not
    the cwd -- an absolute path to the main repo's decomp.db passed from inside
    a worktree is fine, and is the documented way to work from a worktree.
    """
    p = Path(db_path)
    if p.name != "decomp.db":
        return None                          # scratch/test DBs are none of our business
    main = _main_checkout_for(p.parent if str(p.parent) not in ("", ".") else Path.cwd())
    if main is None:
        return None                          # not in a linked worktree
    main_db = main / "decomp.db"
    if not main_db.exists():
        return None                          # nothing better to point at
    try:
        if p.exists() and p.resolve() == main_db.resolve():
            return None                      # already symlinked to the real one
    except OSError:
        pass
    return main_db


def check_is_shadow(db_path: str | Path) -> bool:
    """True if `db_path` is (or would be) a worktree-local shadow decomp.db."""
    return shadow_target(db_path) is not None


def check_not_shadow_db(db_path: str | Path, *, allow_shadow: bool = False) -> None:
    """Raise ShadowDatabaseError if `db_path` is a worktree-local decomp.db.

    Checked against the DB's OWN directory, not the cwd -- passing an absolute
    path to the main repo's decomp.db from inside a worktree is fine and is the
    documented way to work from a worktree.
    """
    if allow_shadow or os.environ.get("DC3_ALLOW_SHADOW_DB") == "1":
        return
    main_db = shadow_target(db_path)
    if main_db is None:
        return
    p = Path(db_path)
    main = main_db.parent
    if not p.exists():
        state = "does not exist yet"
    else:
        try:
            is_sqlite = p.open("rb").read(16).startswith(b"SQLite format 3")
        except OSError:
            is_sqlite = True
        state = ("exists but has no verdicts" if is_sqlite
                 else "is the tripwire file setup_worktree.sh plants, not a database")
    raise ShadowDatabaseError(
        f"refusing to use a worktree-local decomp.db -- it would answer "
        f"plausibly and wrongly.\n"
        f"  requested   : {p}  ({state})\n"
        f"  main checkout: {main}\n"
        f"  real DB      : {main_db}\n"
        f"A worktree build writes a shadow decomp.db with every row and NO "
        f"verdicts/percentages, so work queries come back empty and read as "
        f"'this class is exhausted'.\n"
        f"Fix: pass --db {main_db} (or db_path=...) explicitly.\n"
        f"If you genuinely want a throwaway per-worktree DB, set "
        f"DC3_ALLOW_SHADOW_DB=1."
    )


def get_connection(
    db_path: str | Path = DEFAULT_DB_PATH,
    *,
    allow_shadow: bool = False,
) -> sqlite3.Connection:
    """Get a database connection with row factory enabled.

    Automatically runs pending migrations on first access per DB path.
    Raises ShadowDatabaseError for a worktree-local `decomp.db` (see above).
    """
    check_not_shadow_db(db_path, allow_shadow=allow_shadow)
    db_str = str(db_path)
    conn = sqlite3.connect(db_str)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode = WAL")
    conn.execute("PRAGMA foreign_keys = ON")

    if db_str not in _migrated_dbs:
        _migrated_dbs.add(db_str)
        cursor = conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
        )
        if cursor.fetchone() is not None:
            version = conn.execute("SELECT version FROM schema_version").fetchone()[0]
            if version < SCHEMA_VERSION:
                _run_migrations(conn, version, SCHEMA_VERSION)

    return conn


def init_database(
    db_path: str | Path = DEFAULT_DB_PATH,
    *,
    allow_shadow: bool = False,
) -> sqlite3.Connection:
    """Initialize database with schema. Safe to call multiple times.

    Refuses to CREATE a worktree-local `decomp.db` -- that is where the shadow
    comes from in the first place. Pass allow_shadow=True only if a throwaway
    per-worktree database is genuinely what you want.
    """
    conn = get_connection(db_path, allow_shadow=allow_shadow)

    # Check if already initialized
    cursor = conn.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
    )
    if cursor.fetchone() is None:
        # Fresh database: create the v1 BASE schema, stamp it v1, and walk the
        # SAME ladder every existing database walked.
        #
        # WHY NOT stamp SCHEMA_VERSION here (what this did until 2026-09-11).
        # `SCHEMA` is the v1 shape. Stamping it 19 made the version column a
        # lie that nothing could ever repair: `_run_migrations` only runs for
        # `version < SCHEMA_VERSION`, so a brand-new decomp.db was permanently
        # frozen missing every migration-created table and column. Measured on
        # the code this replaces -- fresh vs ladder-built, same module:
        #
        #   missing tables/views : function_patterns, patch_queue,
        #                          pattern_scan_examined, pattern_scan_units,
        #                          pattern_scans, v_function_patterns,
        #                          v_latest_pattern_scan      (7)
        #   missing functions.*  : is_stub, verdict_reason, unicorn_verdict,
        #                          pattern_flags_scan_id, ... (36 columns)
        #
        # so `query_functions(objdiff_pattern=...)` on a fresh DB died with
        # "no such table: pattern_scans" and `ingest_report` wrote rows nothing
        # else could query.
        #
        # WHY THE LADDER AND NOT A v20-SHAPED BASE CREATE. Two descriptions of
        # one schema drift, and this file already demonstrates the cost: the
        # base CREATE re-states file_pairs / merged_symbols / decompilations /
        # xrefs, which migrations v3/v6/v7 also create -- harmless only because
        # both spellings happen to still agree. The ladder is the single
        # description that every *existing* database was actually built from,
        # and every step of it is idempotent (CREATE ... IF NOT EXISTS, and
        # ALTER TABLE guarded on "duplicate column"), which is exactly what
        # running it over the base schema requires. Cost is ~20 no-op DDL
        # statements once, at creation.
        conn.executescript(SCHEMA)
        conn.execute("INSERT INTO schema_version (version) VALUES (1)")
        conn.commit()
        _run_migrations(conn, 1, SCHEMA_VERSION)
        print(f"Initialized database at {db_path}")
    else:
        # Check version for migrations
        version = conn.execute("SELECT version FROM schema_version").fetchone()[0]
        if version < SCHEMA_VERSION:
            _run_migrations(conn, version, SCHEMA_VERSION)

    return conn


# Dropped 2026-08-19 and deliberately NOT re-added by any migration below:
#   has_assert_revs, has_ltcg_pooling
# Both were identically 0 over all 52,547 rows, had no writer anywhere in live
# code, and objdiff has no ASSERT_REVS or LTCG_POOLING detector at all -- they are
# vestigial schema from the archived 2026-03 meta-strategy experiment. A column
# that is always 0 silently answers "no" to every filter built on it, which is
# strictly worse than the column not existing. Do not resurrect them; if the
# concepts come back, add a detector first and a column second.
_DROPPED_DEAD_COLUMNS = ("has_assert_revs", "has_ltcg_pooling")


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

    if from_version < 5 <= to_version:
        # Migration v4 -> v5: Add pre_refactor_patch column for backup patches
        print("  Migration v5: Adding pre_refactor_patch column...")
        try:
            conn.execute("ALTER TABLE attempts ADD COLUMN pre_refactor_patch TEXT")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise

    if from_version < 6 <= to_version:
        # Migration v5 -> v6: Add merged_symbols table and granular merged tracking
        print("  Migration v6: Adding merged_symbols table and granular tracking...")

        # Create merged_symbols table
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS merged_symbols (
                id INTEGER PRIMARY KEY,
                function_id INTEGER NOT NULL REFERENCES functions(id) ON DELETE CASCADE,
                symbol_name TEXT NOT NULL,
                call_count INTEGER DEFAULT 1,
                category TEXT,
                resolved_symbols TEXT,
                UNIQUE(function_id, symbol_name)
            );
            CREATE INDEX IF NOT EXISTS idx_merged_symbols_function ON merged_symbols(function_id);
            CREATE INDEX IF NOT EXISTS idx_merged_symbols_category ON merged_symbols(category);
        """)

        # Add new columns to functions table
        new_columns = [
            ("has_addtostrings", "BOOLEAN DEFAULT 0"),
            ("has_makestring", "BOOLEAN DEFAULT 0"),
            ("has_setobjconcrete", "BOOLEAN DEFAULT 0"),
            ("verdict_reason", "TEXT"),
            ("merged_symbol_count", "INTEGER DEFAULT 0"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

        # Add partial index for quick AddToStrings candidate lookup
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_functions_addtostrings
            ON functions(has_addtostrings) WHERE has_addtostrings = 1
        """)

    if from_version < 7 <= to_version:
        # Migration v6 -> v7: Add Ghidra decompilation cache tables
        print("  Migration v7: Adding decompilations and xrefs cache tables...")
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS decompilations (
                symbol TEXT PRIMARY KEY,
                address TEXT,
                code TEXT NOT NULL,
                signature TEXT,
                error TEXT,
                cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE IF NOT EXISTS xrefs (
                symbol TEXT PRIMARY KEY,
                address TEXT,
                callers_json TEXT NOT NULL,
                callees_json TEXT NOT NULL,
                callers_count INTEGER,
                callees_count INTEGER,
                error TEXT,
                cached_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            );

            CREATE INDEX IF NOT EXISTS idx_decompilations_address ON decompilations(address);
            CREATE INDEX IF NOT EXISTS idx_xrefs_address ON xrefs(address);
        """)

    if from_version < 8 <= to_version:
        # Migration v7 -> v8: Add unicorn verdict columns to functions table
        print("  Migration v8: Adding unicorn verdict columns...")
        new_columns = [
            ("unicorn_verdict", "TEXT"),       # EQUIVALENT, DIVERGENT, SKIPPED, ERROR
            ("unicorn_class", "TEXT"),          # build_env, regalloc, logic, NULL
            ("unicorn_confidence", "TEXT"),     # high, stable_divergent, input_sensitive
            ("unicorn_tested_at", "TIMESTAMP"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise
        conn.execute("""
            CREATE INDEX IF NOT EXISTS idx_functions_unicorn_verdict
            ON functions(unicorn_verdict)
        """)

    if from_version < 9 <= to_version:
        # Migration v8 -> v9: Add unicorn_reason column for fine-grained divergence tracking
        print("  Migration v9: Adding unicorn_reason column...")
        try:
            conn.execute("ALTER TABLE functions ADD COLUMN unicorn_reason TEXT")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise

    if from_version < 10 <= to_version:
        # Migration v9 -> v10: Add columns for new Rust pattern detectors
        print("  Migration v10: Adding pattern detector columns...")
        new_columns = [
            ("has_makestring_mismatch", "BOOLEAN DEFAULT 0"),
            ("has_address_relocation", "BOOLEAN DEFAULT 0"),
            ("has_boolean_negation", "BOOLEAN DEFAULT 0"),
            ("has_float_precision", "BOOLEAN DEFAULT 0"),
            ("detected_patterns", "TEXT"),  # JSON array of all detected pattern type strings
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 11 <= to_version:
        # Migration v10 -> v11: Add columns for fsel_ternary and float_to_int_to_float detectors
        print("  Migration v11: Adding fsel_ternary and float_to_int_to_float columns...")
        new_columns = [
            ("has_fsel_ternary", "BOOLEAN DEFAULT 0"),
            ("has_float_to_int_to_float", "BOOLEAN DEFAULT 0"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 12 <= to_version:
        # Migration v11 -> v12: Add columns for remaining Rust pattern detectors
        print("  Migration v12: Adding remaining pattern detector columns...")
        new_columns = [
            ("has_register_swap", "BOOLEAN DEFAULT 0"),
            ("has_comparison_style", "BOOLEAN DEFAULT 0"),
            ("has_control_flow", "BOOLEAN DEFAULT 0"),
            ("has_commutative_op_order", "BOOLEAN DEFAULT 0"),
            ("has_offset_swap", "BOOLEAN DEFAULT 0"),
            ("has_anonymous_namespace_hash", "BOOLEAN DEFAULT 0"),
            ("has_static_guard_counter", "BOOLEAN DEFAULT 0"),
            ("has_dynamic_cast_mismatch", "BOOLEAN DEFAULT 0"),
            ("has_dead_store_elimination", "BOOLEAN DEFAULT 0"),
            ("has_prologue_mismatch", "BOOLEAN DEFAULT 0"),
            ("has_alloca_mismatch", "BOOLEAN DEFAULT 0"),
            ("has_scope_counter_mismatch", "BOOLEAN DEFAULT 0"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 13 <= to_version:
        # Migration v12 -> v13: Add is_stub column for unimplemented stub tracking
        print("  Migration v13: Adding is_stub column...")
        try:
            conn.execute("ALTER TABLE functions ADD COLUMN is_stub INTEGER DEFAULT 0")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise
        conn.execute(
            "CREATE INDEX IF NOT EXISTS idx_functions_is_stub "
            "ON functions(is_stub) WHERE is_stub = 1"
        )

    if from_version < 14 <= to_version:
        # Migration v13 -> v14: Add patch_queue table for intelligent merger agent
        print("  Migration v14: Adding patch_queue table...")
        conn.executescript("""
            CREATE TABLE IF NOT EXISTS patch_queue (
                id INTEGER PRIMARY KEY,
                attempt_id INTEGER REFERENCES attempts(id),
                function_id INTEGER REFERENCES functions(id),
                symbol TEXT NOT NULL,
                demangled TEXT,
                unit TEXT,
                patch TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'pending',
                priority INTEGER DEFAULT 0,
                start_percent REAL,
                end_percent REAL,
                failure_reason TEXT,
                merger_session_id TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                applied_at TIMESTAMP
            );
            CREATE INDEX IF NOT EXISTS idx_patch_queue_status ON patch_queue(status);
        """)

    if from_version < 15 <= to_version:
        # Migration v14 -> v15: Add unicorn verdict provenance columns.
        # These let us re-classify by query when the signal model changes
        # instead of re-running the full 25k-function batch.
        #   unicorn_signal_version           — bumped per signal-model change
        #   unicorn_probe_schedule_hash      — hash of (fill, obj_mem, args, mock)
        #   unicorn_unmapped_pages_fingerprint — Phase 3 fingerprint (write-only here)
        print("  Migration v15: Adding unicorn verdict provenance columns...")
        new_columns = [
            ("unicorn_signal_version", "INTEGER"),
            ("unicorn_probe_schedule_hash", "TEXT"),
            ("unicorn_unmapped_pages_fingerprint", "TEXT"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 16 <= to_version:
        # Migration v15 -> v16: Add unicorn HARNESS provenance columns.
        #
        # v15's unicorn_signal_version describes the COMPARATOR semantics. It
        # says nothing about the emulation harness those semantics ran on, and
        # eight harness defects fixed 2026-08-18/19 changed verdicts wholesale
        # without touching one comparator rule -- so signal_version stayed 3 and
        # nothing in the DB distinguished a verdict from the broken harness
        # (which overstated real bugs by roughly 8x) from a verdict from the
        # fixed one. These two columns close that gap.
        #   unicorn_harness_version  — see scripts/unicorn_runner/signal_version.py
        #                              HARNESS_VERSION for the h1..hN changelog.
        #                              NULL or 1 == pre-2026-08-18, do not trust.
        #   unicorn_harness_build    — git short rev of the tree that measured it.
        print("  Migration v16: Adding unicorn harness provenance columns...")
        new_columns = [
            ("unicorn_harness_version", "INTEGER"),
            ("unicorn_harness_build", "TEXT"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    if from_version < 17 <= to_version:
        # Migration v16 -> v17: objdiff pattern findings as measured FACTS with
        # a ruler attached, instead of more `has_*` booleans.
        #
        # WHY NOT FOUR MORE COLUMNS.  objdiff 4.2.6 split the over-broad
        # LINKER_MERGED detector into five classes (WRONG_CALLEE,
        # TEMPLATE_INSTANTIATION_MISMATCH, REGISTER_SAVE_HELPER_MISMATCH,
        # UNVERIFIABLE_CALLEE_NAME, and a LINKER_MERGED that keeps ~2% of its
        # former population).  The obvious move was `has_wrong_callee` and three
        # siblings.  This table exists because that move is how the LAST six
        # columns died.
        #
        # Six `has_*` columns read a confident, uniform 0 on all 52,568 rows --
        # not because the build was clean, but because `sync_objdiff.py` runs
        # objdiff with `functionRelocDiffs=none`, under which the detectors
        # behind them cannot fire at all.  A boolean column CANNOT DISTINGUISH
        # "measured, and this function does not have it" from "the ruler in
        # force could not have seen it" from "never measured".  All three are 0.
        # `has_assert_revs` and `has_ltcg_pooling` were dropped outright for the
        # same reason.  The defect was never the detector; it was that the
        # storage discarded the one fact that decides how to read the value.
        #
        # So: every finding is a row, every row belongs to a SCAN, and a scan
        # carries the ruler, the exact objdiff binary, the tree it measured and
        # whether that tree was verified patched.  Absence is expressible three
        # different ways and they are all distinguishable:
        #
        #   no pattern_scans row for a ruler        -> never measured
        #   scan exists, no pattern_scan_examined   -> that function was dropped
        #                                              (and coverage_json says why)
        #   examined, no function_patterns row      -> genuinely did not fire
        #
        # `pattern_scans.ruler` is NOT NULL by design.  A pattern population
        # without its ruler is not a weaker number, it is not a number: the same
        # objects and the same detectors give LINKER_MERGED = 0 under `none`,
        # and a four-figure count under `all`.
        #
        # `patterns_checked` records the detector VOCABULARY of the binary that
        # ran, so a future rename cannot make an old scan's silence look like a
        # negative finding -- the 4.2.5 -> 4.2.6 rename is exactly that hazard,
        # and it is why the stale 1,310 could not simply be adjusted.
        print("  Migration v17: Adding ruler-tagged objdiff pattern scan tables...")
        conn.execute("""
            CREATE TABLE IF NOT EXISTS pattern_scans (
                id INTEGER PRIMARY KEY,
                ruler TEXT NOT NULL,          -- functionRelocDiffs value. NEVER NULL.
                tool_version TEXT NOT NULL,   -- `objdiff-cli --version` verbatim
                project_dir TEXT NOT NULL,    -- the tree that was measured
                build_rev TEXT,               -- git short rev of that tree
                tree_verified INTEGER NOT NULL DEFAULT 0,  -- post-compile fixed point asserted
                universe INTEGER NOT NULL,    -- symbols supplied to the sweep
                examined INTEGER NOT NULL,    -- symbols objdiff actually diffed
                coverage_json TEXT,           -- full drop accounting, reason by reason
                patterns_checked TEXT,        -- JSON array: this binary's vocabulary
                notes TEXT,
                started_at TIMESTAMP,
                finished_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS pattern_scan_examined (
                scan_id INTEGER NOT NULL REFERENCES pattern_scans(id) ON DELETE CASCADE,
                function_id INTEGER NOT NULL REFERENCES functions(id),
                PRIMARY KEY (scan_id, function_id)
            ) WITHOUT ROWID
        """)
        conn.execute("""
            CREATE TABLE IF NOT EXISTS function_patterns (
                scan_id INTEGER NOT NULL REFERENCES pattern_scans(id) ON DELETE CASCADE,
                function_id INTEGER NOT NULL REFERENCES functions(id),
                pattern TEXT NOT NULL,
                confidence TEXT,
                fixability TEXT,              -- LikelyFixable / RarelyHandFixable / ...
                instruction_count INTEGER,
                details TEXT,                 -- raw detector payload, e.g. divergent_callees
                PRIMARY KEY (scan_id, function_id, pattern)
            ) WITHOUT ROWID
        """)
        conn.execute("CREATE INDEX IF NOT EXISTS idx_function_patterns_pattern "
                     "ON function_patterns(scan_id, pattern)")
        conn.execute("CREATE INDEX IF NOT EXISTS idx_function_patterns_fn "
                     "ON function_patterns(function_id)")
        # `pattern_flags_scan_id` is the ONE column added to `functions`, and it
        # is not another flag: it names the scan the reloc-sensitive `has_*`
        # booleans on that row were last derived from.  A reader who wants to
        # know whether `has_linker_merged = 0` means anything can now find out.
        # NULL == the value predates this table and its ruler is unknown.
        try:
            conn.execute("ALTER TABLE functions ADD COLUMN pattern_flags_scan_id INTEGER")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise
        # Latest scan per ruler, so consumers do not each re-derive "which scan".
        conn.execute("DROP VIEW IF EXISTS v_latest_pattern_scan")
        conn.execute("""
            CREATE VIEW v_latest_pattern_scan AS
            SELECT s.* FROM pattern_scans s
            WHERE s.id = (SELECT MAX(s2.id) FROM pattern_scans s2
                          WHERE s2.ruler = s.ruler)
        """)
        # The join a caller actually wants.  Restricted to the latest scan per
        # ruler and carrying the ruler in every row, so a result set cannot be
        # quoted without it.
        conn.execute("DROP VIEW IF EXISTS v_function_patterns")
        conn.execute("""
            CREATE VIEW v_function_patterns AS
            SELECT s.ruler        AS ruler,
                   s.tool_version AS tool_version,
                   s.id           AS scan_id,
                   f.id           AS function_id,
                   f.symbol       AS symbol,
                   f.demangled    AS demangled,
                   f.unit         AS unit,
                   f.size         AS size,
                   f.match_percent_normalized AS match_percent_normalized,
                   p.pattern      AS pattern,
                   p.fixability   AS fixability,
                   p.confidence   AS confidence,
                   p.instruction_count AS instruction_count,
                   p.details      AS details
            FROM function_patterns p
            JOIN v_latest_pattern_scan s ON s.id = p.scan_id
            JOIN functions f ON f.id = p.function_id
        """)

    if from_version < 18 <= to_version:
        # Migration v17 -> v18: what the scan MEASURED, not just what measured it.
        #
        # v17 records the instrument (`tool_version`, with the xxh3 that makes it
        # provenance rather than a claim), the tree's identity (`project_dir`,
        # `build_rev`) and whether the tree was a post-compile fixed point
        # (`tree_verified`).  It records NOTHING about the 4,447 object files the
        # scan actually diffed, and both sides of every one of them move:
        #
        #   TARGET  build/373307D9/obj/**.obj -- `dtk xex split`'s undeclared
        #           output, rewritten by any `config/373307D9/symbols.txt` edit.
        #   BASE    build/373307D9/src/**.obj -- rebuilt by any source commit.
        #
        # `build_rev` is not a substitute, and that is measured rather than
        # argued: scan 16 was taken across a landing merge, recorded
        # `build_rev b91fc0cb5`, repaired 3 stale rows and INTRODUCED 6.  On an
        # active repo a whole-repo rev names *a* commit from the measurement
        # window, never *the* tree that was diffed.
        #
        # WHY A TABLE AND NOT A BLOB ON `pattern_scans`.  A tree-wide digest can
        # only answer "something moved", which on this repo is permanently true
        # and therefore permanently ignored.  Per-unit rows answer "WHICH units
        # moved, and on which side", which is the question a caller can act on:
        # a lane working `default/system/obj/Dir` can be told that ITS unit is
        # current while 200 others are not.  A unit is exactly one target object
        # and one base object -- objdiff's own grain, and the grain
        # `functions.unit` already carries.
        #
        # NULL sha256 is a VALUE: 1,234 of 2,224 units have no base object at
        # all (target-only library units).  "absent then, absent now" must
        # compare equal, and only a recorded NULL can distinguish that from
        # "there was one and it is gone".
        #
        # `raced` is written by `pattern_census.py`, which fingerprints before
        # AND after its sweep: a unit whose objects moved between the two reads
        # has findings that are real but whose baseline is not attributable, and
        # saying so is the whole point of this table.
        print("  Migration v18: Adding per-unit object baselines for pattern scans...")
        conn.execute("""
            CREATE TABLE IF NOT EXISTS pattern_scan_units (
                scan_id INTEGER NOT NULL REFERENCES pattern_scans(id) ON DELETE CASCADE,
                unit TEXT NOT NULL,           -- objdiff.json unit name, e.g. default/system/obj/Dir
                target_sha256 TEXT,           -- build/<v>/obj/**.obj  (NULL == absent)
                base_sha256 TEXT,             -- build/<v>/src/**.obj  (NULL == absent)
                raced INTEGER NOT NULL DEFAULT 0,  -- object moved DURING the scan
                PRIMARY KEY (scan_id, unit)
            ) WITHOUT ROWID
        """)
        conn.execute("CREATE INDEX IF NOT EXISTS idx_pattern_scan_units_scan "
                     "ON pattern_scan_units(scan_id)")

    if from_version < 19 <= to_version:
        # Migration v18 -> v19: record the census's SHARD COUNT on the scan.
        #
        # (Developed as v18 and renumbered on landing: the object-baseline
        # migration took 18 first.  Two lanes adding a migration in the same
        # week is normal here; a silently duplicated version number would
        # not be, so the collision is recorded rather than smoothed over.)
        #
        # `pattern_scans` records the ruler, the binary, the tree and the
        # denominator -- everything about the measurement except how it was
        # PARALLELISED.  That omission made a real question unfalsifiable: two
        # scans of the same binary and the same tree came back 15 s and ~6 min
        # apart, and the obvious hypothesis (one ran with -j 1 and starved, or
        # one ran against a machine already saturated by the build fleet) could
        # not be checked against anything, because no row says.  A wall-clock
        # difference with no recorded shard count is not evidence of anything.
        #
        # `finished_at - started_at` is only interpretable next to `jobs`, so
        # the column is the thing that makes the two timestamps mean something.
        # NULL is honest and distinguishable: it means the row predates this
        # migration, not that the sweep ran single-threaded.
        print("  Migration v19: Adding pattern_scans.jobs (census shard count)...")
        try:
            conn.execute("ALTER TABLE pattern_scans ADD COLUMN jobs INTEGER")
        except sqlite3.OperationalError as e:
            if "duplicate column" not in str(e).lower():
                raise
        # `v_latest_pattern_scan` is `SELECT s.*`; recreate it so the new column
        # is unambiguously in the view rather than relying on when SQLite
        # happens to expand the star.
        conn.execute("DROP VIEW IF EXISTS v_latest_pattern_scan")
        conn.execute("""
            CREATE VIEW v_latest_pattern_scan AS
            SELECT s.* FROM pattern_scans s
            WHERE s.id = (SELECT MAX(s2.id) FROM pattern_scans s2
                          WHERE s2.ruler = s.ruler)
        """)

    if from_version < 20 <= to_version:
        # Migration v19 -> v20: adopt two columns the LIVE database has had for
        # months and that NO migration and NO base CREATE ever created.
        #
        # `functions.has_linker_merged` and `functions.match_percent_normalized`
        # exist in decomp.db, are read by orchestrator code, and came from
        # outside this ladder:
        #
        #   has_linker_merged        -- created by the 2026-03 meta-strategy
        #                               tooling, since deleted. database.py has
        #                               only ever READ it (the oldest commit in
        #                               this file, fd8ffa621, already selects it
        #                               and never creates it). Also read by
        #                               recon.py, unicorn/reclassify_logic.py,
        #                               unicorn_runner/comparator.py, and
        #                               written by backfill_reloc_patterns.py
        #                               and batch_promote.py.
        #   match_percent_normalized -- created by an ad-hoc ALTER inside
        #                               scripts/sync_match_percent.py
        #                               (`ensure_normalized_column`), which
        #                               still runs and stays correct: this
        #                               migration is guarded the same way, so
        #                               whichever touches a database first, the
        #                               other is a no-op.
        #
        # The concrete breakage, measured before this migration existed: the
        # v17 view `v_function_patterns` SELECTs `f.match_percent_normalized`,
        # and SQLite resolves a view's columns lazily -- so the view CREATED
        # fine on a ladder-built database and every query against it failed
        # with "no such column: f.match_percent_normalized". The view has been
        # unusable on any database except the live one since v17 landed.
        #
        # SCOPE, stated rather than left to inference: a live-vs-ladder census
        # finds 25 such orphan columns. Only these two are adopted. The other
        # 23 are the archived meta-strategy experiment (`excluded`,
        # `priority_score`, `ease_score`, `reachable_100`, `has_bool_mask`, ...)
        # plus columns owned by scripts that already carry their own idempotent
        # ALTER and their own writer (`floor_cert_*` in certify_floor.py,
        # `unicorn_source_hash*` in unicorn/apply_refresh.py). This repo
        # DROPPED two always-zero flags for exactly that reason (see
        # `_DROPPED_DEAD_COLUMNS`); resurrecting a dead flag into the canonical
        # ladder would re-make that mistake. `get_stats()` still cannot run on a
        # fresh database because it sums `has_bool_mask` -- that is a real,
        # reported, deliberately-unfixed gap, not an oversight here.
        #
        # On the live database both columns already exist, so both ALTERs hit
        # the duplicate-column guard and this migration is a pure version bump.
        print("  Migration v20: Adopting has_linker_merged + "
              "match_percent_normalized into the ladder...")
        new_columns = [
            ("has_linker_merged", "BOOLEAN DEFAULT 0"),
            ("match_percent_normalized", "REAL"),
        ]
        for col_name, col_def in new_columns:
            try:
                conn.execute(f"ALTER TABLE functions ADD COLUMN {col_name} {col_def}")
            except sqlite3.OperationalError as e:
                if "duplicate column" not in str(e).lower():
                    raise

    # Update schema version
    conn.execute("UPDATE schema_version SET version = ?", (to_version,))
    conn.commit()
    print(f"  Migration complete. Database at v{to_version}")


#: The DDL for `pattern_scan_units`, callable OUTSIDE the migration ladder.
#:
#: Two lanes were adding to this schema in the same week (this table, and a
#: `jobs` column on `pattern_scans`).  Whichever lands second gets the next
#: version number, and a database migrated by the FIRST one is then at a version
#: the ladder considers done -- so the second lane's DDL would never run on it.
#: A writer that calls this first is immune to that ordering, and the reader
#: treats an absent table as "no baseline recorded" rather than as "clean", so
#: neither half can fail silent.
def ensure_pattern_scan_units(conn: sqlite3.Connection) -> None:
    """Create `pattern_scan_units` if the ladder has not (idempotent)."""
    conn.execute("""
        CREATE TABLE IF NOT EXISTS pattern_scan_units (
            scan_id INTEGER NOT NULL REFERENCES pattern_scans(id) ON DELETE CASCADE,
            unit TEXT NOT NULL,
            target_sha256 TEXT,
            base_sha256 TEXT,
            raced INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (scan_id, unit)
        ) WITHOUT ROWID
    """)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_pattern_scan_units_scan "
                 "ON pattern_scan_units(scan_id)")


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

            demangled = func.get("metadata", {}).get("demangled_name", "") or func.get("demangled", func.get("name", ""))
            size = int(func.get("size", 0) or 0)

            # report.json's match% is DELIBERATELY NOT READ HERE, and the local
            # that used to hold it has been removed so nothing looks half-wired.
            # `report generate` returns fuzzy_match_percent: 100.0 for
            # unimplemented stubs (a base_size=0 divide-by-zero guard); ingesting
            # it minted thousands of false COMPLETE verdicts, fixed 2026-03-04.
            # `ninja` re-ingests on every build, so a percent written here would
            # also overwrite every measurement sync_objdiff.py made, on a
            # schedule.  current_percent and verdict have exactly one writer:
            # sync_objdiff.py, which runs a real objdiff diff.
            # Guarded by tests/test_ingest_report.py:
            #   test_mix_of_implemented_and_unimplemented
            #   test_reingest_does_not_clobber_a_measured_percent

            # Check if function exists
            existing = conn.execute(
                "SELECT id, current_percent, best_percent, verdict FROM functions WHERE symbol = ?",
                (symbol,),
            ).fetchone()

            if existing:
                if update_existing:
                    # Update metadata (size, demangled, unit) but NOT verdict.
                    # Don't update current_percent from report.json — it's
                    # unreliable. Only sync_objdiff.py should set match%.
                    conn.execute(
                        """
                        UPDATE functions SET
                            demangled = COALESCE(?, demangled),
                            unit = COALESCE(?, unit),
                            size = COALESCE(?, size),
                            updated_at = CURRENT_TIMESTAMP
                        WHERE id = ?
                        """,
                        (demangled, unit_name, size, existing["id"]),
                    )
                    updated += 1
                else:
                    skipped += 1
            else:
                # Insert new function — no verdict, match% from report is
                # unreliable so leave current_percent NULL for sync_objdiff
                # to fill in later.
                conn.execute(
                    """
                    INSERT INTO functions
                        (symbol, demangled, unit, size)
                    VALUES (?, ?, ?, ?)
                    """,
                    (symbol, demangled, unit_name, size),
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
    exclude_at_limit: bool = False,
    db_path: str | Path = DEFAULT_DB_PATH,
    order_by: str = "percent",
    order_asc: bool = False,
    min_size: int = 0,
    exclude_patterns: list[str] | None = None,
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
) -> dict[str, Any] | None:
    """
    Get next function to work on based on criteria.

    Args:
        pattern: Glob pattern(s) for unit (e.g., "src/system/char/*" or list of patterns)
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage
        exclude_locked: Skip functions locked by other agents
        exclude_complete: Skip functions with verdict COMPLETE (100%)
        exclude_at_limit: Skip functions with verdict AT_LIMIT
        db_path: Database path
        order_by: Sort column - "percent" (default) or "size"
        order_asc: Sort ascending instead of descending
        min_size: Minimum function size in bytes (0 = no minimum)
        exclude_patterns: Glob patterns for units to exclude (default: XDK)
        max_attempts: Skip functions with >= this many attempts (None to disable)

    Returns:
        Function dict or None if no matches
    """
    conn = get_connection(db_path)

    # Use default exclusions if not specified
    if exclude_patterns is None:
        exclude_patterns = DEFAULT_EXCLUDE_PATTERNS

    glob_clause, glob_params = _build_unit_glob_clause(pattern, exclude_patterns)

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

    # Exclude ICF artifacts and linker stubs (not real decomp targets)
    query += f" AND {_EXCLUDE_MERGED}"
    query += f" AND {_EXCLUDE_FN}"
    query += " AND symbol != 'OnlyReturns'"

    if min_size > 0:
        query += f" AND size >= {min_size}"

    # Exclude functions that have been tried too many times
    if max_attempts is not None:
        query += f" AND (attempt_count IS NULL OR attempt_count < {max_attempts})"

    excluded_verdicts = []
    if exclude_complete:
        excluded_verdicts.append('COMPLETE')
    if exclude_at_limit:
        excluded_verdicts.append('AT_LIMIT')
    if excluded_verdicts:
        placeholders = ", ".join(f"'{v}'" for v in excluded_verdicts)
        query += f" AND (verdict IS NULL OR verdict NOT IN ({placeholders}))"

    # Build ORDER BY clause
    direction = "ASC" if order_asc else "DESC"
    if order_by == "size":
        query += f"""
        ORDER BY
            CASE WHEN size IS NULL THEN 1 ELSE 0 END,
            size {direction}
        LIMIT 1
    """
    else:
        query += f"""
        ORDER BY
            CASE WHEN current_percent IS NULL THEN 1 ELSE 0 END,
            current_percent {direction}
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
    # If pattern starts with "system/" or "lazer/", add "default/" prefix
    if pattern.startswith("system/") or pattern.startswith("lazer/"):
        return "default/" + pattern
    return pattern


# Default exclusion patterns for batch operations
# XDK: third-party/SDK code, not decomp targets
# link_glue: linker shims (ICF/ALTERNATENAME), not real decomp work
# binkxenon: third-party Bink video library
DEFAULT_EXCLUDE_PATTERNS = [
    "default/xdk/*",
    "default/link_glue",
    "default/lib/binkxenon/*",
]


def _build_unit_glob_clause(
    patterns: str | list[str],
    exclude_patterns: list[str] | None = None,
) -> tuple[str, list[str]]:
    """
    Build a SQL WHERE clause fragment matching one or more unit GLOB patterns.

    Args:
        patterns: Single pattern string or list of pattern strings.
        exclude_patterns: Optional list of patterns to exclude from results.

    Returns:
        Tuple of (sql_fragment, params) where sql_fragment is like
        "(unit GLOB ? OR unit GLOB ?) AND unit NOT GLOB ?" and params
        is the normalized patterns followed by exclude patterns.
    """
    if isinstance(patterns, str):
        patterns = [patterns]

    normalized = [normalize_unit_pattern(p) for p in patterns]

    if len(normalized) == 1:
        include_clause = "unit GLOB ?"
    else:
        clauses = " OR ".join("unit GLOB ?" for _ in normalized)
        include_clause = f"({clauses})"

    params = normalized

    # Add exclusion patterns if provided
    if exclude_patterns:
        normalized_exclude = [normalize_unit_pattern(p) for p in exclude_patterns]
        exclude_clauses = " AND ".join("unit NOT GLOB ?" for _ in normalized_exclude)
        full_clause = f"{include_clause} AND {exclude_clauses}"
        params = params + normalized_exclude
        return full_clause, params

    return include_clause, params


BOILERPLATE_SYMBOL_PREFIXES = [
    "??__F",   # dynamic atexit destructors
    "??__E",   # dynamic initializers
    "??$MakeString",  # MakeString template instantiations
    "??_9",    # vcall thunks
    "??_E",    # vector deleting destructors
    "??_G",    # scalar deleting destructors
]


def like_prefix_clause(column: str, prefix: str, negate: bool = True) -> str:
    """``column [NOT] LIKE '<prefix>%'`` with the '_'/'%'/'\\' wildcards ESCAPED.

    SQL LIKE treats '_' as a single-char wildcard. A naive
    ``symbol NOT LIKE 'merged_%'`` therefore also matches ``mergedX...`` (and
    a naive ``'??_%'`` matches EVERY ``??``-prefixed ctor/dtor/operator). That
    is the wave-9 measurement bug. Escaping the literal underscore makes the
    clause match exactly the intended prefix. Behaviour-preserving for the
    artifact prefixes (merged_, fn_) whose '_' always coincides with a real
    underscore today, and corrective for any future symbol that would alias.
    """
    esc = prefix.replace("\\", "\\\\").replace("_", "\\_").replace("%", "\\%")
    op = "NOT LIKE" if negate else "LIKE"
    return f"{column} {op} '{esc}%' ESCAPE '\\'"


# Pre-built artifact/STL exclusion fragments used by several query builders.
# (merged_<addr> ICF stubs, fn_<addr> EH funclets, stlpmtx STL instantiations.)
_EXCLUDE_MERGED = like_prefix_clause("symbol", "merged_")
_EXCLUDE_FN = like_prefix_clause("symbol", "fn_")
_EXCLUDE_STLPMTX = "demangled NOT LIKE '%stlpmtx\\_std::%' ESCAPE '\\'"


#: The callee-NAME patterns.  Each of these is a finding of the shape "the
#: target calls X here and we call Y", which objdiff can only make about a
#: symbol PAIR -- and when the enclosing symbol has no name of its own
#: (``fn_<addr>``, an MSVC EH funclet), objdiff pairs it by MASKED BYTE
#: SIGNATURE.  Byte-identical funclets pair arbitrarily, so the "wrong callee"
#: is then a statement about objdiff's guess, not about our source.  objdiff
#: declares that itself, per function, as ``UNVERIFIABLE_PAIRING``.
#:
#: Measured on scan 18 (name_check, objdiff 4.2.8, whole binary): **56 of 62**
#: WRONG_CALLEE rows also carried UNVERIFIABLE_PAIRING.  A worklist that does
#: not subtract them is ~90% rows a lane cannot adjudicate.
PAIRING_SENSITIVE_PATTERNS = frozenset({
    "WRONG_CALLEE",
    "TEMPLATE_INSTANTIATION_MISMATCH",
    "MAKESTRING_TEMPLATE_MISMATCH",
})

#: The pattern that says "objdiff guessed this symbol pair".
UNVERIFIABLE_PAIRING = "UNVERIFIABLE_PAIRING"


class FunctionQueryResult(list):
    """The rows, plus what was subtracted to produce them.

    A plain ``list`` in every respect a caller already relies on (``len``,
    indexing, iteration, ``==`` against a list).  The extra attributes exist so
    that the *hidden* set can never read as absence: a query that silently drops
    56 of 62 rows and returns 6 is indistinguishable, at the call site, from a
    class that only ever had 6 members.

    Attributes:
        unverifiable_hidden:  how many rows the pairing filter removed.  This is
            the true population under the SAME other filters, not a page of it,
            so it is comparable to ``len(self)``.
        unverifiable_pattern: the pattern that was asked for, for the message.
        unverifiable_note:    the rendered line, or ``""`` when nothing was
            hidden and there is nothing to say.  Also carries the *vocabulary*
            warning: a scan taken by objdiff < 4.2.7 has no UNVERIFIABLE_PAIRING
            in its vocabulary at all, so subtracting it removes nothing -- which
            must not be reported as "no funclet rows here".
    """

    unverifiable_hidden: int = 0
    unverifiable_pattern: str | None = None
    unverifiable_note: str = ""


def _pairing_vocabulary_missing(conn: sqlite3.Connection, scan_id: int) -> bool:
    """True when this scan's detector vocabulary predates UNVERIFIABLE_PAIRING.

    ``pattern_scans.patterns_checked`` is the JSON list of detectors the binary
    that took the scan actually speaks.  objdiff 4.2.6 had none of the pairing
    classes, so an exclusion against a 4.2.6 scan is a silent no-op.  Say so
    rather than reporting a clean zero.
    """
    row = conn.execute("SELECT patterns_checked FROM pattern_scans WHERE id = ?",
                       (scan_id,)).fetchone()
    if row is None or not row[0]:
        return False                       # unknown vocabulary: do not claim
    try:
        checked = json.loads(row[0])
    except (ValueError, TypeError):
        return False
    return isinstance(checked, list) and UNVERIFIABLE_PAIRING not in checked


def query_functions(
    pattern: str | list[str] = "*",
    min_percent: float = 0,
    max_percent: float = 100,
    exclude_locked: bool = True,
    exclude_complete: bool = True,
    exclude_at_limit: bool = False,
    verdict_filter: str | None = None,
    limit: int = 20,
    db_path: str | Path = DEFAULT_DB_PATH,
    exclude_patterns: list[str] | None = None,
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
    skip_boilerplate: bool = False,
    unicorn_verdict: str | None = None,
    unicorn_class: str | None = None,
    unicorn_confidence: str | None = None,
    min_unicorn_harness_version: int | None = None,
    is_stub: bool | None = None,
    objdiff_pattern: str | None = None,
    pattern_ruler: str = "name_check",
    stale_units: str = "flag",
    include_unverifiable: bool = False,
) -> "FunctionQueryResult":
    """
    Query multiple functions matching criteria.

    Args:
        verdict_filter: If set, only return functions with this verdict
                        (e.g. 'COMPLETE', 'AT_LIMIT'). Overrides exclude_* flags.
        exclude_patterns: Glob patterns for units to exclude (default: XDK)
        max_attempts: Skip functions with >= this many attempts (None to disable)
        unicorn_verdict: Filter by unicorn verdict (DIVERGENT, EQUIVALENT, SKIPPED, ERROR)
        unicorn_class: Filter by divergence class (logic, build_env, regalloc, ...)
        unicorn_confidence: Filter by confidence (high, stable_divergent,
                            input_sensitive, fixture_sensitive)
        min_unicorn_harness_version: Only rows whose unicorn verdict was produced
                            by harness version >= N. Pass 4 to exclude every
                            verdict measured before the 2026-08-18/19 defect
                            fixes (that harness overstated real bugs ~8x). NULL
                            harness_version rows are always excluded by this
                            filter. See scripts/unicorn_runner/signal_version.py.
        objdiff_pattern:    Only rows carrying this objdiff pattern in the LATEST
                            `pattern_scans` row for `pattern_ruler` -- e.g.
                            'WRONG_CALLEE', 'TEMPLATE_INSTANTIATION_MISMATCH'
                            (the two 4.2.6 marks LikelyFixable), 'LINKER_MERGED',
                            'REGISTER_SAVE_HELPER_MISMATCH'. This is NOT a `has_*`
                            column: it joins `function_patterns`, so a row only
                            appears if some scan under that ruler actually
                            examined it and the detector fired.
        pattern_ruler:      `functionRelocDiffs` value the pattern was measured
                            under. Defaults to 'name_check', the graded ruler and
                            report.json's. Passing 'none' RAISES: the callee,
                            prologue, MakeString and scope detectors cannot fire
                            under it, so every bucket would come back empty --
                            an answer that reads exactly like "this class is
                            exhausted". See docs/analysis/2026-08-21-pattern-
                            census-4.2.6.md.
        stale_units:        What to do with rows whose UNIT's objects have moved
                            since that scan measured them ('flag', the default,
                            'exclude', or 'ignore'). Only consulted when
                            `objdiff_pattern` is set, because only then is a
                            stored finding being served as a fact.

                            A pattern scan is a set of findings about 2,224
                            object PAIRS, and both sides move: the target objects
                            are rewritten by any `symbols.txt` edit, the base
                            objects by any landed commit. Since schema v18 each
                            scan records a per-unit content hash of both sides
                            (`pattern_scan_units`), so "is this row still about
                            the objects on disk?" is answerable per unit -- which
                            is the only grain at which the answer is usable, a
                            whole-tree verdict being permanently red here.

                            'flag' adds `unit_objects_stale` to every returned
                            row: None when the unit is current, otherwise the
                            direction ('target', 'base', 'both', 'unrecorded',
                            'removed'), or 'unfingerprinted' when the scan
                            predates the table and cannot be checked at all.
                            'exclude' drops the stale rows -- ask for it
                            deliberately, because a shrunken result set reads
                            exactly like "this class is exhausted", which is the
                            failure mode this whole subsystem exists to prevent.

        include_unverifiable: Only consulted for the callee-NAME patterns
                            (`PAIRING_SENSITIVE_PATTERNS`). False (the default)
                            EXCLUDES rows whose same-scan pattern set also
                            contains `UNVERIFIABLE_PAIRING` -- objdiff's own
                            declaration that the enclosing symbol pair was
                            guessed by masked byte signature, which is what an
                            unnamed `fn_<addr>` EH funclet always is. On such a
                            row the differing `bl` is evidence about objdiff's
                            pairing, not about our source: 56 of scan 18's 62
                            WRONG_CALLEE rows are of that shape. True returns
                            them. Either way `FunctionQueryResult
                            .unverifiable_hidden` / `.unverifiable_note` carry
                            the count, so the subtraction is never silent.

                            Unverifiable is not uninformative -- those rows still
                            answer "does our tree emit that callee ANYWHERE?".
                            That is `scripts/analysis/callee_emitted_anywhere.py`,
                            not this filter.

    Returns a `FunctionQueryResult` (a list of function dicts, plus the
    hidden-row accounting).

    Raises:
        ValueError: `pattern_ruler='none'` with an `objdiff_pattern` set, or a
                    pattern for which no scan under that ruler exists (absence of
                    a measurement must not read as absence of the pattern).
    """
    conn = get_connection(db_path)

    # Use default exclusions if not specified
    if exclude_patterns is None:
        exclude_patterns = DEFAULT_EXCLUDE_PATTERNS

    glob_clause, glob_params = _build_unit_glob_clause(pattern, exclude_patterns)

    query = f"""
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, verdict_reason, locked_by, attempt_count, is_stub,
               unicorn_verdict, unicorn_class, unicorn_harness_version
        FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
    """
    params: list[Any] = glob_params + [min_percent, max_percent]

    if exclude_locked:
        query += " AND locked_by IS NULL"

    if verdict_filter:
        # Positive filter: only return functions with this specific verdict
        query += f" AND verdict = '{verdict_filter}'"
    else:
        # Negative filter: exclude specified verdicts
        excluded_verdicts = []
        if exclude_complete:
            excluded_verdicts.append('COMPLETE')
        if exclude_at_limit:
            excluded_verdicts.append('AT_LIMIT')
        if excluded_verdicts:
            placeholders = ", ".join(f"'{v}'" for v in excluded_verdicts)
            query += f" AND (verdict IS NULL OR verdict NOT IN ({placeholders}))"

    query += f" AND {_EXCLUDE_MERGED}"
    query += f" AND {_EXCLUDE_STLPMTX}"

    if skip_boilerplate:
        for prefix in BOILERPLATE_SYMBOL_PREFIXES:
            query += f" AND {like_prefix_clause('symbol', prefix)}"

    # Stub filter
    if is_stub is not None:
        query += f" AND is_stub = {1 if is_stub else 0}"

    # objdiff pattern filter -- joins the measured table, never a has_* column.
    pattern_scan_id: int | None = None
    if objdiff_pattern:
        if pattern_ruler == "none":
            raise ValueError(
                "objdiff_pattern with pattern_ruler='none' is refused: under "
                "functionRelocDiffs=none, 10 of objdiff 4.2.6's 25 detectors "
                "cannot fire at all (measured whole-binary 2026-08-21), so the "
                "result would be an empty set that reads like an exhausted "
                "class. Use 'name_check' (graded, report.json's) or 'all'."
            )
        scan = conn.execute(
            "SELECT id FROM pattern_scans WHERE ruler = ? ORDER BY id DESC LIMIT 1",
            (pattern_ruler,)).fetchone()
        if scan is None:
            raise ValueError(
                f"no pattern_scans row for ruler={pattern_ruler!r}, so this "
                f"query cannot distinguish 'no function has {objdiff_pattern}' "
                f"from 'nobody has measured it'. Run "
                f"scripts/analysis/pattern_census.py --ruler {pattern_ruler} "
                f"--apply first."
            )
        query += (" AND id IN (SELECT function_id FROM function_patterns "
                  "WHERE scan_id = ? AND pattern = ?)")
        params.extend([scan[0], objdiff_pattern])
        pattern_scan_id = scan[0]

    # Unicorn verdict filter
    if unicorn_verdict:
        query += f" AND unicorn_verdict = '{unicorn_verdict}'"
        if unicorn_class:
            query += f" AND unicorn_class = '{unicorn_class}'"

    if unicorn_confidence:
        query += f" AND unicorn_confidence = '{unicorn_confidence}'"

    if min_unicorn_harness_version is not None:
        query += (f" AND unicorn_harness_version IS NOT NULL"
                  f" AND unicorn_harness_version >= {int(min_unicorn_harness_version)}")

    # Exclude functions that have been tried too many times
    if max_attempts is not None:
        query += f" AND (attempt_count IS NULL OR attempt_count < {max_attempts})"

    # Subtract the rows objdiff itself declares unverifiable.
    #
    # This runs LAST among the WHERE clauses on purpose: the hidden COUNT has to
    # be taken under every other filter the caller asked for, or it is not
    # comparable to len(rows) and the note becomes its own small lie. Composing
    # the finished WHERE two ways -- NOT EXISTS for the answer, EXISTS for the
    # count -- is what makes the two numbers a partition of one population.
    hidden_count = 0
    vocab_missing = False
    if objdiff_pattern and objdiff_pattern in PAIRING_SENSITIVE_PATTERNS:
        vocab_missing = _pairing_vocabulary_missing(conn, pattern_scan_id)
        pairing = (" {op} (SELECT 1 FROM function_patterns up "
                   "WHERE up.scan_id = ? AND up.function_id = functions.id "
                   "AND up.pattern = '" + UNVERIFIABLE_PAIRING + "')")
        if vocab_missing:
            # The instrument that took this scan could not fire the detector, so
            # whatever the table does or does not hold is not a measurement of
            # the pairing class. Subtracting on it would manufacture a net set
            # out of an instrument's blind spot. Serve the gross set and SAY so.
            pass
        else:
            # Counted whether or not it is subtracted: with the opt-in the note
            # says how many of the rows SHOWN are pairing guesses, which is the
            # same fact from the other side.
            hidden_count = conn.execute(
                "SELECT COUNT(*) FROM (" + query
                + pairing.format(op="AND EXISTS") + ")",
                params + [pattern_scan_id]).fetchone()[0]
            if not include_unverifiable:
                query += pairing.format(op="AND NOT EXISTS")
                params.append(pattern_scan_id)

    query += """
        ORDER BY
            CASE WHEN current_percent IS NULL THEN 1 ELSE 0 END,
            current_percent DESC
        LIMIT ?
    """
    params.append(limit)

    rows = conn.execute(query, params).fetchall()
    out = FunctionQueryResult(dict(row) for row in rows)
    if objdiff_pattern and stale_units != "ignore":
        out = FunctionQueryResult(
            _attach_unit_currency(conn, pattern_scan_id, list(out),
                                  drop_stale=(stale_units == "exclude")))
    out.unverifiable_pattern = objdiff_pattern
    out.unverifiable_hidden = hidden_count
    out.unverifiable_note = _render_unverifiable_note(
        objdiff_pattern, hidden_count, include_unverifiable, vocab_missing)
    return out


def _render_unverifiable_note(pattern: str | None, hidden: int,
                              included: bool, vocab_missing: bool) -> str:
    """The one line that keeps the subtraction from reading as absence."""
    if pattern is None or pattern not in PAIRING_SENSITIVE_PATTERNS:
        return ""
    if vocab_missing:
        return (f"Note: this scan's detector vocabulary has no "
                f"{UNVERIFIABLE_PAIRING} (objdiff < 4.2.7), so the "
                f"byte-signature-paired funclet rows CANNOT be subtracted here "
                f"and are still in this result. Re-derive with "
                f"scripts/analysis/pattern_census.py --ruler name_check --apply.")
    if hidden == 0:
        return ""
    if included:
        return (f"Note: {hidden} of these rows are shown only because "
                f"include_unverifiable was set: enclosing symbol is a "
                f"byte-signature-paired funclet ({UNVERIFIABLE_PAIRING} in the "
                f"same scan), so the differing callee is objdiff's pairing "
                f"guess, not evidence about our source.")
    return (f"Note: {hidden} rows hidden: enclosing symbol is a "
            f"byte-signature-paired funclet ({UNVERIFIABLE_PAIRING} in the same "
            f"scan), so the 'wrong callee' is objdiff's arbitrary pick among "
            f"byte-identical funclets and cannot be adjudicated as written. "
            f"Pass include_unverifiable=true to see them; they DO answer 'does "
            f"our tree emit that callee anywhere?' -- "
            f"scripts/analysis/callee_emitted_anywhere.py.")


def _attach_unit_currency(conn: sqlite3.Connection, scan_id: int,
                          rows: list[dict[str, Any]], *,
                          drop_stale: bool) -> list[dict[str, Any]]:
    """Mark each row with whether ITS unit's objects still match the scan's.

    A pattern row is a finding about one target object diffed against one base
    object.  `pattern_scans` records which objdiff took the measurement; since
    v18 `pattern_scan_units` records those two objects' content hashes, so a
    stored finding can be served WITH its currency instead of as a current fact.

    Deliberately never raises.  `query_functions` is the work-selection index a
    lane calls first, and wedging it would only move people to raw SQL, which
    reports nothing at all.  The failure modes surface as VALUES instead: a
    pre-v18 scan flags every row `unfingerprinted`, an unreadable `objdiff.json`
    flags `unknown`.  Both are louder than the silent `None` a green reading
    would have looked like.
    """
    try:
        from . import object_baseline
    except ImportError:                       # pragma: no cover - packaging only
        import object_baseline                # type: ignore[no-redef]

    recorded: dict[str, dict[str, str | None]] = {}
    try:
        for unit, tgt, base in conn.execute(
                "SELECT unit, target_sha256, base_sha256 FROM pattern_scan_units "
                "WHERE scan_id = ?", (scan_id,)):
            recorded[unit] = {"target": tgt, "base": base}
    except sqlite3.OperationalError:          # table absent: pre-v18 database
        recorded = {}

    if not recorded:
        for r in rows:
            r["unit_objects_stale"] = "unfingerprinted"
        return rows

    project_root = Path(__file__).resolve().parents[2]
    try:
        stale = object_baseline.compare_fingerprints(
            recorded, object_baseline.fingerprint_units(project_root))
    except object_baseline.ObjectBaselineError:
        for r in rows:
            r["unit_objects_stale"] = "unknown"
        return rows

    for r in rows:
        r["unit_objects_stale"] = stale.get(r.get("unit"))
    if drop_stale:
        return [r for r in rows if not r["unit_objects_stale"]]
    return rows


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
    pre_refactor_patch: str | None = None,
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
             actual_cost_usd, duration_ms, enrichment_flags, pre_refactor_patch)
        VALUES (?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, ?, ?, ?, ?, ?, ?, ?,
                ?, ?, ?, ?, ?, ?, ?, ?)
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
            pre_refactor_patch,
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
    verdict_reason: str | None = None,
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

    if verdict_reason is not None:
        updates.append("verdict_reason = ?")
        params.append(verdict_reason)

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
    exclude_at_limit: bool = False,
    db_path: str | Path = DEFAULT_DB_PATH,
    exclude_patterns: list[str] | None = None,
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
) -> dict[str, Any]:
    """
    Get statistics about functions that would be targeted by a batch run.

    Uses the same filters as get_next_function so counts accurately reflect
    what will actually be processed.

    Args:
        pattern: Glob pattern(s) for unit (e.g., "src/system/char/*" or list of patterns)
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage
        limit: Max functions to process (0 = unlimited)
        exclude_at_limit: Also exclude AT_LIMIT verdicts (default: only COMPLETE excluded)
        db_path: Database path
        exclude_patterns: Glob patterns for units to exclude (default: XDK/link_glue/binkxenon)
        max_attempts: Skip functions with >= this many attempts (None to disable)

    Returns:
        Dict with counts and breakdowns
    """
    conn = get_connection(db_path)

    # Use default exclusions if not specified (same as get_next_function)
    if exclude_patterns is None:
        exclude_patterns = DEFAULT_EXCLUDE_PATTERNS
    glob_clause, glob_params = _build_unit_glob_clause(pattern, exclude_patterns)

    # Count functions matching pattern (total in scope)
    total_matching = conn.execute(
        f"SELECT COUNT(*) FROM functions WHERE {glob_clause}",
        glob_params,
    ).fetchone()[0]

    # Symbol filters matching get_next_function
    symbol_filter = (
        f" AND {_EXCLUDE_MERGED}"
        f" AND {_EXCLUDE_FN}"
        " AND symbol != 'OnlyReturns'"
    )

    # Max attempts filter matching get_next_function
    attempts_filter = ""
    if max_attempts is not None:
        attempts_filter = f" AND (attempt_count IS NULL OR attempt_count < {max_attempts})"

    # Count functions in match percentage range
    in_range = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
        """ + symbol_filter,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count locked functions in range
    locked = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NOT NULL
        """ + symbol_filter,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Build excluded verdicts list
    excluded_verdicts = ['COMPLETE']
    if exclude_at_limit:
        excluded_verdicts.append('AT_LIMIT')
    verdict_placeholders = ", ".join(f"'{v}'" for v in excluded_verdicts)
    verdict_filter = f" AND (verdict IS NULL OR verdict NOT IN ({verdict_placeholders}))"

    # Count complete/at_limit functions in range
    excluded_verdict = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND verdict IN ({verdict_placeholders})
        """ + symbol_filter,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count available functions (not locked, not excluded by verdict, within attempt limit)
    available = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NULL
          {verdict_filter}
        """ + symbol_filter + attempts_filter,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count first-try functions (no attempts yet) among available
    first_tries = conn.execute(
        f"""
        SELECT COUNT(*) FROM functions
        WHERE {glob_clause}
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
          AND locked_by IS NULL
          {verdict_filter}
          AND (attempt_count IS NULL OR attempt_count = 0)
        """ + symbol_filter + attempts_filter,
        glob_params + [min_percent, max_percent],
    ).fetchone()[0]

    # Count functions skipped due to max_attempts (for display)
    exceeded_attempts = 0
    if max_attempts is not None:
        exceeded_attempts = conn.execute(
            f"""
            SELECT COUNT(*) FROM functions
            WHERE {glob_clause}
              AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
              AND locked_by IS NULL
              {verdict_filter}
              AND attempt_count >= {max_attempts}
            """ + symbol_filter,
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
        "exceeded_attempts": exceeded_attempts,
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

    # Pattern counts — query all 20 pattern columns in one go
    pattern_columns = [
        "has_linker_merged", "has_bool_mask", "has_makestring_mismatch",
        "has_address_relocation", "has_boolean_negation", "has_float_precision",
        "has_fsel_ternary", "has_float_to_int_to_float",
        "has_register_swap", "has_comparison_style", "has_control_flow",
        "has_commutative_op_order", "has_offset_swap",
        "has_anonymous_namespace_hash", "has_static_guard_counter",
        "has_dynamic_cast_mismatch", "has_dead_store_elimination",
        "has_prologue_mismatch", "has_alloca_mismatch",
        "has_scope_counter_mismatch",
    ]
    sums = ", ".join(f"COALESCE(SUM({col}), 0)" for col in pattern_columns)
    row = conn.execute(f"SELECT {sums} FROM functions").fetchone()
    pattern_counts = {col: row[i] for i, col in enumerate(pattern_columns)}

    # Map column names to stats keys
    result = {
        "total_functions": total,
        "complete": complete,
        "at_limit": at_limit,
        "locked": locked,
        "with_percent": with_percent,
        "total_attempts": total_attempts,
        "avg_percent": round(avg_percent, 2) if avg_percent else None,
    }
    for col, count in pattern_counts.items():
        # has_linker_merged -> pattern_merged, has_bool_mask -> pattern_bool_mask, etc.
        key = "pattern_" + col.removeprefix("has_").removeprefix("linker_")
        result[key] = count

    return result


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


def search_functions_by_name(
    name: str,
    limit: int = 5,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Fuzzy search for functions by name.

    Useful for suggesting correct symbols when an exact match fails.
    Searches both mangled symbol and demangled name fields.

    Args:
        name: Search term (class::method, method name, or partial symbol)
        limit: Max results to return
        db_path: Database path

    Returns:
        List of function dicts with symbol, demangled, unit, current_percent
    """
    conn = get_connection(db_path)

    # Try multiple search strategies in order of specificity
    results = []

    # Strategy 1: LIKE match on demangled name
    rows = conn.execute(
        """
        SELECT symbol, demangled, unit, current_percent
        FROM functions
        WHERE demangled LIKE ?
        ORDER BY current_percent DESC NULLS LAST
        LIMIT ?
        """,
        (f"%{name}%", limit),
    ).fetchall()
    results.extend(dict(r) for r in rows)

    if len(results) >= limit:
        return results[:limit]

    # Strategy 2: LIKE match on mangled symbol (for partial mangled names)
    seen_symbols = {r["symbol"] for r in results}
    rows = conn.execute(
        """
        SELECT symbol, demangled, unit, current_percent
        FROM functions
        WHERE symbol LIKE ? AND symbol NOT IN ({})
        ORDER BY current_percent DESC NULLS LAST
        LIMIT ?
        """.format(",".join("?" * len(seen_symbols)) if seen_symbols else "'__none__'"),
        [f"%{name}%"] + list(seen_symbols) + [limit - len(results)],
    ).fetchall()
    results.extend(dict(r) for r in rows)

    return results[:limit]


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
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
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
        max_attempts: Skip functions with >= this many attempts (None to disable)

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

    query += f" AND {_EXCLUDE_MERGED}"
    query += f" AND {_EXCLUDE_STLPMTX}"

    # Exclude functions that have been tried too many times
    if max_attempts is not None:
        query += f" AND (attempt_count IS NULL OR attempt_count < {max_attempts})"

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
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
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
        max_attempts: Skip functions with >= this many attempts (None to disable)

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

    query += f" AND {like_prefix_clause('f.symbol', 'merged_')}"
    query += " AND f.demangled NOT LIKE '%stlpmtx\\_std::%' ESCAPE '\\'"

    # Exclude functions that have been tried too many times
    if max_attempts is not None:
        query += f" AND (f.attempt_count IS NULL OR f.attempt_count < {max_attempts})"

    query += """
        ORDER BY f.priority_score DESC
        LIMIT ?
    """
    params.append(limit)

    rows = conn.execute(query, params).fetchall()
    return [dict(row) for row in rows]


def query_divergent_logic(
    min_priority: float = 0,
    min_percent: float = 0,
    max_percent: float = 100,
    exclude_locked: bool = True,
    limit: int = 20,
    db_path: str | Path = DEFAULT_DB_PATH,
    max_attempts: int | None = DEFAULT_MAX_ATTEMPTS,
) -> list[dict[str, Any]]:
    """
    Query DIVERGENT functions with logic class (real behavioral bugs).

    Filters for:
    - unicorn_verdict = 'DIVERGENT' (behavior differs from target)
    - unicorn_class = 'logic' (real bugs, not build_env/regalloc artifacts)
    - verdict IS NULL (not yet reported/decided)
    - has_linker_merged = 0 (no unfixable ICF-merged calls)

    These are real bugs to fix - functions that compile but behave differently
    from the target. The "logic" class excludes unfixable build_env and regalloc
    artifacts.

    Args:
        min_priority: Minimum priority_score threshold
        min_percent: Minimum current_percent
        max_percent: Maximum current_percent
        exclude_locked: Skip functions locked by other agents
        limit: Max results to return
        db_path: Database path
        max_attempts: Skip functions with >= this many attempts (None to disable)

    Returns:
        List of function dicts sorted by priority_score DESC, current_percent DESC
    """
    conn = get_connection(db_path)

    query = """
        SELECT id, symbol, demangled, unit, size, current_percent, best_percent,
               verdict, locked_by, attempt_count, unicorn_verdict, unicorn_class,
               has_linker_merged, priority_score
        FROM functions
        WHERE unicorn_verdict = 'DIVERGENT'
          AND unicorn_class = 'logic'
          AND verdict IS NULL
          AND has_linker_merged = 0
          AND excluded = 0
          AND (current_percent IS NULL OR (current_percent >= ? AND current_percent <= ?))
    """
    params: list[Any] = [min_percent, max_percent]

    if min_priority > 0:
        query += " AND (priority_score IS NOT NULL AND priority_score >= ?)"
        params.append(min_priority)

    if exclude_locked:
        query += " AND locked_by IS NULL"

    if max_attempts is not None:
        query += f" AND (attempt_count IS NULL OR attempt_count < {max_attempts})"

    query += """
        ORDER BY priority_score DESC, current_percent DESC
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


def get_unit_success_rates(
    days: int = 7,
    min_attempts: int = 5,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> dict[str, float]:
    """
    Calculate empirical success rates by unit from recent attempt data.

    Success = attempt resulted in 'complete' status.

    Args:
        days: Number of days of recent data to use (default: 7)
        min_attempts: Minimum attempts required for a unit to be included (default: 5)
        db_path: Database path

    Returns:
        Dict mapping unit path -> success rate (0.0 to 1.0)
    """
    conn = get_connection(db_path)

    query = """
        SELECT
            f.unit,
            COUNT(*) as attempts,
            SUM(CASE WHEN a.exit_status = 'complete' THEN 1 ELSE 0 END) as completions
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.started_at >= datetime('now', ?)
          AND f.unit IS NOT NULL
        GROUP BY f.unit
        HAVING COUNT(*) >= ?
    """

    rows = conn.execute(query, (f'-{days} days', min_attempts)).fetchall()

    result = {}
    for row in rows:
        unit = row["unit"]
        attempts = row["attempts"]
        completions = row["completions"]
        result[unit] = completions / attempts if attempts > 0 else 0.0

    return result


def get_function_type_success_rates(
    days: int = 7,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> dict[str, float]:
    """
    Calculate empirical success rates by function type from recent attempt data.

    Args:
        days: Number of days of recent data to use (default: 7)
        db_path: Database path

    Returns:
        Dict mapping function type -> success rate (0.0 to 1.0)
    """
    conn = get_connection(db_path)

    query = """
        SELECT
            CASE
                WHEN f.is_destructor = 1 THEN 'destructor'
                WHEN f.is_constructor = 1 THEN 'constructor'
                WHEN f.is_virtual = 1 THEN 'virtual'
                ELSE 'other'
            END as func_type,
            COUNT(*) as attempts,
            SUM(CASE WHEN a.exit_status = 'complete' THEN 1 ELSE 0 END) as completions
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.started_at >= datetime('now', ?)
        GROUP BY func_type
    """

    rows = conn.execute(query, (f'-{days} days',)).fetchall()

    result = {}
    for row in rows:
        func_type = row["func_type"]
        attempts = row["attempts"]
        completions = row["completions"]
        result[func_type] = completions / attempts if attempts > 0 else 0.0

    return result


def get_size_bucket_success_rates(
    days: int = 7,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> dict[str, float]:
    """
    Calculate empirical success rates by function size bucket from recent attempt data.

    Args:
        days: Number of days of recent data to use (default: 7)
        db_path: Database path

    Returns:
        Dict mapping size bucket -> success rate (0.0 to 1.0)
    """
    conn = get_connection(db_path)

    query = """
        SELECT
            CASE
                WHEN f.size < 50 THEN 'tiny'
                WHEN f.size < 150 THEN 'small'
                WHEN f.size < 400 THEN 'medium'
                WHEN f.size < 1000 THEN 'large'
                ELSE 'huge'
            END as size_bucket,
            COUNT(*) as attempts,
            SUM(CASE WHEN a.exit_status = 'complete' THEN 1 ELSE 0 END) as completions
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.started_at >= datetime('now', ?)
        GROUP BY size_bucket
    """

    rows = conn.execute(query, (f'-{days} days',)).fetchall()

    result = {}
    for row in rows:
        bucket = row["size_bucket"]
        attempts = row["attempts"]
        completions = row["completions"]
        result[bucket] = completions / attempts if attempts > 0 else 0.0

    return result


# ============================================================================
# Merged Symbol Tracking Functions (v6)
# ============================================================================


def upsert_merged_symbol(
    function_id: int,
    symbol_name: str,
    call_count: int = 1,
    category: str | None = None,
    resolved_symbols: list[str] | None = None,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> int:
    """
    Insert or update a merged symbol record for a function.

    Args:
        function_id: Database ID of the function
        symbol_name: Merged symbol name (e.g., "merged_824D1870")
        call_count: Number of times this merged symbol is called
        category: Category of merged symbol ('addtostrings', 'makestring', etc.)
        resolved_symbols: List of demangled names at this merged address
        db_path: Database path

    Returns:
        Row ID of the inserted/updated record
    """
    conn = get_connection(db_path)

    resolved_json = json.dumps(resolved_symbols) if resolved_symbols else None

    # Check if exists
    existing = conn.execute(
        "SELECT id FROM merged_symbols WHERE function_id = ? AND symbol_name = ?",
        (function_id, symbol_name),
    ).fetchone()

    if existing:
        conn.execute(
            """
            UPDATE merged_symbols SET
                call_count = ?,
                category = COALESCE(?, category),
                resolved_symbols = COALESCE(?, resolved_symbols)
            WHERE id = ?
            """,
            (call_count, category, resolved_json, existing["id"]),
        )
        conn.commit()
        return existing["id"]
    else:
        cursor = conn.execute(
            """
            INSERT INTO merged_symbols
                (function_id, symbol_name, call_count, category, resolved_symbols)
            VALUES (?, ?, ?, ?, ?)
            """,
            (function_id, symbol_name, call_count, category, resolved_json),
        )
        conn.commit()
        return cursor.lastrowid


def update_function_merged_flags(
    function_id: int,
    has_addtostrings: bool = False,
    has_makestring: bool = False,
    has_setobjconcrete: bool = False,
    merged_symbol_count: int = 0,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> None:
    """Update the granular merged symbol flags on a function."""
    conn = get_connection(db_path)
    conn.execute(
        """
        UPDATE functions SET
            has_addtostrings = ?,
            has_makestring = ?,
            has_setobjconcrete = ?,
            merged_symbol_count = ?,
            updated_at = CURRENT_TIMESTAMP
        WHERE id = ?
        """,
        (has_addtostrings, has_makestring, has_setobjconcrete, merged_symbol_count, function_id),
    )
    conn.commit()


def get_function_merged_symbols(
    function_id: int, db_path: str | Path = DEFAULT_DB_PATH
) -> list[dict[str, Any]]:
    """Get all merged symbols for a function."""
    conn = get_connection(db_path)
    rows = conn.execute(
        """
        SELECT id, symbol_name, call_count, category, resolved_symbols
        FROM merged_symbols
        WHERE function_id = ?
        ORDER BY call_count DESC
        """,
        (function_id,),
    ).fetchall()

    results = []
    for row in rows:
        d = dict(row)
        if d["resolved_symbols"]:
            d["resolved_symbols"] = json.loads(d["resolved_symbols"])
        results.append(d)
    return results


def query_functions_by_merged_category(
    category: str,
    min_percent: float = 0,
    max_percent: float = 100,
    limit: int = 50,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Query functions that have merged symbols of a specific category.

    Args:
        category: Merged symbol category ('addtostrings', 'makestring', 'setobjconcrete', 'destructor', 'unknown')
        min_percent: Minimum match percentage
        max_percent: Maximum match percentage
        limit: Max results to return
        db_path: Database path

    Returns:
        List of function dicts with merged symbol info
    """
    conn = get_connection(db_path)

    rows = conn.execute(
        """
        SELECT DISTINCT f.id, f.symbol, f.demangled, f.unit, f.current_percent,
               f.verdict, f.merged_symbol_count, f.has_addtostrings, f.has_makestring
        FROM functions f
        JOIN merged_symbols ms ON f.id = ms.function_id
        WHERE ms.category = ?
          AND f.excluded = 0
          AND (f.current_percent IS NULL OR (f.current_percent >= ? AND f.current_percent <= ?))
        ORDER BY f.current_percent DESC
        LIMIT ?
        """,
        (category, min_percent, max_percent, limit),
    ).fetchall()

    return [dict(row) for row in rows]


def get_merged_symbol_stats(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """Get statistics about merged symbols by category."""
    conn = get_connection(db_path)

    # Check if table has data
    total = conn.execute("SELECT COUNT(*) FROM merged_symbols").fetchone()[0]
    if total == 0:
        return {
            "populated": False,
            "message": "Run detect_patterns.py to populate merged symbol data",
        }

    # Category distribution
    categories = conn.execute(
        """
        SELECT category, COUNT(*) as count, COUNT(DISTINCT function_id) as functions
        FROM merged_symbols
        GROUP BY category
        ORDER BY count DESC
        """
    ).fetchall()

    # Function-level stats
    funcs_with_addtostrings = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_addtostrings = 1 AND excluded = 0"
    ).fetchone()[0]
    funcs_with_makestring = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_makestring = 1 AND excluded = 0"
    ).fetchone()[0]
    funcs_with_setobjconcrete = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_setobjconcrete = 1 AND excluded = 0"
    ).fetchone()[0]

    return {
        "populated": True,
        "total_merged_symbols": total,
        "category_distribution": {row["category"]: {"count": row["count"], "functions": row["functions"]} for row in categories},
        "functions_with_addtostrings": funcs_with_addtostrings,
        "functions_with_makestring": funcs_with_makestring,
        "functions_with_setobjconcrete": funcs_with_setobjconcrete,
    }


def clear_merged_symbols_for_function(
    function_id: int, db_path: str | Path = DEFAULT_DB_PATH
) -> int:
    """Clear all merged symbols for a function. Returns count deleted."""
    conn = get_connection(db_path)
    cursor = conn.execute(
        "DELETE FROM merged_symbols WHERE function_id = ?", (function_id,)
    )
    conn.commit()
    return cursor.rowcount


# ============================================================================
# RB3 Reference Tracking Functions
# ============================================================================


def batch_update_rb3_refs(
    updates: list[tuple[str, int]],
    db_path: str | Path = DEFAULT_DB_PATH,
) -> int:
    """
    Batch update has_rb3_ref for multiple functions.

    Args:
        updates: List of (symbol, has_ref) tuples where has_ref is 0 or 1
        db_path: Database path

    Returns:
        Number of rows updated
    """
    if not updates:
        return 0

    conn = get_connection(db_path)

    # Use executemany for efficiency
    cursor = conn.executemany(
        "UPDATE functions SET has_rb3_ref = ? WHERE symbol = ?",
        [(has_ref, symbol) for symbol, has_ref in updates],
    )
    conn.commit()
    return cursor.rowcount


def get_functions_for_unit(
    unit: str,
    db_path: str | Path = DEFAULT_DB_PATH,
) -> list[dict[str, Any]]:
    """
    Get all functions for a specific unit.

    Args:
        unit: Unit path (e.g., "default/system/char/CharBones")
        db_path: Database path

    Returns:
        List of function dicts with symbol and demangled
    """
    conn = get_connection(db_path)
    rows = conn.execute(
        """
        SELECT symbol, demangled
        FROM functions
        WHERE unit = ?
        """,
        (unit,),
    ).fetchall()
    return [dict(row) for row in rows]


def get_rb3_ref_stats(db_path: str | Path = DEFAULT_DB_PATH) -> dict[str, Any]:
    """Get statistics about RB3 reference coverage."""
    conn = get_connection(db_path)

    total = conn.execute("SELECT COUNT(*) FROM functions").fetchone()[0]
    with_ref = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE has_rb3_ref = 1"
    ).fetchone()[0]
    without_ref = total - with_ref

    return {
        "total_functions": total,
        "with_rb3_ref": with_ref,
        "without_rb3_ref": without_ref,
        "coverage_pct": round(100 * with_ref / total, 2) if total > 0 else 0,
    }


# ============================================================================
# Ghidra Decompilation Cache Functions (v7)
# ============================================================================


def get_decompilation(
    conn: sqlite3.Connection, symbol: str
) -> str | None:
    """Get cached decompilation for a symbol. Pure read, no Ghidra.

    Returns the decompilation code string, or None if not cached.
    Entries with non-NULL error are treated as cache misses.
    """
    row = conn.execute(
        "SELECT code, error FROM decompilations WHERE symbol = ?",
        (symbol,),
    ).fetchone()
    if row and row["error"] is None:
        return row["code"]
    return None


def get_xrefs(
    conn: sqlite3.Connection, symbol: str
) -> tuple[list[str], list[str]] | None:
    """Get cached cross-references for a symbol. Pure read, no Ghidra.

    Returns (callers, callees) lists, or None if not cached.
    Entries with non-NULL error are treated as cache misses.
    """
    row = conn.execute(
        "SELECT callers_json, callees_json, error FROM xrefs WHERE symbol = ?",
        (symbol,),
    ).fetchone()
    if row and row["error"] is None:
        callers = json.loads(row["callers_json"]) if row["callers_json"] else []
        callees = json.loads(row["callees_json"]) if row["callees_json"] else []
        return callers, callees
    return None


def put_decompilation(
    conn: sqlite3.Connection,
    symbol: str,
    address: str | None,
    code: str,
    signature: str | None = None,
    error: str | None = None,
) -> None:
    """Store a decompilation result in the cache."""
    conn.execute(
        """
        INSERT OR REPLACE INTO decompilations
            (symbol, address, code, signature, error, cached_at)
        VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
        """,
        (symbol, address, code, signature, error),
    )


def put_xrefs(
    conn: sqlite3.Connection,
    symbol: str,
    address: str | None,
    callers: list[str],
    callees: list[str],
    error: str | None = None,
) -> None:
    """Store cross-reference data in the cache."""
    conn.execute(
        """
        INSERT OR REPLACE INTO xrefs
            (symbol, address, callers_json, callees_json,
             callers_count, callees_count, error, cached_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
        """,
        (
            symbol,
            address,
            json.dumps(callers),
            json.dumps(callees),
            len(callers),
            len(callees),
            error,
        ),
    )


def get_cached_symbols(conn: sqlite3.Connection) -> set[str]:
    """Get set of symbols that have cached decompilations (for resume support)."""
    rows = conn.execute("SELECT symbol FROM decompilations").fetchall()
    return {row["symbol"] for row in rows}


def get_cache_stats(conn: sqlite3.Connection) -> dict[str, Any]:
    """Get statistics about the decompilation/xrefs cache."""
    decomp_total = conn.execute("SELECT COUNT(*) FROM decompilations").fetchone()[0]
    decomp_errors = conn.execute(
        "SELECT COUNT(*) FROM decompilations WHERE error IS NOT NULL"
    ).fetchone()[0]
    xrefs_total = conn.execute("SELECT COUNT(*) FROM xrefs").fetchone()[0]
    xrefs_errors = conn.execute(
        "SELECT COUNT(*) FROM xrefs WHERE error IS NOT NULL"
    ).fetchone()[0]

    return {
        "decompilations_total": decomp_total,
        "decompilations_ok": decomp_total - decomp_errors,
        "decompilations_errors": decomp_errors,
        "xrefs_total": xrefs_total,
        "xrefs_ok": xrefs_total - xrefs_errors,
        "xrefs_errors": xrefs_errors,
    }
