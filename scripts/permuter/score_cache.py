"""Persistent score cache — avoids redundant builds and objdiff runs.

Three layers of deduplication:
1. Source dedup: skip variants with identical source to baseline (no build)
2. Obj hash dedup: after build, hash .obj — skip objdiff if hash seen this session
3. Persistent cache: SQLite table mapping (symbol, source_md5) -> match% across sessions

The cache DB lives at `permuter_cache.db` in the project root.
"""

from __future__ import annotations

import hashlib
import sqlite3
from pathlib import Path
from typing import Optional


_CACHE_DB = Path(__file__).resolve().parent.parent.parent / "permuter_cache.db"

_SCHEMA = """
CREATE TABLE IF NOT EXISTS score_cache (
    symbol      TEXT NOT NULL,
    source_md5  TEXT NOT NULL,
    obj_md5     TEXT,
    match_pct   REAL NOT NULL,
    build_ok    INTEGER NOT NULL DEFAULT 1,
    timestamp   REAL NOT NULL,
    PRIMARY KEY (symbol, source_md5)
);
"""


def md5_bytes(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def md5_file(path: Path) -> str:
    return md5_bytes(path.read_bytes())


class ScoreCache:
    """Persistent + session-local score cache.

    Session-local:
        - source_seen: set of source_md5 hashes (source dedup)
        - obj_scores: dict of obj_md5 -> match_pct (obj hash dedup)

    Persistent (SQLite):
        - (symbol, source_md5) -> (obj_md5, match_pct, build_ok)
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
        self.misses = 0
        # Open DB
        self._conn = sqlite3.connect(str(db_path))
        self._conn.execute("PRAGMA journal_mode=WAL")
        self._conn.execute(_SCHEMA)
        self._conn.commit()

    def close(self):
        if self._conn:
            self._conn.close()
            self._conn = None

    def lookup_source(self, source_md5: str) -> Optional[tuple[float, bool]]:
        """Check persistent cache for a source hash.

        Returns (match_pct, build_ok) or None if not cached.
        """
        row = self._conn.execute(
            "SELECT match_pct, build_ok FROM score_cache "
            "WHERE symbol = ? AND source_md5 = ?",
            (self.symbol, source_md5),
        ).fetchone()
        if row is not None:
            self.hits_persistent += 1
            return (row[0], bool(row[1]))
        return None

    def lookup_obj(self, obj_md5: str) -> Optional[float]:
        """Check session-local obj hash cache.

        Returns match_pct or None if not seen this session.
        """
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
    ):
        """Store a score result in both session and persistent cache."""
        import time

        if obj_md5 is not None:
            self._obj_scores[obj_md5] = match_pct

        self._conn.execute(
            "INSERT OR REPLACE INTO score_cache "
            "(symbol, source_md5, obj_md5, match_pct, build_ok, timestamp) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            (self.symbol, source_md5, obj_md5, match_pct, int(build_ok), time.time()),
        )
        self._conn.commit()
        self.misses += 1

    def stats_summary(self) -> str:
        """Return a short summary of cache hit/miss stats."""
        total = self.hits_source + self.hits_obj + self.hits_persistent + self.misses
        if total == 0:
            return "cache: no lookups"
        hit_total = self.hits_source + self.hits_obj + self.hits_persistent
        return (
            f"cache: {hit_total}/{total} hits "
            f"(source={self.hits_source}, obj={self.hits_obj}, "
            f"persistent={self.hits_persistent}, builds={self.misses})"
        )

    def clear_symbol(self):
        """Clear all cached entries for the current symbol."""
        self._conn.execute(
            "DELETE FROM score_cache WHERE symbol = ?",
            (self.symbol,),
        )
        self._conn.commit()
