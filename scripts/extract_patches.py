#!/usr/bin/env python3
"""Extract and apply patches from decomp.db to main repository."""

import argparse
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path


def get_best_patches(db_path: str, min_percent: float = 0.0) -> list[dict]:
    """Get best patch for each function (highest end_percent)."""
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row

    # Priority: functions.source_patch > attempts.patch (by end_percent)
    patches = []

    # First get functions with source_patch
    for row in conn.execute("""
        SELECT symbol, demangled, source_patch, current_percent
        FROM functions
        WHERE source_patch IS NOT NULL AND source_patch != ''
    """):
        patches.append({
            'symbol': row['symbol'],
            'demangled': row['demangled'],
            'patch': row['source_patch'],
            'percent': row['current_percent'],
            'source': 'source_patch'
        })

    seen_symbols = {p['symbol'] for p in patches}

    # Then get best attempt for functions without source_patch
    for row in conn.execute("""
        SELECT f.symbol, f.demangled, a.patch, a.end_percent
        FROM attempts a
        JOIN functions f ON a.function_id = f.id
        WHERE a.patch IS NOT NULL AND a.patch != ''
          AND a.end_percent >= ?
          AND length(a.patch) > 200  -- Skip trivial patches
        ORDER BY a.end_percent DESC
    """, (min_percent,)):
        if row['symbol'] not in seen_symbols:
            patches.append({
                'symbol': row['symbol'],
                'demangled': row['demangled'],
                'patch': row['patch'],
                'percent': row['end_percent'],
                'source': 'attempt'
            })
            seen_symbols.add(row['symbol'])

    conn.close()
    return sorted(patches, key=lambda p: p['percent'], reverse=True)


def clean_patch(patch: str) -> str:
    """Remove spurious changes from patch (e.g., .gitkeep deletions)."""
    lines = patch.split('\n')
    cleaned = []
    skip_until_next_diff = False

    for line in lines:
        if line.startswith('diff --git'):
            # Check if this is a spurious file
            if '.gitkeep' in line or 'orig/' in line:
                skip_until_next_diff = True
                continue
            skip_until_next_diff = False

        if not skip_until_next_diff:
            cleaned.append(line)

    return '\n'.join(cleaned)


def apply_patch(patch: str, dry_run: bool = False) -> tuple[bool, str]:
    """Apply a patch to current directory."""
    with tempfile.NamedTemporaryFile(mode='w', suffix='.patch', delete=False) as f:
        f.write(patch)
        patch_file = f.name

    try:
        cmd = ['git', 'apply']
        if dry_run:
            cmd.append('--check')
        cmd.append(patch_file)

        result = subprocess.run(cmd, capture_output=True, text=True)
        return result.returncode == 0, result.stderr
    finally:
        Path(patch_file).unlink()


def main():
    parser = argparse.ArgumentParser(description='Extract and apply patches from decomp.db')
    parser.add_argument('--db', default='decomp.db', help='Path to database')
    parser.add_argument('--min-percent', type=float, default=80.0,
                        help='Minimum match %% to include (default: 80)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Check patches without applying')
    parser.add_argument('--list', action='store_true',
                        help='List available patches without applying')
    parser.add_argument('--all', action='store_true',
                        help='Include all patches regardless of match %%')
    parser.add_argument('--export', metavar='FILE',
                        help='Export combined patch to file instead of applying')
    args = parser.parse_args()

    min_pct = 0.0 if args.all else args.min_percent
    patches = get_best_patches(args.db, min_pct)

    if not patches:
        print("No patches found in database")
        return 1

    print(f"Found {len(patches)} patches (min {min_pct}% match)\n")

    if args.list:
        for p in patches:
            print(f"  {p['percent']:6.2f}%  {p['demangled'][:60]}")
        return 0

    # Clean and combine patches
    combined = []
    for p in patches:
        cleaned = clean_patch(p['patch'])
        if cleaned.strip():
            combined.append(f"# {p['demangled']} ({p['percent']:.1f}%)\n{cleaned}")

    full_patch = '\n\n'.join(combined)

    if args.export:
        Path(args.export).write_text(full_patch)
        print(f"Exported to {args.export}")
        return 0

    # Apply patches one by one
    applied = 0
    failed = 0

    for p in patches:
        cleaned = clean_patch(p['patch'])
        if not cleaned.strip():
            continue

        success, error = apply_patch(cleaned, dry_run=args.dry_run)
        status = "+" if success else "x"
        action = "would apply" if args.dry_run else "applied"

        if success:
            print(f"{status} {action}: {p['demangled'][:50]} ({p['percent']:.1f}%)")
            applied += 1
        else:
            print(f"{status} FAILED: {p['demangled'][:50]}")
            if error:
                print(f"   {error.strip()}")
            failed += 1

    print(f"\n{'Would apply' if args.dry_run else 'Applied'}: {applied}, Failed: {failed}")
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
