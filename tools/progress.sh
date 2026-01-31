#!/bin/bash
# Quick progress report for DC3 decomp

cd "$(dirname "$0")/.." || exit 1

# Generate report silently
ninja build/373307D9/report.json >/dev/null 2>&1

# Extract and display metrics
jq -r '
def pct: . * 100 | round / 100;
def kb: tonumber / 1024 | round;

.categories as $cats |
($cats | map(select(.id == "game" or .id == "engine"))) as $core |
($core | map(.measures.total_code | tonumber) | add) as $core_total |
($core | map(.measures.matched_code | tonumber) | add) as $core_matched |
($core | map(.measures.total_functions) | add) as $core_funcs_total |
($core | map(.measures.matched_functions) | add) as $core_funcs_matched |
(($cats[0].measures.total_code | tonumber) * $cats[0].measures.fuzzy_match_percent +
 ($cats[1].measures.total_code | tonumber) * $cats[1].measures.fuzzy_match_percent) / $core_total as $core_fuzzy |

"DC3 Decomp Progress
═══════════════════

Game + Engine (core):
  Matched Code:      \(($core_matched / $core_total * 100) | pct)% (\($core_matched | . / 1024 | round)KB / \($core_total | . / 1024 | round)KB)
  Fuzzy Match:       \($core_fuzzy | pct)%
  Matched Functions: \(($core_funcs_matched / $core_funcs_total * 100) | pct)% (\($core_funcs_matched) / \($core_funcs_total))

Overall (includes SDK/libs):
  Matched Code:      \(.measures.matched_code_percent | pct)%
  Fuzzy Match:       \(.measures.fuzzy_match_percent | pct)%
"' build/373307D9/report.json
