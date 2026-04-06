# SkeletonHistory Pipeline for Native Port -- Design Document

**Date**: 2026-03-31
**Status**: Research complete, implementation pending

## Problem Statement

FreestyleMoveRecorder's displacement-based scoring always returns 0 on native because:

1. `SkeletonUpdate::sInstance` is never created -- creation goes through `LiveCameraInput::PreInit()` (line 553 of `src/system/gesture/LiveCameraInput.cpp`), which is Xbox-only
2. Without sInstance, `SkeletonUpdate::InstanceHandle()` returns a handle wrapping `nullptr`
3. `SkeletonUpdateHandle::History()` returns `nullptr` when `mInst == nullptr`
4. `Skeleton::PrevTrackedSkeleton()` asserts `history != nullptr` (line 273 of `src/system/gesture/Skeleton.cpp`) and would crash if the null check weren't guarded
5. `Skeleton::Displacement()` returns false (zero displacement), so `CompareSkeletonJointDisplacement` gets no displacement data and scoring falls through to position-only comparison

Additionally, `GestureMgr_NativePoll()` passes `data.mHistory = nullptr` (line 170 of `native/src/platform/GestureMgr_Native.cpp`), so even SkeletonCallbacks that receive SkeletonUpdateData never get valid history.

## Architecture Overview

### Xbox Pipeline (original)

```
LiveCameraInput::PreInit()
  -> creates LiveCameraInput::sInstance
  -> calls SkeletonUpdate::CreateInstance()
     -> creates SkeletonUpdate (inherits SkeletonHistoryArchive + SkeletonHistory + Hmx::Object)
     -> starts SkeletonUpdateThread on CPU core 5

SkeletonUpdateThread (runs on dedicated core):
  -> calls SkeletonUpdate::Update()
     -> NuiSkeletonGetNextFrame() -- reads Kinect hardware
     -> SkeletonUpdate::UpdateCallbacks()
        -> for each tracked skeleton[i]: AddToHistory(i, skeleton[i])  // <-- HISTORY POPULATION
        -> each skeleton.Poll(i, skeletonFrame) -- updates joint positions
        -> creates SkeletonUpdateData { .mHistory = this (SkeletonUpdate*) }
        -> calls each SkeletonCallback::Update(data)

Main thread (SkeletonUpdate::PostUpdate()):
  -> waits for SkeletonUpdateThread via sSkeletonUpdatedEvent
  -> creates SkeletonUpdateData { .mHistory = this }
  -> calls each SkeletonCallback::PostUpdate(data)
```

### Native Pipeline (current)

```
GestureMgr::Init()
  -> early returns to GestureMgr_NativeInit()
  -> skips LiveCameraInput::PreInit() entirely
  -> no SkeletonUpdate::CreateInstance() ever called

GestureMgr::Poll()
  -> GestureMgr_NativePoll()
     -> reads skeleton from pose server or dummy
     -> fills GestureMgr::mSkeletons[] directly
     -> creates SkeletonUpdateData { .mHistory = nullptr }  // <-- NULL HISTORY
     -> calls mgr->PostUpdate(&data)
```

### Key Data Structures

**SkeletonHistoryArchive** (base of SkeletonUpdate):
- `mHistories[6]` -- one `vector<ArchiveSkeleton>` per skeleton slot, each reserved to 160 entries
- `AddToHistory(idx, skeleton)` -- snapshots skeleton's camera-space joint positions into front of ring buffer
- `ClearHistory(idx)` -- clears when skeleton stops tracking

**SkeletonHistory** (abstract interface):
- `PrevSkeleton(skeleton, targetMs, out archiveSkeleton, out elapsedMs)` -- walks history backwards from current frame, accumulating ElapsedMs until targetMs is reached
- Used by `Skeleton::Displacement()` to compute joint displacement vectors over time

**ArchiveSkeleton** -- snapshot of a Skeleton's camera-space joint positions + confidence + elapsed time. Created via `ArchiveSkeleton::Set(const Skeleton&)`.

**MocapSkeletonIterator** -- precedent for standalone SkeletonHistory without SkeletonUpdate. Inherits both SkeletonHistoryArchive and SkeletonHistory directly. Used for offline mocap analysis.

## Proposed Design: NativeSkeletonHistory

The cleanest approach is to create a lightweight native-only SkeletonHistory that lives alongside `GestureMgr_NativePoll()`, following the MocapSkeletonIterator pattern rather than trying to instantiate the full SkeletonUpdate (which has deep Xbox dependencies: NUI_SKELETON_FRAME allocation, dedicated thread, HANDLE-based events, LiveCameraInput coupling).

### Option A: Standalone NativeSkeletonHistory (Recommended)

Create a new class in `native/src/platform/GestureMgr_Native.cpp`:

```cpp
class NativeSkeletonHistory : public SkeletonHistoryArchive, public SkeletonHistory {
public:
    bool PrevSkeleton(const Skeleton &s, int targetMs,
                      ArchiveSkeleton &out, int &elapsedMs) const override {
        return PrevFromArchive(*this, s, targetMs, out, elapsedMs);
    }
};

static NativeSkeletonHistory *sNativeHistory = nullptr;
```

**Changes to GestureMgr_NativeInit()** (line ~26):
- Create `sNativeHistory = new NativeSkeletonHistory()` after creating NativeCameraInput

**Changes to GestureMgr_NativePoll()** (lines 160-173):
- After filling skeletons from pose server, before building SkeletonUpdateData:
  ```cpp
  // Populate skeleton history (mirrors SkeletonUpdate::UpdateCallbacks lines 348-355)
  for (int i = 0; i < NUM_SKELETONS; i++) {
      Skeleton &skel = mgr->GetSkeleton(i);
      if (skel.IsTracked()) {
          sNativeHistory->AddToHistory(i, skel);
      } else {
          sNativeHistory->ClearHistory(i);
      }
  }
  ```
- Change `data.mHistory = nullptr` to `data.mHistory = sNativeHistory`

**Changes to GestureMgr_NativeTerminate()** (line ~90):
- `delete sNativeHistory; sNativeHistory = nullptr;`

### Option B: Instantiate SkeletonUpdate on Native (Not Recommended)

Would require:
- Providing a NativeCameraInput where `LiveCameraInput::sInstance` is expected (SkeletonUpdate constructor calls `SetCameraInput(LiveCameraInput::sInstance)` at line 123)
- Stubbing `NUI_SKELETON_FRAME` allocation (already done with `#ifndef HX_NATIVE` guards)
- Stubbing the update thread (already guarded at line 133)
- Handling the destructor's `SetEvent`/`WaitForSingleObject`/`CloseHandle` chain (lines 141-146) which assumes the thread handle exists

This is fragile because SkeletonUpdate was designed for Xbox's threading model. The existing `#ifdef HX_NATIVE` guards in SkeletonUpdateHandle only handle the "sInstance is null" case -- they don't make SkeletonUpdate itself work on native.

### Option C: Make FreestyleMoveRecorder Work Without History

Could rewrite `CompareSkeletonJointDisplacement` to fall back to position-only scoring when history is null. But this would degrade scoring quality -- displacement vectors are the primary scoring signal (the code takes `max(dispContrib, posContrib)` at line 791, and displacement usually wins because it captures motion direction).

## FreestyleMoveRecorder Native Stub Situation

Currently, FreestyleMoveRecorder.cpp has a large `#ifdef HX_NATIVE` block (lines 26-55) that stubs **every** method to no-ops/return-0. This means even with SkeletonHistory working, the scoring code won't run until these stubs are replaced with the real implementations.

The stubbing comment says: "Kinect gesture recording -- entirely PPC-specific (__fsel intrinsics, STLport)."

This is **incorrect** for the scoring functions. Analysis:
- `CompareDisplacementVectors` -- uses `__fsel` (available via `web_stubs.cpp` on native), pure math
- `CompareSkeletonJointDisplacement` -- uses `SkeletonUpdate::InstanceHandle()` + `History()`, pure math otherwise
- `CalcFrameScore` -- uses `TheOSCMessenger`, `__fsel`, `pow`, pure math
- `CompareSkeletonPositions` -- uses `NormPos`, `__fsel`, pure math
- `GetScore(BaseSkeleton*, ...)` -- uses CalcFrameScore, pure control flow
- `GetScore(int, ...)` -- wraps the above with skeleton lookup

The functions that truly need Kinect:
- `Poll()` -- accesses `LiveCameraInput::mDepthPolled`, `PollNewStream(kBufferDepth)`, `GetStreamTex(kBufferDepth)`, raw depth buffer texel operations. This is the **only** function with real hardware dependency.
- `StartRecording/StopRecording` -- touch depth frame allocation

## Required Changes Summary

### Phase 1: SkeletonHistory Pipeline (Minimum viable)

| File | Change | Lines |
|------|--------|-------|
| `native/src/platform/GestureMgr_Native.cpp` | Add `NativeSkeletonHistory` class (8 lines) | New code near top |
| `native/src/platform/GestureMgr_Native.cpp` | Create/destroy in Init/Terminate | Lines 26-28, 90-104 |
| `native/src/platform/GestureMgr_Native.cpp` | Populate history in NativePoll, pass to SkeletonUpdateData | Lines 146-173 |

Estimated: ~25 lines of new code.

### Phase 2: Un-stub FreestyleMoveRecorder Scoring

| File | Change | Lines |
|------|--------|-------|
| `src/system/hamobj/FreestyleMoveRecorder.cpp` | Move scoring functions out of `#ifdef HX_NATIVE` stub block | Lines 26-55 |

Functions to un-stub (move to shared code):
- `CompareDisplacementVectors` (line 579)
- `CompareSkeletonJointDisplacement` (line 675)
- `CompareSkeletonPositions` (line 636)
- `CalcFrameScore` (line 725)
- `GetScore(BaseSkeleton*, ...)` (line 823)
- `GetScore(int, ...)` (line 883)
- `UpdateRecordingAttempt` (line 442) -- needed by GetScore
- `GetLiveSkeleton` (line 407) -- needed by GetScore(int)

Functions to keep stubbed (Kinect depth buffer dependency):
- `Poll()` -- depth buffer access
- `StartRecording/StopRecording/StartRecordingDancerTake` -- depth frame init
- `DrawDebug` -- uses SkeletonViz with CameraInput

Functions that are safe to un-stub trivially:
- `Free()` -- just resets state
- `ClearRecording()` -- just clears take
- `StartPlayback/StopPlayback` -- just set playback state
- `ClearDancerTake` -- just resets counter
- `ClearFrameScores` -- just clears scores
- `PlaybackComplete` -- conditional write

The constructor also needs un-stubbing to properly initialize `mAngleLimits`, `mTrackedJoints`, `mPositions`, and allocate `mFrameBuffer`.

### Phase 3: Split Poll() (Optional)

Split `FreestyleMoveRecorder::Poll()` into:
- `PollDepthCapture()` -- the depth buffer recording (Xbox-only)
- `PollTiming()` -- the `mRecordPos`/`mPlaybackPos` advancement + `UpdateFakeSkeleton()` (portable)

This would let native record/playback the skeleton pose data without depth buffers.

## Dependencies

- `__fsel` intrinsic: Available on native via `native/src/web_stubs.cpp` (line 119)
- `TheOSCMessenger`: Used in CalcFrameScore for tuning parameters. Needs to exist on native (check if it's stubbed or real).
- `DancerSkeleton`: Pure data class, already compiles on native.
- `FreestyleMoveFrame`: Already has `#ifdef HX_NATIVE` for operator new (line 10 of `FreestyleMove.h`).
- `SkeletonUpdateHandle::History()`: Already handles null mInst (returns nullptr). With Phase 1, sInstance remains null but CompareSkeletonJointDisplacement calls `InstanceHandle()` directly (line 704). This needs adjustment -- see Risk Assessment.

## Risk Assessment

### Risk 1: CompareSkeletonJointDisplacement's InstanceHandle usage (MEDIUM)

At line 704 of FreestyleMoveRecorder.cpp, `CompareSkeletonJointDisplacement` creates its own `SkeletonUpdateHandle` to get `History()`. This bypasses the SkeletonUpdateData pipeline entirely:

```cpp
SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
const SkeletonHistory *history = handle.History();
```

With Option A (NativeSkeletonHistory), `SkeletonUpdate::sInstance` is still null, so `History()` returns nullptr, and `Displacement()` still fails.

**Mitigation**: Either:
1. Modify `CompareSkeletonJointDisplacement` to accept a `const SkeletonHistory*` parameter instead of fetching it internally (requires changing the call in CalcFrameScore too)
2. Store the NativeSkeletonHistory pointer somewhere accessible via SkeletonUpdateHandle (e.g., a static on SkeletonUpdate for native)
3. Use Option B and actually instantiate SkeletonUpdate on native (higher risk)

Option (1) is cleanest. `CompareSkeletonJointDisplacement` is only called from `CalcFrameScore`, which is only called from `GetScore`, so the refactor surface is small. But this changes the decomp source's function signatures.

Alternative: Add a `static SkeletonHistory* sNativeHistory` to SkeletonUpdate and return it from `InstanceHandle().History()` when `sInstance == nullptr`. This avoids signature changes.

### Risk 2: ArchiveSkeleton copies TrackedJoint data (LOW)

`ArchiveSkeleton::Set()` reads `skeleton.TrackedJoints()[i].mJointPos[kCoordCamera]`. The native skeleton provider fills camera-space positions in `Skeleton::Poll()` which transforms raw positions through the skeleton frame's gravity matrix. The dummy/pose skeletons currently skip this transform since there's no NUI_SKELETON_FRAME -- they write directly to raw positions.

Need to verify that `Skeleton::Poll()` on native properly populates `mTrackedJoints[].mJointPos[kCoordCamera]` from the raw positions that NativeSkeletonProvider fills in.

### Risk 3: Memory budget for history (LOW)

Each ArchiveSkeleton is ~0x1A0 bytes. 6 slots x 160 entries = 960 ArchiveSkeletons = ~250KB. This is fine for native's memory model.

### Risk 4: ElapsedMs accuracy (LOW)

`ArchiveSkeleton::Set()` captures `skeleton.ElapsedMs()`, which comes from `SkeletonFrame::mElapsedMs` (the delta between NUI frames). On native, this is currently 0 because no SkeletonFrame is populated. `PrevFromArchive` walks history by accumulating ElapsedMs to find the frame at targetMs offset.

**Mitigation**: In `GestureMgr_NativePoll`, compute elapsed ms from delta time (`TheTaskMgr.DeltaUISeconds() * 1000.0f`) and set it on the skeleton before calling `AddToHistory`.

### Risk 5: Decomp source modification (LOW)

Option A only adds `#ifdef HX_NATIVE` code to the native platform file. No decomp source changes needed for Phase 1.

Phase 2 restructures the `#ifdef` blocks in FreestyleMoveRecorder.cpp but doesn't change the PPC codegen (the real implementations stay identical, just the stub block gets smaller).

## Recommendation

Implement Option A (NativeSkeletonHistory) with the Risk 1 mitigation of adding a static `SkeletonHistory*` fallback to SkeletonUpdate for native. This gives us:

1. Zero changes to decomp-critical code paths
2. History population identical to Xbox's `UpdateCallbacks()` (lines 348-355 of SkeletonUpdate.cpp)
3. Clean separation -- native history lives in the native platform file
4. Path to Phase 2 (un-stubbing scoring) without touching Poll()'s depth buffer code

Total estimated effort: ~40 lines for Phase 1, ~30 lines of `#ifdef` restructuring for Phase 2.
