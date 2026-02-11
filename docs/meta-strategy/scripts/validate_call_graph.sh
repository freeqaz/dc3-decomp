#!/bin/bash
# validate_call_graph.sh - Check if call graph infrastructure is worth building
#
# Usage:
#   ./docs/meta-strategy/scripts/validate_call_graph.sh
#
# Decision criteria:
#   >= 10 functions with 20+ callers -> Build full call graph
#   < 10 functions with 20+ callers  -> Skip, use simpler scoring

set -e
cd "$(git rev-parse --show-toplevel)"

DB="decomp.db"

if [ ! -f "$DB" ]; then
    echo "Error: $DB not found"
    echo "Run from project root or ensure database exists"
    exit 1
fi

echo "=== Call Graph Validation ==="
echo ""

# Check if call_edges table exists
TABLE_EXISTS=$(sqlite3 "$DB" "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='call_edges';")

if [ "$TABLE_EXISTS" -eq 0 ]; then
    echo "call_edges table does not exist."
    echo ""
    echo "To populate it, run the call graph extraction from PHASE2_INFRASTRUCTURE.md"
    echo "or use Ghidra MCP to extract cross-references."
    exit 0
fi

# Check edge count
EDGE_COUNT=$(sqlite3 "$DB" "SELECT COUNT(*) FROM call_edges;")
echo "Total call edges: $EDGE_COUNT"

if [ "$EDGE_COUNT" -eq 0 ]; then
    echo "call_edges table is empty. Run extraction first."
    exit 0
fi

echo ""
echo "=== High Fan-In Functions (20+ callers) ==="
sqlite3 -header -column "$DB" "
SELECT
    callee_symbol as symbol,
    COUNT(*) as callers
FROM call_edges
GROUP BY callee_symbol
HAVING callers >= 20
ORDER BY callers DESC
LIMIT 20;
"

# Count for decision
HIGH_FANIN=$(sqlite3 "$DB" "
SELECT COUNT(*) FROM (
    SELECT callee_symbol, COUNT(*) as cnt
    FROM call_edges
    GROUP BY callee_symbol
    HAVING cnt >= 20
);
")

echo ""
echo "=== Caller Count Distribution ==="
sqlite3 -header -column "$DB" "
SELECT
    CASE
        WHEN cnt >= 50 THEN '50+'
        WHEN cnt >= 20 THEN '20-49'
        WHEN cnt >= 10 THEN '10-19'
        WHEN cnt >= 5 THEN '5-9'
        WHEN cnt >= 1 THEN '1-4'
        ELSE '0'
    END as caller_range,
    COUNT(*) as functions
FROM (
    SELECT callee_symbol, COUNT(*) as cnt
    FROM call_edges
    GROUP BY callee_symbol
)
GROUP BY caller_range
ORDER BY
    CASE caller_range
        WHEN '50+' THEN 1
        WHEN '20-49' THEN 2
        WHEN '10-19' THEN 3
        WHEN '5-9' THEN 4
        WHEN '1-4' THEN 5
        ELSE 6
    END;
"

echo ""
echo "=== Decision ==="
echo "Functions with 20+ callers: $HIGH_FANIN"
echo ""
if [ "$HIGH_FANIN" -ge 10 ]; then
    echo "RECOMMENDATION: Build full call graph infrastructure"
    echo "Reason: $HIGH_FANIN functions have high fan-in, worth prioritizing"
else
    echo "RECOMMENDATION: Skip call graph infrastructure"
    echo "Reason: Only $HIGH_FANIN functions have 20+ callers"
    echo "Use simpler scoring based on size/match% instead"
fi
