# Session 59: Game Screen Venue Rendering

**Date**: 2026-03-12
**Goal**: Navigate from main menu into a song and render the 3D venue on game_screen
**Result**: SUCCESS — venue geometry, fully-lit character, and HUD elements all render. 505 draw calls/frame on game_screen, stable through 10000 frames with no crashes. Character lighting fixed (zero-color LightPreset detection). Scene is static (animation blocked by unimplemented LightPreset::Load and song.anim DTA script crashes).

## Milestone

First time the native port has rendered a 3D venue during gameplay. The full pipeline works:
- Menu navigation (main_screen -> choose_mode -> song_select -> YMCA -> multiuser -> loading -> game_screen)
- Venue .milo loading (world/dci/dci.milo merges into world dir)
- Character mesh loading with full textures and lighting (skin, hair, outfit visible)
- HUD overlay (pink move card rectangles — TexMovie render targets not yet written to)
- 357 mesh draw calls per frame on game_screen, 99.6% non-black pixel coverage

Screenshots show the DCI venue with floor, walls, DJ booth, lighting rigs, stage circles, and center-stage character (Angel with blue hair and outfit, fully lit).

## Challenges and Fixes

### 1. ObjRef Ring Corruption During Merge (SIGSEGV at offset 0x28)

**Problem**: When `FileMerger::FinishLoading` calls `MergeDirs()`, the merge walks ObjRef rings to redirect references from source objects to destination objects. Some rings are corrupt — null links or infinite loops — causing SIGSEGV during `ObjRef::ReplaceList()`. The crash address 0x28 is the offset of `mTypeDef` in `Hmx::Object`, indicating a null object dereference.

This affects the crowd_clips and audio_merger merges that happen AFTER the visual venue merge completes.

**Root cause**: Not fully diagnosed. The ObjRef ring is a circular doubly-linked list tracking all references to an object. During complex multi-file merges (venue + crowd + audio + song all merging into the same world dir), some rings get corrupted — likely due to objects being destroyed mid-walk or LP64 pointer issues in the ref tracking.

**Fix (two layers)**:

*Layer 1 — Ring validation in `Object::ReplaceRefs()`* (`src/system/obj/Object.cpp`):
Before walking the ring, probe it with a bounded loop checking for null links and infinite loops. Skip corrupt rings with a warning instead of crashing.

```cpp
#ifdef HX_NATIVE
ObjRef *probe = mRefs.next;
int count = 0;
while (probe != &mRefs && probe != nullptr && count < 100000) {
    if (!probe->next || !probe->prev) {
        fprintf(stderr, "DC3 Native: ReplaceRefs skipping corrupt ring for '%s'\n", Name());
        return;
    }
    probe = probe->next;
    count++;
}
if (count >= 100000 || probe == nullptr) {
    fprintf(stderr, "DC3 Native: ReplaceRefs skipping corrupt/infinite ring for '%s'\n", Name());
    return;
}
#endif
```

*Layer 2 — Signal recovery in `FileMerger::FinishLoading()`* (`src/system/char/FileMerger.cpp`):
Wraps the entire `MergeDirs()` call in a `sigsetjmp`/`siglongjmp` recovery block. If a SIGSEGV occurs during merge, the signal handler jumps back, skips the failed merge, calls `PostMerge()`, and continues. This catches any crash that slips past the ring validation.

```cpp
#ifdef HX_NATIVE
static sigjmp_buf sMergeRecovery;
static volatile sig_atomic_t sMergeGuardActive = 0;
static void MergeGuardHandler(int sig, siginfo_t *info, void *ctx) {
    if (sMergeGuardActive) {
        sMergeGuardActive = 0;
        siglongjmp(sMergeRecovery, sig);
    }
    struct sigaction sa = {};
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, nullptr);
    raise(sig);
}
#endif
```

**Why this is a hack**: `siglongjmp` from a signal handler is technically undefined behavior for SIGSEGV (the faulting instruction is re-executed). It works in practice because we jump to a completely different execution path. The proper fix would be to find and fix the root cause of the ring corruption, but that's deep in the object lifecycle system and would require extensive debugging of the merge pipeline.

### 2. Missing Function Implementations

Several functions were called during the venue loading / game_screen pipeline that were previously stubbed:

| Function | File | What It Does | Fix |
|----------|------|-------------|-----|
| `RndShadowMap::PrepShadow()` | ShadowMap.cpp | Prepares shadow map for character self-shadowing | Full implementation: finds floor-spot or directional light, computes light-space frustum around drawable's bounding sphere, renders shadow depth pass, sets shadow map on renderer |
| `RndFlare::CalcRect()` | Flare.cpp | Computes screen-space rectangle for lens flare rendering | Full implementation from Ghidra decompilation: accounts for HiResScreen tiling, aspect ratio, CalcScale, visible area clipping |
| `SpotlightDrawer::RemoveFromLists()` | SpotlightDrawer.cpp | Removes spotlight from static/dynamic entry lists when destroyed | Implementation using iterator erase pattern |
| `RndTexBlendController::GetBlendState()` | TexBlendController.cpp | Computes texture blend factor based on camera distance | Implementation with min/max distance interpolation |
| `AreDancersColliding1D()` | SongCollision.cpp | Already had an implementation at line 87 — was mistakenly treated as forward-declaration-only | Removed duplicate, kept existing implementation |

### 3. Signal Handler Persistence (`SA_RESETHAND`)

**Problem**: The main signal handler in `main_native.cpp` was installed with `SA_RESETHAND`, which causes the handler to be reset to `SIG_DFL` after the first signal. When the merge recovery handler caught a SIGSEGV and returned, subsequent SIGSEGVs had no handler and killed the process.

**Fix**: Removed `SA_RESETHAND` from `sa.sa_flags` so the handler persists across multiple signals.

### 4. Player State for Song Loading

**Problem**: The venue loading pipeline in `HamDirector::OnFileLoaded` requires valid player data — character outfit, crew, presence flags — to be set before the song merge starts. On Xbox, the multiuser_screen Kinect skeleton chooser sets all this. On native, those screens are auto-skipped.

**Fix**: `MultiUserGesturePanel::Poll()` (the auto-skip path) now:
- Sets `player_present` on player 0's provider (controller-based single player)
- Clears player 1 (no second player)
- Calls `MetaPerformer::SetDefaultSongCharacter()` to assign default character/crew/outfit
- Sets default venue ("dci") if none selected

### 5. Input Script Navigation

The `MILO_INPUT_SCRIPT` system drives menu navigation headlessly:
```
700 confirm      # main_screen -> choose_mode
1000 confirm     # choose_mode -> song_select (perform mode)
2000 down        # scroll song list
2100 down
2200 down
2400 confirm     # select song -> multiuser -> loading -> game_screen
```

Button names map to JoypadAction enums via the native fallback in `Joypad_Native.cpp`.

### 6. Zero-Color LightPreset Lights (Character Dark Silhouette)

**Problem**: Character rendered as a dark silhouette despite having correct mesh geometry, materials, and textures. Venue elements on `Cam.cam` (HUD camera) looked correct but character on `world.cam` was dark.

**Root cause**: The DCI venue environment has 3 directional lights (`key_light`, `fill_light`, `rim_light`) with valid directions but ALL colors set to `(0.0, 0.0, 0.0)`. On Xbox, `LightPreset` animations (driven by DTA scripts during song start) animate these light colors to proper values. On native, the DTA song-start scripts don't fire, so the lights stay at their initial zero-color state.

The renderer's fallback lighting system (`Rnd_Wgpu.cpp`) has a three-tier approach:
1. Use environment directional lights if present
2. Add supplemental fill lights if fewer than 2
3. Fall back to full three-point lighting if none found

The problem was that zero-color lights with valid directions passed the "are there lights?" check (tier 1), preventing the fallback (tier 3) from activating. The character was lit by lights with zero intensity.

**Fix**: Added zero-color detection in `Rnd_Wgpu.cpp` before the fill/fallback logic:

```cpp
// Check if all directional lights have zero color (uninitialized LightPresets)
bool allZeroColor = true;
for (int li = 0; li < lightIdx; li++) {
    if (scene.lightColors[li][0] > 0.01f || scene.lightColors[li][1] > 0.01f || scene.lightColors[li][2] > 0.01f) {
        allZeroColor = false;
        break;
    }
}
if (allZeroColor) lightIdx = 0; // treat zero-color lights as no lights
```

This resets `lightIdx` to 0 when all directional lights have near-zero color, allowing the three-point fallback to activate. Character now renders fully textured with skin, hair, and outfit visible.

**Diagnosis method**: Used `MILO_CAPTURE_FRAME` frame capture to compare character vs venue mesh properties. Both had proper materials/textures. Added temporary lighting diagnostics dumping ambient, light dirs, and colors per camera. Discovered `world.cam` had zero-color lights while `Cam.cam` (HUD overlay) had fallback lights.

### 7. Pink Flashcard HUD Rectangles (Deferred)

**Problem**: Move card overlay elements render as pink rectangles instead of showing move icons.

**Root cause**: The `flashcard_default.mesh` elements on `Cam.cam` have `flashcard_default.mat` with uploaded textures, but those textures are `TexMovie` render-to-texture targets. The move icon rendering pipeline (which draws move icons into these textures at runtime) hasn't written to them yet. They need the `RndTexRenderer`/`TexMovie` render-to-texture pipeline to be functional on native.

**Status**: Deferred — requires render-to-texture system, which is a larger task.

## What's Rendering

The DCI venue (indoor dance club) renders with:
- Floor geometry with circle/ring decals
- Walls and DJ booth structure
- Lighting rig geometry (purple/pink stage lights)
- **Fully-lit character** (Angel with blue hair, skin tones, outfit — three-point fallback lighting)
- HUD overlay (pink rectangles = TexMovie render targets not yet written to)

### 8. Null Pointer Crashes on game_screen (Fixed)

Three null pointer crashes occurred during gameplay polling, all from missing game objects on native:

| Crash | Root Cause | Fix |
|-------|-----------|-----|
| **HamCharacter::SongAnimation** SIGSEGV | `Driver()->FirstClip()` returns null (character clips not loaded), then `c->Type()` dereferences null | `#ifdef HX_NATIVE if (!c) return -1;` guard |
| **HamCamShot::EndAnim** SIGSEGV | `dynamic_cast<Character*>(cacheIt->mTrans)` returns null (Character not loaded as expected type) | Null check before `theChar->SetEnv(nullptr)` |
| **PoseFatalities::Poll** SIGSEGV | `mPoseBeatAnims[side]` is null (pose beat animations not loaded) | `#ifdef HX_NATIVE` null check before `SetFrame()` |

### 9. Scene Animation Investigation (Deferred)

**Problem**: Venue and character are completely static — no animation, no camera movement, no lighting changes.

**Root causes identified**:
1. **LightPreset::Load is unimplemented** — only a weak stub in `engine_stubs_generated.cpp`. Factory IS registered in `WorldInit()`, but Load does nothing → 0 LightPreset objects deserialized from venue .milo (despite venue having 45 Environ + 58 Light objects).
2. **Song.anim DTA script cascades crash** — `SongAnim(0)` SetFrame triggers CamShot::SetFrame → DTA script execution → references missing game objects → SIGABRT. The song.anim approach is not viable without a full DTA runtime.
3. **SetupAnims() timing**: `HamDirector::Enter()` runs BEFORE venue .milo loads (`mVenue` is nil). Song anims are only populated when venue is available. Fixed by re-running `SetupAnims()` in Poll when `mVenue` becomes available.

**Status**: Deferred. Implementing `LightPreset::Load` (a full decomp function) would enable venue light presets. Song.anim driving requires DTA runtime.

## What's Not Rendering Correctly

- **Pink HUD rectangles**: Move card geometry renders but textures are TexMovie render-to-texture targets. Requires render-to-texture pipeline (deferred).
- **No crowd**: Crowd characters aren't visible (crowd_clips merge skipped by siglongjmp recovery handler).
- **No post-processing**: Bloom, color correction, venue lighting effects are stubbed.
- **Static scene**: No animation — venue and character are frozen in their initial pose. Root causes: LightPreset::Load unimplemented (0 presets deserialized), song.anim DTA scripts crash on missing objects.

## Files Modified

| File | Change | Category |
|------|--------|----------|
| `src/system/obj/Object.cpp` | ObjRef ring validation before ReplaceRefs walk | Safety |
| `src/system/char/FileMerger.cpp` | sigsetjmp/siglongjmp SIGSEGV recovery around MergeDirs | Safety |
| `native/src/main_native.cpp` | Removed SA_RESETHAND from signal handler flags | Bug fix |
| `src/system/rndobj/ShadowMap.cpp` | Full PrepShadow implementation | Implementation |
| `src/system/rndobj/Flare.cpp` | Full CalcRect implementation | Implementation |
| `src/system/world/SpotlightDrawer.cpp` | RemoveFromLists implementation | Implementation |
| `src/system/rndobj/TexBlendController.cpp` | GetBlendState implementation | Implementation |
| `src/system/hamobj/SongCollision.cpp` | Decomp improvements (Equals, CheckCollision, IsCollision) | Decomp |
| `src/lazer/meta_ham/MultiUserGesturePanel.cpp` | Player state setup for native auto-skip path | Game flow |
| `native/src/platform/Rnd_Wgpu.cpp` | Zero-color LightPreset detection, fallback lighting activation | Rendering fix |
| `src/system/hamobj/HamCharacter.cpp` | Null guard on FirstClip() in SongAnimation() | Crash fix |
| `src/system/hamobj/HamCamShot.cpp` | Null guard on Character cast in EndAnim() | Crash fix |
| `src/system/hamobj/PoseFatalities.cpp` | Null guard on mPoseBeatAnims in Poll() | Crash fix |
| `src/system/hamobj/HamDirector.cpp` | SetupAnims re-init in Poll when venue loads; diagnostic cleanup | Game flow |

## Technical Debt

1. **siglongjmp recovery is a hack** — should find root cause of ObjRef ring corruption during multi-file merges
2. **Ring validation is O(n)** — probes up to 100k nodes before each ReplaceRefs; could be expensive for large objects
3. **fprintf diagnostics** — multiple `fprintf(stderr, ...)` statements throughout the merge pipeline should be removed or gated behind a debug env var once stable
4. **Zero-color light detection is a workaround** — proper fix would be running LightPreset animations on native (requires DTA song-start scripts)
5. **Crowd merge skipped** — the recovery handler catches and skips crowd_clips merges, meaning no crowd characters render
6. **TexMovie render-to-texture not functional** — HUD flashcard elements need the render-to-texture pipeline

## Metrics

| Metric | Value |
|--------|-------|
| Draw calls per frame | 505 (game_screen) |
| Non-black pixels | ~99.6% coverage |
| Frames stable | 10000 (clean exit) |
| Crashes | 0 (merge crashes recovered via siglongjmp, 3 null ptr crashes fixed) |
| Screenshots captured | frames 2800-6000+ (game_screen with venue + character) |
| New function implementations | 4 (PrepShadow, CalcRect, RemoveFromLists, GetBlendState) |
| Null pointer fixes | 3 (HamCharacter, HamCamShot, PoseFatalities) |
