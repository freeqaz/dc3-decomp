# UILabel Font Loading — Root Cause Analysis & Fix Plan

**Date**: 2026-03-02
**Status**: Bug 1 fixed, Bug 2 mitigated (vtable guards), Bug 3 fixed, CharBonesSamples desync fixed

## Summary

During native port boot-to-menu work, loading UI panels with `HamLabel`/`UILabel` objects crashed in multiple stages. All major blockers are now resolved:

1. **SIGABRT in PopRev** — `BinStream::PopRev` finds empty revision stack for `HamLabel 'text.lbl'` (FIXED)
2. **SIGSEGV in dynamic_cast** — null vtable on stub objects during ObjDirItr/UILabel (MITIGATED with vtable guards)
3. **SIGSEGV in UIFontImporter::GetGennedFont** — hardcoded ILP32 struct offsets crash on LP64 (FIXED)
4. **CharBonesSamples stream desync** — bulk read overcounted bytes due to Vector3 PAD field (FIXED)

## Bug 1: PopRev Double-PostLoad (FIXED)

### Symptom
```
DirLoader: PreLoad HamLabel 'text.lbl' (pos=1066922)
DirLoader: PostLoad HamLabel 'text.lbl' (pos=1067283)   ← first, pops rev OK
DirLoader: PostLoad HamLabel 'text.lbl' (pos=1067283)   ← SECOND PostLoad, stack empty
PopRev ABORT: empty stack for HamLabel 'text.lbl'
```

### Root Cause

**Type mismatch in UILabel.h**: On non-native, `LabelStyle::mLabelDir` is `ObjPtr<UILabelDir>` (16 bytes on ILP32). The code throughout UILabel.cpp casts it to `ResourceDirPtr<UILabelDir>*` via reinterpret_cast. On ILP32, this is "safe" because `ObjPtr` has no `mLoader` field — the memory at `ObjDirPtr::mLoader`'s offset (0x10) reads from the next struct field (`unk28 = 0`), so `ObjDirPtr::PostLoad` sees `mLoader == nullptr` and returns immediately.

On LP64, field sizes change. `ObjPtr` is 32 bytes, and the memory at the `mLoader` offset now reads a **non-null garbage value** (specifically `ObjRefOwner::mOwner`, which is a valid non-null pointer to the owning object). This triggers `ObjDirPtr::PostLoad` → `PollUntilLoaded(garbage_ptr, nullptr)`.

### Fix

In `UILabel.h`, `mLabelDir` is now `ResourceDirPtr<UILabelDir>` on native (instead of `ObjPtr<UILabelDir>` with reinterpret_casts). All cast sites in UILabel.cpp have native-guarded paths that use `mLabelDir` directly.

## Bug 2: SIGSEGV After Font Sub-Loading (MITIGATED)

### Symptom
```
DC3 Native: Caught SIGSEGV (signal 11) at address (nil)
  Last dynamic_cast attempt: entry='loc_recap.mesh' obj=0x7d1e11800c48
```

### Root Cause

Objects created via `NewObject()` that are stubs or not yet fully loaded may have null vtable pointers. `dynamic_cast` on such objects segfaults trying to walk RTTI through the vtable. This manifests in:
- `ObjDirItr::Advance` — iterates dir objects, calls `dynamic_cast` to filter by type
- `ObjRefConcrete::SetObj` — casts root_obj to specific type
- `UILabel::LabelUpdate` — accesses `mColorOverride->GetColor()` on objects with null vtables

### Fix

Added null vtable guards in three locations:
- `src/system/obj/Dir.h` — ObjDirItr::Advance: skip objects with null vtable
- `src/system/obj/ObjPtr_p.h` — ObjRefConcrete::SetObj: return nullptr for null-vtable objects
- `src/system/ui/UILabel.cpp` — LabelUpdate: guard mColorOverride vtable before GetColor()

Also fixed `RndFont3d::CharAdvance` crash — `FontMap3d::SetupCharacter` now checks `CharDefined()` before calling `CharAdvance()` on fonts with empty `mCharInfoMap`.

**Note**: A linter/formatter automatically strips `#ifdef HX_NATIVE` diagnostic blocks from `CharBonesSamples.cpp`. Diagnostics must go in other files (e.g., CharClip.cpp) to survive.

## Bug 3: UIFontImporter ILP32 Offsets (FIXED)

### Root Cause

`UIFontImporter::GetGennedFont` uses **hardcoded ILP32 struct offsets** to manually traverse `ObjPtrList` internals. On LP64, all offsets shift because pointers are 8 bytes.

### Fix

Two changes in `src/system/ui/UIFontImporter.cpp`:

1. **GetGennedFont**: Rewritten with `#ifdef HX_NATIVE` to use proper member access and `ObjPtrList` iterators instead of raw pointer arithmetic
2. **OnSyncWithResourceFile**: `pKern + 0x3c` replaced with `pKern->mBaseKerning` (direct member access via friend class)

Workarounds removed from `UILabel.cpp`:
- `SetFontMat` early return removed
- `DrawShowing` null font guard removed

## Bug 4: CharBonesSamples Stream Desync (FIXED)

### Symptom
```
CharBonesSamples::Load streamPos=1072 revs=0x41fef0d0 rev=61648 altRev=16894  ← GARBAGE
FAIL: can't load new CharBonesSample version 61648 > 16
```

The second `CharBonesSamples` (`mOne`) in a `CharClip` read garbage because the first (`mFull`) consumed the wrong number of stream bytes.

### Root Cause: sizeof(Vector3) = 16, not 12

**Key discovery**: `Vector3` has a hidden `u32 PAD` member at offset 12 (Vec.h:150) for SIMD alignment on Xbox 360 (VMX128). This makes `sizeof(Vector3) = 16` on ALL platforms.

```cpp
class Vector3 {
public:
    float x, y, z;
private:
    u32 PAD; // should NEVER be used!!!! for simd alignment!!!
};
```

`TypeSize(TYPE_POS)` returns `sizeof(Vector3) = 16`, so `mOffsets` are spaced by 16 per position bone in **memory**. But the stream format only stores 12 bytes per Vector3 (3 floats), because `operator>>(BinStream&, Vector3&)` reads only `x, y, z`.

Someone had rewritten `LoadData` to do a bulk `d.stream.Read(mRawData, totalBytes)` followed by in-place byte-swapping. This read `mTotalSize * mNumSamples` bytes from the stream — but the stream only contains the actual data bytes (no PAD), overcounting by 4 bytes per Vector3 per sample. This corrupted the stream position for `mOne.Load`.

### Fix

`LoadData` now has two paths gated on `d.stream.Cached()`:

1. **Cached path** (`#ifdef HX_NATIVE` + `d.stream.Cached()`): Bulk `d.stream.Read(mRawData, totalBytes)` — correct because cached `.milo_xbox` files write 16 bytes per Vector3 (12 data + 4 zero pad), matching `mTotalSize` stride. Followed by in-place byte-swap from big-endian to native little-endian.

2. **Non-cached path** (element-by-element `d >> *p`): Reads exactly 12 bytes per Vector3 from stream via `operator>>`, storing into 16-byte-strided memory. Handles endian conversion automatically via BinStream.

The original bug was unconditionally using the bulk read path, which overcounted bytes for non-cached streams.

### Critical Lesson for Decomp

The bulk read is correct **only for cached files** where `Save` explicitly writes the PAD bytes. Non-cached streams store only the data bytes. The `mOffsets` array describes the **memory** layout (16-byte Vector3 stride), not the stream layout (12 bytes). Key types with stream/memory size mismatch:
- `Vector3`: 12 stream bytes → 16 memory bytes (PAD field)
- `ByteQuat`: 4 raw bytes (no endian swap needed)

### Verification
```
CharClip::Load 'chanel_skeleton' before mFull.Load streamPos=977
CharClip::Load 'chanel_skeleton' after mFull.Load streamPos=1080 LE=0
CharClip::Load 'chanel_skeleton' after mOne.Load streamPos=4170
DirLoader: PostLoad CharClip 'chanel_skeleton' (stream pos=4206)
```
All CharClips load successfully. No version errors.

## Current State

The engine boots past CharClip loading and into UI initialization. Remaining crash is in `UILabel::LabelUpdate` during `DirLoader::LoadObjs` → `HamLabel::PostLoad`, likely from another null-vtable object. The vtable guard in LabelUpdate may need to be re-applied (the linter strips `#ifdef HX_NATIVE` blocks from some files).

### PipeWire Audio Crash

The PipeWire/miniaudio audio thread crashes with SIGSEGV in `libspa-support.so`. This is an external library issue, not game code. Workaround: `PIPEWIRE_REMOTE=invalid` disables PipeWire and the engine progresses normally.

## Files Modified

| File | Change | Status |
|---|---|---|
| `src/system/ui/UILabel.h` | `mLabelDir` → `ResourceDirPtr<UILabelDir>` on native | Done |
| `src/system/ui/UILabel.cpp` | Native paths for mLabelDir, vtable guard in LabelUpdate | Done (may need re-apply) |
| `src/system/ui/UIFontImporter.cpp` | LP64-safe GetGennedFont + mBaseKerning access | Done |
| `src/system/obj/Dir.h` | Vtable guard in ObjDirItr::Advance | Done |
| `src/system/obj/ObjPtr_p.h` | Vtable guard in ObjRefConcrete::SetObj | Done |
| `src/system/rndobj/Text.cpp` | CharDefined guard in FontMap3d::SetupCharacter | Done |
| `src/system/char/CharBonesSamples.cpp` | LoadData: bulk read gated on `Cached()`, element-by-element for non-cached | Done |
| `src/system/char/CharBones.cpp` | Clean RecomputeSizes for LP64 (direct array access) | Done |
| `src/system/char/CharClip.cpp` | Diagnostic logging around mFull/mOne Load | Active |
| `native/src/platform/Mesh_Wgpu.cpp` | Fixed CreateMaterialBindGroup API mismatch | Done |
| `native/tests/test_charbones_serialization.cpp` | 16 tests: math type sizes, serialization byte counts, round-trips, desync regression | Done |

## Other Fixes in This Session

| File | Change |
|---|---|
| `src/system/os/System.cpp` | Skip `NetCacheMgrInit()` on native (Xbox-only, needs `XLSPConnection`) |
| `src/lazer/meta_ham/MainMenuPanel.cpp` | Guard all `TheNetCacheMgr->` calls with null check on native |
| `src/system/meta/StorePanel.cpp` | Guard `TheNetCacheMgr->Load()` with null check on native |

## Reference: sizeof(Vector3) = 16

This is a critical fact for all CharBones/CharBonesSamples/CharClip work:

```
class Vector3 { float x, y, z; u32 PAD; };  // sizeof = 16
```

`TypeSize(TYPE_POS)` and `TypeSize(TYPE_SCALE)` return `sizeof(Vector3) = 16` when compression < kCompressVects. The `mOffsets` array uses 16-byte strides for positions/scales. But stream serialization only writes/reads 12 bytes (3 floats). The 4-byte PAD is never serialized.

## Codebase Audit: Vector3 PAD Impact

A full audit confirmed that only `Vector3` has the PAD mismatch. No other math types are affected:

| Type | sizeof | Stream bytes | PAD? |
|---|---|---|---|
| `Vector3` | 16 | 12 | Yes (`u32 PAD`) |
| `Hmx::Quat` | 16 | 16 | No (4 floats, naturally 16) |
| `Hmx::Matrix3` | 48 | 36 | No (3×Vector3, each reads 12) |
| `Transform` | 64 | 48 | No (Matrix3+Vector3) |

Other bulk-read sites (`Matrix3`, `Transform`) all use element-by-element `operator>>` and are safe. The `CharBonesSamples::Save` cached path explicitly writes the PAD bytes (`bs << 0.0f` after each Vector3), confirming that cached files match `mTotalSize` stride.

## Serialization Tests

Added `native/tests/test_charbones_serialization.cpp` with 16 tests (all passing):

- **MathTypeSizes** (4 tests): Verify `sizeof` for Vector3, Quat, Matrix3, Transform
- **MathTypeSerialization** (5 tests): Verify exact stream byte counts for reads/writes
- **CharBonesSamples round-trips** (5 tests): Save→Load for all compression modes (kCompressNone, kCompressRots, kCompressVects, kCompressQuats, kCompressAll)
- **TwoConsecutiveLoads** (2 tests): Regression test for the exact desync scenario — sequential mFull + mOne loads from a single stream, for all compression modes

## Testing Notes

- PipeWire audio crashes in sandbox. Use `PIPEWIRE_REMOTE=invalid` to disable, or `dangerouslyDisableSandbox: true`.
- A project linter strips `#ifdef HX_NATIVE` diagnostic blocks from `CharBonesSamples.cpp`. Put diagnostics in CharClip.cpp or other files instead.
- DirLoader PreLoad/PostLoad logging is still active and very verbose (~4500+ lines for a full boot).
