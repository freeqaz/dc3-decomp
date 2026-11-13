# Lip Sync System — Native Port Plan

**Goal**: Characters' mouths move during songs and dialogue, matching viseme
animations from the original game data.

## Background

Dance Central 3 has a full lip sync pipeline built into the Milo engine. During
gameplay, characters lip-sync to vocal tracks using pre-authored viseme weight
data sampled at 30 Hz. The system blends between facial animation clips (one per
viseme shape) on face bones like jaw, lips, tongue, and cheeks.

The native port viewer currently has **zero lip sync support** — face bones stay
in their rest pose.

## Engine Architecture

Three classes form the lip sync pipeline:

### CharLipSync (data container)
- Stores viseme weight keyframes sampled at 30 Hz
- Each frame has weights for ~12-15 named visemes (e.g. "AA", "EE", "OO", "FV", "MBP")
- Loaded from `.lipsync` files (binary) or parsed from DTA arrays
- `CharLipSync::FindLipSyncForSound()` — static lookup that maps a playing `Sound` to its lip sync data via `sLipSyncMap`
- Duration: `(mFrames - 1) / 30.0f` seconds

### CharLipSyncDriver (animation driver)
- A `CharPollable` — polled each frame in the character animation pipeline
- Takes a `CharLipSync` + a viseme clip dir (`mClips`) containing one `CharClip` per viseme
- On `Poll()`:
  1. Gets current song time from `TheTaskMgr`
  2. Samples `CharLipSync::PlayBack` at current frame (30 Hz interpolated)
  3. For each active viseme, calls `ScaleAdd()` on the corresponding `CharClip` with the viseme weight
  4. Result: face bones are blended between viseme poses based on audio timing
- Also handles:
  - Blink generation (`ApplyBlinks()` via `CharFaceServo`)
  - Override clips (procedural face expressions layered on top)
  - Blend in/out for smooth transitions between lip sync tracks
  - Song owner sharing (one lip sync driver can reference another's playback)

### CharFaceServo (face bone mesh manager)
- A `CharBonesMeshes` subclass specialized for facial blending
- Maps viseme clips to face bone transforms
- Handles the `Base` clip (neutral face) and `Blink` clips (eye closure)
- `Poll()` calls `PoseMeshes()` to write blended face bone transforms to mesh objects
- Procedural blink weight system for runtime eye blinks

## Data Files

| File | Location | Contents |
|------|----------|----------|
| `viseme.milo_xbox` | `char/main/dancer/<name>/gen/` | Per-character viseme clip dir — contains ~15 CharClip objects, one per viseme shape |
| `viseme_resource.milo_xbox` | `char/shared/gen/` | Shared viseme resources |
| `lipsynchelper.milo_xbox` | `sfx/gen/` | Lip sync helper utilities |
| `.lipsync` files | In `.ark` archives, per-song | Pre-authored viseme weight keyframes for each vocal track |

## Implementation Plan

### Phase 1: Viseme Clip Loading (Viewer) — DONE

Load the viseme clip directory alongside the outfit and dance clips.

```
milo-viewer aubrey02.milo_xbox \
  --clips clips.milo_xbox \
  --visemes viseme.milo_xbox
```

- `--visemes <path>` CLI flag added to viewer
- Loads viseme dir via `ObjDirPtr::LoadFile()`
- Creates `CharFaceServo` on the Character, wired to the viseme clip dir
- `SetClips()` finds "Base" and "Blink" clips automatically
- `SetClipType()` populates bones via `CharBoneDir`, with fallback to stuff from clips
- Face servo polled each frame after body animation

**Deliverable**: Face has a neutral "Base" clip applied (jaw closed, default expression).

### Phase 2: Procedural Blink — DONE

- Simple blink timer: random 2-6s interval, 0.15s blink duration
- Triangle pulse weight: ramp up first half, ramp down second half
- `SetProceduralBlinkWeight()` + `ApplyProceduralWeights()` called before `Poll()`
- Works in both `advanceCharAnim` and direct-pose paths

**Deliverable**: Character blinks naturally during idle and dance.

### Phase 3: Viseme Playback from Pre-authored Data

For full lip sync during songs:

- Load `.lipsync` data from the `.ark` archives (requires song-specific data path)
- Create a `CharLipSyncDriver`, set its `mClips` to the viseme dir, assign the `CharLipSync` data
- On each frame, call `CharLipSyncDriver::Poll()` which:
  - Samples viseme weights at current song time
  - ScaleAdds each active viseme clip to the face servo's bone buffer
  - Face servo's `PoseMeshes()` writes the blended result to face mesh bones

**Deliverable**: Character lip-syncs to the vocal track during song playback.

### Phase 4: Full Game Integration

When the full native game engine runs songs:
- `HamCharacter` creates `CharLipSyncDriver` automatically
- Song system calls `CharLipSync::FindLipSyncForSound()` to bind lip sync data to playing audio
- `CharLipSyncDriver::Poll()` runs in the normal CharPollable pipeline
- No viewer-specific code needed — it's all engine infrastructure

## Viseme List (Typical DC3)

Based on the Milo engine's standard viseme set:

| Viseme | Mouth Shape | Example |
|--------|-------------|---------|
| Base | Neutral/closed | Rest position |
| AA | Open jaw | "father" |
| AE | Wide open | "cat" |
| AH | Relaxed open | "but" |
| AO | Rounded open | "dog" |
| EE | Wide smile | "see" |
| EH | Slight open | "bed" |
| IH | Slight smile | "sit" |
| OO | Pursed lips | "food" |
| UH | Slight round | "book" |
| FV | Lower lip tuck | "five" |
| MBP | Lips closed | "map" |
| SZ | Teeth together | "size" |
| TH | Tongue tip | "think" |
| Blink | Eyes closed | Procedural |

## Complexity Estimate

| Phase | Effort | Dependencies |
|-------|--------|-------------|
| Phase 1: Viseme loading | Low | Viewer `--visemes` flag, CharFaceServo setup |
| Phase 2: Procedural blink | Low | Phase 1, simple timer |
| Phase 3: Lip sync playback | Medium | Phase 1, `.lipsync` file loading from ark, song time sync |
| Phase 4: Full game | None | Already implemented in engine code — just needs the full game loop |

## Key Files

- `src/system/char/CharLipSync.h/.cpp` — Viseme weight data container
- `src/system/char/CharLipSyncDriver.h/.cpp` — Animation driver (Poll, playback, blending)
- `src/system/char/CharFaceServo.h/.cpp` — Face bone mesh manager
- `src/system/char/CharBonesMeshes.h/.cpp` — Parent class of CharFaceServo (bone→mesh transform pipeline)
- `src/system/char/CharEyes.h/.cpp` — Eye system (interacts with blink weights via CharFaceServo)
- `native/src/viewer/milo_viewer.cpp` — Viewer integration point

## Decomp Status (validated 2026-03-04)

All source files exist in `src/system/char/`. Match percentages verified via objdiff.

### CharFaceServo — 100% complete (no workable functions remain)

All 27 functions are COMPLETE or AT_LIMIT. The sole AT_LIMIT is `Load` at 99.5%.
Key functions all matching: `Poll`, `SetClips`, `ScaleAdd`, `ApplyProceduralWeights`,
`SetBlinkClipLeft/Right`, `SetClipType`, `Enter`, `ReallocateInternal`, `TryScaleDown`.
**No decomp work needed for the native port.**

### CharBonesMeshes — 100% complete (1 workable template, ignorable)

33/36 COMPLETE, 3 AT_LIMIT (`AcquirePose` 99.8%, `PoseMeshes` 99.1%, `insert` 96.6%).
1 unchecked template function (`_M_fill_insert_aux` at 37.3%) — STL internals, not decomp-relevant.
**No decomp work needed for the native port.**

### CharLipSyncDriver — 97% complete (1 workable function)

36/41 COMPLETE, 4 AT_LIMIT, 1 workable.

| Function | Match | Status |
|----------|-------|--------|
| `SetLipSync` | 73.5% | **Workable** — sets lip sync data + clips on driver |
| `Poll` | 90.7% | AT_LIMIT — main per-frame driver loop |
| `UpdatePlayback` | 94.1% | AT_LIMIT — playback time advancement |
| `ScaleAddViseme` | 99.8% | AT_LIMIT — applies weighted viseme clip |
| `Load` | 99.6% | AT_LIMIT |

Key complete functions: `ApplyBlinks` (100%), `Enter` (100%), `SetClips` (100%),
`ClearLipSync` (100%), `Sync` (100%), `BlendInOverrides/BlendOutOverrides` (100%),
`BlendInOverrideClip` (100%), `ResetOverrideBlend` (100%).

**1 function could be improved** (`SetLipSync` at 73.5%). The AT_LIMIT functions are
already high-match and functionally correct (unicorn_equivalent).

### CharLipSync — 88% complete (6 workable functions)

36/46 COMPLETE, 4 AT_LIMIT, 6 workable.

| Function | Match | Status |
|----------|-------|--------|
| `Generator::NextFrame` | 87.5% | **Workable** — advances generator to next viseme frame |
| `PlayBack::Poll` | 85.5% | **Workable** — samples viseme weights at current time |
| `Print` | 62.9% | **Workable** — debug printing (low priority for native port) |
| `PlayBack::Set` | 58.7% | **Workable** — initializes playback with lip sync data + clips |
| `Generator::RemoveViseme` | 43.1% | **Workable** — removes a viseme from generator state |
| `stlpmtx_std::fill<_Bit_iter>` | 97.1% | Workable — STL template, low priority |

Key complete functions: `FindLipSyncForSound` (100%), `Init/Terminate` (100%),
`RegisterLipSync/UnregisterLipSync` (100%), `Parse/OnParse/OnParseArray` (100%),
`Load` (99.6% AT_LIMIT), `PlayBack::Reset` (100%), `PlayBack::SetClips` (96.3% AT_LIMIT),
`Generator::Init` (100%), `Generator::AddWeight` (100%), `Generator::Finish` (96.6% AT_LIMIT).

**Priority workable functions for native port**: `PlayBack::Poll` (85.5%) and
`PlayBack::Set` (58.7%) — both needed for runtime viseme sampling.

### CharEyes — 85% complete (8 workable functions)

~78/94 COMPLETE, ~8 AT_LIMIT, 8 workable.

| Function | Match | Status |
|----------|-------|--------|
| `ObjVector::push_back` | 87.5% | Workable — STL template |
| `vector::_M_erase` (single) | 78.6% | Workable — STL template |
| `EyeDesc::operator=` | 76.7% | **Workable** |
| `vector::_M_erase` (range) | 73.1% | Workable — STL template |
| `Enter` | 69.7% | **Workable** — eye system initialization |
| `vector::_M_fill_insert_aux` | 67.9% | Workable — STL template |
| `vector::operator=` | 57.3% | Workable — STL template |
| `SetFocusInterest` | 46.7% | **Workable** — sets eye focus target |

Key complete functions: `ProceduralBlinkUpdate` (100%), `SetEnableBlinks` (100%),
`ForceBlink` (100%), `GetHead` (100%), `GetCurrentInterest` (100%),
`Poll` (94.7% AT_LIMIT), `Copy` (100%), `Exit` (100%).

CharEyes is relevant to Phase 2 (procedural blink) since `CharLipSyncDriver::ApplyBlinks()`
delegates to `CharFaceServo`, which CharEyes also uses for blink weights. The blink
system in CharEyes is fully decomped (`ProceduralBlinkUpdate`, `SetEnableBlinks`, etc.).

## Call Graph — Functions Needed by Phase

### Phase 1 (Viseme Loading) — All complete
```
CharFaceServo::SetClips()          → 100% COMPLETE
CharFaceServo::Enter()             → 100% COMPLETE
CharFaceServo::Poll()              → 100% COMPLETE
CharBonesMeshes::PoseMeshes()      → 99.1% AT_LIMIT
CharFaceServo::ReallocateInternal()→ 100% COMPLETE
```

### Phase 2 (Procedural Blink) — All complete
```
CharFaceServo::ApplyProceduralWeights() → 100% COMPLETE
CharFaceServo::SetBlinkClipLeft/Right() → 100% COMPLETE
CharFaceServo::BlinkWeightLeft()        → 100% COMPLETE
CharEyes::ProceduralBlinkUpdate()       → 100% COMPLETE
CharEyes::SetEnableBlinks()             → 100% COMPLETE
```

### Phase 3 (Lip Sync Playback) — Mostly complete, some workable
```
CharLipSyncDriver::Poll()              → 90.7% AT_LIMIT
CharLipSyncDriver::UpdatePlayback()    → 94.1% AT_LIMIT
CharLipSyncDriver::ScaleAddViseme()    → 99.8% AT_LIMIT
CharLipSyncDriver::ApplyBlinks()       → 100% COMPLETE
CharLipSyncDriver::SetLipSync()        → 73.5% WORKABLE ← could improve
CharLipSync::PlayBack::Poll()          → 85.5% WORKABLE ← could improve
CharLipSync::PlayBack::Set()           → 58.7% WORKABLE ← could improve
CharLipSync::FindLipSyncForSound()     → 100% COMPLETE
CharLipSync::PlayBack::Reset()         → 100% COMPLETE
```

### Phase 4 (Full Game) — No additional work
All infrastructure functions (Init, Terminate, Load, Save, Handle, Copy,
SyncProperty) are COMPLETE or AT_LIMIT across all units.

## Summary

The lip sync pipeline is **well decomped**. Phases 1-2 require zero decomp work —
all functions in the CharFaceServo and CharBonesMeshes call paths are complete.
Phase 3 has a few workable functions (`PlayBack::Poll`, `PlayBack::Set`, `SetLipSync`)
but the AT_LIMIT functions at 90%+ are functionally correct and will work for the
native port. The main native port integration work is in `milo_viewer.cpp`, not decomp.
