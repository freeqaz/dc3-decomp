#!/usr/bin/env python3
"""
Test suite for decompilation caching in pyghidra-mcp.

Tests cache functionality including:
- Cache hit/miss behavior
- Binary change detection
- Thread safety
- Cache statistics
- Cache clearing
"""

import json
import sqlite3
import sys
import tempfile
import time
from pathlib import Path
from unittest.mock import MagicMock, patch

# Import cache_manager directly from pyghidra-mcp fork (avoid __init__.py which
# pulls in heavy deps like pyghidra, click_option_group, etc.)
import importlib.util
_cache_spec = importlib.util.spec_from_file_location(
    "cache_manager",
    Path.home() / "code" / "milohax" / "pyghidra-mcp" / "src" / "pyghidra_mcp" / "cache_manager.py"
)
_cache_module = importlib.util.module_from_spec(_cache_spec)
_cache_spec.loader.exec_module(_cache_module)
CacheManager = _cache_module.CacheManager
compute_binary_hash = _cache_module.compute_binary_hash


def test_cache_init():
    """Test cache initialization and database creation."""
    print("TEST: Cache initialization")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache_dir = Path(tmpdir)
        cache = CacheManager(cache_dir=cache_dir, enabled=True)

        assert cache.enabled, "Cache should be enabled"
        assert cache.db_path == cache_dir / "cache.db", "DB path should match cache_dir"
        assert cache.db_path.exists(), "Database file should be created"
        print("  ✓ Cache initialized successfully")
        print(f"  ✓ Database created at {cache.db_path}")


def test_cache_put_get():
    """Test basic cache put and get operations."""
    print("\nTEST: Cache put and get")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        # Put a value
        address = "0x1004010"
        binary_hash = "abc123def456"
        code = "void Character::Poll() { /* decompiled */ }"

        success = cache.put(address, binary_hash, code)
        assert success, "Put operation should succeed"
        print(f"  ✓ Cached decompilation for {address}")

        # Get the value
        cached = cache.get(address, binary_hash)
        assert cached == code, "Retrieved code should match cached code"
        print(f"  ✓ Retrieved cached decompilation")

        # Get non-existent entry
        cached = cache.get("0xdeadbeef", binary_hash)
        assert cached is None, "Non-existent entry should return None"
        print(f"  ✓ Non-existent entry returns None")


def test_cache_hit_count():
    """Test hit count tracking."""
    print("\nTEST: Cache hit count tracking")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        address = "0x1004010"
        binary_hash = "abc123"
        code = "void func() {}"

        cache.put(address, binary_hash, code)

        # Get multiple times to increment hit count
        for i in range(5):
            cache.get(address, binary_hash)

        # Check stats
        stats = cache.get_stats()
        assert stats["total_hits"] == 5, "Should have 5 hits"
        print(f"  ✓ Hit count tracked correctly: {stats['total_hits']} hits")


def test_binary_hash_change():
    """Test cache invalidation on binary change."""
    print("\nTEST: Binary hash change invalidation")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        address = "0x1004010"
        old_hash = "abc123"
        new_hash = "xyz789"
        code = "void func() {}"

        # Cache with old hash
        cache.put(address, old_hash, code)
        assert cache.get(address, old_hash) is not None
        print(f"  ✓ Cached with hash {old_hash}")

        # Invalidate old hash
        deleted = cache.invalidate_on_binary_change(old_hash, new_hash)
        assert deleted == 1, "Should have deleted 1 entry"
        print(f"  ✓ Invalidated {deleted} entries with old hash")

        # Verify old hash no longer works
        assert cache.get(address, old_hash) is None
        print(f"  ✓ Old hash entries no longer accessible")


def test_cache_clear():
    """Test cache clearing."""
    print("\nTEST: Cache clearing")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        # Add multiple entries
        for i in range(10):
            cache.put(f"0x{i:08x}", "hash1", f"code{i}")

        stats = cache.get_stats()
        assert stats["total_entries"] == 10, "Should have 10 entries"
        print(f"  ✓ Cached {stats['total_entries']} entries")

        # Clear
        cleared = cache.clear()
        assert cleared == 10, "Should have cleared 10 entries"
        print(f"  ✓ Cleared {cleared} entries")

        # Verify empty
        stats = cache.get_stats()
        assert stats["total_entries"] == 0, "Should have no entries"
        print(f"  ✓ Cache is now empty")


def test_cache_disabled():
    """Test cache with disabled flag."""
    print("\nTEST: Cache disabled mode")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=False)

        assert not cache.enabled, "Cache should be disabled"
        print(f"  ✓ Cache disabled")

        # Operations should be no-ops
        result = cache.put("0x1000", "hash", "code")
        assert result is False, "Put should return False when disabled"
        print(f"  ✓ Put operation is no-op when disabled")

        result = cache.get("0x1000", "hash")
        assert result is None, "Get should return None when disabled"
        print(f"  ✓ Get operation is no-op when disabled")

        stats = cache.get_stats()
        assert not stats["enabled"], "Stats should show disabled"
        print(f"  ✓ Stats show cache is disabled")


def test_cache_stats():
    """Test cache statistics."""
    print("\nTEST: Cache statistics")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        # Add entries with different binary hashes
        for hash_val in ["hash1", "hash2", "hash3"]:
            for i in range(5):
                cache.put(f"0x{i:08x}", hash_val, f"code_{hash_val}_{i}")

        # Get to generate hits
        for i in range(3):
            cache.get(f"0x{i:08x}", "hash1")

        stats = cache.get_stats()
        assert stats["total_entries"] == 15, "Should have 15 entries"
        assert stats["total_hits"] == 3, "Should have 3 hits"
        assert stats["binary_hashes"] == 3, "Should have 3 unique binary hashes"
        assert stats["cache_size_mb"] >= 0, "Cache size should be >= 0"
        print(f"  ✓ Entries: {stats['total_entries']}")
        print(f"  ✓ Hits: {stats['total_hits']}")
        print(f"  ✓ Hit rate: {stats['hit_rate']}%")
        print(f"  ✓ Cache size: {stats['cache_size_mb']}MB")
        print(f"  ✓ Unique binaries: {stats['binary_hashes']}")


def test_binary_hash_function():
    """Test binary hash computation."""
    print("\nTEST: Binary hash computation")

    # Create a test file
    with tempfile.NamedTemporaryFile(delete=False) as f:
        f.write(b"test binary content")
        f.flush()
        test_file = Path(f.name)

    try:
        hash1 = compute_binary_hash(test_file)
        assert isinstance(hash1, str), "Hash should be string"
        assert len(hash1) == 64, "SHA256 hash should be 64 hex chars"
        print(f"  ✓ Generated hash: {hash1[:16]}...")

        # Same file should produce same hash
        hash2 = compute_binary_hash(test_file)
        assert hash1 == hash2, "Same file should produce same hash"
        print(f"  ✓ Hash is deterministic")

        # Modify file and hash again
        with open(test_file, "ab") as f:
            f.write(b"modified")
        hash3 = compute_binary_hash(test_file)
        assert hash1 != hash3, "Modified file should have different hash"
        print(f"  ✓ Modified file produces different hash")

    finally:
        test_file.unlink()


def test_cache_database_schema():
    """Test database schema is correct."""
    print("\nTEST: Database schema validation")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        # Check tables exist
        with sqlite3.connect(cache.db_path) as conn:
            cursor = conn.execute(
                "SELECT name FROM sqlite_master WHERE type='table' AND name='decompilation_cache'"
            )
            assert cursor.fetchone() is not None, "decompilation_cache table should exist"
            print(f"  ✓ Table 'decompilation_cache' exists")

            # Check indices exist
            cursor = conn.execute(
                "SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'idx_%'"
            )
            indices = [row[0] for row in cursor.fetchall()]
            assert "idx_binary_hash" in indices, "idx_binary_hash index should exist"
            assert "idx_hit_count" in indices, "idx_hit_count index should exist"
            print(f"  ✓ Indices exist: {', '.join(indices)}")

            # Check WAL mode
            cursor = conn.execute("PRAGMA journal_mode")
            mode = cursor.fetchone()[0]
            assert mode.upper() == "WAL", "Should use WAL mode"
            print(f"  ✓ Journal mode: {mode}")


def test_concurrent_access():
    """Test concurrent cache access (basic)."""
    print("\nTEST: Concurrent cache access")
    import threading

    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)
        errors = []

        def worker(thread_id):
            try:
                for i in range(10):
                    addr = f"0x{thread_id:02x}{i:06x}"
                    cache.put(addr, "hash", f"code_{thread_id}_{i}")
                    cache.get(addr, "hash")
            except Exception as e:
                errors.append((thread_id, e))

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(5)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()

        assert len(errors) == 0, f"No errors should occur: {errors}"
        stats = cache.get_stats()
        assert stats["total_entries"] == 50, "Should have 50 entries"
        print(f"  ✓ 5 threads accessed cache concurrently")
        print(f"  ✓ Total entries: {stats['total_entries']}")
        print(f"  ✓ No corruption or deadlocks")


def test_cache_performance():
    """Test cache performance (informal)."""
    print("\nTEST: Cache performance")
    with tempfile.TemporaryDirectory() as tmpdir:
        cache = CacheManager(cache_dir=Path(tmpdir), enabled=True)

        address = "0x1004010"
        binary_hash = "abc123"
        code = "void func() { /* large code block */ }" * 100  # Make it large

        # Measure put time
        start = time.time()
        cache.put(address, binary_hash, code)
        put_time = time.time() - start

        # Measure get time (cache hit)
        start = time.time()
        for _ in range(100):
            cache.get(address, binary_hash)
        get_time = (time.time() - start) / 100

        print(f"  ✓ Put time: {put_time*1000:.2f}ms")
        print(f"  ✓ Get time (avg): {get_time*1000:.2f}ms")
        assert put_time < 0.1, "Put should be fast"
        assert get_time < 0.01, "Get should be very fast (<10ms)"


def run_all_tests():
    """Run all cache tests."""
    print("=" * 70)
    print("Decompilation Cache Test Suite")
    print("=" * 70)

    tests = [
        test_cache_init,
        test_cache_put_get,
        test_cache_hit_count,
        test_binary_hash_change,
        test_cache_clear,
        test_cache_disabled,
        test_cache_stats,
        test_binary_hash_function,
        test_cache_database_schema,
        test_concurrent_access,
        test_cache_performance,
    ]

    passed = 0
    failed = 0

    for test in tests:
        try:
            test()
            passed += 1
        except AssertionError as e:
            print(f"  ✗ FAILED: {e}")
            failed += 1
        except Exception as e:
            print(f"  ✗ ERROR: {e}")
            failed += 1

    print("\n" + "=" * 70)
    print(f"Results: {passed} passed, {failed} failed")
    print("=" * 70)

    return failed == 0


if __name__ == "__main__":
    success = run_all_tests()
    sys.exit(0 if success else 1)
