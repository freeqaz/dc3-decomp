#!/usr/bin/env python3
"""cache_decompilations.py - Bulk-populate decompilations + xrefs cache via Ghidra MCP

Queries Ghidra MCP decompile_function and list_cross_references for each
non-excluded function, then stores results in the decompilations/xrefs tables
of decomp.db.

Prerequisite: Ghidra MCP must be running:
    ./tools/ghidra/pyghidra-service.sh start

Usage:
    python3 docs/meta-strategy/scripts/cache_decompilations.py
    python3 docs/meta-strategy/scripts/cache_decompilations.py --limit 100
    python3 docs/meta-strategy/scripts/cache_decompilations.py --resume       # skip already-cached
    python3 docs/meta-strategy/scripts/cache_decompilations.py --xrefs-only   # only cache xrefs
    python3 docs/meta-strategy/scripts/cache_decompilations.py --decomp-only  # only cache decompilations
"""

import argparse
import json
import sqlite3
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
DB_PATH = PROJECT_ROOT / "decomp.db"

# Add tools directory to path for mcp_client import
sys.path.insert(0, str(PROJECT_ROOT / "tools" / "ghidra"))
from mcp_client import MCPClient, MCPError


def get_functions(conn: sqlite3.Connection, limit: int = 0,
                  skip_decomp: set = None, skip_xrefs: set = None,
                  mode: str = "both") -> list:
    """Get non-excluded function symbols to process.

    Returns list of (symbol,) tuples. When resuming, filters out symbols
    already cached based on mode.
    """
    query = "SELECT symbol FROM functions WHERE excluded = 0"
    query += " ORDER BY current_percent DESC, size DESC"

    if limit > 0:
        query += f" LIMIT {limit}"

    cursor = conn.execute(query)
    symbols = [row[0] for row in cursor.fetchall()]

    if skip_decomp or skip_xrefs:
        if mode == "both":
            # Only skip if cached in BOTH tables
            both = (skip_decomp or set()) & (skip_xrefs or set())
            symbols = [s for s in symbols if s not in both]
        elif mode == "decomp":
            symbols = [s for s in symbols if s not in (skip_decomp or set())]
        elif mode == "xrefs":
            symbols = [s for s in symbols if s not in (skip_xrefs or set())]

    return symbols


def parse_decompilation(result) -> tuple:
    """Extract (code, signature) from Ghidra decompile_function response."""
    if isinstance(result, dict):
        code = result.get("code", "")
        signature = result.get("signature")
        if not code and "decompilation" in result:
            code = result["decompilation"]
        return code, signature
    elif isinstance(result, str):
        return result, None
    return "", None


def parse_xrefs(result) -> tuple:
    """Extract (callers, callees) from Ghidra list_cross_references response.

    Returns two lists of function name strings.
    """
    callers = []
    callees = []

    refs = []
    if isinstance(result, dict):
        refs = result.get("cross_references",
               result.get("references",
               result.get("xrefs", [])))
    elif isinstance(result, list):
        refs = result
    elif isinstance(result, str):
        return callers, callees

    for ref in refs:
        if not isinstance(ref, dict):
            continue

        ref_type = ref.get("type", "").upper()
        name = ref.get("function_name",
               ref.get("from_function",
               ref.get("caller", "")))

        if not name:
            continue

        # Cross-references TO a symbol are callers (CALL type)
        if "CALL" in ref_type:
            callers.append(name)
        else:
            # DATA/READ/WRITE refs aren't callers/callees for our purposes
            pass

    return callers, callees


def run_cache(db_path: Path, limit: int = 0, resume: bool = False,
              batch_size: int = 50, verbose: bool = False,
              mode: str = "both"):
    """Main caching loop."""
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row

    # Get already-cached symbols for resume
    skip_decomp = set()
    skip_xrefs = set()
    if resume:
        rows = conn.execute("SELECT symbol FROM decompilations").fetchall()
        skip_decomp = {row[0] for row in rows}
        rows = conn.execute("SELECT symbol FROM xrefs").fetchall()
        skip_xrefs = {row[0] for row in rows}

    do_decomp = mode in ("both", "decomp")
    do_xrefs = mode in ("both", "xrefs")

    symbols = get_functions(conn, limit=limit,
                            skip_decomp=skip_decomp if resume else None,
                            skip_xrefs=skip_xrefs if resume else None,
                            mode=mode)
    total = len(symbols)

    if total == 0:
        if resume:
            print("All functions already cached. Nothing to do.")
        else:
            print("No functions found to process.")
        conn.close()
        return

    print(f"Functions to process: {total}")
    if resume:
        print(f"Already cached: {len(skip_decomp)} decompilations, {len(skip_xrefs)} xrefs")
    print(f"Mode: {mode} (decomp={do_decomp}, xrefs={do_xrefs})")

    # Connect to Ghidra MCP
    print("Connecting to Ghidra MCP...")
    try:
        client = MCPClient()
        client.initialize()
        print(f"Connected (session: {client.session_id[:16]}...)")
    except MCPError as e:
        print(f"Error: Could not connect to Ghidra MCP: {e}", file=sys.stderr)
        print("Start it with: ./tools/ghidra/pyghidra-service.sh start", file=sys.stderr)
        conn.close()
        sys.exit(1)

    try:
        binaries_resp = client.list_binaries()
        # Reuse the resolution logic from extract_callgraph
        binary_name = _resolve_binary_name(binaries_resp)
        if not binary_name:
            print(f"Error: Could not find default.xex binary in Ghidra.", file=sys.stderr)
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

    # Stats
    processed = 0
    decomp_ok = 0
    decomp_err = 0
    xrefs_ok = 0
    xrefs_err = 0
    start_time = time.time()

    for i, symbol in enumerate(symbols):
        # --- Decompilation ---
        if do_decomp and (not resume or symbol not in skip_decomp):
            try:
                result = client.decompile_function(symbol)
                code, signature = parse_decompilation(result)

                if code:
                    conn.execute("""
                        INSERT OR REPLACE INTO decompilations
                            (symbol, address, code, signature, error, cached_at)
                        VALUES (?, NULL, ?, ?, NULL, CURRENT_TIMESTAMP)
                    """, (symbol, code, signature))
                    decomp_ok += 1
                else:
                    conn.execute("""
                        INSERT OR REPLACE INTO decompilations
                            (symbol, address, code, signature, error, cached_at)
                        VALUES (?, NULL, '', NULL, ?, CURRENT_TIMESTAMP)
                    """, (symbol, "empty response"))
                    decomp_err += 1

            except MCPError as e:
                err_msg = str(e)[:200]
                if verbose:
                    print(f"  decomp error [{symbol[:50]}]: {err_msg[:80]}")
                conn.execute("""
                    INSERT OR REPLACE INTO decompilations
                        (symbol, address, code, signature, error, cached_at)
                    VALUES (?, NULL, '', NULL, ?, CURRENT_TIMESTAMP)
                """, (symbol, err_msg))
                decomp_err += 1

                _maybe_reinit(client, err_msg, verbose)

        # --- Cross-references ---
        if do_xrefs and (not resume or symbol not in skip_xrefs):
            try:
                xref_result = client.list_xrefs(symbol)
                callers, callees = parse_xrefs(xref_result)

                conn.execute("""
                    INSERT OR REPLACE INTO xrefs
                        (symbol, address, callers_json, callees_json,
                         callers_count, callees_count, error, cached_at)
                    VALUES (?, NULL, ?, ?, ?, ?, NULL, CURRENT_TIMESTAMP)
                """, (symbol, json.dumps(callers), json.dumps(callees),
                      len(callers), len(callees)))
                xrefs_ok += 1

            except MCPError as e:
                err_msg = str(e)[:200]
                if verbose:
                    print(f"  xrefs error [{symbol[:50]}]: {err_msg[:80]}")
                conn.execute("""
                    INSERT OR REPLACE INTO xrefs
                        (symbol, address, callers_json, callees_json,
                         callers_count, callees_count, error, cached_at)
                    VALUES (?, NULL, '[]', '[]', 0, 0, ?, CURRENT_TIMESTAMP)
                """, (symbol, err_msg))
                xrefs_err += 1

                _maybe_reinit(client, err_msg, verbose)

        processed += 1

        # Batch commit + progress
        if (i + 1) % batch_size == 0:
            conn.commit()
            elapsed = time.time() - start_time
            rate = (i + 1) / elapsed if elapsed > 0 else 0
            eta_secs = (total - i - 1) / rate if rate > 0 else 0
            eta_min = eta_secs / 60

            parts = []
            if do_decomp:
                parts.append(f"decomp {decomp_ok}ok/{decomp_err}err")
            if do_xrefs:
                parts.append(f"xrefs {xrefs_ok}ok/{xrefs_err}err")
            status = ", ".join(parts)

            print(f"  [{i+1}/{total}] {status}, "
                  f"{rate:.1f}/s, ETA {eta_min:.0f}m")

    # Final commit
    conn.commit()

    elapsed = time.time() - start_time
    print(f"\nCaching complete in {elapsed/60:.1f} minutes")
    print(f"  Processed: {processed}")
    if do_decomp:
        print(f"  Decompilations: {decomp_ok} ok, {decomp_err} errors")
    if do_xrefs:
        print(f"  Xrefs: {xrefs_ok} ok, {xrefs_err} errors")

    # Print cache summary
    _print_summary(conn)
    conn.close()


def _maybe_reinit(client: MCPClient, err_msg: str, verbose: bool):
    """Reinitialize MCP session if error looks like session expiry."""
    if "session" in err_msg.lower() or "expired" in err_msg.lower():
        try:
            client.initialize(force=True)
            if verbose:
                print("  Session reinitialized")
        except MCPError:
            pass


def _resolve_binary_name(binaries_resp) -> str:
    """Extract the default.xex binary name from list_binaries response.

    Copied from extract_callgraph.py for consistency.
    """
    names = []
    if isinstance(binaries_resp, dict):
        raw = binaries_resp.get("result",
              binaries_resp.get("binaries",
              binaries_resp.get("programs", [])))
    elif isinstance(binaries_resp, list):
        raw = binaries_resp
    elif isinstance(binaries_resp, str):
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

    for name in names:
        bare = name.lstrip("/")
        if bare.startswith("default.xex"):
            return bare

    if names:
        return names[0].lstrip("/")

    return ""


def _print_summary(conn: sqlite3.Connection):
    """Print cache statistics."""
    d_total = conn.execute("SELECT COUNT(*) FROM decompilations").fetchone()[0]
    d_ok = conn.execute(
        "SELECT COUNT(*) FROM decompilations WHERE error IS NULL"
    ).fetchone()[0]
    x_total = conn.execute("SELECT COUNT(*) FROM xrefs").fetchone()[0]
    x_ok = conn.execute(
        "SELECT COUNT(*) FROM xrefs WHERE error IS NULL"
    ).fetchone()[0]
    fn_total = conn.execute(
        "SELECT COUNT(*) FROM functions WHERE excluded = 0"
    ).fetchone()[0]

    print(f"\n=== Cache Summary ===")
    print(f"  Functions (non-excluded): {fn_total}")
    print(f"  Decompilations cached: {d_total} ({d_ok} ok, {d_total - d_ok} errors)")
    print(f"  Xrefs cached: {x_total} ({x_ok} ok, {x_total - x_ok} errors)")
    if fn_total > 0:
        print(f"  Coverage: {d_total*100.0/fn_total:.1f}% decomp, "
              f"{x_total*100.0/fn_total:.1f}% xrefs")


def main():
    parser = argparse.ArgumentParser(
        description="Bulk-cache Ghidra decompilations and xrefs into decomp.db")
    parser.add_argument("--db", type=str, default=str(DB_PATH),
                        help="Database path")
    parser.add_argument("--limit", type=int, default=0,
                        help="Limit number of functions to process (0 = all)")
    parser.add_argument("--resume", action="store_true",
                        help="Skip functions already in cache")
    parser.add_argument("--batch-size", type=int, default=50,
                        help="Commit and report progress every N functions")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Print per-function errors")
    parser.add_argument("--decomp-only", action="store_true",
                        help="Only cache decompilations (skip xrefs)")
    parser.add_argument("--xrefs-only", action="store_true",
                        help="Only cache xrefs (skip decompilations)")
    args = parser.parse_args()

    if args.decomp_only and args.xrefs_only:
        print("Error: --decomp-only and --xrefs-only are mutually exclusive",
              file=sys.stderr)
        sys.exit(1)

    mode = "both"
    if args.decomp_only:
        mode = "decomp"
    elif args.xrefs_only:
        mode = "xrefs"

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Error: Database not found: {db_path}", file=sys.stderr)
        sys.exit(1)

    run_cache(db_path, limit=args.limit, resume=args.resume,
              batch_size=args.batch_size, verbose=args.verbose,
              mode=mode)


if __name__ == "__main__":
    main()
