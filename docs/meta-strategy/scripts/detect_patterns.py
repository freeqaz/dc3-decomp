#!/usr/bin/env python3
"""detect_patterns.py - Populate pattern columns from objdiff analysis

Runs objdiff-cli with --analyze --verdict on functions at 80%+ match
and populates has_linker_merged, has_bool_mask, has_assert_revs, etc.

Also extracts granular merged symbol information for high-value target
identification (e.g., MILO_NOTIFY_ONCE candidates via merged_AddToStrings).

Usage:
    ./docs/meta-strategy/scripts/detect_patterns.py [--limit N] [--min-percent P]
"""

import sqlite3
import subprocess
import json
import argparse
import sys
from pathlib import Path

# Find project root
SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
DB_PATH = PROJECT_ROOT / "decomp.db"
OBJDIFF_CLI = PROJECT_ROOT / "bin" / "objdiff-cli"

# Known merged symbol addresses for categorization
# These are well-known ICF merge targets from the linker
MERGED_SYMBOL_CATEGORIES = {
    # AddToStrings - HIGH VALUE (MILO_NOTIFY_ONCE candidates)
    "merged_AddToStrings": "addtostrings",
    "merged_82372AA0": "addtostrings",  # AddToStrings address from symbols.txt

    # MakeString variants - common, unfixable
    "merged_MakeString": "makestring",
    "merged_824D1870": "makestring",  # 901 MakeString variants merged here!
    "merged_823314D8": "makestring",  # Common MakeString address
    "merged_82331360": "makestring",  # Another MakeString variant

    # SetObjConcrete - ObjPtr merged calls
    "merged_SetObjConcrete": "setobjconcrete",
}


def categorize_merged_symbol(symbol_name: str, resolved_names: list[str] | None = None) -> str:
    """
    Categorize a merged symbol based on its name or resolved symbols.

    Args:
        symbol_name: The merged symbol name (e.g., "merged_824D1870")
        resolved_names: Optional list of demangled names at this address

    Returns:
        Category string: 'addtostrings', 'makestring', 'setobjconcrete', 'destructor', 'unknown'
    """
    # Check direct mapping first
    if symbol_name in MERGED_SYMBOL_CATEGORIES:
        return MERGED_SYMBOL_CATEGORIES[symbol_name]

    # Check resolved names for patterns
    if resolved_names:
        for name in resolved_names:
            # Destructors (??_G and ??_E are vector/scalar deleting destructors)
            if "??_G" in name or "??_E" in name or "~" in name:
                return "destructor"
            # AddToStrings pattern
            if "AddToStrings" in name:
                return "addtostrings"
            # MakeString pattern
            if "MakeString" in name:
                return "makestring"
            # SetObjConcrete/ObjPtr pattern
            if "SetObjConcrete" in name or "ObjPtr" in name:
                return "setobjconcrete"

    return "unknown"


def detect_patterns(db_path: Path, min_percent: float = 80.0, limit: int = 5000, verbose: bool = False):
    """Run objdiff --analyze on functions and extract patterns."""
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")

    # Get candidates (80%+ match, not excluded, not complete, not yet analyzed)
    cursor = conn.execute("""
        SELECT id, symbol, current_percent FROM functions
        WHERE current_percent >= ?
          AND current_percent < 100
          AND excluded = 0
          AND (has_linker_merged IS NULL
               OR (has_linker_merged = 0 AND has_bool_mask = 0
                   AND has_assert_revs = 0 AND has_ltcg_pooling = 0
                   AND primary_pattern IS NULL))
        ORDER BY current_percent DESC
        LIMIT ?
    """, (min_percent, limit))

    candidates = cursor.fetchall()
    total = len(candidates)
    print(f"Found {total} functions to analyze (>= {min_percent}%, not excluded, < 100%)")

    processed = 0
    errors = 0
    merged_symbols_added = 0

    for row in candidates:
        func_id = row['id']
        symbol = row['symbol']
        pct = row['current_percent']

        try:
            # Run objdiff with pattern analysis
            result = subprocess.run(
                [str(OBJDIFF_CLI), 'diff', '-p', str(PROJECT_ROOT), symbol,
                 '-f', 'json', '--analyze', '--verdict'],
                capture_output=True, text=True, timeout=60,
                cwd=PROJECT_ROOT
            )

            if result.returncode != 0:
                if verbose:
                    print(f"  Error: objdiff failed for {symbol}: {result.stderr[:100]}")
                errors += 1
                continue

            data = json.loads(result.stdout)
            analysis = data.get('analysis', {})
            patterns = analysis.get('patterns', [])
            verdict = data.get('verdict', {})

            # Extract pattern flags
            has_linker = any(p.get('pattern') == 'LINKER_MERGED' for p in patterns)
            has_bool = any(p.get('pattern') == 'BOOL_MASK' for p in patterns)
            has_assert = any(p.get('pattern') == 'ASSERT_REVS' for p in patterns)
            has_ltcg = any(p.get('pattern') == 'LTCG_POOLING' for p in patterns)

            # Extract merged symbol details for granular tracking
            merged_functions = []
            for p in patterns:
                if p.get('pattern') == 'LINKER_MERGED':
                    details = p.get('details', {})
                    merged_functions = details.get('merged_functions', [])
                    break

            # Process merged symbols
            has_addtostrings = False
            has_makestring = False
            has_setobjconcrete = False
            merged_count = len(merged_functions)

            # Clear existing merged symbols for this function (re-analyze)
            conn.execute("DELETE FROM merged_symbols WHERE function_id = ?", (func_id,))

            for mf in merged_functions:
                merged_name = mf.get('name', '')
                call_count = mf.get('count', 1)

                # Categorize the merged symbol
                category = categorize_merged_symbol(merged_name)

                # Track category flags
                if category == 'addtostrings':
                    has_addtostrings = True
                elif category == 'makestring':
                    has_makestring = True
                elif category == 'setobjconcrete':
                    has_setobjconcrete = True

                # Insert into merged_symbols table
                conn.execute("""
                    INSERT INTO merged_symbols (function_id, symbol_name, call_count, category)
                    VALUES (?, ?, ?, ?)
                """, (func_id, merged_name, call_count, category))
                merged_symbols_added += 1

            # Determine if there are unfixable patterns from verdict
            classification = verdict.get('classification', '')
            verdict_reason = None
            if classification in ('AT_LIMIT', 'UNFIXABLE'):
                # Check verdict factors for more detail
                factors = verdict.get('factors', [])
                reasons = []
                for f in factors:
                    if f.get('name') == 'merged_call_ratio' and f.get('result') == 'above_threshold':
                        has_linker = True
                        reasons.append('high_merged_ratio')
                    elif f.get('name') == 'bool_mask_detected' and f.get('value'):
                        has_bool = True
                        reasons.append('bool_mask')
                if reasons:
                    verdict_reason = ','.join(reasons)

            # Primary pattern (first one detected, prefer fixability info)
            primary = None
            for p in patterns:
                fixability = p.get('fixability', '')
                if fixability in ('likely_fixable', 'maybe_fixable'):
                    primary = p.get('pattern')
                    break
            if not primary and patterns:
                primary = patterns[0].get('pattern')

            # Compute reachable_100 based on unfixable patterns
            reachable = not any([has_linker, has_bool, has_assert, has_ltcg])

            # If verdict says AT_LIMIT, mark as not reachable
            if classification == 'AT_LIMIT':
                reachable = False

            # Update database with all pattern info
            conn.execute("""
                UPDATE functions SET
                    has_linker_merged = ?,
                    has_bool_mask = ?,
                    has_assert_revs = ?,
                    has_ltcg_pooling = ?,
                    primary_pattern = ?,
                    reachable_100 = ?,
                    has_addtostrings = ?,
                    has_makestring = ?,
                    has_setobjconcrete = ?,
                    merged_symbol_count = ?,
                    verdict_reason = ?
                WHERE id = ?
            """, (has_linker, has_bool, has_assert, has_ltcg, primary, reachable,
                  has_addtostrings, has_makestring, has_setobjconcrete, merged_count,
                  verdict_reason, func_id))

            processed += 1

            if processed % 100 == 0:
                conn.commit()
                print(f"  Processed {processed}/{total} ({processed*100//total}%)")

        except subprocess.TimeoutExpired:
            if verbose:
                print(f"  Timeout: {symbol}")
            errors += 1
            continue
        except json.JSONDecodeError as e:
            if verbose:
                print(f"  JSON error for {symbol}: {e}")
            errors += 1
            continue
        except Exception as e:
            if verbose:
                print(f"  Error processing {symbol}: {e}")
            errors += 1
            continue

    conn.commit()

    # Report statistics
    print(f"\nProcessed {processed} functions ({errors} errors)")
    print(f"Added {merged_symbols_added} merged symbol records")
    print_statistics(conn)
    conn.close()


def print_statistics(conn):
    """Print pattern distribution statistics."""
    cursor = conn.execute("""
        SELECT
            SUM(CASE WHEN has_linker_merged THEN 1 ELSE 0 END) as linker_merged,
            SUM(CASE WHEN has_bool_mask THEN 1 ELSE 0 END) as bool_mask,
            SUM(CASE WHEN has_assert_revs THEN 1 ELSE 0 END) as assert_revs,
            SUM(CASE WHEN has_ltcg_pooling THEN 1 ELSE 0 END) as ltcg_pooling,
            SUM(CASE WHEN reachable_100 = 1 THEN 1 ELSE 0 END) as can_reach_100,
            COUNT(*) as total_analyzed
        FROM functions
        WHERE excluded = 0
          AND current_percent >= 80
          AND current_percent < 100
          AND (has_linker_merged IS NOT NULL OR has_bool_mask IS NOT NULL)
    """)
    stats = cursor.fetchone()

    print("\n=== Pattern Detection Results ===")
    print(f"  LINKER_MERGED:  {stats[0] or 0}")
    print(f"  BOOL_MASK:      {stats[1] or 0}")
    print(f"  ASSERT_REVS:    {stats[2] or 0}")
    print(f"  LTCG_POOLING:   {stats[3] or 0}")
    print(f"  Can reach 100%: {stats[4] or 0} / {stats[5] or 0}")

    # Distribution by primary pattern
    cursor = conn.execute("""
        SELECT
            COALESCE(primary_pattern, 'NONE') as pattern,
            COUNT(*) as count,
            ROUND(AVG(current_percent), 1) as avg_pct
        FROM functions
        WHERE excluded = 0
          AND current_percent >= 80
          AND current_percent < 100
          AND primary_pattern IS NOT NULL
        GROUP BY primary_pattern
        ORDER BY count DESC
        LIMIT 10
    """)
    print("\n=== Primary Pattern Distribution ===")
    for row in cursor:
        print(f"  {row[0]}: {row[1]} (avg {row[2]}%)")

    # Merged symbol category breakdown (v6)
    cursor = conn.execute("""
        SELECT category, COUNT(*) as count, COUNT(DISTINCT function_id) as functions
        FROM merged_symbols
        GROUP BY category
        ORDER BY count DESC
    """)
    categories = cursor.fetchall()
    if categories:
        print("\n=== Merged Symbol Categories ===")
        for row in categories:
            print(f"  {row[0] or 'unknown'}: {row[1]} calls in {row[2]} functions")

    # High-value target identification
    cursor = conn.execute("""
        SELECT COUNT(*) FROM functions
        WHERE has_addtostrings = 1 AND excluded = 0 AND current_percent < 100
    """)
    addtostrings_count = cursor.fetchone()[0]
    if addtostrings_count > 0:
        print(f"\n=== HIGH-VALUE TARGETS ===")
        print(f"  AddToStrings candidates (MILO_NOTIFY_ONCE): {addtostrings_count}")
        # List them
        cursor = conn.execute("""
            SELECT symbol, demangled, current_percent FROM functions
            WHERE has_addtostrings = 1 AND excluded = 0 AND current_percent < 100
            ORDER BY current_percent DESC
            LIMIT 10
        """)
        for row in cursor:
            print(f"    {row[2]:.1f}% - {row[1] or row[0]}")


def main():
    parser = argparse.ArgumentParser(description='Detect patterns in functions using objdiff')
    parser.add_argument('--limit', type=int, default=5000, help='Max functions to process')
    parser.add_argument('--min-percent', type=float, default=80.0, help='Minimum match percent')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--db', type=str, default=str(DB_PATH), help='Database path')
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Error: Database not found: {db_path}")
        sys.exit(1)

    if not OBJDIFF_CLI.exists():
        print(f"Error: objdiff-cli not found: {OBJDIFF_CLI}")
        sys.exit(1)

    detect_patterns(db_path, args.min_percent, args.limit, args.verbose)


if __name__ == '__main__':
    main()
