Based on the analysis, here are the commands tailored to the strategy we identified. From most
   targeted to broadest:

  1. Near-complete functions (95-99.9%) across our best subsystems

  # The sweet spot: functions almost at 100% in subsystems we're best at
  ./bin/orchestrate batch 'default/system/rndobj/*' 'default/system/char/*'
  'default/system/obj/*' 'default/system/ui/*' \
    --min-percent 95 --max-percent 99.99 --limit 50 -j 3

  # Same thing but with incremental builds for speed
  ./bin/orchestrate batch 'default/system/rndobj/*' 'default/system/char/*'
  'default/system/obj/*' 'default/system/ui/*' \
    --min-percent 95 --max-percent 99.99 --limit 50 -j 3 --incremental-only

  2. Use the priority scoring system (if scores are populated)

  # Let the scoring infrastructure pick the best targets
  ./bin/orchestrate batch --strategy priority --reachable-only --limit 30 -j 3

  # Preview what it would pick first
  ./bin/orchestrate targets --reachable-only --limit 30

  3. Unit-completion strategy (bring entire units to 100%)

  # Focus on nearly-done compilation units
  ./bin/orchestrate batch --strategy unit-completion --reachable-only --limit 30 -j 3

  # Preview which units are close
  ./bin/orchestrate targets --strategy unit-completion --reachable-only

  4. The 88-95% band in our strong subsystems

  # Slightly harder but still in our wheelhouse
  ./bin/orchestrate batch 'default/system/rndobj/*' 'default/system/char/*' \
    --min-percent 88 --max-percent 94.99 --limit 30 -j 3

  5. Quick recon before committing

  # Check status and what's available
  ./bin/orchestrate status

  # Query to see what we'd be working on
  ./bin/orchestrate query --pattern 'default/system/rndobj/*' --min-percent 95 --max-percent
  99.99 --limit 30

  # Dry-run cost estimate
  ./bin/orchestrate query --pattern 'default/system/char/*' --min-percent 95 --max-percent 99.99
   --estimate-cost

  6. Cleanup if needed before starting

  # Clear any stale locks from previous runs
  ./bin/orchestrate release-locks

  # Re-initialize the worktree pool
  ./bin/orchestrate init --pool-size 3 --force

  My recommendation for the first batch run: start with command #1 (the 95-99.99% band across
  all four strong subsystems). These are the functions where we have the highest probability of
  pushing to 100% per attempt, and they span the patterns (Load, Save, SyncProperty, Copy) that
  our workflow handles best.

