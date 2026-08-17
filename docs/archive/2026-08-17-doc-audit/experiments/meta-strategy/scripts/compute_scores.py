#!/usr/bin/env python3
"""compute_scores.py - Calculate ease, impact, confidence, priority scores

Implements the ease × impact × confidence formula to prioritize functions.
Pattern-based fixability is the most important factor.
Incorporates empirical success rates from recent attempt data.

Usage:
    ./docs/meta-strategy/scripts/compute_scores.py
"""

import sqlite3
import argparse
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_ROOT = SCRIPT_DIR.parent.parent.parent
DB_PATH = PROJECT_ROOT / "decomp.db"

# Add orchestrator module to path for database functions
ORCHESTRATOR_PATH = PROJECT_ROOT / "scripts"
sys.path.insert(0, str(ORCHESTRATOR_PATH))

from orchestrator.database import (
    get_unit_success_rates,
    get_function_type_success_rates,
    get_size_bucket_success_rates,
)

# Global caches for empirical success rates (loaded once per run)
_unit_success_rates: dict[str, float] = {}
_type_success_rates: dict[str, float] = {}
_size_success_rates: dict[str, float] = {}


def compute_ease(row: dict) -> int:
    """Ease = how quickly can we match this?

    Higher score = easier to complete.
    Incorporates empirical success rates from recent attempt data.
    """
    score = 50  # Base score

    size = row.get('size') or 0
    pct = row.get('current_percent') or 0
    fan_out = row.get('fan_out') or 0
    unit = row.get('unit') or ''

    # PATTERN-BASED FIXABILITY (most important factor)
    if row.get('has_linker_merged'):
        score -= 40  # Permanent gap from ICF
    if row.get('has_bool_mask'):
        score -= 30  # Compiler bool handling
    if row.get('has_assert_revs'):
        score -= 25  # Instruction scheduling
    if row.get('has_ltcg_pooling'):
        score -= 20  # Link-time optimization

    # If reachable, add bonus based on primary pattern
    if row.get('reachable_100'):
        primary = row.get('primary_pattern')
        if primary == 'COMPARISON_STYLE':
            score += 25  # 70%+ success rate
        elif primary == 'CONTROL_FLOW':
            score += 20  # 60-70% success rate
        elif primary == 'REGISTER_SWAP':
            score += 10  # 30% success rate
        elif primary is None:
            score += 30  # No detected issues

    # EMPIRICAL: Size bucket success rate (data shows tiny functions have 94.7% success!)
    size_bucket = 'tiny' if size < 50 else 'small' if size < 150 else 'medium' if size < 400 else 'large' if size < 1000 else 'huge'
    size_rate = _size_success_rates.get(size_bucket, 0.3)
    if size_rate >= 0.8:
        score += 25  # Tiny functions are almost always successful
    elif size_rate >= 0.5:
        score += 15
    elif size_rate >= 0.3:
        score += 5
    elif size_rate < 0.15:
        score -= 10  # Huge functions rarely succeed

    # Match percentage (higher = closer to done)
    if pct >= 99:
        score += 20
    elif pct >= 95:
        score += 15
    elif pct >= 90:
        score += 10
    elif pct >= 80:
        score += 5

    # Leaf function bonus (fewer dependencies)
    if fan_out == 0:
        score += 10
    elif fan_out <= 3:
        score += 5

    # Reference implementation available
    if row.get('has_rb3_ref'):
        score += 15

    # EMPIRICAL: Unit success rate (from recent attempt data)
    unit_rate = _unit_success_rates.get(unit, 0.3)  # Default to 30% if unknown
    if unit_rate >= 0.6:
        score += 20  # High success unit
    elif unit_rate >= 0.4:
        score += 10
    elif unit_rate < 0.1:
        score -= 15  # This unit rarely succeeds

    return max(0, min(score, 100))


def compute_impact(row: dict) -> int:
    """Impact = how valuable is matching this?

    Higher score = more valuable to complete.
    """
    score = 20  # Base score

    size = row.get('size') or 0
    fan_in = row.get('fan_in') or 0
    is_ctor = row.get('is_constructor')
    is_dtor = row.get('is_destructor')
    is_virtual = row.get('is_virtual')
    unit = row.get('unit') or ''
    demangled = row.get('demangled') or ''

    # Fan-in (how many functions call this one)
    if fan_in >= 20:
        score += 40
    elif fan_in >= 10:
        score += 30
    elif fan_in >= 5:
        score += 20
    elif fan_in >= 1:
        score += 10

    # Type anchor bonus (helps with class reconstruction)
    if is_ctor:
        score += 25
    elif is_dtor:
        score += 15
    elif is_virtual:
        score += 10

    # Size impact (larger = more code matched)
    if size > 1000:
        score += 25
    elif size > 500:
        score += 20
    elif size > 200:
        score += 10

    # Shared subsystem bonus (used across the codebase)
    shared_subsystems = ['system/', 'obj/', 'utl/']
    if any(unit.startswith(s) or f'/{s}' in unit for s in shared_subsystems):
        score += 15

    # Named function bonus (easier to understand purpose)
    if demangled and '::' in demangled:
        score += 5

    return min(score, 100)


def compute_confidence(row: dict) -> int:
    """Confidence = how sure are we the approach will work?

    Higher score = more confident in success.
    Incorporates empirical success rates from recent attempt data.
    """
    score = 50  # Base

    has_rb3 = row.get('has_rb3_ref')
    string_refs = row.get('string_ref_count') or 0
    attempt_count = row.get('attempt_count') or 0
    demangled = row.get('demangled') or ''
    unit = row.get('unit') or ''
    pct = row.get('current_percent') or 0
    is_ctor = row.get('is_constructor')
    is_dtor = row.get('is_destructor')
    is_virtual = row.get('is_virtual')

    # RB3 reference (huge confidence boost)
    if has_rb3:
        score += 30

    # String references (help understand purpose)
    if string_refs:
        score += min(string_refs * 5, 15)

    # Demangled name quality
    if demangled and '::' in demangled:
        score += 10
    elif demangled:
        score += 5

    # High match percentage (we're already close)
    if pct >= 99:
        score += 15
    elif pct >= 95:
        score += 10
    elif pct >= 90:
        score += 5

    # Previous attempts (negative if many failed, capped at DEFAULT_MAX_ATTEMPTS)
    if attempt_count == 0:
        score += 10  # Fresh target
    elif attempt_count <= 2:
        score += 5  # Few attempts
    elif attempt_count >= 10:
        score -= 20  # Many failed attempts - strongly deprioritize
    elif attempt_count >= 5:
        score -= 10  # Several failed attempts

    # Well-understood subsystems
    known_systems = ['system/math', 'system/utl', 'system/os', 'obj/Data']
    if any(s in unit for s in known_systems):
        score += 10

    # EMPIRICAL: Function type success rate (destructors: 61%, constructors: 47%)
    func_type = 'destructor' if is_dtor else 'constructor' if is_ctor else 'virtual' if is_virtual else 'other'
    type_rate = _type_success_rates.get(func_type, 0.3)
    if type_rate >= 0.5:
        score += 15  # Destructors/constructors are reliably successful
    elif type_rate >= 0.4:
        score += 10
    elif type_rate < 0.2:
        score -= 5  # This type rarely succeeds

    return max(0, min(score, 100))


def update_type_anchors(conn: sqlite3.Connection):
    """Mark constructors, destructors, and virtual functions."""
    # Mark constructors (MSVC mangled names)
    conn.execute("""
        UPDATE functions SET is_constructor = 1
        WHERE (demangled LIKE '%::%(%'
               AND demangled LIKE '%' || SUBSTR(demangled, INSTR(demangled, '::') + 2,
                   INSTR(SUBSTR(demangled, INSTR(demangled, '::') + 2), '(') - 1) || '%')
           OR symbol LIKE '%??0%'
    """)

    # Mark destructors
    conn.execute("""
        UPDATE functions SET is_destructor = 1
        WHERE demangled LIKE '%::~%'
           OR symbol LIKE '%??1%'
    """)

    # Mark virtual functions (heuristic)
    conn.execute("""
        UPDATE functions SET is_virtual = 1
        WHERE demangled LIKE '%virtual%'
           OR symbol LIKE '%@%'
    """)

    conn.commit()
    print("Type anchors marked")


def update_scores(db_path: Path, verbose: bool = False):
    """Compute and update all scores."""
    global _unit_success_rates, _type_success_rates, _size_success_rates

    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode = WAL")
    conn.row_factory = sqlite3.Row

    # Load empirical success rates from recent attempt data
    print("Loading empirical success rates from last 7 days...")
    _unit_success_rates = get_unit_success_rates(days=7, min_attempts=5, db_path=db_path)
    _type_success_rates = get_function_type_success_rates(days=7, db_path=db_path)
    _size_success_rates = get_size_bucket_success_rates(days=7, db_path=db_path)

    if verbose:
        print(f"  Unit success rates: {len(_unit_success_rates)} units")
        print(f"  Type success rates: {_type_success_rates}")
        print(f"  Size success rates: {_size_success_rates}")

    # First, mark type anchors
    update_type_anchors(conn)

    # Get all non-excluded, non-complete functions
    cursor = conn.execute("""
        SELECT symbol, size, current_percent, fan_in, fan_out,
               is_constructor, is_destructor, is_virtual,
               has_rb3_ref, string_ref_count, attempt_count,
               demangled, unit, verdict,
               has_linker_merged, has_bool_mask, has_assert_revs,
               has_ltcg_pooling, primary_pattern, reachable_100, excluded
        FROM functions
        WHERE excluded = 0
    """)

    updated = 0
    for row in cursor.fetchall():
        row_dict = dict(row)
        ease = compute_ease(row_dict)
        impact = compute_impact(row_dict)
        confidence = compute_confidence(row_dict)

        # Base priority
        base_priority = (ease * impact * confidence) / 10000

        # Apply reachable_100 multiplier
        reachable = row_dict.get('reachable_100')
        if reachable:
            multiplier = 1.5
        else:
            multiplier = 0.5

        priority = base_priority * multiplier

        # Already at 100% - priority is 0
        pct = row_dict.get('current_percent') or 0
        if pct >= 100:
            priority = 0

        # AT_LIMIT functions get very low priority
        if row_dict.get('verdict') == 'AT_LIMIT':
            priority = priority * 0.1

        conn.execute("""
            UPDATE functions
            SET ease_score = ?, impact_score = ?, confidence_score = ?, priority_score = ?
            WHERE symbol = ?
        """, (ease, impact, confidence, priority, row_dict['symbol']))

        updated += 1

    conn.commit()

    # Report statistics
    print(f"Updated scores for {updated} functions")
    print_statistics(conn)
    conn.close()


def print_statistics(conn: sqlite3.Connection):
    """Print score distribution statistics."""
    # Top priority functions
    cursor = conn.execute("""
        SELECT symbol, demangled, unit, current_percent,
               ease_score, impact_score, confidence_score, priority_score,
               reachable_100
        FROM functions
        WHERE priority_score > 0 AND excluded = 0
        ORDER BY priority_score DESC
        LIMIT 15
    """)

    print("\n=== Top 15 Priority Functions ===")
    for row in cursor:
        symbol = row[0][:40] if row[0] else '?'
        demangled = (row[1] or '')[:50]
        pct = row[3] or 0
        ease, impact, conf, priority = row[4], row[5], row[6], row[7]
        reachable = "✓" if row[8] else "✗"
        print(f"  {priority:6.2f} | {pct:5.1f}% | E{ease:2d} I{impact:2d} C{conf:2d} | {reachable} | {demangled or symbol}")

    # Score distribution
    cursor = conn.execute("""
        SELECT
            CASE
                WHEN priority_score >= 50 THEN 'HIGH (50+)'
                WHEN priority_score >= 20 THEN 'MEDIUM (20-49)'
                WHEN priority_score >= 5 THEN 'LOW (5-19)'
                WHEN priority_score > 0 THEN 'VERY LOW (<5)'
                ELSE 'ZERO'
            END as tier,
            COUNT(*) as count,
            ROUND(AVG(current_percent), 1) as avg_pct
        FROM functions
        WHERE excluded = 0 AND current_percent < 100
        GROUP BY tier
        ORDER BY
            CASE tier
                WHEN 'HIGH (50+)' THEN 1
                WHEN 'MEDIUM (20-49)' THEN 2
                WHEN 'LOW (5-19)' THEN 3
                WHEN 'VERY LOW (<5)' THEN 4
                ELSE 5
            END
    """)

    print("\n=== Priority Score Distribution ===")
    for row in cursor:
        print(f"  {row[0]}: {row[1]} functions (avg {row[2]}%)")

    # Reachable summary
    cursor = conn.execute("""
        SELECT
            CASE WHEN reachable_100 THEN 'Can reach 100%' ELSE 'Has unfixable pattern' END as status,
            COUNT(*) as count
        FROM functions
        WHERE excluded = 0 AND current_percent >= 80 AND current_percent < 100
        GROUP BY reachable_100
    """)

    print("\n=== Reachable 100% (80%+ functions) ===")
    for row in cursor:
        print(f"  {row[0]}: {row[1]}")


def main():
    parser = argparse.ArgumentParser(description='Compute priority scores for functions')
    parser.add_argument('--db', type=str, default=str(DB_PATH), help='Database path')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    args = parser.parse_args()

    db_path = Path(args.db)
    if not db_path.exists():
        print(f"Error: Database not found: {db_path}")
        return 1

    update_scores(db_path, args.verbose)
    return 0


if __name__ == '__main__':
    exit(main())
