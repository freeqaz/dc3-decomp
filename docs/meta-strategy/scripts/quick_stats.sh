#!/bin/bash
# quick_stats.sh - Quick overview of decomp progress
#
# Usage:
#   ./docs/meta-strategy/scripts/quick_stats.sh

set -e
cd "$(git rev-parse --show-toplevel)"

DB="decomp.db"

if [ ! -f "$DB" ]; then
    echo "Error: $DB not found"
    exit 1
fi

echo "=== DC3 Decomp Quick Stats ==="
echo ""

echo "--- Overall Progress ---"
sqlite3 -header -column "$DB" "
SELECT
    COUNT(*) as total_functions,
    SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) as matched_100,
    ROUND(100.0 * SUM(CASE WHEN current_percent >= 100 THEN 1 ELSE 0 END) / COUNT(*), 2) as pct_matched,
    SUM(CASE WHEN excluded = 1 THEN 1 ELSE 0 END) as excluded
FROM functions;
"

echo ""
echo "--- Progress by Range ---"
sqlite3 -header -column "$DB" "
SELECT
    CASE
        WHEN current_percent >= 100 THEN '100% (matched)'
        WHEN current_percent >= 99 THEN '99-99.9%'
        WHEN current_percent >= 95 THEN '95-98.9%'
        WHEN current_percent >= 90 THEN '90-94.9%'
        WHEN current_percent >= 80 THEN '80-89.9%'
        WHEN current_percent >= 50 THEN '50-79.9%'
        ELSE '<50%'
    END as range,
    COUNT(*) as count
FROM functions
WHERE excluded = 0
GROUP BY range
ORDER BY
    CASE range
        WHEN '100% (matched)' THEN 1
        WHEN '99-99.9%' THEN 2
        WHEN '95-98.9%' THEN 3
        WHEN '90-94.9%' THEN 4
        WHEN '80-89.9%' THEN 5
        WHEN '50-79.9%' THEN 6
        ELSE 7
    END;
"

echo ""
echo "--- Verdict Distribution (incomplete only) ---"
sqlite3 -header -column "$DB" "
SELECT
    COALESCE(verdict, 'unanalyzed') as verdict,
    COUNT(*) as count
FROM functions
WHERE current_percent < 100
  AND excluded = 0
GROUP BY verdict
ORDER BY count DESC;
"

echo ""
echo "--- Near Complete (99%+) ---"
sqlite3 "$DB" "
SELECT COUNT(*) || ' functions at 99%+ (NEAR_COMPLETE targets)'
FROM functions
WHERE current_percent >= 99
  AND current_percent < 100
  AND excluded = 0;
"
