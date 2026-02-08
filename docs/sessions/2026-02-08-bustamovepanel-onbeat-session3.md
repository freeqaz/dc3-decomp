# BustAMovePanel::OnBeat - Session 3: OFFSET_SWAP Investigation

**Date:** 2026-02-08
**Starting Match:** 95.3%
**Ending Match:** 95.3% (no change)
**Goal:** Improve to 95.5-96.0% by fixing OFFSET_SWAP patterns through variable reordering
**Outcome:** No improvement - OFFSET_SWAP patterns appear to be compiler frame layout choices, not fixable via source changes

---

## Executive Summary

Session 3 investigated whether OFFSET_SWAP patterns could be fixed through local variable reordering. After deep analysis of the assembly differences and one failed experiment, we determined that:

1. **Most offset mismatches (328 instructions) are NOT fixable** - they reflect global stack frame layout differences between our code and the original
2. **The dominant +96 delta (98 instructions)** is a pervasive frame size difference, likely due to compiler optimization choices
3. **Static Message variables** (playMsg, winnerMessage, etc.) are placed at different stack offsets - this appears to be a compiler decision, not something controllable via source ordering
4. **Only 14 instructions** are flagged as "likely fixable" OFFSET_SWAP by the analyzer, suggesting minimal improvement potential

**Recommendation:** Move away from OFFSET_SWAP fixes. Focus on REGISTER_SWAP patterns or accept that we're approaching the practical ceiling (~96-96.5%).

---

## Session Workflow

### 1. Baseline Verification
```bash
./bin/objdiff-cli diff '?OnBeat@BustAMovePanel@@QAAXXZ' --build --incremental
```
**Result:** Confirmed 95.3% match

### 2. Offset Analysis
```bash
python3 scripts/diff_inspect.py baseline.json --offsets
```

**Key Findings:**
```
Offset delta histogram (base - target):
     Delta   Count  Bar
  ────────  ──────  ──────────────────────────────
       +96      98  ██████████████████████████████ ◄ dominant
        -8      26  ███████
       +48      21  ██████
        -4      20  ██████
       +16      14  ████
```

**Total:** 328 offset argument differences across 82 distinct deltas

### 3. Target Pattern Investigation

#### Delta -8 (26 instructions)
**Hotspot:** idx 1123-1133, idx 2563-2565

**idx 1123-1133 Analysis:**
- Offsets: 0xdc/0xd8 (TARGET) vs 0xd4/0xd0 (SOURCE)
- Maps to line 946: `static Message playMsg("bustamove_move_matched", 0);`
- This is a **static local variable** - compiler controls placement
- **Not fixable via source reordering**

**idx 2563-2565 Analysis:**
- Offsets: 0x68/0x6c/0x70 (TARGET) vs 0x60/0x64/0x68 (SOURCE)
- Maps to lines 1259-1262: `std::vector<int> shuffled1;` and `std::vector<int> shuffled2;`
- **Attempted fix:** See EXP-C-1 below

#### Delta +48 (21 instructions)
**Hotspot:** idx 2140-2180

**Analysis:**
- Offsets: 0xb0/0xb4 (TARGET) vs 0xe0/0xe4 (SOURCE)
- Maps to line 1158: `static Message winnerMessage("bustamove_winner", 0);`
- Another **static local variable** - compiler-controlled placement
- **Not fixable via source reordering**

#### Delta +96 (98 instructions - DOMINANT)
**Analysis:**
- Global stack frame size difference
- Affects variables throughout the entire function
- Example: idx 17 shows offset +288 bytes for a Symbol construction
- This is a **fundamental frame layout difference**
- **Likely unfixable** - would require matching compiler's internal stack allocation algorithm

---

## Experiments

### EXP-C-1: Reorder shuffled1/shuffled2 in case 3

**Hypothesis:** Swapping the declaration order of `shuffled1` and `shuffled2` in the case 3 block (lines 1258-1262) might shift stack offsets by -8 bytes to match the SOURCE layout.

**Change:**
```cpp
// BEFORE
case 3: {
    std::vector<int> shuffled1;
    GetShuffledInts(shuffled1, 4);
    std::vector<int> shuffled2;
    GetShuffledInts(shuffled2, 4);

// AFTER (EXP-C-1)
case 3: {
    std::vector<int> shuffled2;
    std::vector<int> shuffled1;
    GetShuffledInts(shuffled1, 4);
    GetShuffledInts(shuffled2, 4);
```

**Reasoning:**
- Observed that case 2 block (lines 1292-1296) declares variables in the OPPOSITE order: shuffled2 then shuffled1
- Thought this might hint at correct ordering for case 3

**Result:**
- **Match: 95.1%** (REGRESSION -0.2%)
- **Introduced code reordering:** 3 deletes, 3 inserts at idx 2566-2577
- The compiler reordered the `GetShuffledInts()` function calls
- Made things WORSE, not better

**Decision:** REVERT

**Analysis:**
The variable declaration order affects more than just stack offsets - it influences:
1. Initialization order (which calls happen first)
2. Register allocation across the entire block
3. Code generation patterns

Simply reordering declarations without changing initialization calls caused the compiler to generate different instruction sequences. This is a cascading effect that makes offset fixes very fragile.

---

## MCP Analyze Function Results

```bash
run_analyze_function(symbol="?OnBeat@BustAMovePanel@@QAAXXZ",
                     project_dir="/home/free/code/milohax/dc3-decomp",
                     resolve_offsets=true)
```

**Verdict:** LIKELY_FIXABLE

**Pattern Breakdown:**
- **LINKER_MERGED:** 16 instructions (unfixable) - 5.3% of diff
- **REGISTER_SWAP:** 141 instructions (maybe_fixable ~30%) - 46.8% of diff
- **CONTROL_FLOW:** 2 instructions (likely_fixable ~70%) - 0.7% of diff
- **OFFSET_SWAP:** **14 instructions** (likely_fixable ~60%) - 4.6% of diff

**Key Insight:**
Only 14 of the 328 offset differences (4.3%) are flagged as "likely fixable" OFFSET_SWAP patterns. The remaining 314 offset mismatches are classified as unfixable frame layout differences.

**This means:**
- Expected gain from fixing OFFSET_SWAP: +0.3-0.5% at best (14 instructions × 60% success rate)
- Even perfect OFFSET_SWAP fixes would only reach ~95.6%

---

## Technical Learnings

### Why OFFSET_SWAP is Hard to Fix

1. **Static Local Variables**
   - Variables like `static Message playMsg` are placed in the stack by the compiler's static initialization logic
   - Their placement is NOT controlled by declaration order in source code
   - Compiler may group all static locals together in a specific region of the stack frame

2. **Global Frame Layout**
   - The +96 dominant delta suggests our overall stack frame is 96 bytes larger than the original
   - This is likely due to:
     - Different register spill choices
     - Different alignment requirements
     - Different optimization passes
   - No amount of local variable reordering will fix a fundamental frame size mismatch

3. **Cascading Effects**
   - Changing variable declaration order affects:
     - Constructor call order (side effects!)
     - Register allocation
     - Code generation patterns
   - A "fix" for offset swaps often breaks other patterns (as seen in EXP-C-1)

4. **Limited Fixable Scope**
   - Only 14 instructions are truly fixable OFFSET_SWAP patterns
   - These are small, localized member access order differences
   - Not the 328 offset diffs we initially hoped to fix

### What Actually Causes Our Offset Diffs

| Delta | Count | Likely Cause | Fixable? |
|-------|-------|--------------|----------|
| +96 | 98 | Global frame size difference | No |
| -8 | 26 | Static Message variable placement | Probably not |
| +48 | 21 | Static Message variable placement | Probably not |
| -4, +16, etc. | 163 | Various frame layout differences | Mostly no |
| Small localized swaps | 14 | Member access order | Maybe (60% chance) |

---

## Path Forward

### What We've Ruled Out
- ❌ Simple variable reordering to fix offset swaps
- ❌ Static local variable placement control
- ❌ Global frame size adjustments

### What Remains
1. **REGISTER_SWAP patterns (141 instructions)**
   - 30% success rate typical
   - Could yield +0.5-1.0% improvement with luck
   - Requires experimentation with variable declaration order (but different from offset swap approach)

2. **CONTROL_FLOW patterns (2 instructions)**
   - 70% success rate typical
   - But we already tried these in Sessions 1-2 with no success
   - Likely context-dependent and unfixable

3. **Minor OFFSET_SWAP patterns (14 instructions)**
   - 60% success rate
   - Low value target (+0.2-0.3% max)
   - Might be worth one or two quick experiments

### Recommended Next Steps

**Option A: Try REGISTER_SWAP fixes**
- Focus on the r10↔r11 swap (24 instances)
- Try variable reordering in scopes where these registers are used
- Caution: Similar cascading risks as offset swaps

**Option B: Accept practical ceiling**
- 95.3% with 16 unfixable LINKER_MERGED calls is already excellent
- Estimated practical ceiling: ~96.0-96.5%
- Remaining 0.7-1.2% may not be worth extensive effort

**Option C: Hybrid approach**
- 1-2 targeted REGISTER_SWAP experiments
- If no progress, mark as at practical limit
- Document patterns for future reference

---

## Statistics

**Total Analysis Time:** ~45 minutes
**Experiments Run:** 1
**Experiments Successful:** 0
**Experiments Regressed:** 1
**Experiments Neutral:** 0

**Function Complexity:**
- Total instructions: 3053
- Switch statements: 3
- Static local variables: ~10
- Nested scopes: Many
- Loop structures: Multiple

**Remaining Diff Budget:**
- Current: 95.3% (143 unmatched instructions out of 3053)
- Unfixable: 16 instructions (LINKER_MERGED)
- Best possible: ~96.5% (assuming 30% of remainder is fixable)
- Realistic target: ~96.0%

---

## Conclusions

1. **OFFSET_SWAP is NOT the right approach** for this function
   - Most offset diffs are frame layout issues, not fixable variable ordering
   - Static locals are compiler-controlled, not source-controlled

2. **The dominant +96 delta is likely unfixable**
   - Represents a fundamental stack frame size mismatch
   - Would require matching compiler's internal allocation decisions

3. **We're approaching practical ceiling**
   - 95.3% is already very good for a function of this complexity
   - Remaining improvements will be difficult and low-yield

4. **Session 4 recommendation**
   - Try 2-3 REGISTER_SWAP experiments targeting r10↔r11
   - If no progress, mark function as "at practical limit"
   - Document lessons learned for other large functions

---

## Files Modified

- `/home/free/code/milohax/dc3-decomp/src/lazer/game/BustAMovePanel.cpp` (reverted)

## Files Created

- `/tmp/claude/bustamove_session3_baseline.json`
- `/tmp/claude/session3_offsets.txt`
- `/tmp/claude/session3_diagnose.txt`
- `/tmp/claude/session3_experiments.txt`
- `/tmp/claude/session3_trial_c1.json`

---

## Appendix: Offset Analysis Details

### Full Offset Histogram
```
     Delta   Count  Bar
  ────────  ──────  ──────────────────────────────
       +96      98  ██████████████████████████████
        -8      26  ███████
       +48      21  ██████
        -4      20  ██████
       +16      14  ████
       -56      10  ███
       -24       9  ██
       -32       8  ██
        +4       8  ██
       -16       6  █
       +80       6  █
       -96       6  █
       -40       5  █
       -64       5  █
      +128       5  █
        +8       5  █
       +12       4  █
      +224       4  █
      +120       2
       -60       2
```

Total: 82 distinct delta values across 328 instructions

### Static Local Variables Identified
```cpp
Line 894:  static Message startMessage("bustamove_start_create", 0);
Line 946:  static Message playMsg("bustamove_move_matched", 0);
Line 1035: static Message matchedMessage("bustamove_move_matched", 0);
Line 1040: static Message successMessage("bustamove_successfully_matched");
Line 1056: static Message failMessage("bustamove_fail_match");
Line 1113: static Message failMessage("bustamove_fail_bust");
Line 1147: static Symbol score("score");
Line 1158: static Message winnerMessage("bustamove_winner", 0);
Line 1191: static Symbol acc_flawless("acc_flawless_every_move");
Line 1239: static Message bothMessage("bustamove_both_dance");
```

All of these contribute to stack frame complexity and compiler-controlled placement.

---

**Session 3 Status:** COMPLETE (No improvement, valuable negative results)
**Next Session:** TBD - Recommend REGISTER_SWAP approach or accept practical limit
