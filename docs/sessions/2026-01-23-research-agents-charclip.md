# Session: Research Agents + CharClip::Load Implementation

**Date:** 2026-01-23
**Focus:** Parallel research agents to identify high-leverage targets, implementation of CharClip::Load
**Duration:** ~1 hour

---

## Summary

Used 8 parallel Sonnet research agents to analyze high-impact function candidates based on the Gap Analysis priorities. Successfully implemented CharClip::Load using RB3 reference code, achieving a 48 percentage point improvement. Fixed critical uninitialized pointer bug in CSHA1::Transform.

### Key Results

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| **Matched Code** | 55.28% | 55.41% | +0.13% (~8KB) |
| **Matched Functions** | 68.57% | 68.67% | +0.10% |

### Functions Improved

| Function | Before | After | Change | Notes |
|----------|--------|-------|--------|-------|
| CharClip::Load | 28.1% | 76.1% | **+48pp** | Added ~100 lines from RB3 reference |
| CSHA1::Transform | 54.3% | 54.3% | Bug fix | Fixed uninitialized m_block pointer |

---

## Methodology: Parallel Research Agents

Launched 8 Sonnet subagents simultaneously to analyze high-impact candidates:

```
Agent ID   | Target                      | Verdict
-----------|-----------------------------|---------
a0efe13    | CharClip::Load              | DEFINITELY FIXABLE (28% → 95%+)
ac1a38b    | SongSequence::OnSongLoaded  | LIKELY FIXABLE (93% → 97%)
aafcdd7    | CamShot::Load               | FIXABLE (81% → 95%)
acda381    | SongSequence::DoNext        | HIGHLY FIXABLE (stub, 14% → 95%+)
a4c9b53    | Key.cpp math functions      | DIFFICULT (FMA issues)
adebe56    | RndMat::SyncProperty        | COMPLETED (79% → 96.7%)
a4d4871    | CSHA1::Transform            | FIXABLE (bug found)
ac70f7a    | RndText::Load               | INCOMPLETE (DC3-specific)
```

### Research Findings Summary

**High-Value Targets (RB3 reference available):**
- CharClip::Load - Incomplete code, complete RB3 reference ✅ IMPLEMENTED
- CamShot::Load - Missing LoadDrawables path
- SongSequence::OnSongLoaded - Control flow inversion at line 253

**Stub Functions (need reverse engineering):**
- SongSequence::DoNext - 14.5%, only early-exit logic exists, ~2200 bytes missing

**Incomplete Implementations (need binary RE):**
- RndMat::SyncProperty - 79.8% → **96.7%** (COMPLETED, see session doc)
- RndText::Load - 61.3%, DC3 has different style system than RB3

**Complex Patterns:**
- CSHA1::Transform - 1728 register swaps, difficult to match
- Key.cpp QuatSpline/InterpTangent - Fused multiply-add issues

---

## Implementation Details

### CharClip::Load (28.1% → 76.1%)

**Problem:** Function ended at line 466 with comment `// there's more`

**Solution:** Added complete implementation from RB3 reference:

```cpp
// Added ~100 lines including:
if (oldRev - 9U <= 1) { bool b118; d >> b118; }
if (oldRev > 9) { int oldVer; d >> oldVer; mOldVer = oldVer; }
if (oldRev > 0xB) { d >> mDoNotCompress; }
mTransitions.Load(d, oldRev);
// Beat events loading (both old and new format)
// CharBonesSamples loading (mFull/mOne)
// mZeros, mBeatTrack, mSyncAnim loading
// Final validation
```

**Remaining issues (24% gap):**
- 5 LINKER_MERGED patterns (unfixable)
- 114 register swap instructions
- 6 control flow differences

### CSHA1::Transform Bug Fix

**Problem:** `m_block` pointer was never initialized but used throughout Transform()

**Fix:**
```cpp
// Before (line 222):
// &m_block->c = &m_workspace;  // Syntactically wrong comment

// After:
m_block = (SHA1_WORKSPACE_BLOCK *)m_workspace;
```

**Result:** Bug fixed, but 54.3% match unchanged due to fundamental register allocation differences in SHA-1 macro expansions.

---

## Deferred Work (For Future Sessions)

### Quick Wins Available

1. **SongSequence::OnSongLoaded** (93% → ~97%)
   - Invert condition at line 253: `if (curEntry.unk21)` → `if (!curEntry.unk21)`
   - Swap if/else bodies
   - ~5 minute fix

2. **CamShot::Load** (81% → ~95%)
   - Add missing LoadDrawables call path
   - Research agent identified specific location

### Requires Reverse Engineering

1. **SongSequence::DoNext** (14.5%)
   - Stub function with `// need GamePanel` comment
   - ~2200 bytes of logic missing
   - Requires Ghidra analysis of target binary

2. **RndMat::SyncProperty** (79.8% → 96.7%) **COMPLETED**
   - Added 10 missing properties from BaseMaterial
   - Fixed property ordering and SYNC patterns
   - See [2026-01-23-rndmat-syncproperty.md](2026-01-23-rndmat-syncproperty.md)

3. **RndText::Load** (61.3%)
   - DC3 has different style system than RB3
   - Cannot directly copy from RB3 reference

---

## Lessons Learned

### Parallel Research Agents

**Effective for:**
- Quickly triaging multiple candidates
- Identifying fixability before investing time
- Finding incomplete vs pattern-matching issues

**Agent accuracy:**
- CharClip::Load: Predicted 28% → 95%+, achieved 76% (linker patterns)
- CSHA1::Transform: Predicted 54% → 75-90%+, achieved 54% (register allocation)

**Takeaway:** Agents correctly identify fixability but may overestimate improvement potential due to unfixable linker/register patterns.

### RB3 Reference Value

Functions with RB3 reference code are much easier to fix than DC3-specific code:
- CharClip::Load: Had RB3 reference → 48pp improvement
- RndText::Load: DC3-specific style system → deferred
- RndMat::SyncProperty: Used binary strings + Ghidra → 96.7% (completed)

### Priority Strategy

User requested "low match percentage functions for maximum progress":
- 28% → 76% = ~1100 bytes gained
- 93% → 97% = ~140 bytes gained

Low-percentage functions with RB3 references provide best ROI.

---

## Files Modified

```
src/system/char/CharClip.cpp    # Added ~100 lines to Load function
src/system/math/SHA1.cpp        # Fixed m_block initialization
```

---

## Commands Used

```bash
# Research agent queries
objdiff-cli report query build/373307D9/report.json --functions --min-percent 10 --max-percent 50 --limit 10

# Function-specific analysis
objdiff-cli diff -p . "CharClip::Load" -f markdown --verdict --include-instructions

# Progress check
ninja build/373307D9/report.json && ./tools/progress.sh
```

---

## Next Session Recommendations

1. **Quick win:** Implement SongSequence::OnSongLoaded control flow fix (~5 min)
2. **Medium effort:** Complete CamShot::Load with LoadDrawables path
3. **Research task:** Use Ghidra to extract SongSequence::DoNext logic
4. **Documentation:** Update GAP_ANALYSIS.md with new CharClip::Load status
