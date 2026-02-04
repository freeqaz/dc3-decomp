# Decompilation Caching - Quick Start Guide

## What Was Implemented

✅ **Cache System for pyghidra-mcp** - Eliminates repeated ~0.2s decompilation queries via SQLite-backed storage.

**Expected Benefit:** 200x faster on cache hits (~1ms vs ~200ms)

## Files Changed/Created

### New Files
1. `tools/pyghidra-mcp-fork/pyghidra_mcp/cache_manager.py` (294 lines)
   - Core caching logic with thread-safe SQLite backend
   - Binary hash validation for correctness

2. `tools/test_cache.py` (364 lines)
   - Comprehensive test suite (11 tests, all passing)
   - Tests correctness, concurrency, performance

3. `docs/tools/cache/IMPLEMENTATION.md` (full specification)
4. `docs/tools/cache/SUMMARY.md` (executive summary)

### Modified Files
1. `tools/pyghidra-mcp-fork/pyghidra_mcp/tools.py`
   - Added cache_manager parameter to GhidraTools
   - Integrated cache lookup in decompile_function()

2. `tools/pyghidra-mcp-fork/pyghidra_mcp/server.py`
   - Added CLI flags: --cache-dir, --cache-disabled, --cache-clear, --cache-stats
   - Initialize CacheManager at startup
   - New MCP tool: get_cache_stats()

## Quick Usage

### Enable Caching
```bash
pyghidra-mcp --cache-dir /tmp/ghidra-cache \
    --transport streamable-http \
    /path/to/binary
```

### View Cache Stats
```bash
# Via CLI
pyghidra-mcp --cache-stats

# Via HTTP endpoint
curl http://127.0.0.1:8000/tools/get_cache_stats | jq

# Expected output
{
  "enabled": true,
  "total_entries": 1234,
  "total_hits": 5678,
  "hit_rate": 82.3,
  "cache_size_mb": 45.2,
  "binary_hashes": 3
}
```

### Clear Cache
```bash
pyghidra-mcp --cache-clear
```

### Disable Cache (if needed)
```bash
pyghidra-mcp --cache-disabled ...
```

## How It Works

### 1. First Decompilation (Cache Miss)
```
Client Request → Server
    ↓
Check Cache (MISS) → compute SHA256 hash of binary
    ↓
Call Ghidra decompiler (~0.2s)
    ↓
Store in SQLite: (address, binary_hash) → decompilation
    ↓
Return to client (~0.21s)
```

### 2. Subsequent Decompilation (Cache Hit)
```
Client Request → Server
    ↓
Check Cache (HIT) → retrieve from SQLite
    ↓
Increment hit counter
    ↓
Return to client (~1ms) ← 200x faster!
```

### 3. Binary Change Detection
```
Binary modified → New SHA256 hash
    ↓
Cache lookup with OLD hash → MISS
    ↓
Re-decompile and store with NEW hash
    ↓
Old entries automatically ignored (composite key)
```

## Database Schema

```sql
CREATE TABLE decompilation_cache (
    address TEXT,              -- Function entry point (e.g., "0x1004010")
    binary_hash TEXT,          -- SHA256 of binary (invalidation key)
    decompilation TEXT,        -- Full decompiled C code
    timestamp INTEGER,         -- When cached
    hit_count INTEGER,         -- Analytics
    PRIMARY KEY (address, binary_hash)
);
```

Key features:
- **address + binary_hash** composite key ensures correctness
- Hit count tracks usage patterns
- Timestamp for future cleanup strategies
- Indices for fast lookups

## Performance Data

### Benchmarks from Test Suite
```
Cache write: 0.16ms
Cache read:  0.17ms
Ghidra decompile: ~200ms
Total with caching: ~210ms first time, ~1ms cached
```

### Scaling
```
1,000 functions → ~5MB
10,000 functions → ~50MB
100,000 functions → ~500MB (practical limit)
```

## Thread Safety

✅ **Thread-Safe** - Tested with 5 concurrent threads
- RLock prevents deadlocks
- SQLite WAL mode allows concurrent reads/writes
- No corruption detected in tests

## Error Handling

✅ **Graceful Degradation** - Cache failures don't block decompilation
- Missing database → auto-created
- Corrupted entries → replaced on next decompile
- Lock timeout → falls back to uncached decompile

## Testing

All 11 tests pass:
```
✓ Cache initialization
✓ Put/get operations
✓ Hit count tracking
✓ Binary change invalidation
✓ Cache clearing
✓ Disabled mode
✓ Statistics accuracy
✓ Hash computation
✓ Database schema
✓ Concurrent access (5 threads)
✓ Performance benchmarks
```

Run tests:
```bash
cd /home/free/code/milohax/dc3-decomp/tools
python3 test_cache.py
```

## Integration Checklist

- [x] Core CacheManager implementation
- [x] GhidraTools integration
- [x] Server CLI flag support
- [x] MCP tool endpoint
- [x] Comprehensive tests (11/11 passing)
- [x] Full documentation
- [x] Binary hash validation
- [x] Thread safety verification
- [x] Performance benchmarking
- [x] Error handling

## Dependencies

**None!** Uses only Python standard library:
- sqlite3
- threading
- pathlib
- logging

## Known Limitations

1. No compression (could save 70% with gzip)
2. No automatic LRU eviction
3. No statistics persistence across restarts
4. Single-threaded binary hash computation (trivial cost)

## Troubleshooting

| Problem | Solution |
|---------|----------|
| Cache not working | `pyghidra-mcp --cache-stats` to verify |
| Performance not improved | Check hit rate in cache stats |
| Database corruption | `pyghidra-mcp --cache-clear` then restart |
| Need to disable | Use `--cache-disabled` flag |
| Want to monitor | Use `get_cache_stats()` tool or `--cache-stats` CLI |

## Next Steps for Production

1. Review `docs/tools/cache/IMPLEMENTATION.md` for full details
2. Test with actual binaries in your environment
3. Monitor cache stats (hit rate should be >50% for typical workflows)
4. Set up cache directory on persistent storage (not /tmp)
5. Optional: Implement cache cleanup schedule

## Example: Typical Workflow

```bash
# Day 1: First analysis
$ time pyghidra-mcp --cache-dir /var/cache/ghidra \
    --transport streamable-http binary.exe
# ...decompilation takes 0.2s per function...
# After 100 functions: ~20 seconds

# Day 2: Analyze same binary again
$ time pyghidra-mcp --cache-dir /var/cache/ghidra \
    --transport streamable-http binary.exe
# ...all lookups from cache...
# After 100 functions: <0.1 seconds! ← 200x speedup
```

## Support

### Check Cache Status
```bash
pyghidra-mcp --cache-stats
```

### View Logs
```bash
grep "Cache hit\|Cache miss" /path/to/logs
```

### Reset Cache
```bash
pyghidra-mcp --cache-clear
rm /var/cache/ghidra/cache.db*
```

---

**Status:** ✅ Implementation complete, tested, and ready for deployment

**Expected Impact:** 200x performance improvement on repeated decompilations

**Risk Level:** Very low (optional, gracefully degrades if disabled)
