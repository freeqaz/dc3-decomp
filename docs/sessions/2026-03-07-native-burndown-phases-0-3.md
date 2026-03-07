# Session: Native Port Burndown Phases 0-3

**Date**: 2026-03-07

## Overview

Executed Phases 0-3 of the DC3 Native Port Burndown Plan. Fixed text rendering, locale loading, and audited 420 `#ifdef HX_NATIVE` guards. All 139 unit tests now pass (was 136/139).

## Phase 0: Quick Wins (Done)

- **demo.yaml**: Fixed undefined `dare_streets` scene and duplicate `dare_front` key
- **standard.wgsl**: Synced standalone shader with full 542-line version from `standard_wgsl.inc` (skinned mesh, multi-light, shadows)
- **DECOMP_GAPS.md**: Updated to reflect current state

## Phase 1: Locale Loading (Done)

- **Root cause**: `Locale::mInitialized` was uninitialized (UB). On Xbox debug builds BSS wasn't zeroed so it happened to be `true`. On native, it was `false`, skipping all locale file loading.
- **Fix**: Proper constructor initializer list: `mInitialized(true)` — no `#ifdef` needed
- **Result**: 2091 locale symbols loaded from 2 files. UI text now shows English strings instead of raw tokens.
- **Bonus fix**: `LocalePanel::~LocalePanel()` virtual destructor was declared but never defined. GCC couldn't emit vtable -> weak zero-filled stub won -> SIGSEGV at address -24 during `UIManager::Init()`. Added empty destructor body.

## Phase 2: Text Positioning (Done - Was a Non-Issue)

Investigation showed text positioning is **already correct**:
- `sFlipYZ` coordinate transform (Milo Y-forward -> D3D Z-forward) works
- Ortho projection for UI camera works
- ViewProj matrix pipeline correct

The real issue was **`RndText::FitTextJust()` was missing** — declared but never defined, causing `undefined symbol` crash at runtime when any text used `kFitEllipsis` fit type.

**FitTextJust implementation**: Binary search that shrinks font size until text fits within `mWidth`/`mHeight` bounds. Bisects between 0.2 and original `mStyles[0].mSize`, calling `WrapText()` at each step to measure bounds.

Screenshot at `choose_mode_screen` (frame 800) confirms localized text renders at correct positions. Saved to `archive/screenshots/choose_mode_screen_f800.png`.

## Phase 3: `#ifdef HX_NATIVE` Audit

### Classification of 420 Guards

| Category | Count | % | Action |
|---|---|---|---|
| LP64 Portability | ~98 | 23% | Keep — real platform differences |
| Platform Stub | ~75 | 18% | Keep — Xbox API replacements |
| **Workaround/Defensive** | **~87** | **21%** | Investigate root causes |
| Correct Divergence | ~110 | 26% | Keep — legitimate code paths |
| New Native Feature | ~50 | 12% | Keep — WebGPU, YOLO, etc. |

### Bugs Found & Fixed

1. **`MakeString before InitMakeString`** — `NativeDetectDataDir()` in `System_Native.cpp` called `MakeString()` before `SystemPreInit()` had a chance to call `InitMakeString()`. Fixed by calling `InitMakeString()` early in the native `SystemPreInit(argc, argv, config)` overload. This was causing all 3 `HeadlessBootTest` failures.

2. **`PostPurchaseEnumJob::OnCompletion` undefined symbol** — Implementation lives in `PlatformMgr_Xbox.cpp` (Xbox-only). Native port had no fallback. Added `#ifdef HX_NATIVE` empty stub in `JobMgr.cpp`.

3. **`lbl_82F14008` stub type mismatch** — `engine_stubs_generated.cpp` had `int lbl_82F14008() { return 0; }` (function) but Rnd.cpp declares it as `extern int` (variable). Fixed to `int lbl_82F14008 = 0;`. Added missing `lbl_830A4100` and `lbl_830A4104` globals.

4. **`RtlDeleteCriticalSection` missing** — Called by `CriticalSection::~CriticalSection()` but never defined in `xdk_shims.cpp`. Added proper implementation (pthread_mutex_destroy + cleanup).

5. **`RndText::FitTextJust` missing definition** — Declared in Text.h, called in `UpdateText()`, but never implemented. Wrote full binary-search implementation from Ghidra decompilation.

6. **`StyleState::mActive` missing member** — Concurrent edit added usage without the declaration. Added `bool mActive` to `StyleState` class in Text.h.

7. **`HamStoreProvider` iterator type** — Used `HamStoreFilter**` (raw pointer) as iterator, works on MSVC STLport but not libstdc++. Fixed to `std::vector<HamStoreFilter*>::iterator`.

8. **`Multiply(Vector3, Matrix3)` forward declaration** — Called before its definition in Mtx.h. Added forward declaration.

### Unfixable Architectural Issues

- **Object ref-ring corruption** (23 guards): Objects destroyed in arbitrary order during `DeleteObjects()`. Refs to already-freed objects become dangling. Workarounds (live-object tracking, ring snapshots, `gSuppressDirPtrDelete`) are necessary.
- **`if (!this)` null guards** (4 guards): GCC doesn't optimize away null-this checks like MSVC does. These are technically UB but removing them crashes.
- **Font `displayableChars` garbage** (2 guards): Font loading occasionally produces corrupt character counts. Clamping to [0, 10000] prevents crashes. Root cause likely in font binary deserialization.

### Test Results

| Before | After |
|---|---|
| 136/139 pass | **139/139 pass** |
| 3 HeadlessBoot failures | All pass |

Remaining issue: `free(): invalid pointer` at test shutdown (global destructor order — Symbol/StringTable cleanup). All tests pass before this point.

## Files Changed

### Decomp source (`src/`)
- `src/system/rndobj/Text.cpp` — Added `FitTextJust()` implementation
- `src/system/rndobj/Text.h` — Added `StyleState::mActive` member
- `src/system/rndobj/Rnd.cpp` — Restored `extern` declarations for BSS labels
- `src/system/utl/JobMgr.cpp` — Added native stub for `PostPurchaseEnumJob::OnCompletion`
- `src/system/math/Mtx.h` — Forward declaration for `Multiply(Vector3, Matrix3, Vector3)`
- `src/lazer/meta_ham/HamStoreProvider.cpp` — Fixed iterator type for GCC compatibility

### Native port (`native/`)
- `native/src/platform/System_Native.cpp` — Call `InitMakeString()` before `NativeDetectDataDir()`
- `native/src/xdk_shims.cpp` — Added `RtlDeleteCriticalSection()`
- `native/src/engine_stubs_generated.cpp` — Fixed `lbl_82F14008` from function to variable, added `lbl_830A4100`/`lbl_830A4104`

### Docs
- `docs/native/DECOMP_GAPS.md` — Updated text rendering, locale, and next steps sections
- `archive/screenshots/choose_mode_screen_f800.png` — Screenshot proof of working text
