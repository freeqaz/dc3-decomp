#!/bin/bash
# Test script for orchestrator - run this outside of Claude session

set -e

echo "=== Testing Orchestrator ==="

# 1. Check database
echo "1. Checking database..."
sqlite3 decomp.db "SELECT COUNT(*) as total FROM functions;"

# 2. Clear any stale locks
echo "2. Clearing stale locks..."
sqlite3 decomp.db "UPDATE functions SET locked_by = NULL, locked_at = NULL WHERE locked_by IS NOT NULL;"

# 3. Query a function
echo "3. Querying target function..."
./bin/orchestrate query --pattern "src/system/char/*" --min-percent 99 --max-percent 99.5 --limit 3

# 4. Dry run
echo "4. Testing dry run..."
./bin/orchestrate single "?Load@CharFaceServo@@UAAXAAVBinStream@@@Z" --dry-run

# 5. Actual run (comment out if you want to just test setup)
echo "5. Running actual agent..."
./bin/orchestrate single "?Load@CharFaceServo@@UAAXAAVBinStream@@@Z"

echo "=== Test Complete ==="
