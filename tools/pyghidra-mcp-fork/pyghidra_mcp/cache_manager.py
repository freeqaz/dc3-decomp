"""
Decompilation caching layer for pyghidra-mcp.

Caches Ghidra decompilations to avoid repeated ~0.2s queries.
Uses SQLite for persistence and supports cache statistics/management.
"""

import hashlib
import json
import logging
import sqlite3
import threading
import time
from pathlib import Path
from typing import Optional

logger = logging.getLogger(__name__)


class CacheManager:
    """Manages decompilation cache with SQLite backend."""

    def __init__(self, cache_dir: Optional[Path] = None, enabled: bool = True):
        """Initialize cache manager.

        Args:
            cache_dir: Directory to store cache.db. Defaults to current directory.
            enabled: Whether caching is enabled. If False, all operations are no-ops.
        """
        self.enabled = enabled
        self.cache_dir = Path(cache_dir) if cache_dir else Path.cwd()
        self.db_path = self.cache_dir / "cache.db"
        self._lock = threading.RLock()

        if not self.enabled:
            logger.info("Caching disabled")
            return

        try:
            self.cache_dir.mkdir(parents=True, exist_ok=True)
            self._init_db()
            logger.info(f"Cache initialized at {self.db_path}")
        except Exception as e:
            logger.error(f"Failed to initialize cache: {e}")
            self.enabled = False

    def _init_db(self) -> None:
        """Initialize SQLite database with schema."""
        try:
            with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                # Enable WAL mode for thread-safe access
                conn.execute("PRAGMA journal_mode=WAL")
                conn.execute("PRAGMA synchronous=NORMAL")

                conn.execute(
                    """
                    CREATE TABLE IF NOT EXISTS decompilation_cache (
                        address TEXT NOT NULL,
                        binary_hash TEXT NOT NULL,
                        decompilation TEXT NOT NULL,
                        timestamp INTEGER NOT NULL,
                        hit_count INTEGER DEFAULT 0,
                        PRIMARY KEY (address, binary_hash)
                    )
                    """
                )

                conn.execute(
                    """
                    CREATE INDEX IF NOT EXISTS idx_binary_hash
                    ON decompilation_cache(binary_hash)
                    """
                )

                conn.execute(
                    """
                    CREATE INDEX IF NOT EXISTS idx_hit_count
                    ON decompilation_cache(hit_count)
                    """
                )

                conn.commit()
        except Exception as e:
            logger.error(f"Database initialization failed: {e}")
            raise

    def get(self, address: str, binary_hash: str) -> Optional[str]:
        """Get decompilation from cache.

        Args:
            address: Function address as string (e.g., "0x1004010")
            binary_hash: Hash of binary to validate cache validity

        Returns:
            Decompilation text or None if not cached
        """
        if not self.enabled:
            return None

        try:
            with self._lock:
                with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                    cursor = conn.execute(
                        """
                        SELECT decompilation FROM decompilation_cache
                        WHERE address = ? AND binary_hash = ?
                        """,
                        (address, binary_hash),
                    )
                    row = cursor.fetchone()

                    if row:
                        # Increment hit count
                        conn.execute(
                            """
                            UPDATE decompilation_cache
                            SET hit_count = hit_count + 1
                            WHERE address = ? AND binary_hash = ?
                            """,
                            (address, binary_hash),
                        )
                        conn.commit()
                        return row[0]

            return None
        except Exception as e:
            logger.error(f"Cache get failed: {e}")
            return None

    def put(self, address: str, binary_hash: str, decompilation: str) -> bool:
        """Store decompilation in cache.

        Args:
            address: Function address as string
            binary_hash: Hash of binary
            decompilation: Decompiled C code

        Returns:
            True if successful, False otherwise
        """
        if not self.enabled:
            return False

        try:
            with self._lock:
                with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                    current_time = int(time.time())
                    conn.execute(
                        """
                        INSERT OR REPLACE INTO decompilation_cache
                        (address, binary_hash, decompilation, timestamp, hit_count)
                        VALUES (?, ?, ?, ?, COALESCE(
                            (SELECT hit_count FROM decompilation_cache
                             WHERE address = ? AND binary_hash = ?), 0
                        ))
                        """,
                        (address, binary_hash, decompilation, current_time, address, binary_hash),
                    )
                    conn.commit()
            return True
        except Exception as e:
            logger.error(f"Cache put failed: {e}")
            return False

    def invalidate_on_binary_change(self, old_hash: str, new_hash: str) -> int:
        """Invalidate cache entries when binary changes.

        Args:
            old_hash: Previous binary hash
            new_hash: New binary hash

        Returns:
            Number of entries invalidated
        """
        if not self.enabled:
            return 0

        try:
            with self._lock:
                with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                    cursor = conn.execute(
                        "DELETE FROM decompilation_cache WHERE binary_hash = ?",
                        (old_hash,),
                    )
                    deleted = cursor.rowcount
                    conn.commit()
            return deleted
        except Exception as e:
            logger.error(f"Cache invalidation failed: {e}")
            return 0

    def clear(self) -> int:
        """Clear entire cache.

        Returns:
            Number of entries cleared
        """
        if not self.enabled:
            return 0

        try:
            with self._lock:
                with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                    cursor = conn.execute("DELETE FROM decompilation_cache")
                    deleted = cursor.rowcount
                    conn.commit()
            return deleted
        except Exception as e:
            logger.error(f"Cache clear failed: {e}")
            return 0

    def get_stats(self) -> dict:
        """Get cache statistics.

        Returns:
            Dictionary with cache stats
        """
        if not self.enabled:
            return {
                "enabled": False,
                "total_entries": 0,
                "total_hits": 0,
                "hit_rate": 0.0,
                "cache_size_mb": 0.0,
                "binary_hashes": 0,
            }

        try:
            with self._lock:
                with sqlite3.connect(self.db_path, timeout=5.0) as conn:
                    # Get entry count
                    cursor = conn.execute(
                        "SELECT COUNT(*) FROM decompilation_cache"
                    )
                    total_entries = cursor.fetchone()[0]

                    # Get total hits
                    cursor = conn.execute(
                        "SELECT SUM(hit_count) FROM decompilation_cache"
                    )
                    total_hits = cursor.fetchone()[0] or 0

                    # Get unique binary hashes
                    cursor = conn.execute(
                        "SELECT COUNT(DISTINCT binary_hash) FROM decompilation_cache"
                    )
                    binary_hashes = cursor.fetchone()[0]

                    # Calculate hit rate
                    hit_rate = 0.0
                    if total_entries > 0:
                        # Rough estimate: hits / (entries + hits)
                        hit_rate = (total_hits / (total_entries + total_hits)) * 100 if total_hits > 0 else 0.0

                    # Get database file size
                    cache_size_mb = 0.0
                    if self.db_path.exists():
                        cache_size_mb = self.db_path.stat().st_size / (1024 * 1024)

            return {
                "enabled": True,
                "total_entries": total_entries,
                "total_hits": total_hits,
                "hit_rate": round(hit_rate, 1),
                "cache_size_mb": round(cache_size_mb, 2),
                "binary_hashes": binary_hashes,
            }
        except Exception as e:
            logger.error(f"Failed to get cache stats: {e}")
            return {
                "enabled": True,
                "error": str(e),
            }


def compute_binary_hash(binary_path: Path) -> str:
    """Compute SHA256 hash of binary file.

    Args:
        binary_path: Path to binary file

    Returns:
        Hex digest of SHA256 hash
    """
    sha256 = hashlib.sha256()
    try:
        with open(binary_path, "rb") as f:
            for chunk in iter(lambda: f.read(8192), b""):
                sha256.update(chunk)
        return sha256.hexdigest()
    except Exception as e:
        logger.error(f"Failed to hash binary {binary_path}: {e}")
        # Return a dummy hash on error to ensure cache miss
        return f"error_{int(time.time())}"
