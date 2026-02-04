# Session: Branch Patterns & Global Pooling Investigation

**Date:** 2026-01-25
**Focus:** Investigating why functions plateau at 95-99% match, testing optimization theories

## Summary

Started with parallel Sonnet agents on 12 system/rndobj functions, discovered most were AT_LIMIT due to linker optimizations. Investigated a theory about "branch predictor abuse" being misidentified as linker merging. Found the real issues and documented them.

## Agent Results

### Round 1: Sonnet Agents (12 functions)
Most functions were already AT_LIMIT. Key finding: RndLight::Load was actually a **stub**, not a near-match.

| Function | Result | Notes |
|----------|--------|-------|
| RndMat::SyncProperty | 97.7% unchanged | Linker-merged + macro-generated |
| RndMesh::Load | 94.9% → 95.9% | +1.0% improvement |
| RndMesh::Handle | 97.8% → 97.9% | +0.1% improvement |
| EventTrigger::Load | 98.3% unchanged | AT_LIMIT (bool mask) |
| RndTexRenderer::SyncProperty | 97.9% unchanged | Tail-call optimization diff |
| RndLight::Load | **27.7% → 81.8%** | **Major: was a stub!** |
| BaseMaterial::Copy | 99.6% unchanged | AT_LIMIT (ICF) |
| RndPostProc::Copy | 99.8% unchanged | AT_LIMIT (ICF) |
| RndCam::Load | 99.2% unchanged | AT_LIMIT (ASSERT_REVS) |
| RndDrawable::Load | 99.2% unchanged | AT_LIMIT (ASSERT_REVS) |
| RndLine::Load | 99.1% unchanged | AT_LIMIT (ASSERT_REVS) |
| RndMultiMesh::Load | 99.1% unchanged | AT_LIMIT (ASSERT_REVS) |

### Round 2: Opus Agents (same 12 functions)
Opus correctly identified RndLight::Load as a stub and implemented it fully.

## Research Findings

### Theory Investigated
> "the merging hasn't been too bad, and i think it's getting tripped up into thinking the abusing-the-branch-predictor jumps are the linker's fault"

### Conclusions

1. **Linker-merged functions ARE real** - ICF (Identical COMDAT Folding) genuinely merges functions like `operator>>(BinStream&, Color&)` with `operator>>(BinStream&, Rect&)` into `merged_Read4FloatStruct`

2. **Branch condition differences are compiler codegen**, not branch predictor tricks:
   - Original: `cmpwi` + `ble` for `if (x != 0)`
   - Our build: `cmpwi` + `beq` for same code
   - **Fix:** Use `if (x > 0)` instead of `if (x != 0)` for unsigned types

3. **Global variable pooling is NOT the issue** for gRevs - both compilers pool identically

4. **Float constant pooling IS an issue** - Original linker places floats adjacent to static data for shared base-register access. This is a link-time optimization we cannot replicate at .obj level.

5. **ASSERT_REVS scheduling** - Consistent ~0.8-0.9% gap due to argument evaluation order. Unfixable.

## Pattern Applied: `!= 0` → `> 0`

Created tooling to find and apply this pattern:

| File | Function | Diff Score Change |
|------|----------|-------------------|
| CharFaceServo.cpp | CharFaceServo::Load | -5 |
| UIListArrow.cpp | UIListArrow::Load | -5 |
| Trans.cpp | RndTransformable::Load | -5 |
| Bitmap.cpp | RndBitmap::LoadHeader | 0 (no change) |

**Total: -15 diff score points**

## Files Modified

### Code Changes
- `src/system/rndobj/Lit.cpp` - RndLight::Load full implementation (+70 lines)
- `src/system/char/CharFaceServo.cpp` - `!= 0` → `> 0`
- `src/system/ui/UIListArrow.cpp` - `!= 0` → `> 0`
- `src/system/rndobj/Trans.cpp` - `!= 0` → `> 0`
- `src/system/rndobj/Bitmap.cpp` - `!= 0` → `> 0`

### Documentation
- `docs/decomp/TECHNICAL_NOTES.md` - Added sections on:
  - Unsigned zero comparisons pattern
  - Known unfixable issues (LTCG, float pooling, ASSERT_REVS, ICF)
  - Lesson #26
- `CLAUDE.md` - Added "Known Patterns" section

### Tooling Created
- `/tmp/claude/find_zero_comparisons.py` - Script to find `!= 0` candidates

## Byte Impact

| Change | Bytes Gained |
|--------|--------------|
| RndLight::Load implementation | +689 bytes |
| `!= 0` → `> 0` pattern (4 files) | +18 bytes |
| **Total** | **~707 bytes** |

## Key Lessons Learned

1. **Check if "near-match" functions are actually stubs** - RndLight::Load looked like 27% match but was just a stub with `bs >> mCubeTexture;`

2. **Most 99%+ functions are genuinely AT_LIMIT** - Due to:
   - ICF (Identical COMDAT Folding)
   - LTCG (Link-Time Code Generation)
   - Compiler instruction scheduling heuristics

3. **The `!= 0` → `> 0` pattern works** but only for unsigned zero comparisons. Other transformations (`> X` → `>= X+1`) make things worse.

4. **Float constant pooling is unfixable** - It's a linker optimization that merges constants across TUs at link time.

## Next Steps

- Apply `!= 0` → `> 0` pattern to any remaining candidates (only 4 found)
- Focus on stub functions (<50% match) rather than near-matches (95%+)
- Consider functions with high byte-count potential rather than high percentage potential
