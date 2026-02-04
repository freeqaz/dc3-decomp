# Batch Cross-Reference Query Implementation

**Goal**: Reduce callgraph extraction from ~4 hours to ~10 minutes by batching xref queries.

## Current State

- `extract_callgraph.py` calls `list_xrefs()` once per function (31K calls)
- Each call: HTTP request → Ghidra MCP → address resolution → reference enumeration → response
- Rate: ~2/s = ~4.3 hours for full extraction

## Target State

- Batch 100 symbols per MCP request
- 310 requests instead of 31,000
- Expected rate: ~10-20 minutes total

---

## Critical Analysis (2026-01-29)

### The Real Bottleneck

**HTTP overhead is NOT the primary bottleneck.** The slowness comes from `find_function()` in `tools.py` (lines 89-221):

1. **Strategy 1** (lines 109-113): Iterates through ALL 31K functions with `fm.getFunctions(True)` to find exact name match. **O(n) per lookup**.

2. **Strategy 2** (lines 115-171): Map file address lookup (O(1)), then Ghidra address resolution.

3. **Strategies 3-5** (lines 173-218): If still not found, iterates ALL functions AGAIN for each search variant (demangled name, method name). **3 × O(31K)** worst case.

4. **Strategy 6** (lines 199-218): Another full iteration for partial matches.

**Problem with original plan**: The batch method still calls `self.find_function_address()` for each symbol, which triggers the expensive `find_function()`. Batching HTTP requests without fixing this yields only ~20% improvement:

```
Current:     31K × 500ms = 4.3 hours
Batch (bad): 310 × (100ms + 100 × 400ms) = 3.45 hours  ← NOT 10-20 min!
```

### The Real Fix

The extraction script passes **mangled symbol names** from the database. The map file (`symbol_lookup.py`) already has O(1) lookups via `_symbols` and `_address_to_symbol` dicts.

**Solution**: Pre-resolve symbols to addresses client-side, pass addresses to Ghidra:
1. Client looks up symbol address from map file (O(1))
2. Pass hex address to Ghidra batch method
3. Ghidra uses direct `getAddressFactory().getAddress(addr)` + `getReferencesTo(addr)`
4. Bypasses `find_function()` entirely

---

## Implementation Checklist (Revised)

### 1. Data Models (`tools/pyghidra-mcp-fork/pyghidra_mcp/models.py`)

Add after `CrossReferenceInfos` (around line 91):

```python
class BatchCrossReferenceResult(BaseModel):
    """Result for a single address in a batch query."""
    target_symbol: str = Field(description="The symbol/address that was queried")
    target_address: str | None = Field(description="Resolved address, if found")
    cross_references: list[CrossReferenceInfo] = Field(default_factory=list)
    status: str = Field(description="found | not_found | error")
    error: str | None = None


class BatchCrossReferencesResults(BaseModel):
    """Container for batch cross-reference results."""
    results: list[BatchCrossReferenceResult] = Field(default_factory=list)
    total_queried: int = 0
    total_found: int = 0
    total_references: int = 0
```

### 2. Batch Method - Address-Only Path (`tools/pyghidra-mcp-fork/pyghidra_mcp/tools.py`)

Add after `list_cross_references` method (around line 420):

```python
@handle_exceptions
def list_cross_references_batch(self, entries: list[dict]) -> list:
    """Batch cross-reference lookup for multiple symbols.

    IMPORTANT: For performance, pass pre-resolved addresses.

    Args:
        entries: List of {"symbol": str, "address": str | None}
                 If address is provided, uses direct lookup (fast).
                 If address is None, falls back to find_function_address (slow).

    Returns:
        List of BatchCrossReferenceResult
    """
    from pyghidra_mcp.models import BatchCrossReferenceResult

    fm = self.program.getFunctionManager()
    rm = self.program.getReferenceManager()
    addr_factory = self.program.getAddressFactory()
    results = []

    for entry in entries:
        symbol = entry.get("symbol", "")
        address_str = entry.get("address")  # Pre-resolved hex address

        try:
            addr = None

            # Fast path: pre-resolved address
            if address_str:
                try:
                    addr = addr_factory.getAddress(address_str)
                except Exception:
                    # Try with 0x prefix
                    try:
                        addr = addr_factory.getAddress(f"0x{address_str}")
                    except Exception:
                        pass

            # Slow fallback: symbol lookup (avoid if possible)
            if addr is None:
                addr = self.find_function_address(symbol)

            if addr is None:
                results.append(BatchCrossReferenceResult(
                    target_symbol=symbol,
                    target_address=None,
                    status="not_found"
                ))
                continue

            # Get references - this is fast in Ghidra
            cross_refs = []
            for ref in rm.getReferencesTo(addr):
                func = fm.getFunctionContaining(ref.getFromAddress())
                cross_refs.append(CrossReferenceInfo(
                    function_name=func.getName() if func else None,
                    from_address=str(ref.getFromAddress()),
                    to_address=str(ref.getToAddress()),
                    type=str(ref.getReferenceType()),
                ))

            results.append(BatchCrossReferenceResult(
                target_symbol=symbol,
                target_address=str(addr),
                cross_references=cross_refs,
                status="found"
            ))

        except Exception as e:
            results.append(BatchCrossReferenceResult(
                target_symbol=symbol,
                target_address=None,
                status="error",
                error=str(e)[:200]
            ))

    return results
```

### 3. MCP Tool Registration (`tools/pyghidra-mcp-fork/pyghidra_mcp/server.py`)

Add after `list_cross_references` tool (around line 425):

```python
@mcp.tool()
def list_cross_references_batch(
    binary_name: str,
    entries: list[dict],  # Changed: now expects {"symbol": str, "address": str | None}
    ctx: Context
) -> BatchCrossReferencesResults:
    """Batch query cross-references for multiple functions.

    For best performance, pre-resolve addresses client-side and pass them in entries.
    Each entry should be {"symbol": "...", "address": "82xxxxxx" or None}.

    Recommended batch size: 50-100 symbols.
    """
    from pyghidra_mcp.models import BatchCrossReferencesResults

    try:
        pyghidra_context: PyGhidraContext = ctx.request_context.lifespan_context
        program_info = _get_program_info_or_raise(pyghidra_context, binary_name)
        tools = GhidraTools(program_info)

        results = tools.list_cross_references_batch(entries)

        total_refs = sum(len(r.cross_references) for r in results)
        found = sum(1 for r in results if r.status == "found")

        return BatchCrossReferencesResults(
            results=results,
            total_queried=len(entries),
            total_found=found,
            total_references=total_refs
        )
    except Exception as e:
        logger.error(f"Error in batch xref query: {e}")
        raise McpError(
            ErrorData(code=INTERNAL_ERROR, message=f"Error in batch query: {e!s}")
        )
```

Add import at top of server.py:
```python
from pyghidra_mcp.models import BatchCrossReferencesResults
```

### 4. Client Method with Address Pre-Resolution (`tools/ghidra/mcp_client.py`)

Add method and helper:

```python
def list_xrefs_batch(self, entries: list[dict]) -> dict:
    """Batch cross-reference lookup.

    Args:
        entries: List of {"symbol": str, "address": str | None}
    """
    return self.call_tool("list_cross_references_batch", {
        "binary_name": self.binary,
        "entries": entries
    })
```

### 5. Update Extraction Script - Address Pre-Resolution (`docs/meta-strategy/scripts/extract_callgraph.py`)

Key change: **Pre-resolve addresses from the map file before calling Ghidra**.

```python
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent

# Import map file parser for O(1) address lookups
sys.path.insert(0, str(PROJECT_ROOT / "tools" / "pyghidra-mcp-fork"))
from pyghidra_mcp.symbol_lookup import MapFileParser

MAP_FILE = PROJECT_ROOT / "orig/373307D9/ham_xbox_r.map"
BATCH_SIZE = 100

def run_extraction_batch(db_path: Path, limit: int = 0, resume: bool = False,
                         verbose: bool = False):
    """Batch extraction with address pre-resolution."""
    conn = sqlite3.connect(db_path)
    ensure_progress_table(conn)

    # Load map file for O(1) symbol->address lookup
    map_parser = MapFileParser(MAP_FILE)
    map_parser.parse()
    print(f"Loaded {len(map_parser._symbols)} symbols from map file")

    # ... MCP client setup same as before ...

    symbols = get_functions(conn, limit=limit, resume=resume)
    total = len(symbols)

    # Process in batches
    for batch_start in range(0, total, BATCH_SIZE):
        batch_end = min(batch_start + BATCH_SIZE, total)
        batch_symbols = symbols[batch_start:batch_end]

        # Pre-resolve addresses from map file (O(1) per symbol)
        entries = []
        for symbol in batch_symbols:
            address = map_parser.get_address(symbol)
            entries.append({
                "symbol": symbol,
                "address": f"{address:08x}" if address else None
            })

        try:
            result = client.list_xrefs_batch(entries)

            for r in result.get("results", []):
                symbol = r["target_symbol"]

                # Insert edges
                for xref in r.get("cross_references", []):
                    if "CALL" in xref.get("type", "").upper():
                        conn.execute(
                            "INSERT OR IGNORE INTO call_edges (caller_symbol, callee_symbol) VALUES (?, ?)",
                            (xref["function_name"], symbol)
                        )

                # Track progress
                conn.execute(
                    "INSERT OR IGNORE INTO callgraph_progress (symbol) VALUES (?)",
                    (symbol,)
                )

            conn.commit()

        except MCPError as e:
            if verbose:
                print(f"  Batch error: {e}")
            # Mark batch as processed to avoid infinite retry
            for symbol in batch_symbols:
                conn.execute(
                    "INSERT OR IGNORE INTO callgraph_progress (symbol) VALUES (?)",
                    (symbol,)
                )
            conn.commit()

        # Progress report
        elapsed = time.time() - start_time
        rate = batch_end / elapsed if elapsed > 0 else 0
        print(f"  [{batch_end}/{total}] {rate:.1f} symbols/s")
```

---

## Performance Expectation (Revised)

With address pre-resolution:
- Map file lookup: O(1) per symbol, ~1ms total for batch of 100
- Ghidra `getReferencesTo(addr)`: ~5-10ms per symbol (direct, no iteration)
- HTTP overhead: ~100ms per batch

```
310 batches × (100ms HTTP + 100 × 10ms Ghidra) = 310 × 1.1s = 5.7 minutes
```

Target of 10-20 minutes is achievable. Could potentially be faster.

---

## Testing Plan

1. **Unit test batch method** (in Ghidra MCP):
   ```python
   result = tools.list_cross_references_batch([
       {"symbol": "?Fail@Debug@@QAAXPBDPAX@Z", "address": "82674a08"},  # with address
       {"symbol": "??2@YAPAXI@Z", "address": None},  # fallback to lookup
       {"symbol": "nonexistent", "address": "99999999"}  # bad address
   ])
   assert len(result) == 3
   assert result[0].status == "found"
   ```

2. **Integration test** (from client):
   ```bash
   python3 -c "
   import sys; sys.path.insert(0, 'tools/ghidra')
   from mcp_client import MCPClient
   client = MCPClient(); client.initialize()
   # Test with pre-resolved address
   result = client.list_xrefs_batch([
       {'symbol': '?Fail@Debug@@QAAXPBDPAX@Z', 'address': '82674a08'}
   ])
   print(result)
   "
   ```

3. **Performance test**:
   ```bash
   time python3 docs/meta-strategy/scripts/extract_callgraph.py --limit 1000
   # Before (single queries): ~500s
   # After (batch + addresses): ~20s
   ```

---

## Rollback Plan

Keep existing `list_xrefs()` and `run_extraction()` intact. The batch versions are additive. If issues arise, revert to single-query mode.

---

## Notes

- Batch size of 100 balances payload size vs. roundtrip overhead
- Error handling per-symbol prevents one bad symbol from failing entire batch
- Progress tracking works per-symbol for resume capability
- **Critical**: Address pre-resolution is what makes this fast, not just batching

## Open Questions

1. Should we add a fallback to `getFunctionAt()` if `getReferencesTo()` returns nothing? Some symbols might be data, not code.
2. Consider caching the map file parse result across script invocations (already in-memory within single run).
