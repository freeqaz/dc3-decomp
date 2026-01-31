#!/usr/bin/env python3
"""Debug failing patches from decomp.db."""

import sqlite3
import subprocess
import tempfile
from pathlib import Path

def main():
    conn = sqlite3.connect('decomp.db')
    conn.row_factory = sqlite3.Row

    symbols = [
        '?OnSelectedSym@UIList@@IAA?AVDataNode@@PAVDataArray@@@Z',
        '?LockAndDelete@CharClip@@SAXQAPAV1@HH@Z',
        '?SetTextToken@UILabel@@UAAXVSymbol@@@Z'
    ]

    for sym in symbols:
        row = conn.execute("""
            SELECT f.demangled, a.patch, a.end_percent
            FROM attempts a
            JOIN functions f ON a.function_id = f.id
            WHERE f.symbol = ?
            AND a.patch IS NOT NULL AND a.patch != ''
            ORDER BY a.end_percent DESC
            LIMIT 1
        """, (sym,)).fetchone()

        if row:
            print(f"\n{'='*60}")
            print(f"{row['demangled']} ({row['end_percent']:.1f}%)")
            print('='*60)

            patch = row['patch']

            # Write patch to temp file
            patch_file = Path(f'/tmp/claude/patch_{sym.replace("?", "").replace("@", "_")[:30]}.patch')
            patch_file.write_text(patch)

            # Try to apply
            result = subprocess.run(
                ['git', 'apply', '--check', str(patch_file)],
                capture_output=True, text=True
            )

            if result.returncode != 0:
                print(f"PATCH FAILS:\n{result.stderr}")

                # Try with -3 for 3-way merge
                result3 = subprocess.run(
                    ['git', 'apply', '--check', '--3way', str(patch_file)],
                    capture_output=True, text=True
                )
                if result3.returncode == 0:
                    print("BUT: Would work with --3way merge")

                # Show relevant context
                print("\nPATCH CONTENT (first 1500 chars):")
                print(patch[:1500])
            else:
                print("Patch would apply cleanly!")
        else:
            print(f"No patch found for {sym}")

if __name__ == '__main__':
    main()
