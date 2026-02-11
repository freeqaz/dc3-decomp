#!/bin/bash
# find_quick_wins.sh - Find high-value targets for quick progress
#
# Usage:
#   ./docs/meta-strategy/scripts/find_quick_wins.sh [limit]
#
# Examples:
#   ./docs/meta-strategy/scripts/find_quick_wins.sh      # Default 15 results per section
#   ./docs/meta-strategy/scripts/find_quick_wins.sh 30   # Show 30 results per section

set -e
cd "$(git rev-parse --show-toplevel)"

DB="decomp.db"
LIMIT="${1:-15}"

if [ ! -f "$DB" ]; then
    echo "Error: $DB not found"
    exit 1
fi

echo "# Quick Win Targets"
echo ""
echo "Generated: $(date '+%Y-%m-%d %H:%M')"
echo ""

# Check if pattern columns exist
HAS_PATTERNS=$(sqlite3 "$DB" "
SELECT COUNT(*) FROM pragma_table_info('functions')
WHERE name = 'reachable_100';
")

# Check if call_edges table exists and has data
TABLE_EXISTS=$(sqlite3 "$DB" "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='call_edges';")
if [ "$TABLE_EXISTS" -eq 1 ]; then
    HAS_CALL_GRAPH=$(sqlite3 "$DB" "SELECT CASE WHEN COUNT(*) > 0 THEN 1 ELSE 0 END FROM call_edges;")
else
    HAS_CALL_GRAPH=0
fi

# Check if fan_in column exists
HAS_FANIN=$(sqlite3 "$DB" "
SELECT COUNT(*) FROM pragma_table_info('functions')
WHERE name = 'fan_in';
")

echo "## High-Impact Functions (Most Dependents)"
echo ""
echo "Functions that are called by many others. Fixing these validates call sites across the codebase."
echo ""

if [ "$HAS_CALL_GRAPH" -eq 1 ]; then
    echo "| Symbol | Demangled | Callers | Match % |"
    echo "|--------|-----------|---------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || f.symbol,
        SUBSTR(f.demangled, 1, 45) || CASE WHEN LENGTH(f.demangled) > 45 THEN '...' ELSE '' END,
        c.caller_count,
        f.current_percent || '% |'
    FROM functions f
    JOIN (
        SELECT callee_symbol, COUNT(*) as caller_count
        FROM call_edges
        GROUP BY callee_symbol
        HAVING caller_count >= 5
    ) c ON f.symbol = c.callee_symbol
    WHERE f.current_percent < 100
      AND f.excluded = 0
    ORDER BY c.caller_count DESC, f.current_percent DESC
    LIMIT $LIMIT;
    "
elif [ "$HAS_FANIN" -eq 1 ]; then
    echo "| Symbol | Demangled | Fan-In | Match % |"
    echo "|--------|-----------|--------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || symbol,
        SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
        fan_in,
        current_percent || '% |'
    FROM functions
    WHERE current_percent < 100
      AND excluded = 0
      AND fan_in >= 5
    ORDER BY fan_in DESC, current_percent DESC
    LIMIT $LIMIT;
    "
else
    echo "*Call graph not populated. Run \`validate_call_graph.sh\` after extracting cross-references.*"
fi

echo ""
echo "## Reachable 100% (No Unfixable Patterns)"
echo ""

if [ "$HAS_PATTERNS" -eq 1 ]; then
    echo "Functions that can actually reach 100% match - no LINKER_MERGED, BOOL_MASK, etc."
    echo ""
    echo "| Symbol | Demangled | Size | Match % |"
    echo "|--------|-----------|------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || symbol,
        SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
        size,
        current_percent || '% |'
    FROM functions
    WHERE reachable_100 = 1
      AND current_percent >= 90
      AND current_percent < 100
      AND excluded = 0
    ORDER BY current_percent DESC, size ASC
    LIMIT $LIMIT;
    "
else
    echo "*Pattern columns not populated. Using verdict as proxy.*"
    echo ""
    echo "| Symbol | Demangled | Size | Match % | Verdict |"
    echo "|--------|-----------|------|---------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || symbol,
        SUBSTR(demangled, 1, 40) || CASE WHEN LENGTH(demangled) > 40 THEN '...' ELSE '' END,
        size,
        current_percent || '%',
        COALESCE(verdict, '-') || ' |'
    FROM functions
    WHERE current_percent >= 95
      AND current_percent < 100
      AND excluded = 0
      AND (verdict IS NULL OR verdict IN ('LIKELY_FIXABLE', 'MAYBE_FIXABLE'))
    ORDER BY current_percent DESC, size ASC
    LIMIT $LIMIT;
    "
fi

echo ""
echo "## Small Functions Near Complete"
echo ""
echo "Functions under 200 bytes at 95%+ match. Often quick fixes."
echo ""
echo "| Symbol | Demangled | Size | Match % |"
echo "|--------|-----------|------|---------|"
sqlite3 -separator '|' "$DB" "
SELECT
    '| ' || symbol,
    SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
    size,
    current_percent || '% |'
FROM functions
WHERE current_percent >= 95
  AND current_percent < 100
  AND size < 200
  AND excluded = 0
ORDER BY current_percent DESC, size ASC
LIMIT $LIMIT;
"

echo ""
echo "## Type Anchors (Constructors/Destructors)"
echo ""
echo "Constructors and destructors anchor class layout and vtables."
echo ""
echo "| Symbol | Demangled | Size | Match % |"
echo "|--------|-----------|------|---------|"
sqlite3 -separator '|' "$DB" "
SELECT
    '| ' || symbol,
    SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
    size,
    current_percent || '% |'
FROM functions
WHERE current_percent >= 90
  AND current_percent < 100
  AND excluded = 0
  AND (
    demangled LIKE '%::%::%(%'
    OR demangled LIKE '%::~%'
    OR symbol LIKE '%??0%'
    OR symbol LIKE '%??1%'
  )
ORDER BY current_percent DESC
LIMIT $LIMIT;
"

echo ""
echo "## High-Impact + High-Match Combo"
echo ""
echo "Functions with both high fan-in AND high match %. Best ROI targets."
echo ""

if [ "$HAS_CALL_GRAPH" -eq 1 ]; then
    echo "| Symbol | Demangled | Callers | Match % |"
    echo "|--------|-----------|---------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || f.symbol,
        SUBSTR(f.demangled, 1, 45) || CASE WHEN LENGTH(f.demangled) > 45 THEN '...' ELSE '' END,
        c.caller_count,
        f.current_percent || '% |'
    FROM functions f
    JOIN (
        SELECT callee_symbol, COUNT(*) as caller_count
        FROM call_edges
        GROUP BY callee_symbol
    ) c ON f.symbol = c.callee_symbol
    WHERE f.current_percent >= 90
      AND f.current_percent < 100
      AND f.excluded = 0
      AND c.caller_count >= 3
    ORDER BY (c.caller_count * f.current_percent) DESC
    LIMIT $LIMIT;
    "
elif [ "$HAS_FANIN" -eq 1 ]; then
    echo "| Symbol | Demangled | Fan-In | Match % |"
    echo "|--------|-----------|--------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || symbol,
        SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
        fan_in,
        current_percent || '% |'
    FROM functions
    WHERE current_percent >= 90
      AND current_percent < 100
      AND excluded = 0
      AND fan_in >= 3
    ORDER BY (fan_in * current_percent) DESC
    LIMIT $LIMIT;
    "
else
    echo "*Call graph not populated. Showing high-match large functions instead.*"
    echo ""
    echo "| Symbol | Demangled | Size | Match % |"
    echo "|--------|-----------|------|---------|"
    sqlite3 -separator '|' "$DB" "
    SELECT
        '| ' || symbol,
        SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
        size,
        current_percent || '% |'
    FROM functions
    WHERE current_percent >= 90
      AND current_percent < 100
      AND excluded = 0
      AND size >= 300
    ORDER BY (size * current_percent) DESC
    LIMIT $LIMIT;
    "
fi

echo ""
echo "## Fresh Targets (Never Attempted)"
echo ""
echo "High-match functions with zero previous attempts."
echo ""
echo "| Symbol | Demangled | Size | Match % |"
echo "|--------|-----------|------|---------|"
sqlite3 -separator '|' "$DB" "
SELECT
    '| ' || symbol,
    SUBSTR(demangled, 1, 45) || CASE WHEN LENGTH(demangled) > 45 THEN '...' ELSE '' END,
    size,
    current_percent || '% |'
FROM functions
WHERE current_percent >= 80
  AND current_percent < 100
  AND excluded = 0
  AND (attempt_count = 0 OR attempt_count IS NULL)
ORDER BY current_percent DESC, size DESC
LIMIT $LIMIT;
"

echo ""
echo "---"
echo ""
echo "Run with larger limit: \`$0 30\`"
