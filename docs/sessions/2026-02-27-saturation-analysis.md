# Decomp Saturation Analysis & Remaining Targets

**Date**: 2026-02-27

---

## Session Summary

Exhaustive search for improvable functions across the decomp. The project is at **92.6% fuzzy match** (29,927 complete / 32,328 non-excluded). This session focused on finding remaining gains after the approved Xenia boot plan was completed.

### Functions Improved (from prior session, uncommitted)

| Function | Before | After | Fix |
|----------|--------|-------|-----|
| `CharClipDisplay::SetText` | 82.4% | **99.0%** | Header: `char *mClipNameBuffer` → `char mClipNameBuffer[64]`, removed 15 fake unk fields |
| `Game::PostUpdate` | 92.9% | **94.9%** | Cast: `(const Skeleton*(&)[6])` → `*(const Skeleton*const(*)[6])` — loads pointer value instead of taking address |

### Functions Reported AT_LIMIT

| Function | Match% | Root Cause |
|----------|--------|------------|
| `MoveMgr::GetRoutinePreferredVariant` | 97.3% | `beqlr` vs `beq` — compiler conditional return decision |
| `TaskTimeline::ClearTasks` | 95.7% | cr0 vs cr6 — condition register allocation for `delete` null check |
| `ObjectDir::ResetViewports` | 97.9% | FPR regswap f12↔f30 + fmadds/fmsubs FMA selection |
| `HollaBackMinigame::SetMoveState` | 99.2% | Symbol relocation noise + GPR regswap |

### Micro-Improvement (auto-applied by permuter)

- `StubCameraInput::StubSkeletonData`: `while (i > 0)` → `while ((int)i >= 1)` (+0.02%)

---

## Key Findings

### 1. LINKER_MERGED Blocking (~80% of Remaining)

Of 735 remaining workable functions, approximately **80% are blocked by LINKER_MERGED (ICF) patterns**. These functions call into addresses where the linker merged multiple identical functions into one, making the call targets unreachable from our compiled objects. These are all effectively AT_LIMIT but haven't been bulk-reported yet.

**Next step**: Bulk-report LINKER_MERGED functions as AT_LIMIT to clean up the remaining count. This could be automated with a script that runs recon and auto-reports any function where the pattern is `LINKER_MERGED [BLOCKED]`.

### 2. Remaining Workable Distribution

```
95-100%:  210 functions (most are LINKER_MERGED blocked)
90-95%:   167 functions
80-90%:   175 functions
60-80%:   148 functions
below 60:  35 functions
```

### 3. Untried Functions at 95%+

There are **20 functions at 95%+ with 0 prior attempts**. Most will be LINKER_MERGED blocked, but worth triaging:

| Function | Match% | Unit |
|----------|--------|------|
| `HamSongMgr::GetCrewStarsForDifficulty` | 98.2% | lazer/meta_ham/HamSongMgr |
| `MultiUserGesturePanel::UpdateCrewPic` | 98.0% | lazer/meta_ham/MultiUserGesturePanel |
| `Game::PauseForSkeletonLoss` | 97.0% | lazer/game/Game |
| `DataArray::Execute` | 96.8% | system/obj/DataArray |
| ... and 16 more | | |

### 4. Batch Permuter Results

Ran the permuter across 40 functions (20 at 90-99%, 20 at 50-90%). **Zero meaningful improvements** found across 651 variants. The comparison equivalence / signed-unsigned patterns the permuter tests are exhausted.

---

## Struct & Header Discoveries

### CharClipDisplay Inline Buffer (Fixed)

**File**: `src/system/char/CharClipDisplay.h`

The `mClipNameBuffer` field was declared as `char *` (pointer) but the binary treats it as an inline `char[64]` array. This caused `strcpy` codegen differences: `lwz` (load pointer) vs `addi` (compute address). Confirmed via Ghidra showing `addi r10, r3, 0x24` at the member's offset.

The 15 fake `int unkXX` fields (unk28 through unk60) were actually part of the 64-byte inline buffer. After the fix, `SetText` jumped from 82.4% to 99.0%.

**Side effect**: `DrawBeatString(float, Hmx::Color const &)` regressed slightly from 99.6% to 98.3% (2 register swaps introduced by the header change). This is a known tradeoff — the struct is correct per Ghidra.

### Pointer-vs-Reference Cast Semantics (Fixed)

**File**: `src/lazer/game/Game.cpp`

`(const Skeleton*(&)[6])data->mSkeletonsRight` creates a reference bound to the member's memory location (generates `addi` — compute address). The target code loads the pointer value (generates `lwz`). Fixed with `*(const Skeleton*const(*)[6])(data->mSkeletonsRight)` which dereferences through a pointer-to-array.

### Potential Struct Opportunities (Not Yet Investigated)

No new struct layout issues were discovered this session, but the CharClipDisplay fix pattern suggests looking for other cases where:
- A `char *` member should be an inline `char[]` buffer
- `strcpy` codegen shows `lwz` (pointer load) vs `addi` (address compute)
- Fake `unk` fields immediately after a pointer might be buffer contents

A systematic Ghidra scan for `addi rX, rY, <small_offset>` patterns at member access sites could identify more of these.

---

## Build Process Observations

### Symbol Relocation Noise in diff_arg

Many functions show 10-30 `diff_arg` mismatches that are pure **symbol relocation differences** — the exact same instruction but with different symbol addresses. Examples:

```
Target: lis r29, lbl_82F616C4
Base:   lis r29, ?$S7@?4??SetMoveState@...
```

These are static local variables that get different symbol names in our build vs the original. The objdiff scoring penalizes these as partial mismatches, artificially lowering apparent match%. Functions like `SetMoveState` (99.2%) have 20+ diff_args that are all relocation noise.

**Potential improvement**: objdiff could be enhanced to ignore diff_arg mismatches where both sides reference the same-type symbol (static local, string literal, vtable) and only the address differs. This would give more accurate match% for near-complete functions.

### Float Constant Pool Ordering

In `ObjectDir::ResetViewports`, five `lis` instructions loading float constant addresses (`__real@00000000`, `__real@bf800000`, etc.) all show as diff_arg despite being identical constants. The addresses differ because our compiler places them in a different order in the `.rdata` section.

This is a systemic issue — every function that loads multiple float constants will have these diff_args. Not fixable from source; would require matching the exact constant pool layout of the original compiler.

### FMA Instruction Selection (fmadds vs fmsubs)

In `ResetViewports`, the compiler chooses `fmsubs` where the target uses `fmadds` (or vice versa). These are mathematically equivalent when one operand is zero (the multiply term), but the compiler's choice depends on FPR register allocation. Changing the source expression sign (positive vs negative with corresponding +/- change) does NOT change the codegen — the compiler makes the same FMA selection regardless. This is unfixable.

---

## Next Steps

### 1. Triage the 20 Untried 95%+ Functions

Run recon on each of the 20 untried functions at 95%+. Any that are reachable should be analyzed. `Game::PauseForSkeletonLoss` at 97.0% is the most promising (private void, correct mangling: `?PauseForSkeletonLoss@Game@@AAAXXZ`).

### 2. Flow System Code Fixes (Highest Leverage)

Fix the FlowPtr copy pattern across 5-7 Flow*::Copy methods, rewrite FlowSetProperty
Load/Execute, and add null guards to FlowCommand::Load. See `docs/decomp/LOW_HANGING_FRUIT.md`
for full details.

### 3. HamDirector Code Bugs

Fix 4 specific code bugs: ReactToCollision math, ClosestMove incomplete loop,
FindNextDircut branch polarity, UnloadMergers loop structure.

### 4. Batch Promotion (Phase 2 from NEXT_STEPS.md)

Use Unicorn behavioral testing to promote behaviorally-equivalent functions to COMPLETE
and report unfixable functions as AT_LIMIT. This will reduce the "remaining" count
significantly.

### 5. Permuter Pattern Expansion

The current permuter patterns (comparison equivalence, signed/unsigned, variable extraction, declaration reorder) are exhausted. New patterns to consider:
- FMA expression reordering (`a*b + c*d` association choices)
- Loop increment/decrement direction (`while (i > 0) { i-- }` vs `for (i = count-1; i >= 0; i--)`)
- Condition register steering (force cr0 usage by restructuring conditions)

---

## Post-Session Corrections (2026-02-27, follow-up investigation)

### LINKER_MERGED Estimate Was Wrong

The claim that "~80% of remaining workable functions are blocked by LINKER_MERGED" was
incorrect. A 25-function stratified sample (across all match% ranges) found **zero**
LINKER_MERGED patterns in workable functions. LINKER_MERGED functions had already been
moved to AT_LIMIT status prior to this session. The actual blockers are register swaps
(~80%), symbol relocation noise (~60%), control flow differences (~50%), and code logic
bugs (~25%).

### Struct Layout Sweep — All Investigated Classes Are Correct

The "Struct Layout Sweep" next step was completed. Deep investigation of every
suspected struct problem found all layouts correct:

- **FlowNode**: Correct. Issues are code logic in subclasses (wrong INIT_REVS, missing
  debug code, FlowPtr copy semantics).
- **CharClip / CharBonesSamples**: Correct. The -8 offset delta was stack frame size
  difference from `__FILE__` string length, not a struct error. Every field confirmed
  via m2c decompilation of target binary.
- **HamDirector**: Correct. The -4 delta was stack frame offsets. Real issues are code
  bugs (incomplete loops, wrong math, branch polarity).
- **FontMap::Page**: Correct. The offset swap was compiler instruction scheduling
  (different store order for independent assignments). Confirmed by CleanupSyncMeshes
  at 99.8% match using the same struct.
- **VorbisReader**: Correct. `mReadBuffer` and `mHdrBuf` are correctly pointers (accessed
  via `lwz` in target). Inline arrays `mNonce[16]` and `mKeyMask[16]` already correct.

No additional `char* → char[]` inline buffer conversions were found beyond the
CharClipDisplay fix already applied.

### Updated Priority

The remaining work is dominated by:
1. **Code logic fixes** in the Flow system (~30 functions, 55-98%)
2. **Specific code bugs** in HamDirector (4 functions with known fixes)
3. **Bulk AT_LIMIT triage** for ~400 functions with only unfixable patterns
4. **Unit-by-unit sweeps** for the remaining improvable functions
