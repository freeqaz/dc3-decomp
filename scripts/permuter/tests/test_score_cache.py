"""Tests for the score cache and dedup layers."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from scripts.permuter.score_cache import ScoreCache, md5_bytes


class TestScoreCache(unittest.TestCase):
    """Test the persistent ScoreCache."""

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp()
        self._db_path = Path(self._tmpdir) / "test_cache.db"

    def tearDown(self):
        import shutil
        shutil.rmtree(self._tmpdir, ignore_errors=True)

    def test_store_and_lookup(self):
        cache = ScoreCache("test_symbol", db_path=self._db_path)
        src_md5 = md5_bytes(b"int foo() { return 1; }")
        obj_md5 = md5_bytes(b"\x00\x01\x02\x03")

        # Initially not cached
        self.assertIsNone(cache.lookup_source(src_md5))

        # Store
        cache.store(src_md5, obj_md5, 95.5, True)

        # Now cached
        result = cache.lookup_source(src_md5)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result[0], 95.5)
        self.assertTrue(result[1])

        cache.close()

    def test_persistent_across_sessions(self):
        """Cache persists across ScoreCache instances."""
        src_md5 = md5_bytes(b"void bar() {}")

        # Session 1: store
        cache1 = ScoreCache("sym1", db_path=self._db_path)
        cache1.store(src_md5, "obj123", 88.0, True)
        cache1.close()

        # Session 2: lookup
        cache2 = ScoreCache("sym1", db_path=self._db_path)
        result = cache2.lookup_source(src_md5)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result[0], 88.0)
        cache2.close()

    def test_different_symbols_isolated(self):
        """Different symbols don't share cache entries."""
        src_md5 = md5_bytes(b"shared source")

        cache = ScoreCache("sym_a", db_path=self._db_path)
        cache.store(src_md5, "obj1", 90.0, True)

        # Same source hash but different symbol
        cache_b = ScoreCache("sym_b", db_path=self._db_path)
        self.assertIsNone(cache_b.lookup_source(src_md5))

        cache.close()
        cache_b.close()

    def test_obj_hash_dedup(self):
        """Obj hash dedup works within a session."""
        cache = ScoreCache("test_sym", db_path=self._db_path)

        # Not seen yet
        self.assertIsNone(cache.lookup_obj("obj_abc"))

        # Store populates obj cache
        cache.store("src1", "obj_abc", 92.0, True)

        # Now obj hash is cached
        result = cache.lookup_obj("obj_abc")
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result, 92.0)

        cache.close()

    def test_obj_dedup_not_persistent(self):
        """Obj hash dedup is session-local, not persistent."""
        cache1 = ScoreCache("test_sym", db_path=self._db_path)
        cache1.store("src1", "obj_xyz", 85.0, True)
        cache1.close()

        # New session: obj hash not cached (but source hash is)
        cache2 = ScoreCache("test_sym", db_path=self._db_path)
        self.assertIsNone(cache2.lookup_obj("obj_xyz"))
        # Source hash IS still cached
        self.assertIsNotNone(cache2.lookup_source("src1"))
        cache2.close()

    def test_build_failure_cached(self):
        """Build failures are cached to avoid rebuilding bad variants."""
        cache = ScoreCache("test_sym", db_path=self._db_path)
        src_md5 = md5_bytes(b"syntax error {{{")

        cache.store(src_md5, None, 0.0, False)

        result = cache.lookup_source(src_md5)
        self.assertIsNotNone(result)
        self.assertAlmostEqual(result[0], 0.0)
        self.assertFalse(result[1])

        cache.close()

    def test_stats_tracking(self):
        """Hit/miss counters work correctly."""
        cache = ScoreCache("test_sym", db_path=self._db_path)
        src1 = md5_bytes(b"source1")
        src2 = md5_bytes(b"source2")

        # Miss (store)
        cache.store(src1, "obj1", 90.0, True)
        self.assertEqual(cache.misses, 1)

        # Persistent hit
        cache.lookup_source(src1)
        self.assertEqual(cache.hits_persistent, 1)

        # Obj hit
        cache.lookup_obj("obj1")
        self.assertEqual(cache.hits_obj, 1)

        # Summary includes counts
        summary = cache.stats_summary()
        self.assertIn("source=0", summary)  # source dedup is tracked by scorer
        self.assertIn("obj=1", summary)
        self.assertIn("persistent=1", summary)
        self.assertIn("builds=1", summary)

        cache.close()

    def test_clear_symbol(self):
        """clear_symbol removes all entries for the symbol."""
        cache = ScoreCache("test_sym", db_path=self._db_path)
        cache.store(md5_bytes(b"a"), "o1", 90.0, True)
        cache.store(md5_bytes(b"b"), "o2", 91.0, True)

        cache.clear_symbol()

        self.assertIsNone(cache.lookup_source(md5_bytes(b"a")))
        self.assertIsNone(cache.lookup_source(md5_bytes(b"b")))
        cache.close()

    def test_upsert_overwrites(self):
        """Storing same (symbol, source_md5) overwrites."""
        cache = ScoreCache("test_sym", db_path=self._db_path)
        src_md5 = md5_bytes(b"source")

        cache.store(src_md5, "obj1", 90.0, True)
        cache.store(src_md5, "obj2", 95.0, True)

        result = cache.lookup_source(src_md5)
        self.assertAlmostEqual(result[0], 95.0)
        cache.close()


class TestSourceDedup(unittest.TestCase):
    """Test that source-identical variants are caught."""

    def test_md5_deterministic(self):
        data = b"hello world"
        self.assertEqual(md5_bytes(data), md5_bytes(data))

    def test_md5_different(self):
        self.assertNotEqual(md5_bytes(b"a"), md5_bytes(b"b"))


if __name__ == "__main__":
    unittest.main()
