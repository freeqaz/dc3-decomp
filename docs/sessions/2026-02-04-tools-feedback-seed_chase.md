# Tools Feedback - seed_chase (2026-02-04)

## Context
- Function: `seed_chase` in `src/system/oggvorbis/psy.c`
- Initial match: 89.8% (verdict: LIKELY_FIXABLE)
- Result: 100% after switching `alloca` to `_alloca`

## What Helped
- `analyze-function` flagged **LIKELY_FIXABLE** and highlighted **control-flow diffs**, which correctly narrowed the search.
- `objdiff` instruction diff made the prologue mismatch obvious (`_RtlCheckStack12`/intrinsic path vs `alloca`), pointing to `_alloca` as the fix.
- Pattern summary (REGISTER_SWAP) was accurate enough to avoid unnecessary detours.

## Pain Points / Confusion
- **m2c decomp output** contained many unset-register artifacts; hard to trust for tight loops.
- **REGISTER_SWAP** detection didn’t suggest concrete source-level edits or which locals to reorder.
- **alloca vs _alloca** nuance was not surfaced; the tool didn’t hint that `_RtlCheckStack12` implies the intrinsic stack-alloc path.

## Improvement Ideas
- Add a “low-noise m2c” mode that suppresses obvious artifacts and highlights stable structure.
- For REGISTER_SWAP, suggest variable reordering heuristics based on usage groups.
- Add a hint when prologue shows `_RtlCheckStack12` / stack probe: “try `_alloca` (intrinsic) instead of `alloca` wrapper.”

## Notes
- The fix was a single-line change but required spotting the stack allocation calling convention difference.
- `objdiff` was decisive; `m2c` was not.
