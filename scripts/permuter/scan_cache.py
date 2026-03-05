"""Scan cache — SQLite-backed cache for pattern_scan results.

Caches per-file, per-pattern hit counts keyed by file content hash.
When a file hasn't changed (same hash), its cached results are reused
instead of re-parsing and re-running all patterns.

Cache location: <repo_root>/.scan_cache.db
"""

from __future__ import annotations

import hashlib
import sqlite3
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
CACHE_DB = REPO_ROOT / ".scan_cache.db"


def _get_conn() -> sqlite3.Connection:
    conn = sqlite3.connect(str(CACHE_DB))
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS scan_hits (
            file_hash   TEXT NOT NULL,
            pattern     TEXT NOT NULL,
            func_name   TEXT NOT NULL,
            variant_count INTEGER NOT NULL,
            PRIMARY KEY (file_hash, pattern, func_name)
        )
    """)
    # Tracks which (file_hash, pattern) combos have been scanned,
    # even if they produced 0 hits
    conn.execute("""
        CREATE TABLE IF NOT EXISTS scan_done (
            file_hash   TEXT NOT NULL,
            pattern     TEXT NOT NULL,
            PRIMARY KEY (file_hash, pattern)
        )
    """)
    conn.execute("""
        CREATE INDEX IF NOT EXISTS idx_scan_hash_pattern
        ON scan_hits (file_hash, pattern)
    """)
    return conn


def hash_file(path: Path) -> str | None:
    """Return xxhash-style fast hash of file contents, or None if unreadable."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    return hashlib.md5(data).hexdigest()


def get_cached(conn: sqlite3.Connection, file_hash: str, pattern_name: str) -> list[tuple[str, int]] | None:
    """Get cached hits for a file+pattern combo.

    Returns list of (func_name, variant_count) or None if not cached.
    """
    # Check if this combo was ever scanned
    done = conn.execute(
        "SELECT 1 FROM scan_done WHERE file_hash = ? AND pattern = ?",
        (file_hash, pattern_name),
    ).fetchone()
    if done is None:
        return None  # Not cached

    rows = conn.execute(
        "SELECT func_name, variant_count FROM scan_hits "
        "WHERE file_hash = ? AND pattern = ?",
        (file_hash, pattern_name),
    ).fetchall()
    return rows


def store_hits(
    conn: sqlite3.Connection,
    file_hash: str,
    pattern_name: str,
    hits: list[tuple[str, int]],  # (func_name, variant_count)
):
    """Store scan hits for a file+pattern combo."""
    # Delete old entries for this hash+pattern
    conn.execute(
        "DELETE FROM scan_hits WHERE file_hash = ? AND pattern = ?",
        (file_hash, pattern_name),
    )
    if hits:
        # Deduplicate by func_name (sum variant counts for overloads)
        merged: dict[str, int] = {}
        for fn, vc in hits:
            merged[fn] = merged.get(fn, 0) + vc
        conn.executemany(
            "INSERT INTO scan_hits (file_hash, pattern, func_name, variant_count) "
            "VALUES (?, ?, ?, ?)",
            [(file_hash, pattern_name, fn, vc) for fn, vc in merged.items()],
        )
    # Mark this combo as scanned (upsert)
    conn.execute(
        "INSERT OR REPLACE INTO scan_done (file_hash, pattern) VALUES (?, ?)",
        (file_hash, pattern_name),
    )
    conn.commit()


def store_hits_batch(
    conn: sqlite3.Connection,
    entries: list[tuple[str, str, list[tuple[str, int]]]],
):
    """Store multiple (file_hash, pattern, hits) entries in a single transaction."""
    conn.execute("BEGIN")
    try:
        for file_hash, pattern_name, hits in entries:
            conn.execute(
                "DELETE FROM scan_hits WHERE file_hash = ? AND pattern = ?",
                (file_hash, pattern_name),
            )
            if hits:
                merged: dict[str, int] = {}
                for fn, vc in hits:
                    merged[fn] = merged.get(fn, 0) + vc
                conn.executemany(
                    "INSERT INTO scan_hits (file_hash, pattern, func_name, variant_count) "
                    "VALUES (?, ?, ?, ?)",
                    [(file_hash, pattern_name, fn, vc) for fn, vc in merged.items()],
                )
            conn.execute(
                "INSERT OR REPLACE INTO scan_done (file_hash, pattern) VALUES (?, ?)",
                (file_hash, pattern_name),
            )
        conn.execute("COMMIT")
    except Exception:
        conn.execute("ROLLBACK")
        raise


def invalidate_file(conn: sqlite3.Connection, file_hash: str):
    """Remove all cached entries for a file hash."""
    conn.execute("DELETE FROM scan_hits WHERE file_hash = ?", (file_hash,))
    conn.commit()


def clear_cache():
    """Delete the entire cache database."""
    if CACHE_DB.exists():
        CACHE_DB.unlink()


def cache_stats() -> dict:
    """Return cache statistics."""
    if not CACHE_DB.exists():
        return {"files": 0, "entries": 0, "size_kb": 0}

    conn = _get_conn()
    files = conn.execute(
        "SELECT COUNT(DISTINCT file_hash) FROM scan_hits"
    ).fetchone()[0]
    entries = conn.execute("SELECT COUNT(*) FROM scan_hits").fetchone()[0]
    conn.close()

    size_kb = CACHE_DB.stat().st_size // 1024
    return {"files": files, "entries": entries, "size_kb": size_kb}
