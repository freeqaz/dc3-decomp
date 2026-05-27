"""Persistent score cache — avoids redundant builds and objdiff runs.

Three layers of deduplication:
1. Source dedup: skip variants with identical source to baseline (no build)
2. Obj hash dedup: after build, hash .obj — skip objdiff if hash seen this session
3. Persistent cache: SQLite table mapping (symbol, source_md5) -> match% across sessions

Cache invalidation:
The persistent cache also stores a `dep_hash` — a digest of every transitive
header listed in the TU's compiler-generated .d file. On lookup, the dep_hash
is re-computed against the CURRENT header state; if any tracked header has
changed since the entry was stored, the entry is invalidated and treated as a
miss. This prevents stale "100% same" hits when a batched edit modifies a
header that the cached variant's .cpp transitively includes.

The cache DB lives at `permuter_cache.db` in the project root.
"""

from __future__ import annotations

import hashlib
import sqlite3
import threading
from pathlib import Path
from typing import Iterable, Optional

from .repo_paths import get_cache_db_path

_CACHE_DB = get_cache_db_path()

_SCHEMA = """
CREATE TABLE IF NOT EXISTS score_cache (
    symbol      TEXT NOT NULL,
    source_md5  TEXT NOT NULL,
    obj_md5     TEXT,
    match_pct   REAL NOT NULL,
    build_ok    INTEGER NOT NULL DEFAULT 1,
    timestamp   REAL NOT NULL,
    dep_hash    TEXT,
    PRIMARY KEY (symbol, source_md5)
);
"""


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def md5_file(path: Path) -> str:
    return md5_bytes(path.read_bytes())


def parse_dep_file(dep_path: Path) -> list[Path]:
    """Parse a compiler-generated .d file (Makefile dependency format) and
    return all listed dependency paths (excluding the .o target itself).

    Returns an empty list if the file doesn't exist or can't be parsed.
    """
    if not dep_path.exists():
        return []
    try:
        text = dep_path.read_text(errors="replace")
    except OSError:
        return []

    # Strip line continuations
    text = text.replace("\\\n", " ")
    deps: list[Path] = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        # Drop target side
        rhs = line.split(":", 1)[1]
        for tok in rhs.split():
            tok = tok.strip()
            if not tok or tok in ("\\",):
                continue
            deps.append(Path(tok))
    return deps


def compute_dep_hash(dep_path: Path) -> Optional[str]:
    """Hash every file listed in `dep_path` (a .d file). Returns None if the
    .d file is missing or empty — caller should treat as "no dep info" and
    refuse cache hits accordingly.

    The hash digests `(resolved_path, file_md5)` pairs sorted by path so the
    result is deterministic regardless of .d file ordering.
    """
    deps = parse_dep_file(dep_path)
    if not deps:
        return None

    entries: list[tuple[str, str]] = []
    for d in deps:
        try:
            resolved = d.resolve()
        except OSError:
            resolved = d
        try:
            h = md5_file(resolved)
        except OSError:
            # Missing dep file — record as a tombstone so a recreated file
            # invalidates the cache.
            h = "MISSING"
        entries.append((str(resolved), h))
    entries.sort(key=lambda e: e[0])

    digest = hashlib.md5()
    for path_str, file_h in entries:
        digest.update(path_str.encode("utf-8", errors="surrogateescape"))
        digest.update(b"\x00")
        digest.update(file_h.encode("ascii"))
        digest.update(b"\x00")
    return digest.hexdigest()


def _migrate_schema(conn: sqlite3.Connection) -> None:
    """Add the dep_hash column to legacy DBs that pre-date this feature."""
    cols = {row[1] for row in conn.execute("PRAGMA table_info(score_cache)")}
    if "dep_hash" not in cols:
        try:
            conn.execute("ALTER TABLE score_cache ADD COLUMN dep_hash TEXT")
            conn.commit()
        except sqlite3.OperationalError:
            pass


class ScoreCache:
    """Persistent + session-local score cache.

    Session-local:
        - source_seen: set of source_md5 hashes (source dedup)
        - obj_scores: dict of obj_md5 -> match_pct (obj hash dedup)

    Persistent (SQLite):
        - (symbol, source_md5) -> (obj_md5, match_pct, build_ok, dep_hash)
    """

    def __init__(self, symbol: str, db_path: Path = _CACHE_DB):
        self.symbol = symbol
        self._db_path = db_path
        # Session-local caches
        self._obj_scores: dict[str, float] = {}
        # Stats
        self.hits_source = 0
        self.hits_obj = 0
        self.hits_persistent = 0
        self.stale_dep = 0
        self.misses = 0
        # Lock guarding both _obj_scores and _conn: sqlite3 connections are not
        # safe to share across threads, and _obj_scores mutations must be atomic.
        self._lock = threading.Lock()
        # Open DB — check_same_thread=False lets us hold the connection in the
        # main thread and call it under _lock from worker threads.
        self._conn = sqlite3.connect(str(db_path), check_same_thread=False)
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute("PRAGMA busy_timeout=30000")
        self._conn.execute(_SCHEMA)
        _migrate_schema(self._conn)
        self._conn.commit()

    def close(self):
        with self._lock:
            if self._conn:
                self._conn.close()
                self._conn = None

    def lookup_source(
        self,
        source_md5: str,
        current_dep_hash: Optional[str] = None,
    ) -> Optional[tuple[float, bool]]:
        """Check persistent cache for a source hash.

        If `current_dep_hash` is provided, also verify the cached entry's
        stored `dep_hash` matches. A mismatch is treated as a miss and the
        stale entry is removed — this prevents 100% hits from outliving the
        header state that produced them.

        Returns (match_pct, build_ok) or None if not cached (or stale).
        """
        with self._lock:
            row = self._conn.execute(
                "SELECT match_pct, build_ok, dep_hash FROM score_cache "
                "WHERE symbol = ? AND source_md5 = ?",
                (self.symbol, source_md5),
            ).fetchone()
            if row is None:
                return None

            match_pct, build_ok, stored_dep_hash = row[0], bool(row[1]), row[2]

            # If we have a current dep_hash to compare against, enforce it.
            # A cached row with stored_dep_hash = NULL is from a legacy entry
            # written before this fix shipped — treat as stale to be safe (forces
            # one rebuild, then the new entry has a dep_hash).
            if current_dep_hash is not None:
                if stored_dep_hash is None or stored_dep_hash != current_dep_hash:
                    self.stale_dep += 1
                    self._conn.execute(
                        "DELETE FROM score_cache "
                        "WHERE symbol = ? AND source_md5 = ?",
                        (self.symbol, source_md5),
                    )
                    self._conn.commit()
                    return None

            self.hits_persistent += 1
            return (match_pct, build_ok)

    def lookup_obj(self, obj_md5: str) -> Optional[float]:
        """Check session-local obj hash cache.

        Returns match_pct or None if not seen this session.
        """
        with self._lock:
            if obj_md5 in self._obj_scores:
                self.hits_obj += 1
                return self._obj_scores[obj_md5]
            return None

    def store(
        self,
        source_md5: str,
        obj_md5: Optional[str],
        match_pct: float,
        build_ok: bool,
        dep_hash: Optional[str] = None,
    ):
        """Store a score result in both session and persistent cache."""
        import time

        with self._lock:
            if obj_md5 is not None:
                self._obj_scores[obj_md5] = match_pct

            self._conn.execute(
                "INSERT OR REPLACE INTO score_cache "
                "(symbol, source_md5, obj_md5, match_pct, build_ok, timestamp, dep_hash) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                (
                    self.symbol,
                    source_md5,
                    obj_md5,
                    match_pct,
                    int(build_ok),
                    time.time(),
                    dep_hash,
                ),
            )
            self._conn.commit()
            self.misses += 1

    def stats_summary(self) -> str:
        """Return a short summary of cache hit/miss stats."""
        total = (
            self.hits_source
            + self.hits_obj
            + self.hits_persistent
            + self.misses
        )
        if total == 0:
            return "cache: no lookups"
        hit_total = self.hits_source + self.hits_obj + self.hits_persistent
        stale_str = f", stale={self.stale_dep}" if self.stale_dep else ""
        return (
            f"cache: {hit_total}/{total} hits "
            f"(source={self.hits_source}, obj={self.hits_obj}, "
            f"persistent={self.hits_persistent}, builds={self.misses}"
            f"{stale_str})"
        )

    def clear_symbol(self):
        """Clear all cached entries for the current symbol."""
        with self._lock:
            self._conn.execute(
                "DELETE FROM score_cache WHERE symbol = ?",
                (self.symbol,),
            )
            self._conn.commit()
