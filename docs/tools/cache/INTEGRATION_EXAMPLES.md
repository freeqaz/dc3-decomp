# Cache Integration Examples

## Code Integration Points

### 1. Using CacheManager Directly

```python
from pyghidra_mcp.cache_manager import CacheManager, compute_binary_hash
from pathlib import Path

# Initialize cache
cache = CacheManager(
    cache_dir=Path("/tmp/ghidra-cache"),
    enabled=True
)

# Compute binary hash
binary_path = Path("/path/to/binary.exe")
binary_hash = compute_binary_hash(binary_path)

# Store decompilation
address = "0x1004010"
code = """
void Character::Poll() {
    // ... decompiled code ...
}
"""
cache.put(address, binary_hash, code)

# Retrieve decompilation
cached_code = cache.get(address, binary_hash)
if cached_code:
    print("Cache hit!")
    print(cached_code)
else:
    print("Cache miss - need to decompile")

# Get statistics
stats = cache.get_stats()
print(f"Cache hit rate: {stats['hit_rate']}%")
print(f"Total entries: {stats['total_entries']}")

# Handle binary change
new_hash = compute_binary_hash(binary_path)
if binary_hash != new_hash:
    deleted = cache.invalidate_on_binary_change(binary_hash, new_hash)
    print(f"Invalidated {deleted} stale entries")
```

### 2. GhidraTools with Cache

```python
from pyghidra_mcp.tools import GhidraTools
from pyghidra_mcp.cache_manager import CacheManager

# Create cache manager
cache = CacheManager(cache_dir=Path("/tmp/cache"), enabled=True)

# Initialize tools with cache
tools = GhidraTools(program_info, cache_manager=cache)

# Decompile (automatically uses cache)
result = tools.decompile_function("Character::Poll")
print(result.code)  # From cache or fresh decompilation
```

### 3. Server-Level Integration

```python
from pyghidra_mcp.server import main
import sys

# Command-line with caching enabled
sys.argv = [
    "pyghidra-mcp",
    "--cache-dir", "/var/cache/ghidra",
    "--cache-stats",  # View stats before starting
]

# This would print cache statistics and exit
main()
```

### 4. HTTP Endpoint Usage

```python
import requests
import json

# Get cache statistics via HTTP
response = requests.get("http://127.0.0.1:8000/tools/get_cache_stats")
stats = response.json()

print(json.dumps(stats, indent=2))
# Output:
# {
#   "enabled": true,
#   "total_entries": 1234,
#   "total_hits": 5678,
#   "hit_rate": 82.3,
#   "cache_size_mb": 45.2,
#   "binary_hashes": 3
# }
```

## Workflow Examples

### Scenario 1: Analysis Pipeline with Caching

```python
from pyghidra_mcp.cache_manager import CacheManager
from pyghidra_mcp.tools import GhidraTools
from pathlib import Path

class AnalysisPipeline:
    def __init__(self, cache_dir: Path):
        self.cache = CacheManager(cache_dir=cache_dir, enabled=True)

    def analyze_function(self, program_info, function_names: list[str]):
        """Analyze multiple functions with caching."""
        tools = GhidraTools(program_info, cache_manager=self.cache)
        results = []

        for name in function_names:
            # Cache automatically used inside tools.decompile_function()
            result = tools.decompile_function(name)
            results.append({
                'name': result.name,
                'code': result.code,
                'signature': result.signature
            })

        # Print statistics
        stats = self.cache.get_stats()
        print(f"Decompiled {len(results)} functions")
        print(f"Cache hit rate: {stats['hit_rate']}%")

        return results

# Usage
pipeline = AnalysisPipeline(Path("/var/cache/ghidra"))
# First run: cache misses, ~0.2s per function
# Second run: cache hits, ~1ms per function
```

### Scenario 2: Binary Change Handling

```python
from pyghidra_mcp.cache_manager import CacheManager, compute_binary_hash
from pathlib import Path

class BinaryAnalyzer:
    def __init__(self, cache_dir: Path, binary_path: Path):
        self.cache = CacheManager(cache_dir=cache_dir, enabled=True)
        self.binary_path = binary_path
        self.current_hash = compute_binary_hash(binary_path)
        self.stored_hash = None

    def load_state(self):
        """Load previous binary hash from metadata."""
        # In practice, store in config file or DB
        self.stored_hash = self.current_hash

    def check_binary_changed(self) -> bool:
        """Check if binary has been updated."""
        new_hash = compute_binary_hash(self.binary_path)
        if new_hash != self.current_hash:
            # Binary changed - invalidate cache
            self.cache.invalidate_on_binary_change(
                self.current_hash,
                new_hash
            )
            self.current_hash = new_hash
            return True
        return False

    def analyze(self, functions: list[str]):
        """Analyze functions, handling binary changes."""
        if self.check_binary_changed():
            print("Binary changed - cache invalidated")

        # Now safe to use cache
        for func_name in functions:
            # Will use new hash automatically
            pass

# Usage
analyzer = BinaryAnalyzer(
    Path("/var/cache"),
    Path("/path/to/binary.exe")
)
analyzer.load_state()
analyzer.analyze(["func1", "func2"])
```

### Scenario 3: Cache Monitoring and Maintenance

```python
from pyghidra_mcp.cache_manager import CacheManager
from pathlib import Path
import time

class CacheMonitor:
    def __init__(self, cache_dir: Path):
        self.cache = CacheManager(cache_dir=cache_dir, enabled=True)
        self.last_stats = None

    def monitor_hit_rate(self, target_hit_rate: float = 0.80) -> bool:
        """Monitor cache effectiveness."""
        stats = self.cache.get_stats()

        # Check if hit rate is acceptable
        current_rate = stats['hit_rate'] / 100.0
        if current_rate < target_hit_rate:
            print(f"Warning: Hit rate {stats['hit_rate']}% below target {target_hit_rate*100}%")
            # Could trigger cache prewarming
            return False

        print(f"✓ Hit rate {stats['hit_rate']}% is good")
        return True

    def cleanup_if_needed(self, max_size_mb: float = 500.0):
        """Clear cache if it exceeds size limit."""
        stats = self.cache.get_stats()

        if stats['cache_size_mb'] > max_size_mb:
            print(f"Cache size {stats['cache_size_mb']}MB exceeds limit {max_size_mb}MB")
            cleared = self.cache.clear()
            print(f"Cleared {cleared} entries")
        else:
            print(f"Cache size {stats['cache_size_mb']}MB is within limits")

    def print_summary(self):
        """Print cache summary."""
        stats = self.cache.get_stats()
        print(f"""
Cache Summary:
  Enabled: {stats['enabled']}
  Total Entries: {stats['total_entries']}
  Total Hits: {stats['total_hits']}
  Hit Rate: {stats['hit_rate']}%
  Cache Size: {stats['cache_size_mb']}MB
  Unique Binaries: {stats['binary_hashes']}
        """)

# Usage in monitoring script
monitor = CacheMonitor(Path("/var/cache/ghidra"))
monitor.print_summary()
monitor.monitor_hit_rate()
monitor.cleanup_if_needed()
```

### Scenario 4: Concurrent Access

```python
from pyghidra_mcp.cache_manager import CacheManager
from pathlib import Path
import threading
import time

class ConcurrentDecompiler:
    def __init__(self, cache_dir: Path):
        self.cache = CacheManager(cache_dir=cache_dir, enabled=True)
        self.results = {}

    def decompile_function(self, address: str, binary_hash: str):
        """Worker thread - decompile with caching."""
        # Try cache first
        code = self.cache.get(address, binary_hash)
        if code:
            print(f"[Cache HIT] {address}")
        else:
            print(f"[Cache MISS] {address}")
            # In real scenario, call Ghidra here
            code = f"void func_{address}() {{}}"
            self.cache.put(address, binary_hash, code)

        self.results[address] = code

    def parallel_decompile(self, addresses: list[str], binary_hash: str, num_threads: int = 4):
        """Decompile multiple functions in parallel."""
        threads = []

        for address in addresses:
            # Create thread pool
            while len(threading.enumerate()) > num_threads + 1:
                time.sleep(0.01)

            thread = threading.Thread(
                target=self.decompile_function,
                args=(address, binary_hash)
            )
            threads.append(thread)
            thread.start()

        # Wait for all threads
        for thread in threads:
            thread.join()

        # Print statistics
        stats = self.cache.get_stats()
        print(f"\nDecompilation complete:")
        print(f"  Total functions: {len(addresses)}")
        print(f"  Cache hits: {stats['total_hits']}")
        print(f"  Hit rate: {stats['hit_rate']}%")

# Usage
decompiler = ConcurrentDecompiler(Path("/var/cache"))
addresses = [f"0x{i:08x}" for i in range(100)]
decompiler.parallel_decompile(addresses, "hash123", num_threads=8)
```

## Configuration Examples

### Production Deployment

```python
# config.py
from pathlib import Path

CACHE_CONFIG = {
    'enabled': True,
    'cache_dir': Path('/var/cache/ghidra'),
    'max_size_mb': 500,
    'cleanup_interval_hours': 24,
}

# main.py
from pyghidra_mcp.cache_manager import CacheManager
from config import CACHE_CONFIG

cache = CacheManager(
    cache_dir=CACHE_CONFIG['cache_dir'],
    enabled=CACHE_CONFIG['enabled']
)
```

### Testing Deployment

```bash
# Disable cache for testing
pyghidra-mcp --cache-disabled \
    --transport stdio \
    /path/to/test/binary

# Or with fresh cache each time
pyghidra-mcp --cache-clear \
    --cache-dir /tmp/test-cache \
    --transport stdio \
    /path/to/test/binary
```

### Development Environment

```bash
# Enable detailed logging
export PYTHONVERBOSE=1

# Start with cache enabled, monitor output
pyghidra-mcp --cache-dir ~/.cache/ghidra \
    --transport streamable-http \
    ~/dev/binary.exe 2>&1 | grep -i cache
```

## Performance Tuning

### Cache Hit Rate Optimization

```python
# Monitor and analyze hit patterns
stats = cache.get_stats()
hit_rate = stats['hit_rate']

if hit_rate < 50:
    # Consider prewarming cache with common functions
    print("Low hit rate - consider prewarming")
elif hit_rate > 90:
    print("Excellent hit rate - cache is working well")

# Check if binary changes frequently
if stats['binary_hashes'] > 5:
    print("Many binary versions - may need larger cache")
```

### Database Optimization

```python
# For large caches, consider periodic maintenance
import sqlite3

def optimize_cache_db(db_path: Path):
    """Optimize cache database."""
    with sqlite3.connect(db_path) as conn:
        # Analyze query plans
        conn.execute("ANALYZE")
        # Vacuum to reclaim space
        conn.execute("VACUUM")
        conn.commit()

optimize_cache_db(Path("/var/cache/ghidra/cache.db"))
```

## Error Handling Patterns

### Safe Cache Usage

```python
from pyghidra_mcp.cache_manager import CacheManager

def safe_decompile_with_cache(program_info, function_name, cache):
    """Decompile with cache, handling failures gracefully."""
    try:
        tools = GhidraTools(program_info, cache_manager=cache)
        return tools.decompile_function(function_name)
    except Exception as e:
        # Cache can fail, but decompilation should still work
        logger.warning(f"Cache operation failed: {e}")

        # Fall back to uncached decompilation
        tools = GhidraTools(program_info, cache_manager=None)
        return tools.decompile_function(function_name)
```

## Monitoring and Logging

### Log Cache Activity

```python
import logging

# Configure logging to see cache operations
logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger("pyghidra_mcp.cache_manager")

# Now you'll see:
# "Cache hit for Character::Poll at 0x1004010"
# "Cache miss for Character::Poll, decompiling..."
# "Cache stats: {'total_entries': 1234, ...}"
```

### Export Metrics

```python
import json
from datetime import datetime

def export_cache_metrics(cache, output_file: Path):
    """Export cache metrics for analysis."""
    stats = cache.get_stats()

    metrics = {
        'timestamp': datetime.now().isoformat(),
        'cache_stats': stats,
    }

    with open(output_file, 'w') as f:
        json.dump(metrics, f, indent=2)

export_cache_metrics(cache, Path("cache_metrics.json"))
```

---

These examples demonstrate common integration patterns and best practices for using the decompilation cache effectively.
