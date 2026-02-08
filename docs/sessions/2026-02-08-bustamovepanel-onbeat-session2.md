# BustAMovePanel::OnBeat - Session 2: Systematic Improvement Campaign

**Date:** 2026-02-08
**Function:** `BustAMovePanel::OnBeat` (`?OnBeat@BustAMovePanel@@QAAXXZ`)
**Starting Match:** 95.1%
**Ending Match:** 95.3%
**Net Improvement:** +0.2%

---

## Executive Summary

Successfully improved BustAMovePanel::OnBeat from 95.1% to 95.3% through systematic experimentation following the deep-trace playbook. Key achievement was fixing a complex arithmetic mismatch in the winner determination logic by converting two separate `if` statements to an `if-else-if` chain.

---

## Session Context

This session continued the work from Session 1 (2026-02-07), which established the 95.1% baseline and identified key mismatch patterns:
- 1 CONTROL_FLOW mismatch (idx 2953: bne vs beq)
- 141 REGISTER_SWAP instructions
- 14 OFFSET_SWAP instructions
- 16 LINKER_MERGED calls (unfixable ceiling)

Session 1 tried 3 experiments focused on the idx 2953 branch inversion, all of which either regressed or stayed neutral.

---

## Experiments Conducted

### EXP-A-4: Extract Scoring Logic to Helper Function

**Target:** Fix idx 2953 branch inversion (bne vs beq) in duplicate scoring code
**Approach:** Extract lines 939-945 and 1024-1030 (identical scoring logic) to a new `HandlePerfectMoveScore()` helper function
**Hypothesis:** Forcing identical call sites would unify compiler optimization strategy

**Implementation:**
```cpp
// Added to BustAMovePanel.h
void HandlePerfectMoveScore(MoveRating);

// Added to BustAMovePanel.cpp
void BustAMovePanel::HandlePerfectMoveScore(MoveRating rating) {
    if (rating == kMoveRatingSuperPerfect
        || (((bool *)&mFlawlessFlags)[!mActivePlayer] = false,
            rating == kMoveRatingPerfect)) {
        mMatchCount++;
        int score = (rating == kMoveRatingSuperPerfect) ? 50000 : 40000;
        IncreaseScore(!mActivePlayer, score);
        static Message matchedMessage("bustamove_move_matched", 0);
        matchedMessage[0] = DataNode(mMatchCount);
        TheHamProvider->Handle(matchedMessage, false);
    }
}

// Replaced both call sites with:
HandlePerfectMoveScore(rating);
```

**Result:**
- Match: **90.2% (-4.9% MAJOR REGRESSION)**
- equal: 2032 (down from 2163, **-131 instructions**)
- delete: 193 (up from 44, **+149 instructions**)
- Severe code reordering throughout function

**Decision:** **REVERTED**

**Analysis:**
Helper function extraction caused massive non-local effects. The compiler made completely different inlining decisions, resulting in extensive code reordering. This demonstrates that even semantically clean refactoring can severely damage match percentage when the compiler's optimization choices diverge.

**Lesson Learned:**
Don't extract helper functions in decomp work unless there's strong evidence the original code used them. Even structurally "cleaner" code can make matching worse.

---

### EXP-B-1: Rewrite Winner Logic with if-else-if Chain ✅

**Target:** Fix complex arithmetic mismatch at idx 2123-2133 in winner determination logic
**Approach:** Change two separate `if` statements to `if-else-if` chain

**Original Code (lines 1152-1158):**
```cpp
int winner = -1; // -1 = tie
if (score0 > score1) {
    winner = 0;
}
if (score1 > score0) {
    winner = 1;
}
```

**Modified Code:**
```cpp
int winner = -1; // -1 = tie
if (score0 > score1) {
    winner = 0;
} else if (score1 > score0) {
    winner = 1;
}
```

**Assembly Impact:**

**BEFORE (idx 2123-2133):**
```asm
TARGET:
2126: cmpw cr6, r27, r3    # Simple comparison
2127-2130: mr/ble/mr/b     # Conditional moves and branches
2131: cmpw cr6, r3, r27    # Another comparison

SOURCE:
2123: xoris r10, r28, 0x8000   # XOR with high bit
2124: subf r9, r28, r3         # Subtract
2126: addc r10, r9, r10        # Add with carry
2131: cmpw cr6, r3, r28        # Comparison
2132: subfe r10, r10, r10      # Subtract with borrow
2133: and r29, r10, r11        # AND operation
```

**AFTER:**
Replaced the 8-instruction arithmetic sequence with a simpler branch direction mismatch (`ble` vs `bge`)

**Result:**
- Match: **95.3% (+0.2% IMPROVEMENT)**
- equal: 2165 (up from 2163, **+2 instructions**)
- insert: 39 (down from 43, **-4 instructions**)
- delete: 42 (down from 44, **-2 instructions**)
- diff_op: 2 (up from 1, **+1 control flow mismatch**)

**Decision:** **KEPT**

**Analysis:**
The improvement came from converting a complex arithmetic computation pattern (used by the compiler to avoid branches in the original two-if version) into a simpler control flow pattern. While we added a new `diff_op` (ble vs bge at idx 2126), we eliminated:
- 4 insert operations
- 2 delete operations
- 1 complex replace operation
- Net gain of 2 equal instructions

This is a favorable trade: we exchanged a hard-to-fix 8-instruction arithmetic mismatch for a simpler 1-instruction branch direction mismatch.

**Why This Worked:**
The two separate `if` statements gave the compiler freedom to use arithmetic tricks to avoid branch misprediction penalties. The `if-else-if` chain forces a more predictable control flow structure that's closer to the target binary's approach.

---

### EXP-B-2: Swap Comparison Order

**Target:** Fix new ble vs bge branch mismatch at idx 2126 introduced by EXP-B-1
**Approach:** Change `score0 > score1` to `score1 < score0`

**Implementation:**
```cpp
// Tried:
if (score1 < score0) {
    winner = 0;
} else if (score1 > score0) {
    winner = 1;
}
```

**Result:**
- Match: **95.3% (neutral)**
- Identical statistics to EXP-B-1
- Branch direction unchanged

**Decision:** **REVERTED** (kept EXP-B-1 version)

**Analysis:**
Comparison operator swap did not affect the compiler's branch instruction choice. The `ble` vs `bge` mismatch persists regardless of whether we write `score0 > score1` or `score1 < score0`. This suggests the branch direction is determined by other context, possibly:
- Register allocation decisions earlier in the function
- Branch prediction hints
- Code layout and alignment considerations

---

## Deep-Trace Analysis: idx 2953 Branch Inversion

The original target (idx 2953, now idx 2949 after B-1 changes) remains unfixed. Deep analysis revealed:

**Pattern:** Identical source code at two locations compiles differently
- **Location 1:** Lines 939-945 (kBAMState_Playing case)
- **Location 2:** Lines 1024-1030 (kBAMState_RecordCountIn case)

**Source Pattern:**
```cpp
if (rating == kMoveRatingSuperPerfect
    || (((bool *)&mFlawlessFlags)[!mActivePlayer] = false,
        rating == kMoveRatingPerfect)) {
    int score = (rating == kMoveRatingSuperPerfect) ? 50000 : 40000;
    IncreaseScore(!mActivePlayer, score);
    // ...
}
```

**Assembly Context (idx 2949 in current code):**
```asm
2952: cmpwi cr6, r29, 0x1         # Test rating == kMoveRatingPerfect

TARGET:
2953: bne cr6, 0x8088             # Skip block if rating != Perfect

SOURCE:
2953: beq cr6, 0x2d98             # Enter block if rating == Perfect
2954: b 0x2e30                    # Skip to end
```

**Root Cause:**
Context-dependent compiler optimization. The compiler chooses different control flow strategies based on:
1. Earlier register allocation state
2. Branch layout heuristics (predicting hot paths)
3. Code scheduling opportunities from the comma operator side effect

**Why Previous Attempts Failed:**
- Session 1 EXP-A-1: Removed redundant `== 0` check → regression
- Session 1 EXP-A-2: Inverted if/else branches → major regression
- Session 2 EXP-A-4: Helper function extraction → major regression

All attempts to directly manipulate the control flow at this location caused worse regressions elsewhere. This suggests the branch inversion is a **consequence of earlier decisions**, not something fixable locally.

**Hypothesis for Future Work:**
The idx 2949 branch inversion may be fixable only by:
1. Changing code context much earlier in the function
2. Affecting register allocation decisions that cascade to this point
3. Structural changes to function layout (unlikely to succeed based on EXP-A-4 results)

---

## Current State Analysis

### Match Statistics

**Overall:** 95.3% (12052/12056 bytes, 3053 instructions)

**Instruction Breakdown:**
| Category    | Count | Percentage | Change from Baseline |
|-------------|-------|------------|---------------------|
| equal       | 2165  | 70.9%      | +2 ✅               |
| diff_arg    | 751   | 24.6%      | 0                   |
| replace     | 54    | 1.8%       | -1 ✅               |
| insert      | 39    | 1.3%       | -4 ✅               |
| delete      | 42    | 1.4%       | -2 ✅               |
| diff_op     | 2     | 0.1%       | +1 ⚠️              |

### Remaining Patterns

**CONTROL_FLOW (2 mismatches):**
- idx 2126: `ble` vs `bge` - NEW from EXP-B-1, testing `score0 > score1`
- idx 2949: `bne` vs `beq` - ORIGINAL, testing `rating == kMoveRatingPerfect`

**LINKER_MERGED (16 calls - UNFIXABLE):**
- Identical COMDAT Folding (ICF) merged functions
- This is the practical ceiling for this function

**REGISTER_SWAP (141 instructions):**
- Dominant: r10↔r11 (24 instances, span 1503-2673)
- Also: r5↔r6 (16), f30↔f31 (15), f29↔f31 (14), r3↔r5 (14)
- Typical fix rate: ~30% with careful variable reordering

**OFFSET_SWAP (7 swaps):**
- Dominant: +96 byte stack delta (98 instructions, 29.9% of offset diffs)
- Also: -8 bytes (26 instructions), +48 bytes (21 instructions)
- Fixable via variable declaration reordering

### Diagnostic Insights

**Stack Layout Delta:**
The dominant +96 byte offset difference indicates the TARGET binary has a larger stack frame than SOURCE. This pervasive pattern affects many instructions and suggests:
- Different register save/restore decisions
- Different local variable layout
- Possible alignment padding differences

**Register Allocation Cascades:**
The r10↔r11 swap spanning 1170 instructions (idx 1503-2673) indicates a long-range register allocation decision made early in the function affects a large section of code.

**Real Structural Mismatches:**
Notable replace instructions indicate genuine code generation differences:
- idx 560: `srawi` vs `clrrwi` - different bit manipulation approaches
- idx 3015-3016: `stfs` vs `stw` - float store vs word store (type difference)

---

## Lessons Learned

### What Worked

1. **if-else-if Chain over Separate ifs**
   Converting two independent `if` statements to `if-else-if` can force the compiler away from arithmetic optimization tricks and toward simpler control flow.

2. **Trading Complex for Simple Mismatches**
   Replacing an 8-instruction arithmetic sequence with a 1-instruction branch direction mismatch is a net win, even if both are "wrong".

3. **Systematic Experimentation**
   Following the deep-trace playbook and testing one change at a time allowed us to identify the successful approach.

### What Didn't Work

1. **Helper Function Extraction**
   Extracting duplicate code to a helper function caused massive code reordering (-131 equal instructions). Don't assume "cleaner" code matches better.

2. **Direct Control Flow Manipulation**
   Attempts to fix the idx 2953 branch directly (Session 1 + Session 2) all failed. The branch inversion is context-dependent, not locally fixable.

3. **Comparison Operator Swaps**
   Changing `a > b` to `b < a` didn't affect branch instruction selection. The compiler's choices are based on deeper context.

### Key Insights

1. **Context-Dependent Optimization is Real**
   Identical source code at different locations can compile differently based on surrounding context (register pressure, branch prediction hints, code layout).

2. **Non-Local Effects Dominate**
   Small local changes can cause register allocation cascades affecting hundreds of instructions. This makes decomp work fundamentally different from normal refactoring.

3. **Measure Everything**
   Only by capturing detailed JSON diffs and comparing to baseline can you distinguish real progress from noise.

---

## Next Steps

### Immediate Priority: OFFSET_SWAP Patterns

The dominant +96 byte stack offset delta (98 instructions) is a high-value target:

**Approach:**
1. Use `--offsets` analysis to map offset deltas to source variables
2. Identify local variables responsible for stack layout differences
3. Reorder variable declarations to match target layout
4. Focus first on smaller deltas (-8 bytes, +48 bytes) for easier wins

**Expected Impact:**
Fixing 10-20% of offset swaps could yield +0.3-0.5% improvement.

### Alternative: Control Flow Deep Dive

If offset work proves difficult, try advanced control flow techniques:

**EXP-A-6:** Scoped blocks for temporary isolation
```cpp
{
    MoveRating rating = GetMoveRating(mMoveScore);
    ShowMoveRating(rating, mCreatorSide);
    // Score check...
}
```
Hypothesis: Limiting temporary lifetimes may change register allocation context.

**EXP-A-7:** Explicit variable materialization
```cpp
bool isSuper = (rating == kMoveRatingSuperPerfect);
bool isPerfect = (rating == kMoveRatingPerfect);
if (isSuper || (setFlawlessFalse(), isPerfect)) { ... }
```
Hypothesis: Named temporaries may stabilize register choices.

### Long-Term Goal

**Target:** 96-97% match
**Method:** Systematic reduction of OFFSET_SWAP and REGISTER_SWAP patterns
**Ceiling:** ~96.5% likely practical limit given 16 unfixable LINKER_MERGED calls

---

## Statistics

### Session Summary

| Metric | Session 1 | Session 2 | Overall |
|--------|-----------|-----------|---------|
| Experiments | 3 | 3 | 6 |
| Regressions | 2 | 1 | 3 |
| Improvements | 0 | 1 | 1 |
| Neutral | 1 | 1 | 2 |
| Net Match Change | +0.0% | +0.2% | +0.2% |

### Trend Analysis

**Positive Indicators:**
- Found alternative improvement path (winner logic) after idx 2953 proved intractable
- Reduced insert/delete noise (-6 total non-equal instructions)
- Gained 2 equal instructions
- Systematic approach is working

**Challenges:**
- idx 2949 branch remains stubborn after 4 different approaches
- Added 1 new diff_op (though simpler than what it replaced)
- 16 LINKER_MERGED calls create a practical ceiling

**Outlook:**
Continued systematic work should reach 96-97% match. The function is not yet at its limit - offset swaps and some register swaps are mechanically fixable.

---

## Appendix: Experiment Ledger

Complete experiment log available in `/tmp/claude/bustamove_experiment_ledger.txt`

**Session 2 Experiments:**

```
EXP-A-4 | Extract to HandlePerfectMoveScore helper
Result: match=90.2% (down -4.9%), equal=2032 (-131), delete=193 (+149)
Decision: REVERT - Major regression
Reason: Helper function caused extensive code reordering

EXP-B-1 | Lines 1152-1158 | Change: Rewrite winner logic with if-else-if chain
Result: match=95.3% (up +0.2%), equal=2165 (+2), insert=39 (-4), delete=42 (-2), diff_op=2 (+1)
Decision: KEEP - Improvement!
Reason: Gained 0.2% match, reduced insert/delete noise. Traded complex arithmetic mismatch
        (xoris/subf/addc/subfe) for simpler branch direction (ble vs bge). New diff_op at
        idx 2126 is more fixable than original arithmetic sequence.

EXP-B-2 | Lines 1152-1158 | Change: Try comparison order swap (score1 < score0)
Result: match=95.3% (neutral), same stats as B-1
Decision: REVERT - No improvement, keep B-1 version
Reason: Comparison swap didn't affect branch direction. The ble vs bge mismatch persists.
```

---

## Conclusion

Session 2 successfully improved BustAMovePanel::OnBeat from 95.1% to 95.3% (+0.2%) by applying systematic deep-trace analysis. The key breakthrough was recognizing that the original target (idx 2953 branch) was context-dependent and finding an alternative improvement path through the winner determination logic.

**Key Achievement:**
Replaced a complex 8-instruction arithmetic mismatch with a simpler 1-instruction branch direction mismatch, demonstrating that strategic trade-offs can advance match percentage even when direct fixes fail.

**Recommendation:**
Continue the campaign. The systematic approach is working, and offset swap patterns offer a clear mechanistic path to further improvement. Expect 10-30 total experiments to reach the practical ceiling around 96-97% match.
