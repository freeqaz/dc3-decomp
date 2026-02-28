# Stub Burndown & Data Stub Fix

**Date:** 2026-02-28
**Focus:** Reducing ALTERNATENAME stubs via data stub regeneration, DB cleanup

## Summary

Reduced `link_glue.cpp` from **1,451 ALTERNATENAME stubs to 397** by fixing a gap in data stub coverage. Also reset 1,036 incorrectly-marked DB entries and wrote a plan for eliminating `/FORCE:MULTIPLE`.

## Key Discovery: Data Stub Gap

262 Matching units were missing data stubs because `create_data_stubs.py` hadn't been re-run since they were promoted to Matching. The data stubs provide COMDAT code sections from the original binary — without them, those symbols had to be stubbed via ALTERNATENAME.

**Fix:**
1. Re-ran `create_data_stubs.py` → 707 → 968 data stubs
2. Re-ran `scripts/build/configure.sh` → build.ninja now includes the 261 new data stubs
3. Iteratively removed stubs, testing link each time to find which were still needed

**Result:** 1,451 → 397 stubs (removed 1,054)

### Stub Composition (397 remaining)

| Category | Count | Notes |
|----------|-------|-------|
| `??__E` dynamic initializers | ~236 | Auto-resolve when parent TU's statics are defined |
| Game/engine functions | ~140 | Data stub COMDATs that reference transitive dependencies |
| SDK transitive deps | ~21 | Bink, D3D, FFT, wmemcpy — needed by data stub COMDATs |

## LNK4006 Increase

More data stubs = more cross-unit COMDAT duplicates. LNK4006 rose from 756 → **13,400**. These are cosmetic (the linker correctly picks one definition), but `/FORCE:MULTIPLE` is needed to suppress them.

## DB Cleanup: COMPLETE+is_stub Reset

Found 1,036 functions marked `COMPLETE` with `is_stub=1`. These had been auto-reported by `batch_check` which compared original objects against themselves (100% match trivially). They have **no source implementations**.

**Fix:** Reset all 1,036 to workable (`verdict=NULL`), tagged with `verdict_reason='reset: was COMPLETE+is_stub (no source impl)'`.

### How to work on these stubs

1. Decompile the function (Ghidra) and check RB3 for reference implementations
2. Write the C++ implementation in the correct source file
3. Match it with objdiff
4. Remove the ALTERNATENAME line from `link_glue.cpp`
5. Verify link succeeds

Query: `SELECT symbol, demangled, unit, size FROM functions WHERE is_stub=1 AND verdict IS NULL ORDER BY size ASC`

Top units by workable stub count: Shader (37), Utl (34), PlatformMgr_Xbox (29), HamNavList (25), Voice (22), BinkMovieImpl (20).

## Documentation Written

- **`docs/plans/FORCE_MULTIPLE_ELIMINATION.md`** — Plan to eliminate `/FORCE:MULTIPLE`: link architecture explanation, 13,400 LNK4006 breakdown, 5 strategies (smart data stubs, COMDAT aux records, eliminate data stubs, `/IGNORE:4006`, hybrid)
- Updated `CLEAN_LINK_PROJECT.md`, `BUILD_ROADMAP.md`, `NEXT_STEPS.md`, `INDEX.md` with current numbers and links

## Files Modified

| File | Change |
|------|--------|
| `src/link_glue.cpp` | 1,451 → 397 ALTERNATENAME stubs |
| `decomp.db` | 1,036 COMPLETE+stub functions reset to workable |
| `docs/plans/FORCE_MULTIPLE_ELIMINATION.md` | New — `/FORCE:MULTIPLE` elimination plan |
| `docs/plans/CLEAN_LINK_PROJECT.md` | Updated LNK4006 count, stub count, history |
| `docs/plans/BUILD_ROADMAP.md` | Updated warning count, priorities |
| `docs/plans/NEXT_STEPS.md` | Updated LNK4006 reference |
| `docs/INDEX.md` | Added link to new plan doc |
