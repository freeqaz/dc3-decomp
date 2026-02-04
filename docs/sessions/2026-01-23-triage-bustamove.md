# Session: Near-Match Triage + BustAMovePanel Progress

**Date:** 2026-01-23
**Focus:** Parallel Opus subagents for triage, RB3 reference work, and cold-start decompilation

---

## Summary

Ran 3 parallel Opus subagents:
1. **Triage Agent** - Analyzed 7 near-match functions with `objdiff-cli --verdict`
2. **Key.cpp Agent** - Worked on math functions using RB3 reference
3. **BustAMovePanel Agent** - Cold-start decompilation of DC3-specific code

## Key Wins

| Function | Before | After | File |
|----------|--------|-------|------|
| `GameMode::SetMode` | 99.0% | **100%** | GameMode.cpp |
| `RndMesh::Handle` | 97.4% | **98.77%** | Mesh.cpp |
| `PollCaptureFlashcard` | 0% | **100%** | BustAMovePanel.cpp |
| `QueueMovePromptVO` | 0% | **100%** | BustAMovePanel.cpp |
| `QuatSpline` | 70.1% | **71.4%** | Key.cpp |

**Game Code:** 62.24% → **62.45%** (+2.3KB matched)

---

## Triage Results

**Critical Finding:** All 7 near-match functions are **FIXABLE** - none at linker limit!

| Function | Match | Verdict | Primary Issue | Fix Approach |
|----------|-------|---------|---------------|--------------|
| `InterpTangent` | 82.9% | LIKELY_FIXABLE | 2 control flow diffs, 34 register swaps | Branch restructuring |
| `QuatSpline` | 71.4% | LIKELY_FIXABLE | 8 control flow diffs, compiler polynomial opt | Horner's method (partial) |
| `SetMode` | 99.0% | MAYBE_FIXABLE | 3 register swaps (r10↔r11), 1 merged call | Variable declaration order |
| `RndParticleSys::SyncProperty` | 98.6% | NEEDS_INVESTIGATION | 419 unattributed mismatches | Manual inspection needed |
| `Spotlight::SyncProperty` | 98.7% | MAYBE_FIXABLE | 8 register swaps, 1 merged call | Variable declaration order |
| `RndMesh::Handle` | 97.4% | LIKELY_FIXABLE | 3 control flow, bne↔beq at instr 590 | Branch condition operators |
| `ShaderOptions::GenerateMacros` | 97.2% | LIKELY_FIXABLE | 5 control flow, lwz↔bl at instr 12 | Branch restructuring |

### Triage Patterns Discovered

1. **Control flow differences** are the main blocker, not linker merging
2. **Register swaps** are symptoms of other issues (variable ordering, operand order)
3. **Low merged-call ratios** (<1.2%) mean functions are genuinely fixable
4. **bne vs beq** differences suggest comparison operator changes (`>=` vs `>`, etc.)

---

## BustAMovePanel Functions Implemented

### PollCaptureFlashcard (360 bytes)
Captures skeleton poses for flashcard generation:
- Checks `unk92c` flag for pending capture
- Reads `flashcard_tweak` data variable (default 0.17f) for timing
- Captures live skeleton into `DancerSkeleton unka4[3]` array
- Compares skeleton positions to determine tracking validity
- Sets `unk934 = 4` to trigger 4 frames of flashcard rendering

### QueueMovePromptVO (204 bytes)
Calculates when to play move prompt voice-over:
- Gets VO length via `GetMovePromptVOLength()`
- Calculates beats to wait: `(reps * 4) - 4`
- Converts to seconds using tempo BPM
- Sets `unk9a0` timestamp for VO playback

### Header Changes
- `DancerSkeleton.h`: Added `SetTracked(bool)` setter
- `FreestyleMoveRecorder.h`: Added `CompareSkeletonPositions()` declaration
- `BustAMovePanel.h`: Added `PollCaptureFlashcard()` declaration

---

## Key.cpp Compiler Quirks

### Horner's Method for Polynomials
The base compiler uses pure Horner's method:
```cpp
// Base generates: ((a*t + b)*t + c)*t + d
// Uses only f31 for ref, fixed-offset loads

// Our compiler precomputes powers:
// ref*ref and ref*ref*ref outside loop
// Requires f29, f30, f31, indexed lfsx loads
```

### fnmsubs vs fmsubs
Expressions like `a + -(b*c - d)` vs `a - (b*c - d)` generate different fused multiply-subtract variants even though mathematically equivalent.

---

## Follow-Up Subagent Results

After triage, ran 3 more Opus subagents on the identified targets:

### GameMode::SetMode - **100%** ✓
**Fix:** Wrap `TheGameData->SetInTimeyWimey` in scoped local variable:
```cpp
{
    int val = Property("is_in_timeywimey")->Int();
    TheGameData->SetInTimeyWimey(val);
}
```
**Why it works:** Forces compiler to complete Int() evaluation before loading TheGameData pointer, matching original instruction scheduling.

### RndMesh::Handle - 97.4% → **98.77%**
**Fix:** Changed `mBones.clear()` to `CopyBones(nullptr)` (RB3 pattern)
**Remaining:** 12 instruction diffs - compiler optimization for `mBones.empty()` + MessageTimer destructor scheduling

### ShaderOptions::GenerateMacros - 97.28% (unchanged)
**Blocker:** STL `clear()` inlining behavior differs between original and our build
- Original: `vector<ShaderMacro>::clear()` as out-of-line function call
- Ours: Compiler inlines `clear()` (14 extra instructions)
- Not fixable without invasive STL header changes

---

## Commands Used

```bash
# Triage with verdict
~/code/milohax/objdiff/target/release/objdiff-cli diff -p . "FuncName" -f json --verdict

# Build single file
ninja build/373307D9/src/lazer/game/BustAMovePanel.obj

# Full build + report
ninja
```
