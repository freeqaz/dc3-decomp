# Session: Macro Usage Refactoring

**Date**: 2026-02-05
**Scope**: Code cleanup across `src/system/` to use project macros
**Result**: Net positive - one major improvement (+63%), one minor improvement (+1%), two negligible regressions (<1%)

## Summary

Refactored code in `src/system/` to use project macros (`BEGIN_HANDLERS`, `FOREACH`, etc.) where appropriate, improving code consistency and readability while maintaining or improving decomp match percentages.

## Changes Made

### Priority 1: Handler Macro Refactors

#### SynthSample::Handle() - Full Refactor
**File**: `src/system/synth/SynthSample.cpp:75-146`

Replaced 72-line manual implementation with 8-line macro version:

```cpp
// Before: Manual implementation with bizarre lazy-init pattern using sInitState bitmask
DataNode SynthSample::Handle(DataArray *in, bool b) {
    Symbol sym = in->Sym(b);
    static Symbol sPlatformSizeKb("platform_size_kb");
    // ... 60+ more lines with sInitState bit-checking pattern
}

// After: Clean macro usage
BEGIN_HANDLERS(SynthSample)
    HANDLE_EXPR(platform_size_kb, (mSampleData.SizeAs(SampleData::kPCM) >> 10) + 0)
    HANDLE_EXPR(num_markers, mSampleData.NumMarkers())
    HANDLE_EXPR(marker_name, mSampleData.GetMarker(_msg->Int(2)).Name())
    HANDLE_EXPR(marker_sample, mSampleData.GetMarker(_msg->Int(2)).Sample())
    HANDLE_EXPR(sample_length, (int)(LengthMs() * 0.001f))
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
```

**Bug Fixed**: Original code used `in->Int(b)` where `b` is the warn boolean (typically 1), reading the symbol name instead of the argument. Corrected to `_msg->Int(2)`.

**Match**: 30.2% → 93.8% (+63.6%)

#### ScrollSelect::Handle() - Add Wrapper Macros
**File**: `src/system/ui/ScrollSelect.cpp:78-96`

Replaced manual MessageTimer and warning code with `BEGIN_CUSTOM_HANDLERS`/`END_CUSTOM_HANDLERS`.

**Match**: 95.9% → 97.0% (+1.1%)

### Priority 2: FOREACH Conversions

#### UIScreen.cpp - 5 loops converted to `FOREACH_POST`
All used post-increment (`it++`), so `FOREACH_POST` preserves codegen:
- `Draw()` line 137
- `Enter()` line 166
- `ReenterScreen()` lines 303, 309
- `ForeachPanel()` line 366

#### CharTransDraw.cpp - 2 loops converted to `FOREACH`
- `Load()` line 53
- `DrawShowing()` line 65

**Minor regressions** (register allocation differences from macro expansion):
- `Load`: 97.7% → 97.1% (-0.6%)
- `DrawShowing`: 93.6% → 93.4% (-0.2%)

#### Sound.cpp - 2 loops converted to `FOREACH`
- `Stop()` line 260
- `SynthPoll()` line 445

### Lower Priority: Single-Occurrence Files

All converted to appropriate FOREACH variant (pre/post increment preserved):

| File | Line | Container | Macro |
|------|------|-----------|-------|
| `flow/FlowNode.cpp` | 112 | `mChildNodes` | FOREACH |
| `obj/Dir.cpp` | 476 | `mRefs` | FOREACH |
| `os/Archive.cpp` | 155 | `mFileEntries` | FOREACH |
| `os/HolmesClient.cpp` | 501 | `gRequests` | FOREACH |
| `rndobj/Line.cpp` | 171 | `mPoints` | FOREACH |
| `rndobj/EventTrigger.cpp` | 280 | `drawList` | FOREACH |
| `rndobj/Font.cpp` | 475 | `mMats` | FOREACH |
| `rndobj/MatAnim.cpp` | 235 | `keys` | FOREACH_POST |
| `synth/FxSend.cpp` | 169 | `mRefs` | FOREACH |
| `char/Character.cpp` | 726 | `vec` | FOREACH |
| `ui/UI.cpp` | 374 | `mPushedScreens` | FOREACH |
| `ui/UIFontImporter.cpp` | 348 | `mMatVariations` | FOREACH |

## Not Converted (Container Modifications)

These loops modify containers during iteration - FOREACH is inappropriate:
- `Sound.cpp:244,250,414,428` - erase during iteration
- `DefaultPhysicsManager.cpp:83,95` - prev_it erase pattern
- `ContentMgr.cpp:97,226` - erase during iteration
- `PreloadPanel.cpp:66` - erase during iteration
- `PropAnim.cpp:26` - erase during iteration
- `CacheMgr.cpp:54` - erase during iteration
- `Flow.cpp:268` - next_it safe-iteration pattern

## Impact Summary

| Function | Before | After | Delta |
|----------|--------|-------|-------|
| SynthSample::Handle | 30.2% | 93.8% | +63.6% |
| ScrollSelect::Handle | 95.9% | 97.0% | +1.1% |
| CharTransDraw::Load | 97.7% | 97.1% | -0.6% |
| CharTransDraw::DrawShowing | 93.6% | 93.4% | -0.2% |

## Phase 3: Save/Load Function Analysis

### UIListDir::Save - Field Order Fix
**File**: `src/system/ui/UIListDir.cpp:56-70`

The Save function was serializing fields in the wrong order and included fields that shouldn't be saved (runtime-only data). Fixed the order to match the binary's PostLoad expectations:

```cpp
// Before: Wrong order, included unk270 and mDirection
BEGIN_SAVES(UIListDir)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mOrientation;
    bs << mFadeOffset;
    bs << mElementSpacing;
    bs << mScrollHighlightChange;
    bs << mTestMode;
    bs << mTestNumData;
    bs << mTestComponentState;
    bs << mTestGapSize;
    bs << mTestDisableElements;
    bs << unk270;  // Should not be saved
    bs << mDirection;  // Should not be saved
END_SAVES

// After: Correct order matching PostLoad, runtime data removed
BEGIN_SAVES(UIListDir)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(RndDir)
    bs << mOrientation;
    bs << mFadeOffset;
    bs << mTestMode;
    bs << mTestState.NumDisplay();
    bs << mElementSpacing;
    bs << mTestState.Speed();
    bs << mTestNumData;
    bs << mTestComponentState;
    bs << mTestGapSize;
    bs << mTestDisableElements;
    bs << mScrollHighlightChange;
END_SAVES
```

**Match**: 73.0% → 87.9% (+14.9%)

**Remaining differences**: Stack frame layout, ICF-merged Speed() call, prologue/epilogue conventions.

### CharBonesSamples::Load - BinStreamRev Fix
**File**: `src/system/char/CharBonesSamples.cpp:13-29`

Fixed version parsing to use BinStreamRev properly and match the binary's version checking pattern:

```cpp
// Before: Manual version parsing, late BinStreamRev creation
void CharBonesSamples::Load(BinStream &bs) {
    u32 ver;
    bs >> ver;
    u32 v0 = ver & 0xFFFF;
    u32 v1 = (ver >> 16) & 0xFFFF;
    if (v0 > 0x10) {
        MILO_FAIL("%s can't load new %s version %d > %d", "", "ChaBonesSample", v0, 1);
    }
    // ...
}

// After: Early BinStreamRev, using d.rev/d.altRev
void CharBonesSamples::Load(BinStream &bs) {
    int ver;
    bs >> ver;
    BinStreamRev d(bs, ver);
    if (d.rev > 0x10) {
        MILO_FAIL("%s can't load new %s version %d ", "", "CharBonesSample", d.rev);
    }
    if (d.altRev > 0) {
        MILO_FAIL("%s can't load new %s alt version", "", "CharBonesSample", d.altRev);
    }
    MILO_ASSERT(d.rev > 12, 0x29D);
    LoadHeader(d);
    LoadData(d);
}
```

**Match**: 78.8% → 87.8% (+9.0%)

**Remaining differences**: Different format strings (original has different parameter count), ICF-merged MakeString variant, register allocation.

### Phase 3 Impact Summary

| Function | Before | After | Delta |
|----------|--------|-------|-------|
| UIListDir::Save | 73.0% | 87.9% | **+14.9%** |
| CharBonesSamples::Load | 78.8% | 87.8% | **+9.0%** |

### Functions NOT improved (analysis results)

**OnGetCurrentInterests@Character** (79.9%): Issues are register allocation and control flow from loop structure, not macro-related.

**SetFocusInterest@Character** (74.6%): Similar - control flow and register allocation issues, not macro-related.

**ByteGrinder ops** (82-86%): Not macro-related - these are DataNode return value handling differences.

---

## Takeaways

1. **HANDLE_EXPR macros** generate cleaner code that often matches better than manual implementations with lazy-init patterns.

2. **FOREACH vs FOREACH_POST** matters - check the original loop's increment style (`++it` vs `it++`) before converting.

3. **Minor register allocation differences** from FOREACH macro expansion can cause small (<1%) regressions, but the code clarity benefits usually outweigh this.

4. **Don't convert loops that modify containers** during iteration (erase patterns, next_it patterns).

5. **Save/Load field order must match** - Compare Save() serialization order with PostLoad() deserialization to ensure consistency.

6. **BinStreamRev creation timing matters** - Create it early to match the binary's version unpacking pattern.

7. **Runtime-only data shouldn't be serialized** - Vectors and state that are created at runtime (via SyncObjects, CreateElements) shouldn't be in Save().
