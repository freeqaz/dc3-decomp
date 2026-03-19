# Venue Draw Architecture: Investigation & Corrected Plan

**Date**: 2026-03-19
**Status**: Investigation complete, plan ready for implementation
**Related**:
- [venue-draw-architecture-gap.md](2026-03-19-venue-draw-architecture-gap.md) — original problem statement (contains factual errors corrected here)
- [FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md) — Phase 1-5 complete 2026-03-17

## Key Finding: The Original Document Is Wrong

The original session document (`venue-draw-architecture-gap.md`) states:

> `mVenue` going null after PostMerge **is expected behavior on Xbox**.

**This is incorrect.** Runtime instrumentation proves the venue merger uses **proxy mode**
(`mProxy=1`), which means the venue dir survives PostMerge and `mVenue` remains valid
throughout the gameplay lifecycle. The entire premise of the original plan — that `mVenue`
going null is normal and needs workarounds — is based on a misunderstanding.

## Proof: Runtime Diagnostics

Instrumentation was added to `FileMerger::FinishLoading()`, `PostMerge()`, and
`Merger::Load()` (the `operator>>` deserializer). Running `dc3-native` with
`DC3_VENUE=glitterati DC3_SCREEN=game_screen`:

### Merger deserialization (world.fm categories)

```
DC3 DIAG Merger::Load 'song'  rev=5 proxy=0 subdirs=1 preClear=1
DC3 DIAG Merger::Load 'viz'   rev=5 proxy=1 subdirs=1 preClear=1
DC3 DIAG Merger::Load 'venue' rev=5 proxy=1 subdirs=1 preClear=1
```

The `venue` merger has `proxy=1`. This is serialized in `world/gen/world.milo_xbox`
(3.3KB skeleton ObjectDir containing `world.fm`).

### Venue load + PostMerge

```
DC3 OnFileLoaded(venue) dir=0x...1b00 venue=0x...1b00 'glitterati'
DC3 DIAG FM::FinishLoading 'venue' proxy=1 sDisableAll=0 dl=0x...d50 dlDir=0x...1b00
DC3 DIAG FM::PostMerge 'venue' proxy=1 — deleting loader only (proxy — dir survives)
```

With `proxy=1`, PostMerge deletes only the DirLoader (not the dir). The venue WorldDir
persists as a subdir of `world_panel->mDir` (the world root). `mVenue` stays valid.

### Game loading completes

```
DC3 GamePanel::PollForLoading() — DONE (state 4)!
```

`IsWorldLoaded()` returns true because `mVenue` is non-null (proxy kept it alive).

## Why mVenue MUST Be Non-Null on Xbox

Three independent proofs:

**1. `IsWorldLoaded()` gates on `mVenue`** (HamDirector.cpp:1695):
```cpp
bool result = mVenue && mMerger && !mMerger->HasPendingFiles()
    && mMoveMerger && !mMoveMerger->HasPendingFiles();
```
If `mVenue` is null, this returns false, `GamePanel::PollForLoading()` never reaches
state 4, and the game hangs at the loading screen. The shipped Xbox game doesn't hang.

**2. `SetNewWorld()` asserts `mVenue` non-null** (HamDirector.cpp:1763):
```cpp
MILO_ASSERT(mVenue, 0x7D5);
```
Called from `SyncScene()` → `Enter()`. If `mVenue` were null, this crashes the Xbox game.

**3. 30+ post-merge usages** including `DrawShowing`, `Enter`, `Poll`, `ForceShot`,
`PlayNextShot`, `FindNextShot`, `CollideList`, and `VenueEnter` all depend on `mVenue`.

## How Proxy Mode Works

In `FileMerger::FinishLoading()`:

```
proxy=true:  venue dir added as SUBDIR of world root → survives PostMerge
proxy=false: venue content MERGED INTO world root → temp dir deleted in PostMerge
```

With proxy mode, the object hierarchy is:
```
world_panel->mDir (WorldDir, "world root", from world.milo)
  ├── world.fm (FileMerger, 3 categories)
  ├── modular.fm (FileMerger)
  ├── GameModeMerger.fm (FileMerger)
  ├── [song content — merged, not proxy]
  ├── glitterati/ (WorldDir subdir — PROXY, mVenue points here)
  │   ├── meshes, cameras, lights, environments
  │   ├── CameraManager with camera shots
  │   └── HamCamShot objects
  └── HamVisDir/ (PROXY subdir)
```

- `mVenue` = venue WorldDir subdir (`glitterati/`)
- `GetWorld()` = `mMerger->Dir()` = world root
- These are **different objects** with different draw lists and camera managers

## The Draw Path (Xbox vs Native)

### Xbox (correct architecture)

```
TheUI->Draw()
  → game_screen panels
    → UIPanel::Draw("world_panel")
      → world_panel->mDir->DrawShowing()        // WorldDir "world root"
        → SetTheWorld(this)
        → CameraManager selects camera (from world root's CamMgr)
        → Environment setup
        → RndDir::DrawShowing()                  // draws mDraws
          → HamDirector::DrawShowing()           // HamDirector is in mDraws
            → mVenue->DrawShowing()              // re-entrant (TheWorld set)
              → RndDir::DrawShowing()            // draws venue meshes only
        → PostProcs, HUD
        → SetTheWorld(nullptr)
```

One draw call chain. World root does camera/env. Venue drawn re-entrantly (meshes only).
HamDirector manages which camera shot is active via `PlayNextShot()` →
`GetWorld()->GetCameraManager()->ForceCameraShot()` (forces on the WORLD ROOT's manager).

### Native (current — broken, double-draw)

```
App.cpp explicit draw (line 1264-1296):
  → drawVenue = TheHamDirector->GetVenueWorld()    // = mVenue
  → drawVenue->DrawShowing()                       // FULL path (TheWorld=nil)
    → SetTheWorld(mVenue)
    → CameraManager from VENUE (not world root)
    → Environment from VENUE
    → RndDir::DrawShowing()                        // draws venue meshes
    → SetTheWorld(nullptr)

TheUI->Draw():
  → UIPanel::Draw("world_panel")
    → world_panel->mDir->DrawShowing()             // FULL path again
      → SetTheWorld(world root)
      → World root CameraManager (different from venue's!)
      → RndDir::DrawShowing()                      // draws mDraws
        → HamDirector::DrawShowing()
          → mVenue->DrawShowing()                  // re-entrant
            → RndDir::DrawShowing()                // draws venue meshes AGAIN
      → SetTheWorld(nullptr)
```

**Venue meshes are drawn TWICE per frame.** First from the explicit draw (with venue's
camera), then from the panel hierarchy (with world root's camera, re-entrant).

### Confirmed by instrumentation

```
DC3 DIAG UIPanel::Draw 'world_panel' mDir='world' mDir=0x...a850
DC3 DIAG HamDirector::DrawShowing mVenue=0x...eb20 hide=0 TheWorld=0x...a850
```

`world_panel` IS drawing. HamDirector IS a draw child of the world root (TheWorld is set
to world root when HamDirector::DrawShowing fires). The explicit draw in App.cpp is
redundant — the panel system already handles venue rendering.

## PlayNextShot Camera Mismatch

An additional discrepancy in `HamDirector::PlayNextShot()` (line 2563-2570):

```cpp
#ifdef HX_NATIVE
    WorldDir *world = mVenue;                              // forces on VENUE's CamMgr
#else
    WorldDir *world = dynamic_cast<WorldDir *>(mMerger ? mMerger->Dir() : nullptr);  // WORLD ROOT
#endif
if (world) {
    world->GetCameraManager()->ForceCameraShot(mCurShot, false);
}
```

On Xbox, camera shots are forced on the **world root's** CameraManager (because
`WorldDir::DrawShowing` reads from `mCameraMgr` of `this`, which is the world root in the
panel path). On native, they're forced on the **venue's** CameraManager, which only works
because the explicit draw runs the venue's full path with its own CameraManager.

If we remove the explicit draw, we must also fix `PlayNextShot` to use `GetWorld()` instead
of `mVenue` for camera management.

## Component Loading (.milo Extras)

The `_buildings`, `_sky`, `_set`, `_chairs`, `_table_glasses` component .milo files are
loaded manually in `App.cpp:1069-1084`. On Xbox, these should be loaded by `extras.fm`
(found at HamDirector.cpp:1104-1105: `mVenue->Find<FileMerger>("extras.fm", false)`).

Since `mVenue` is now confirmed alive after PostMerge, `extras.fm` should be findable.
Whether it actually fires its loading cascade needs verification — but the mechanism exists.

## The IsWorldLoaded const_cast Hack

```cpp
#ifdef HX_WEB
if (!mVenue && mMerger && mMerger->Dir()) {
    WorldDir *w = dynamic_cast<WorldDir *>(mMerger->Dir());
    if (w) const_cast<HamDirector *>(this)->mVenue = w;
}
#endif
```

This hack (HamDirector.cpp:1687-1693) is **web-only** (`HX_WEB`), not `HX_NATIVE`. It
reassigns `mVenue` to the world root, which is semantically wrong (they're different
objects). Since proxy mode keeps `mVenue` alive on both native desktop and Xbox, this hack
is unnecessary for desktop. For web, there may be a different issue (async/coroutine
lifecycle differences).

## Corrected Fix Plan

### Phase 1: Remove the explicit draw (App.cpp:1264-1296)

The panel system already draws the venue correctly through:
```
world_panel → world root → HamDirector → mVenue (re-entrant)
```

Delete the `#ifdef HX_NATIVE` explicit draw block. This eliminates the double-draw and
aligns with Xbox behavior.

**Risk**: If the world root's CameraManager doesn't have the right camera, the venue
renders with a default camera. Mitigated by Phase 2.

### Phase 2: Fix PlayNextShot camera target

Change the `#ifdef HX_NATIVE` in PlayNextShot to use `GetWorld()` instead of `mVenue`:

```cpp
// Remove the #ifdef entirely — use the Xbox path for all platforms:
WorldDir *world = dynamic_cast<WorldDir *>(mMerger ? mMerger->Dir() : nullptr);
if (world) {
    world->GetCameraManager()->ForceCameraShot(mCurShot, false);
}
```

This ensures camera shots are forced on the world root (which is what
`WorldDir::DrawShowing` reads from), matching Xbox.

### Phase 3: Remove gNativeVenueDir fallback from gameplay draw

The `gNativeVenueDir` fallback at App.cpp:1267-1268 was needed because `mVenue` was
thought to be null. Since `mVenue` survives PostMerge, the fallback is unnecessary for
gameplay. `gNativeVenueDir` should be kept for the pre-game/menu venue draw (when
HamDirector doesn't exist yet) but the gameplay fallback can be removed.

### Phase 4: Remove IsWorldLoaded const_cast hack

Delete the `#ifdef HX_WEB` block in `IsWorldLoaded()`. `mVenue` stays alive through
proxy mode and doesn't need recovery from `mMerger->Dir()`.

### Phase 5: Investigate component loading

Check if `extras.fm` in the venue fires its loading cascade automatically. If it does,
the manual component loading in App.cpp:1069-1084 can be removed. If not, wire it up
properly (maybe needs to be triggered by a DTA handler after the venue merge completes).

### Phase 6: Investigate pre-game venue draw

The pre-game venue draw (gNativeVenueDir, used when no HamDirector exists) is a separate
concern from the gameplay venue. On Xbox, the menu screen may not draw a venue at all, or
it uses a different mechanism. This is lower priority and can be addressed separately.

### Phase 7: Cleanup

- Remove HX_WEB debug logging in GamePanel, Game, HamDirector
- Remove stale comments about "mVenue going null" (they're wrong)
- Update convergence doc to reflect that explicit draw is removed

## What NOT To Do

1. **Do NOT reassign `mVenue = GetWorld()`** — they are different objects (venue subdir vs
   world root). Reassignment changes semantics of every `mVenue->Find<T>()` call.

2. **Do NOT add more workarounds for mVenue being null** — the underlying mechanism
   (proxy mode) works correctly. Fix the draw path instead.

3. **Do NOT remove HamDirector::DrawShowing()** — it's active code on Xbox, called through
   the panel hierarchy as a draw child of the world root.

## Files Referenced

| File | Lines | What |
|------|-------|------|
| `src/App.cpp` | 1264-1296 | Explicit draw (TO REMOVE) |
| `src/App.cpp` | 1069-1084 | Component loading (investigate) |
| `src/system/hamobj/HamDirector.cpp` | 403-409 | DrawShowing (keep — panel-system draw child) |
| `src/system/hamobj/HamDirector.cpp` | 2563-2570 | PlayNextShot camera target (FIX) |
| `src/system/hamobj/HamDirector.cpp` | 1686-1708 | IsWorldLoaded hack (REMOVE) |
| `src/system/hamobj/HamDirector.cpp` | 1260-1282 | OnFileLoaded venue setup (keep) |
| `src/system/char/FileMerger.cpp` | 197-234 | FinishLoading proxy/merge path |
| `src/system/char/FileMerger.cpp` | 540-580 | PostMerge — proxy keeps dir alive |
| `src/system/world/Dir.cpp` | 479-559 | WorldDir::DrawShowing — re-entrancy guard |
| `src/system/ui/UIPanel.cpp` | 96-100 | UIPanel::Draw — delegates to mDir |
| `docs/native/FILEMERGER_CONVERGENCE.md` | — | Convergence context |

## Diagnostic Instrumentation (Left In Place)

The following `#ifdef HX_NATIVE` diagnostics were added to `FileMerger.cpp` and are
useful for ongoing debugging. They log merger deserialization, FinishLoading proxy state,
and PostMerge behavior. They can be removed once the fix is verified stable:

- `Merger::Load` operator>> — logs name, rev, proxy, subdirs, preClear
- `FinishLoading` — logs merger name, proxy flag, sDisableAll, loader/dir state
- `PostMerge` — logs merger name, proxy flag, delete behavior
