# RhythmBattle Regression Cleanup Session

**Date**: 2026-02-05
**Functions**: `RhythmBattle::OnReset`, `RhythmBattle::UpdateMindControl`

## Context

Post-merge cleanup from main branch. These functions had incomplete implementations that needed RE work.

## Results

| Function | Before | After | Status |
|----------|--------|-------|--------|
| RhythmBattle::OnReset | 36.1% | 55.7% | Stuck |
| RhythmBattle::UpdateMindControl | 35% | 35% | Stuck |

## OnReset Analysis

### Implementation Restored

Restored full if/else structure for `mFinale` from HEAD~1:

**Finale branch (`mFinale == true`)**:
1. Erase first element of `unk150` vector
2. Queue `finale_intro_01` and `finale_intro_02` VOs
3. Hide `score_right` and `score_left` in hud_panel
4. Animate `set_bid.anim` to frame 0

**Non-finale branch**:
1. Find `intro_line1.lbl` in `mPlayerOne->mTextFeedback`
2. Set `unk1c` if found
3. Send `rhythm_battle_title` message to mPlayerOne's feedback
4. Send empty message to mPlayerTwo's feedback

### Blockers

1. **ICF Merged Call**: `merged_SetVirtualObjConcrete` - linker merged identical functions, unfixable
2. **ObjPtr Access Patterns**: Target accesses `mPlayerOne->mTextFeedback` through a different dereferencing pattern than direct field access
3. **~64 Missing Instructions**: Related to Message construction and ObjPtr navigation

### Code Structure (Current)

```cpp
if (mFinale) {
    // finale logic - erase unk150, queue VOs, hide scores, animate
} else {
    // non-finale - find label, set unk1c, HandleType messages
}
```

## UpdateMindControl Analysis

### Missing Implementation (~132 instructions)

Target has significant additional logic not present in current code:

1. **String Comparison**: `CAMP_MINDCONTROL` comparison against some property
2. **ForceShot Call**: `TheHamDirector->ForceShot(gNullStr)`
3. **SetProperty**: `CAMP_MINDCONTROL_DANCE` symbol with property setting
4. **Threshold Checks**:
   - `unk110 > 5.0f` for grooving VO
   - `unk110 > 12.0f` (or similar) for not_grooving VO
5. **unk10c Update**: Uses `DeltaBeat` multiplication pattern

### Current Implementation

Only has basic:
- Mind control mode detection
- Character animation loop (2 iterations)
- Simple grooving/not_grooving checks based on `unk10c`

## Next Steps

1. **Ghidra Decompilation**: Both functions need Ghidra analysis to understand:
   - Exact string comparison logic in UpdateMindControl
   - Property setting flow
   - ObjPtr access patterns in OnReset

2. **HamDirector Interface**: Check if `ForceShot` method exists/is declared

3. **Threshold Values**: Verify exact float constants for grooving checks (5.0f, 12.0f vs current values)

## Files Modified

- `src/system/hamobj/RhythmBattle.cpp` - OnReset implementation restored
