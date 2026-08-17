#!/bin/bash
# check_pattern_distribution.sh - See distribution of patterns in near-match functions
#
# Usage:
#   ./docs/meta-strategy/scripts/check_pattern_distribution.sh
#
# Shows what's blocking progress on 80%+ matched functions

set -e
cd "$(git rev-parse --show-toplevel)"

DB="decomp.db"

if [ ! -f "$DB" ]; then
    echo "Error: $DB not found"
    exit 1
fi

echo "=== Pattern Distribution (80%+ matched functions) ==="
echo ""

# Check if pattern columns exist
HAS_PATTERNS=$(sqlite3 "$DB" "
SELECT COUNT(*) FROM pragma_table_info('functions')
WHERE name = 'has_linker_merged';
")

if [ "$HAS_PATTERNS" -eq 0 ]; then
    echo "Pattern columns not yet added to database."
    echo "Using verdict column as proxy..."
    echo ""

    sqlite3 -header -column "$DB" "
    SELECT
        COALESCE(verdict, 'unanalyzed') as verdict,
        COUNT(*) as count,
        ROUND(AVG(current_percent), 1) as avg_match
    FROM functions
    WHERE current_percent >= 80
      AND current_percent < 100
      AND excluded = 0
    GROUP BY verdict
    ORDER BY count DESC;
    "
else
    echo "Pattern columns found. Showing distribution..."
    echo ""

    sqlite3 -header -column "$DB" "
    SELECT
        CASE
            WHEN has_linker_merged THEN 'LINKER_MERGED (unfixable)'
            WHEN has_bool_mask THEN 'BOOL_MASK (unfixable)'
            WHEN has_assert_revs THEN 'ASSERT_REVS (unfixable)'
            WHEN has_ltcg_pooling THEN 'LTCG_POOLING (unfixable)'
            WHEN reachable_100 = 1 THEN 'CAN_REACH_100'
            ELSE 'UNANALYZED'
        END as category,
        COUNT(*) as count,
        ROUND(AVG(current_percent), 1) as avg_match
    FROM functions
    WHERE current_percent >= 80
      AND current_percent < 100
      AND excluded = 0
    GROUP BY category
    ORDER BY count DESC;
    "
fi

echo ""
echo "=== Reachable 100% Summary ==="

if [ "$HAS_PATTERNS" -eq 1 ]; then
    sqlite3 -header -column "$DB" "
    SELECT
        CASE WHEN reachable_100 THEN 'Can reach 100%' ELSE 'Has unfixable pattern' END as status,
        COUNT(*) as count
    FROM functions
    WHERE current_percent >= 80
      AND current_percent < 100
      AND excluded = 0
    GROUP BY reachable_100;
    "
else
    echo "(Pattern columns not populated yet)"
fi
