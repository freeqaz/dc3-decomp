"""Tests for AST parse caching in extractor.py."""

from __future__ import annotations

import unittest

from scripts.permuter.extractor import (
    _cached_parse,
    ast_cache_clear,
    ast_cache_stats,
)


class TestAstCache(unittest.TestCase):
    """Test the LRU AST parse cache."""

    def setUp(self):
        ast_cache_clear()

    def tearDown(self):
        ast_cache_clear()

    def test_same_source_hits_cache(self):
        """Parsing the same source twice should result in cache size 1."""
        source = b"int foo() { return 1; }"
        _cached_parse(source)
        _cached_parse(source)
        stats = ast_cache_stats()
        self.assertEqual(stats["size"], 1)

    def test_eviction_at_max(self):
        """Parsing 51 different sources should evict the oldest, keeping size at 50."""
        for i in range(51):
            source = f"int f{i}() {{ return {i}; }}".encode()
            _cached_parse(source)
        stats = ast_cache_stats()
        self.assertEqual(stats["size"], 50)

    def test_stats_returns_correct_values(self):
        """ast_cache_stats() returns size and max."""
        stats = ast_cache_stats()
        self.assertEqual(stats["size"], 0)
        self.assertEqual(stats["max"], 50)

        _cached_parse(b"void a() {}")
        _cached_parse(b"void b() {}")
        stats = ast_cache_stats()
        self.assertEqual(stats["size"], 2)
        self.assertEqual(stats["max"], 50)

    def test_clear_empties_cache(self):
        """ast_cache_clear() should reset the cache to empty."""
        _cached_parse(b"int x() { return 0; }")
        self.assertEqual(ast_cache_stats()["size"], 1)
        ast_cache_clear()
        self.assertEqual(ast_cache_stats()["size"], 0)


if __name__ == "__main__":
    unittest.main()
