"""Tests for the dep-hash cache invalidation fix.

The persistent score cache used to key only on (symbol, source_md5), which
silently returned stale "100% same" verdicts when a batched edit modified a
header that the cached variant's .cpp transitively included. The fix attaches
a dep_hash digest of every file listed in the .o's .d file and re-checks it
on every lookup.

Usage:
    python -m pytest scripts/permuter/tests/test_score_cache_dep_hash.py -x -q
"""

from __future__ import annotations

import sqlite3
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

_PROJECT_ROOT = Path(__file__).resolve().parents[3]
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from scripts.permuter.score_cache import (
    ScoreCache,
    compute_dep_hash,
    parse_dep_file,
)


class TestParseDepFile(unittest.TestCase):
    def test_simple_dep_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / "foo.d"
            d.write_text("foo.o: foo.cpp foo.h bar.h\n")
            deps = parse_dep_file(d)
            self.assertEqual(
                sorted(str(p) for p in deps),
                ["bar.h", "foo.cpp", "foo.h"],
            )

    def test_continuation_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / "foo.d"
            d.write_text("foo.o: foo.cpp \\\n\tbar.h \\\n\tbaz.h\n")
            deps = parse_dep_file(d)
            self.assertEqual(
                sorted(str(p) for p in deps),
                ["bar.h", "baz.h", "foo.cpp"],
            )

    def test_missing_file_returns_empty(self):
        deps = parse_dep_file(Path("/nonexistent/path/x.d"))
        self.assertEqual(deps, [])


class TestComputeDepHash(unittest.TestCase):
    def _setup_tu(self, tmp: Path, header_content: str) -> tuple[Path, Path]:
        """Create a .cpp + header + matching .d file. Return (d_path, header_path)."""
        cpp = tmp / "foo.cpp"
        hdr = tmp / "foo.h"
        d = tmp / "foo.d"
        cpp.write_text("/* dummy */\n")
        hdr.write_text(header_content)
        d.write_text(f"foo.o: {cpp} {hdr}\n")
        return d, hdr

    def test_hash_changes_when_header_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            d, hdr = self._setup_tu(tmp, "int x = 1;\n")
            h1 = compute_dep_hash(d)
            self.assertIsNotNone(h1)

            hdr.write_text("int x = 2;\n")  # mutate header
            h2 = compute_dep_hash(d)
            self.assertNotEqual(h1, h2)

    def test_hash_stable_when_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            d, _hdr = self._setup_tu(tmp, "int x = 1;\n")
            h1 = compute_dep_hash(d)
            h2 = compute_dep_hash(d)
            self.assertEqual(h1, h2)

    def test_missing_dep_file_returns_none(self):
        self.assertIsNone(compute_dep_hash(Path("/nonexistent/foo.d")))

    def test_missing_dep_target_tombstones_hash(self):
        # If a file listed in .d disappears, the dep_hash should reflect that
        # so a recreated file invalidates the cache.
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            d, hdr = self._setup_tu(tmp, "int x = 1;\n")
            h1 = compute_dep_hash(d)
            hdr.unlink()
            h2 = compute_dep_hash(d)
            self.assertNotEqual(h1, h2)


class TestScoreCacheDepHash(unittest.TestCase):
    def _new_cache(self, tmp: Path, symbol: str = "Foo__Fv") -> ScoreCache:
        return ScoreCache(symbol=symbol, db_path=tmp / "cache.db")

    def test_lookup_hit_when_dep_hash_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            c = self._new_cache(tmp)
            c.store("srcA", "objA", 100.0, True, dep_hash="depV1")
            res = c.lookup_source("srcA", current_dep_hash="depV1")
            self.assertEqual(res, (100.0, True))
            self.assertEqual(c.hits_persistent, 1)
            self.assertEqual(c.stale_dep, 0)
            c.close()

    def test_lookup_miss_when_dep_hash_changed(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            c = self._new_cache(tmp)
            c.store("srcA", "objA", 100.0, True, dep_hash="depV1")
            res = c.lookup_source("srcA", current_dep_hash="depV2")
            self.assertIsNone(res)
            self.assertEqual(c.hits_persistent, 0)
            self.assertEqual(c.stale_dep, 1)
            c.close()

    def test_stale_entry_is_evicted(self):
        """A mismatched dep_hash must delete the stale row so it doesn't keep
        triggering invalidations on every lookup."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            c = self._new_cache(tmp)
            c.store("srcA", "objA", 100.0, True, dep_hash="old")
            c.lookup_source("srcA", current_dep_hash="new")  # invalidates
            # Row should be gone now
            row = c._conn.execute(
                "SELECT match_pct FROM score_cache WHERE symbol=? AND source_md5=?",
                (c.symbol, "srcA"),
            ).fetchone()
            self.assertIsNone(row)
            c.close()

    def test_legacy_null_dep_hash_treated_as_stale(self):
        """Rows pre-dating the fix have dep_hash = NULL; treat as stale to
        force one rebuild before trusting them."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            c = self._new_cache(tmp)
            c.store("srcA", "objA", 100.0, True, dep_hash=None)
            res = c.lookup_source("srcA", current_dep_hash="anything")
            self.assertIsNone(res)
            self.assertEqual(c.stale_dep, 1)
            c.close()

    def test_lookup_without_current_dep_hash_bypasses_check(self):
        """If the caller doesn't provide a dep_hash, the cache behaves like
        the legacy path (returns whatever was stored). This preserves
        callers that aren't dep-hash-aware."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            c = self._new_cache(tmp)
            c.store("srcA", "objA", 100.0, True, dep_hash="depV1")
            res = c.lookup_source("srcA", current_dep_hash=None)
            self.assertEqual(res, (100.0, True))
            self.assertEqual(c.hits_persistent, 1)
            c.close()


class TestSchemaMigration(unittest.TestCase):
    def test_legacy_db_gets_dep_hash_column(self):
        """Open a DB that pre-dates the dep_hash column. The migration must
        add the column without losing existing rows."""
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            db = tmp / "legacy.db"
            # Hand-create the legacy schema (no dep_hash)
            conn = sqlite3.connect(str(db))
            conn.execute("""
                CREATE TABLE score_cache (
                    symbol      TEXT NOT NULL,
                    source_md5  TEXT NOT NULL,
                    obj_md5     TEXT,
                    match_pct   REAL NOT NULL,
                    build_ok    INTEGER NOT NULL DEFAULT 1,
                    timestamp   REAL NOT NULL,
                    PRIMARY KEY (symbol, source_md5)
                );
            """)
            conn.execute(
                "INSERT INTO score_cache VALUES ('sym', 'src', 'obj', 95.0, 1, 0)"
            )
            conn.commit()
            conn.close()

            # ScoreCache constructor must migrate transparently
            c = ScoreCache(symbol="sym", db_path=db)
            cols = {row[1] for row in c._conn.execute("PRAGMA table_info(score_cache)")}
            self.assertIn("dep_hash", cols)

            # Existing row survives, but missing dep_hash means it's treated
            # as stale on dep-hash-aware lookup.
            res = c.lookup_source("src", current_dep_hash="depV1")
            self.assertIsNone(res)
            # ...and a plain (no dep_hash) lookup still works for legacy callers.
            c2 = ScoreCache(symbol="sym", db_path=db)
            # The above lookup deleted the row, so re-insert for the legacy test
            c2.store("src", "obj", 95.0, True, dep_hash=None)
            res2 = c2.lookup_source("src", current_dep_hash=None)
            self.assertEqual(res2, (95.0, True))
            c.close()
            c2.close()


if __name__ == "__main__":
    unittest.main()
