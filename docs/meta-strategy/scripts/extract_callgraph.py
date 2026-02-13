#!/usr/bin/env python3
"""extract_callgraph.py - Populate call_edges table via Ghidra MCP cross-references

Queries Ghidra MCP list_cross_references for each non-excluded function,
extracts CALL-type references, and inserts caller->callee edges into decomp.db.

Prerequisite: Ghidra MCP must be running:
    ./tools/ghidra/pyghidra-service.sh start

Usage:
    python3 docs/meta-strategy/scripts/extract_callgraph.py
    python3 docs/meta-strategy/scripts/extract_callgraph.py --limit 100  # test run
    python3 docs/meta-strategy/scripts/extract_callgraph.py --resume      # skip already-processed
"""

import sqlite3
import argparse
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
DB_PATH = PROJECT_ROOT / "decomp.db"

# Add tools directory to path for mcp_client import
sys.path.insert(0, str(PROJECT_ROOT / "tools" / "ghidra"))
from mcp_client import MCPClient, MCPError


def ensure_progress_table(conn: sqlite3.Connection):
    """Create progress tracking table if it doesn't exist."""
    conn.execute("""
        CREATE TABLE IF NOT EXISTS callgraph_progress (
            symbol TEXT PRIMARY KEY
        )
    """)
    conn.commit()


def get_functions(conn: sqlite3.Connection, limit: int = 0, resume: bool = False) -> list:
    """Get non-excluded function symbols to process."""
    query = "SELECT symbol FROM functions WHERE excluded = 0"

    if resume:
        # Skip functions already processed (tracked in callgraph_progress)
        query += " AND symbol NOT IN (SELECT symbol FROM callgraph_progress)"

    query += " ORDER BY symbol"

    if limit > 0:
        query += f" LIMIT {limit}"

    cursor = conn.execute(query)
    return [row[0] for row in cursor.fetchall()]


def extract_call_edges(xrefs_result, callee_symbol: str) -> list:
    """Extract caller->callee edges from xref response.

    The MCP list_cross_references returns references TO a given symbol.
    We filter to CALL-type references and return (caller, callee) pairs.
    """
    edges = []

    # Handle various response formats
    refs = []
    if isinstance(xrefs_result, dict):
        refs = xrefs_result.get("references", xrefs_result.get("xrefs", []))
        if not refs and "cross_references" in xrefs_result:
            refs = xrefs_result["cross_references"]
    elif isinstance(xrefs_result, list):
        refs = xrefs_result
    elif isinstance(xrefs_result, str):
        # Sometimes returns as text - skip
        return edges

    for ref in refs:
        if not isinstance(ref, dict):
            continue

        ref_type = ref.get("type", "").upper()

        # Accept CALL-type references (UNCONDITIONAL_CALL, CONDITIONAL_CALL, CALL, etc.)
        if "CALL" not in ref_type:
            continue

        caller_name = ref.get("function_name", "")
        if not caller_name:
            # Try alternate field names
            caller_name = ref.get("from_function", ref.get("caller", ""))

        if caller_name and caller_name != callee_symbol:
            edges.append((caller_name, callee_symbol))

    return edges


def _resolve_binary_name(binaries_resp) -> str:
    """Extract the default.xex binary name from list_binaries response.

    Ghidra uses names like '/default.xex-997567' (with SHA1 suffix).
    The MCPClient lazy resolver can fail silently, so we do it explicitly.
    """
    # Normalize to a list of name strings
    names = []
    if isinstance(binaries_resp, dict):
        raw = binaries_resp.get("result",
              binaries_resp.get("binaries",
              binaries_resp.get("programs", [])))
    elif isinstance(binaries_resp, list):
        raw = binaries_resp
    elif isinstance(binaries_resp, str):
        # Single string response - might be the name itself
        if "default.xex" in binaries_resp:
            return binaries_resp.strip().lstrip("/")
        return ""
    else:
        raw = []

    for entry in raw:
        if isinstance(entry, dict):
            name = entry.get("name", entry.get("program", ""))
        else:
            name = str(entry)
        if name:
            names.append(name)

    # Find default.xex (with or without hash suffix)
    for name in names:
        bare = name.lstrip("/")
        if bare.startswith("default.xex"):
            return bare

    # Fall back to first available binary
    if names:
        return names[0].lstrip("/")

    return ""


def run_extraction(db_path: Path, limit: int = 0, resume: bool = False,
                   batch_size: int = 100, verbose: bool = False):
    """Main extraction loop."""
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")

    # Ensure progress tracking table exists
    ensure_progress_table(conn)

    # Migrate existing call_edges callees into progress table (one-time migration)
    conn.execute("""
        INSERT OR IGNORE INTO callgraph_progress (symbol)
        SELECT DISTINCT callee_symbol FROM call_edges
    """)
    conn.commit()

    # Get function list
    symbols = get_functions(conn, limit=limit, resume=resume)
    total = len(symbols)

    if total == 0:
        if resume:
            print("All functions already processed. Nothing to do.")
        else:
            print("No functions found to process.")
        conn.close()
        return

    print(f"Functions to process: {total}")
    if resume:
        existing = conn.execute("SELECT COUNT(*) FROM callgraph_progress").fetchone()[0]
        print(f"Already processed: {existing}")

    # Connect to Ghidra MCP
    print("Connecting to Ghidra MCP...")
    try:
        client = MCPClient()
        client.initialize()
        print(f"Connected (session: {client.session_id[:16]}...)")

        # Explicitly resolve binary name - the lazy resolver can silently fallback
        binaries_resp = client.list_binaries()
        binary_name = _resolve_binary_name(binaries_resp)
        if not binary_name:
            print(f"Error: Could not find default.xex binary in Ghidra.", file=sys.stderr)
            print(f"  list_binaries returned: {binaries_resp}", file=sys.stderr)
            conn.close()
            sys.exit(1)
        client._binary = binary_name
        client._binary_resolved = True
        print(f"Binary: {binary_name}")
    except MCPError as e:
        print(f"Error: Could not connect to Ghidra MCP: {e}", file=sys.stderr)
        print("Start it with: ./tools/ghidra/pyghidra-service.sh start", file=sys.stderr)
        conn.close()
        sys.exit(1)

    processed = 0
    edges_inserted = 0
    errors = 0
    skipped = 0
    start_time = time.time()

    for i, symbol in enumerate(symbols):
        try:
            # Pass mangled symbol directly - the server resolves addresses via its map file
            xrefs = client.list_xrefs(symbol)
            edges = extract_call_edges(xrefs, symbol)

            if edges:
                conn.executemany(
                    "INSERT OR IGNORE INTO call_edges (caller_symbol, callee_symbol) VALUES (?, ?)",
                    edges
                )
                edges_inserted += len(edges)

            # Track that we've processed this symbol (even if 0 callers)
            conn.execute(
                "INSERT OR IGNORE INTO callgraph_progress (symbol) VALUES (?)",
                (symbol,)
            )
            processed += 1

        except MCPError as e:
            err_msg = str(e)
            if verbose:
                print(f"  Error [{symbol}]: {err_msg[:120]}")
            errors += 1

            # Still mark as processed so we don't retry on resume
            conn.execute(
                "INSERT OR IGNORE INTO callgraph_progress (symbol) VALUES (?)",
                (symbol,)
            )

            # If session expired, try to reinitialize
            if "session" in err_msg.lower() or "expired" in err_msg.lower():
                try:
                    client.initialize(force=True)
                    if verbose:
                        print("  Session reinitialized")
                except MCPError:
                    pass

        except Exception as e:
            if verbose:
                print(f"  Unexpected error [{symbol}]: {e}")
            errors += 1
            # Still mark as processed
            conn.execute(
                "INSERT OR IGNORE INTO callgraph_progress (symbol) VALUES (?)",
                (symbol,)
            )

        # Batch commit and progress report
        if (i + 1) % batch_size == 0:
            conn.commit()
            elapsed = time.time() - start_time
            rate = (i + 1) / elapsed if elapsed > 0 else 0
            eta_secs = (total - i - 1) / rate if rate > 0 else 0
            eta_min = eta_secs / 60

            print(f"  [{i+1}/{total}] {edges_inserted} edges, "
                  f"{errors} errors, {rate:.1f}/s, ETA {eta_min:.0f}m")

    # Final commit
    conn.commit()

    elapsed = time.time() - start_time
    print(f"\nExtraction complete in {elapsed/60:.1f} minutes")
    print(f"  Processed: {processed}")
    print(f"  Edges inserted: {edges_inserted}")
    print(f"  Errors: {errors}")

    # Update fan_in / fan_out
    print("\nUpdating fan_in/fan_out columns...")
    update_fan_counts(conn)

    # Print summary
    print_summary(conn)
    conn.close()


def update_fan_counts(conn: sqlite3.Connection):
    """Update fan_in and fan_out in functions table from call_edges."""
    conn.execute("""
        UPDATE functions SET fan_in = (
            SELECT COUNT(*) FROM call_edges WHERE callee_symbol = functions.symbol
        )
    """)
    conn.execute("""
        UPDATE functions SET fan_out = (
            SELECT COUNT(*) FROM call_edges WHERE caller_symbol = functions.symbol
        )
    """)
    conn.commit()
    print("  fan_in/fan_out updated")


def print_summary(conn: sqlite3.Connection):
    """Print call graph statistics."""
    total_edges = conn.execute("SELECT COUNT(*) FROM call_edges").fetchone()[0]
    unique_callees = conn.execute("SELECT COUNT(DISTINCT callee_symbol) FROM call_edges").fetchone()[0]
    unique_callers = conn.execute("SELECT COUNT(DISTINCT caller_symbol) FROM call_edges").fetchone()[0]

    print(f"\n=== Call Graph Summary ===")
    print(f"  Total edges: {total_edges}")
    print(f"  Unique callees: {unique_callees}")
    print(f"  Unique callers: {unique_callers}")

    # Fan-in distribution
    cursor = conn.execute("""
        SELECT
            CASE
                WHEN fan_in >= 50 THEN '50+'
                WHEN fan_in >= 20 THEN '20-49'
                WHEN fan_in >= 10 THEN '10-19'
                WHEN fan_in >= 5  THEN '5-9'
                WHEN fan_in >= 1  THEN '1-4'
                ELSE '0'
            END as range,
            COUNT(*) as count
        FROM functions
        WHERE excluded = 0
        GROUP BY range
        ORDER BY
            CASE range
                WHEN '50+' THEN 1
                WHEN '20-49' THEN 2
                WHEN '10-19' THEN 3
                WHEN '5-9' THEN 4
                WHEN '1-4' THEN 5
                ELSE 6
            END
    """)
    print(f"\n=== Fan-In Distribution ===")
    for row in cursor:
        print(f"  {row[0]:>6} callers: {row[1]} functions")

    # Top fan-in functions
    cursor = conn.execute("""
        SELECT symbol, demangled, fan_in
        FROM functions
        WHERE excluded = 0 AND fan_in > 0
        ORDER BY fan_in DESC
        LIMIT 10
    """)
    print(f"\n=== Top 10 Fan-In Functions ===")
    for row in cursor:
        name = (row[1] or row[0])[:60]
        print(f"  {row[2]:4d} callers | {name}")


def main():
    parser = argparse.ArgumentParser(
        description='Extract call graph from Ghidra MCP into decomp.db')
    parser.add_argument('--db', type=str, default=str(DB_PATH),
                        help='Database path')
    parser.add_argument('--limit', type=int, default=0,
                        help='Limit number of functions to process (0 = all)')
    parser.add_argument('--resume', action='store_true',
                        help='Skip functions already in call_edges as callee')
    parser.add_argument('--batch-size', type=int, default=100,
                        help='Commit and report progress every N functions')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print per-function errors')
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Error: Database not found: {db_path}", file=sys.stderr)
        sys.exit(1)

    run_extraction(db_path, limit=args.limit, resume=args.resume,
                   batch_size=args.batch_size, verbose=args.verbose)


if __name__ == '__main__':
    main()
