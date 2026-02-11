#!/usr/bin/env python3
"""detect_merged_from_map.py - Populate has_linker_merged from map file

Uses the linker map file (ground truth) to detect ICF-merged functions.
This is more complete than objdiff detection because:
- Map file has all merged addresses, not just those appearing in diffs
- Much faster (no need to run objdiff on each function)

Usage:
    ./docs/meta-strategy/scripts/detect_merged_from_map.py [--dry-run] [--verbose]
"""

import argparse
import sqlite3
import sys
from pathlib import Path

# Add tools directory to path for MergedSymbolLookup
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
sys.path.insert(0, str(PROJECT_ROOT / "tools"))

from merged_symbols import MergedSymbolLookup

DB_PATH = PROJECT_ROOT / "decomp.db"
MAP_FILE = PROJECT_ROOT / "orig" / "373307D9" / "ham_xbox_r.map"


def build_symbol_to_address_map(map_file: Path) -> dict[str, str]:
    """Parse map file and build symbol -> address mapping."""
    import re

    symbol_to_addr: dict[str, str] = {}

    # Parse the map file - same format as MergedSymbolLookup uses
    # Format: 0005:00001360       ??_GObjRef@@UAAPAXI@Z      82331360 f i App.obj
    pattern = re.compile(
        r'^\s*\d{4}:[0-9a-fA-F]+\s+'  # segment:offset
        r'(\S+)\s+'                    # symbol name
        r'([0-9a-fA-F]{8})\s+'         # address (8 hex digits)
    )

    with open(map_file, 'r') as f:
        for line in f:
            match = pattern.match(line)
            if match:
                symbol = match.group(1)
                address = match.group(2).upper()
                symbol_to_addr[symbol] = address

    return symbol_to_addr


def detect_merged_from_map(
    db_path: Path,
    map_file: Path,
    dry_run: bool = False,
    verbose: bool = False,
    stats: bool = False
):
    """Detect merged functions from map file and update database."""

    # Load merged address lookup
    lookup = MergedSymbolLookup(map_file)
    merged_addresses = lookup.get_merged_addresses()

    # Build set of addresses that have multiple symbols (ICF merged)
    merged_addr_set = set(merged_addresses.keys())
    print(f"Found {len(merged_addr_set)} addresses with multiple symbols (ICF merged)")

    # Build symbol -> address mapping
    print("Building symbol-to-address map...")
    symbol_to_addr = build_symbol_to_address_map(map_file)
    print(f"Loaded {len(symbol_to_addr)} symbols from map file")

    # Connect to database
    conn = sqlite3.connect(db_path)
    conn.row_factory = sqlite3.Row

    # Get all symbols from the database
    cursor = conn.execute("""
        SELECT symbol, has_linker_merged, reachable_100, excluded, current_percent
        FROM functions
    """)
    functions = cursor.fetchall()
    print(f"Found {len(functions)} functions in database")

    # Find which DB symbols are at merged addresses
    symbols_to_update = []
    already_marked = 0
    not_in_map = 0
    not_merged = 0

    for row in functions:
        symbol = row['symbol']

        # Check if symbol is in map file
        if symbol not in symbol_to_addr:
            not_in_map += 1
            continue

        addr = symbol_to_addr[symbol]

        # Check if address is merged
        if addr in merged_addr_set:
            if row['has_linker_merged'] == 1:
                already_marked += 1
            else:
                symbols_to_update.append({
                    'symbol': symbol,
                    'address': addr,
                    'current_percent': row['current_percent'],
                    'excluded': row['excluded'],
                })
                if verbose:
                    # Show what symbols share this address
                    shared = merged_addresses[addr]
                    shared_names = [s['symbol'] for s in shared[:3]]
                    if len(shared) > 3:
                        shared_names.append(f"... +{len(shared)-3} more")
                    print(f"  {symbol} @ 0x{addr} shares with {', '.join(shared_names)}")
        else:
            not_merged += 1

    print(f"\nAnalysis complete:")
    print(f"  Symbols not in map file: {not_in_map}")
    print(f"  Symbols not at merged addresses: {not_merged}")
    print(f"  Already marked as merged: {already_marked}")
    print(f"  Need to mark as merged: {len(symbols_to_update)}")

    # Breakdown by exclusion status
    non_excluded = [s for s in symbols_to_update if not s['excluded']]
    excluded = [s for s in symbols_to_update if s['excluded']]
    print(f"    - Non-excluded: {len(non_excluded)}")
    print(f"    - Excluded: {len(excluded)}")

    if dry_run:
        print("\n[DRY RUN] Would update the following:")
        # Show sample
        for s in symbols_to_update[:10]:
            pct = s['current_percent']
            pct_str = f"{pct:.1f}%" if pct is not None else "N/A"
            print(f"  {s['symbol']} ({pct_str})")
        if len(symbols_to_update) > 10:
            print(f"  ... and {len(symbols_to_update) - 10} more")
        return

    # Perform updates
    print("\nUpdating database...")
    update_count = 0

    for s in symbols_to_update:
        conn.execute("""
            UPDATE functions
            SET has_linker_merged = 1, reachable_100 = 0
            WHERE symbol = ?
        """, (s['symbol'],))
        update_count += 1

        if update_count % 500 == 0:
            conn.commit()
            print(f"  Updated {update_count}/{len(symbols_to_update)}")

    conn.commit()
    print(f"Updated {update_count} functions")

    if stats:
        print_statistics(conn)

    conn.close()


def print_statistics(conn):
    """Print merged function statistics."""
    cursor = conn.execute("""
        SELECT
            COUNT(*) as total,
            SUM(CASE WHEN has_linker_merged = 1 THEN 1 ELSE 0 END) as merged,
            SUM(CASE WHEN has_linker_merged = 1 AND excluded = 0 THEN 1 ELSE 0 END) as merged_non_excluded,
            SUM(CASE WHEN reachable_100 = 1 AND excluded = 0 THEN 1 ELSE 0 END) as reachable
        FROM functions
    """)
    stats = cursor.fetchone()

    print("\n=== Updated Statistics ===")
    print(f"  Total functions: {stats[0]}")
    print(f"  Has linker merged: {stats[1]}")
    print(f"  Has linker merged (non-excluded): {stats[2]}")
    print(f"  Reachable 100% (non-excluded): {stats[3]}")

    # Breakdown by match percentage
    cursor = conn.execute("""
        SELECT
            CASE
                WHEN current_percent >= 95 THEN '95-99%'
                WHEN current_percent >= 90 THEN '90-94%'
                WHEN current_percent >= 80 THEN '80-89%'
                ELSE '<80%'
            END as bucket,
            COUNT(*) as total,
            SUM(CASE WHEN has_linker_merged = 1 THEN 1 ELSE 0 END) as merged
        FROM functions
        WHERE excluded = 0 AND current_percent < 100
        GROUP BY bucket
        ORDER BY bucket DESC
    """)

    print("\n=== Merged by Match Percentage ===")
    print(f"  {'Bucket':<10} {'Total':>8} {'Merged':>8} {'Pct':>6}")
    for row in cursor:
        pct = row[2] * 100 // row[1] if row[1] > 0 else 0
        print(f"  {row[0]:<10} {row[1]:>8} {row[2]:>8} {pct:>5}%")


def main():
    parser = argparse.ArgumentParser(
        description='Detect merged functions from linker map file'
    )
    parser.add_argument('--dry-run', '-n', action='store_true',
                        help='Show what would be updated without changing DB')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print each symbol being marked')
    parser.add_argument('--stats', '-s', action='store_true',
                        help='Show summary statistics after update')
    parser.add_argument('--db', type=str, default=str(DB_PATH),
                        help='Database path')
    parser.add_argument('--map-file', '-m', type=str, default=str(MAP_FILE),
                        help='Linker map file path')
    args = parser.parse_args()

    db_path = Path(args.db)
    map_file = Path(args.map_file)

    if not db_path.exists():
        print(f"Error: Database not found: {db_path}")
        sys.exit(1)

    if not map_file.exists():
        print(f"Error: Map file not found: {map_file}")
        sys.exit(1)

    detect_merged_from_map(
        db_path,
        map_file,
        dry_run=args.dry_run,
        verbose=args.verbose,
        stats=args.stats
    )


if __name__ == '__main__':
    main()
