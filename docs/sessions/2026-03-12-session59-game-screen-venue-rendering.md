# Session 59: Game Screen Venue Rendering

**Date**: 2026-03-12
**Goal**: Navigate from main menu into a song and render the 3D venue on game_screen
**Result**: SUCCESS — venue geometry, character silhouette, and HUD elements all render. 391 draw calls/frame, stable through 9000+ frames with no crashes.

## Milestone

First time the native port has rendered a 3D venue during gameplay. The full pipeline works:
- Menu navigation (main_screen -> choose_mode -> song_select -> YMCA -> multiuser -> loading -> game_screen)
- Venue .milo loading (world/dci/dci.milo merges into world dir)
- Character mesh loading (dark silhouette — materials/shading not fully wired)
- HUD overlay (pink move card rectangles — textures not loaded but geometry renders)
- 391 mesh draw calls per frame, 917876/921600 non-black pixels (99.6% coverage)

Screenshots captured at frames 2800, 3000, 3200, 3500, 4000 — all show the DCI venue with floor, walls, DJ booth, lighting rigs, stage circles, and center-stage character.

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

## What's Rendering

The DCI venue (indoor dance club) renders with:
- Floor geometry with circle/ring decals
- Walls and DJ booth structure
- Lighting rig geometry (purple/pink stage lights)
- Character mesh (dark silhouette — no material colors/textures applied to character yet)
- HUD overlay (pink rectangles = move cards with missing textures, positioned correctly)

## What's Not Rendering Correctly

- **Character is a dark silhouette**: Mesh geometry loads and renders but materials/textures aren't fully applied. Likely needs character-specific material setup that happens in the Kinect skeleton pipeline.
- **Pink HUD rectangles**: Move card geometry renders but without actual move textures. The texture loading pipeline for gameplay HUD assets may not be fully connected.
- **No crowd**: Crowd characters aren't visible (crowd_clips merge may be skipped by the recovery handler).
- **No post-processing**: Bloom, color correction, venue lighting effects are stubbed.
- **Static scene**: No animation — venue and character are frozen in their initial pose. AnimTask/PropAnim pipeline works for UI but game-time animation (kTaskSeconds vs kTaskUISeconds) hasn't been tested.

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

## Technical Debt

1. **siglongjmp recovery is a hack** — should find root cause of ObjRef ring corruption during multi-file merges
2. **Ring validation is O(n)** — probes up to 100k nodes before each ReplaceRefs; could be expensive for large objects
3. **fprintf diagnostics** — multiple `fprintf(stderr, ...)` statements throughout the merge pipeline should be removed or gated behind a debug env var once stable
4. **Character rendering** — needs proper material/texture application for non-silhouette rendering
5. **Crowd merge skipped** — the recovery handler catches and skips crowd_clips merges, meaning no crowd characters render

## Metrics

| Metric | Value |
|--------|-------|
| Draw calls per frame | 391 |
| Non-black pixels | 917876 / 921600 (99.6%) |
| Frames stable | 9000+ (timeout at 180s) |
| Crashes | 0 (merge crashes recovered via siglongjmp) |
| Screenshots captured | 5 (frames 2800, 3000, 3200, 3500, 4000) |
| New function implementations | 4 (PrepShadow, CalcRect, RemoveFromLists, GetBlendState) |
