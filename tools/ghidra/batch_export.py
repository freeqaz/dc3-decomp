#!/usr/bin/env python3
"""
Batch export Ghidra decompilations and cross-references to SQLite cache.

Pre-caches all function decompilations so the orchestrator can serve them
from SQLite instead of holding a Ghidra project lock. The binary never
changes (fixed Xbox 360 debug build), so decompiling once is sufficient.

Requires the pyghidra-mcp service to be running:
    ./tools/ghidra/pyghidra-service.sh start

Usage:
    # Export first 10 functions (test)
    python3 tools/ghidra/batch_export.py --limit 10

    # Full export with resume support (default)
    python3 tools/ghidra/batch_export.py --resume

    # Fresh export (wipe cache first)
    python3 tools/ghidra/batch_export.py --fresh

    # Only decompilations or xrefs
    python3 tools/ghidra/batch_export.py --decomp-only
    python3 tools/ghidra/batch_export.py --xrefs-only

    # Show cache statistics
    python3 tools/ghidra/batch_export.py --stats
"""

import argparse
import logging
import re
import sys
import time
from pathlib import Path

# Add project root to path
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(PROJECT_ROOT))

from scripts.orchestrator.database import (
    get_cache_stats,
    get_cached_symbols,
    get_connection,
    init_database,
    put_decompilation,
    put_xrefs,
)
from tools.ghidra.mcp_client import MCPClient, MCPError

# Default paths
DEFAULT_MAP_FILE = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"
DEFAULT_DB = PROJECT_ROOT / "decomp.db"

logger = logging.getLogger(__name__)


def parse_map_file(map_path: Path) -> list[tuple[str, str]]:
    """Parse linker map file to extract function symbols and addresses.

    Looks for entries in the .text section (section 0005) which contains
    compiled code. Each entry has format:
        0005:OFFSET       SYMBOL   ADDRESS f   OBJECT

    Args:
        map_path: Path to the .map file

    Returns:
        List of (symbol, hex_address) tuples
    """
    pattern = re.compile(
        r"^\s*0005:[0-9A-Fa-f]+\s+"  # Section 0005 + offset
        r"(\S+)\s+"                    # Symbol name (capture)
        r"([0-9A-Fa-f]{8})\s+"        # Address (capture)
        r"f\s"                         # Function flag
    )

    symbols = []
    with open(map_path, "r") as f:
        for line in f:
            m = pattern.match(line)
            if m:
                symbol = m.group(1)
                address = m.group(2)
                symbols.append((symbol, address))

    return symbols


def show_stats(db_path: Path) -> None:
    """Display cache statistics."""
    conn = get_connection(str(db_path))
    stats = get_cache_stats(conn)

    print("Ghidra Cache Statistics")
    print("=" * 40)
    print(f"Decompilations: {stats['decompilations_total']} total")
    print(f"  OK:     {stats['decompilations_ok']}")
    print(f"  Errors: {stats['decompilations_errors']}")
    print(f"Xrefs:          {stats['xrefs_total']} total")
    print(f"  OK:     {stats['xrefs_ok']}")
    print(f"  Errors: {stats['xrefs_errors']}")


def batch_export(
    map_path: Path,
    db_path: Path,
    mcp_url: str,
    resume: bool = True,
    decomp_only: bool = False,
    xrefs_only: bool = False,
    limit: int = 0,
    batch_size: int = 100,
) -> None:
    """Run batch export of Ghidra decompilations and xrefs to SQLite.

    Uses the running pyghidra-mcp HTTP service (which has XEXLoaderWV
    and the VMX128-enabled Ghidra build) rather than a standalone
    DirectGhidraClient.

    Args:
        map_path: Path to linker map file
        db_path: Path to decomp.db
        mcp_url: URL of pyghidra-mcp service
        resume: Skip already-cached symbols
        decomp_only: Only export decompilations
        xrefs_only: Only export cross-references
        limit: Max functions to process (0 = all)
        batch_size: Commit every N functions
    """
    do_decomp = not xrefs_only
    do_xrefs = not decomp_only

    # Parse map file
    print(f"Parsing map file: {map_path}")
    all_symbols = parse_map_file(map_path)
    print(f"Found {len(all_symbols)} functions in .text section")

    # Initialize database (runs migrations if needed)
    conn = init_database(str(db_path))

    # Get already-cached symbols for resume
    cached = set()
    if resume:
        cached = get_cached_symbols(conn)
        print(f"Already cached: {len(cached)} symbols")

    # Filter to uncached symbols
    if resume:
        work_list = [(s, a) for s, a in all_symbols if s not in cached]
    else:
        work_list = all_symbols

    if limit > 0:
        work_list = work_list[:limit]

    total = len(work_list)
    if total == 0:
        print("Nothing to do — all symbols already cached.")
        show_stats(db_path)
        return

    print(f"Will process {total} functions" +
          (f" (limited to {limit})" if limit > 0 else ""))
    print()

    # Connect to pyghidra-mcp service
    print(f"Connecting to pyghidra-mcp at {mcp_url}...")
    client = MCPClient(url=mcp_url)
    try:
        client.initialize()
        print(f"Connected (session: {client.session_id})")
        print(f"Binary: {client.binary}")
    except MCPError as e:
        print(f"ERROR: Could not connect to pyghidra-mcp: {e}")
        print("Start the service with: ./tools/ghidra/pyghidra-service.sh start")
        sys.exit(1)
    print()

    # Process functions
    start_time = time.time()
    success_count = 0
    error_count = 0
    consecutive_errors = 0

    for i, (symbol, address) in enumerate(work_list):
        try:
            # Use hex address for lookup — more reliable than mangled names
            # GhidraTools.find_function Strategy 0 handles direct hex addresses
            lookup_key = f"0x{address}"

            # Decompilation
            if do_decomp:
                try:
                    result = client.decompile_function(lookup_key)
                    # MCPClient returns dict with 'code', 'name', 'signature'
                    code = result.get("code", "") if isinstance(result, dict) else str(result)
                    signature = result.get("signature") if isinstance(result, dict) else None
                    put_decompilation(conn, symbol, address, code, signature)
                    consecutive_errors = 0
                except MCPError as e:
                    put_decompilation(
                        conn, symbol, address, code="", error=str(e)
                    )
                    error_count += 1
                    consecutive_errors += 1
                    if i < 5 or consecutive_errors == 1:
                        logger.warning(f"Decomp error for {symbol} @ 0x{address}: {e}")

            # Cross-references
            if do_xrefs:
                try:
                    result = client.list_xrefs(lookup_key)
                    # MCPClient returns dict with 'cross_references' list
                    xref_list = []
                    if isinstance(result, dict):
                        xref_list = result.get("cross_references", [])
                    elif isinstance(result, list):
                        xref_list = result

                    callers = []
                    for xref in xref_list:
                        if isinstance(xref, dict):
                            fn = xref.get("function_name")
                            if fn:
                                callers.append(fn)
                    # Xrefs from this API are incoming refs (callers)
                    put_xrefs(conn, symbol, address, callers, callees=[])
                    consecutive_errors = 0
                except MCPError as e:
                    put_xrefs(
                        conn, symbol, address,
                        callers=[], callees=[], error=str(e),
                    )
                    if i < 5 or consecutive_errors == 1:
                        logger.warning(f"Xrefs error for {symbol}: {e}")

            success_count += 1

        except Exception as e:
            error_count += 1
            consecutive_errors += 1
            logger.error(f"Unexpected error for {symbol}: {e}")

        # Bail if service seems down (many consecutive errors)
        if consecutive_errors >= 20:
            print(f"\nERROR: {consecutive_errors} consecutive errors — service may be down")
            print("Check: ./tools/ghidra/pyghidra-service.sh status")
            conn.commit()
            break

        # Batch commit + progress
        if (i + 1) % batch_size == 0 or i == total - 1:
            conn.commit()

            elapsed = time.time() - start_time
            rate = (i + 1) / elapsed if elapsed > 0 else 0
            remaining = (total - i - 1) / rate if rate > 0 else 0

            pct = 100.0 * (i + 1) / total
            print(
                f"[{pct:5.1f}%] {i+1}/{total} — "
                f"{success_count} ok, {error_count} errors — "
                f"{rate:.1f} fn/s — "
                f"ETA: {remaining/60:.1f} min"
            )

    # Final stats
    elapsed = time.time() - start_time
    print()
    print(f"Batch export complete in {elapsed/60:.1f} minutes")
    print(f"  Processed: {i + 1}")
    print(f"  Success:   {success_count}")
    print(f"  Errors:    {error_count}")
    print()
    show_stats(db_path)


def main():
    parser = argparse.ArgumentParser(
        description="Batch export Ghidra decompilations to SQLite cache"
    )
    parser.add_argument(
        "--map-file",
        type=Path,
        default=DEFAULT_MAP_FILE,
        help="Linker map file path",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=DEFAULT_DB,
        help="Database path (decomp.db)",
    )
    parser.add_argument(
        "--mcp-url",
        default="http://127.0.0.1:8000/mcp",
        help="pyghidra-mcp service URL (default: http://127.0.0.1:8000/mcp)",
    )
    parser.add_argument(
        "--resume",
        action="store_true",
        default=True,
        help="Skip already-cached symbols (default)",
    )
    parser.add_argument(
        "--fresh",
        action="store_true",
        help="Wipe cache tables and re-export everything",
    )
    parser.add_argument(
        "--decomp-only",
        action="store_true",
        help="Only export decompilations (skip xrefs)",
    )
    parser.add_argument(
        "--xrefs-only",
        action="store_true",
        help="Only export cross-references (skip decompilations)",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=0,
        help="Max functions to process (0 = all)",
    )
    parser.add_argument(
        "--batch-size",
        type=int,
        default=100,
        help="Commit every N functions (default: 100)",
    )
    parser.add_argument(
        "--stats",
        action="store_true",
        help="Show cache statistics and exit",
    )
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Enable verbose logging",
    )

    args = parser.parse_args()

    # Setup logging
    level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s %(levelname)s %(message)s",
        datefmt="%H:%M:%S",
    )

    if args.stats:
        show_stats(args.db)
        return

    if args.fresh:
        print("Wiping cache tables...")
        conn = get_connection(str(args.db))
        conn.execute("DELETE FROM decompilations")
        conn.execute("DELETE FROM xrefs")
        conn.commit()
        print("Cache cleared.")

    batch_export(
        map_path=args.map_file,
        db_path=args.db,
        mcp_url=args.mcp_url,
        resume=not args.fresh,
        decomp_only=args.decomp_only,
        xrefs_only=args.xrefs_only,
        limit=args.limit,
        batch_size=args.batch_size,
    )


if __name__ == "__main__":
    main()
