# Venue Draw Architecture Gap

**Date**: 2026-03-19
**Status**: Research / planning
**Related**: [FILEMERGER_CONVERGENCE.md](../native/FILEMERGER_CONVERGENCE.md) (Phase 1-5 complete 2026-03-17)

## Problem Statement

The native port draws the venue by explicitly calling `drawVenue->DrawShowing()` in
`App.cpp`, bypassing the panel system. On Xbox, the venue is drawn entirely through
`TheUI->Draw()` via the `world_panel` hierarchy. The uncommitted changes add further
workarounds (`IsWorldLoaded()` fallback, `const_cast` mVenue recovery, HX_WEB debug
logging) to cope with `mVenue` going null after PostMerge — but `mVenue` going null
**is expected behavior on Xbox**. The workarounds treat a symptom, not the cause.

## How Xbox Draws the Venue

```
App::RunWithoutDebugging()
  TheRnd.BeginDrawing()
  TheUI->Draw()
    UIScreen::Draw("game_screen")
      UIPanel::Draw("world_panel")
        mDir->DrawShowing()               // mDir = WorldDir from world.milo
          WorldDir::DrawShowing()          // TheWorld == null → full path
            SetTheWorld(this)
            CameraManager → selects CamShot
            environment → Select()
            RndDir::DrawShowing()          // draws mDraws (all merged content)
            postprocs, spotlight glow
            HUD (mHUDDir, mHUD)
            SetTheWorld(nullptr)
  TheRnd.EndDrawing()
```

Key points:
- **No explicit venue draw** — `TheUI->Draw()` is the only draw call in the main loop
- `world_panel` is part of `game_screen`'s panel list (defined in `ui/game.dta`)
- `world_panel->mDir` is the WorldDir loaded from `world/world.milo` (3.3KB skeleton)
- FileMerger merges venue content INTO `world_panel->mDir` — this is the persistent dir
- `WorldDir::DrawShowing()` handles camera, environment, draw list, postprocs, HUD

## How the Native Port Draws the Venue

```
App::RunWithoutDebugging()
  TheRnd.BeginDrawing()
  drawVenue->DrawShowing()                 // EXPLICIT — outside panel system
  TheUI->Draw()                            // draws UI, but world_panel draws...?
  TheRnd.EndDrawing()
```

`drawVenue` comes from `TheHamDirector->GetVenueWorld()` which returns `mVenue`.

## Why mVenue Goes Null (Expected Behavior)

The `mVenue` lifecycle on Xbox:

1. `OnFileLoaded("venue")` fires during `on_pre_merge` DTA callback
2. `mVenue = dynamic_cast<WorldDir*>(dir)` — `dir` is the **temporary** loaded dir
3. FileMerger calls `MergeDirs(tempDir, mergerDir, filter)` — content copied to merger dir
4. `PostMerge()` calls `delete dir` — destroys the temporary
5. `~ObjectDir` calls `ReplaceRefs(nullptr)` — walks ObjRef ring, nulls all ObjPtrs
6. `mVenue` (an `ObjPtr<WorldDir>`) is set to `nullptr` by ReplaceRefs

**This is correct.** On Xbox, `mVenue` is a transient reference used briefly during
`OnFileLoaded` for setup (creating stubs, wiring wardrobe). After merge, the venue
content lives in `world_panel->mDir` (= `mMerger->Dir()`), and the panel system draws
it. `mVenue` going null is expected, not a bug.

## The Core Discrepancy

The native port treats `mVenue` as the persistent venue pointer and builds the entire
draw path around it. When `mVenue` goes null (as it's supposed to), the port adds
workarounds instead of using the panel system:

| Workaround | Location | What it does |
|-----------|----------|-------------|
| Explicit DrawShowing | App.cpp:1264-1296 | Draws venue outside panel hierarchy |
| gNativeVenueDir fallback | App.cpp:1267-1268 | Second venue pointer when mVenue is null |
| IsWorldLoaded recovery | HamDirector.cpp:1687-1693 | const_cast recovers mVenue from mMerger->Dir() |
| Component loading | App.cpp:1069-1084 | Manually merges buildings/sky/set into venue |
| Kinect mesh hiding | App.cpp:1271-1287 | Per-venue cleanup that should be in DTA flow |
| HX_WEB debug logging | GamePanel.cpp, Game.cpp | Diagnostics for stuck loading states |

FILEMERGER_CONVERGENCE.md (Phase 1-5, completed 2026-03-17) already removed many
similar hacks. The explicit draw in App.cpp and the mVenue recovery are the remaining
gap — the convergence plan identified them but they weren't addressed.

## What the Fix Should Be

### The target state

`TheUI->Draw()` draws the venue through the panel hierarchy, just like Xbox. The
`#ifdef HX_NATIVE` explicit draw block in App.cpp is deleted. `mVenue` going null
after PostMerge is accepted as normal. `IsWorldLoaded()` checks `GetWorld()`
(= `mMerger->Dir()`) instead of `mVenue`, or the check is restructured.

### Research needed

Before removing the explicit draw, we need to verify:

1. **Does `world_panel` actually draw on native?**
   - Is `game_screen` entering and showing `world_panel`?
   - Is `world_panel->mDir` non-null and populated after merge?
   - Does `UIPanel::Draw("world_panel")` fire during `TheUI->Draw()`?
   - If `WorldDir::DrawShowing()` fires through the panel path, does it render correctly?

2. **Is the venue content in the right place after merge?**
   - After FileMerger finishes, is `world_panel->mDir->mDraws` populated?
   - Are the meshes/cameras/lights/environments all present in the merged dir?
   - Is the CameraManager wired and has shots?

3. **Double-draw risk**
   - If we keep the explicit draw AND `world_panel` also draws, the venue renders twice
   - Need to confirm whether world_panel is currently drawing (and we're double-drawing)
     or whether it's not drawing (and removing explicit draw gives black screen)

4. **Component merging**
   - The `_buildings`, `_sky`, `_set`, `_chairs`, `_table_glasses` component .milo files
     are loaded manually in App.cpp:1069-1084
   - On Xbox, are these loaded by the DTA flow? Or are they part of the venue .milo?
   - If they're separate, the panel/FileMerger path may not include them

5. **Kinect/render-target mesh hiding**
   - App.cpp:1271-1287 hides meshes with empty diffuse textures or kBlendDest materials
   - These are Kinect video feed meshes that don't exist on native
   - On Xbox these are just invisible (Kinect texture is black) — do they need explicit hiding?

6. **mVenue usage outside drawing**
   - `SetNewWorld()` asserts `mVenue` non-null (line 1764)
   - `Enter()` uses `mVenue` for wardrobe, character setup
   - `PlayNextShot()` uses `mVenue` on native for camera management
   - All of these need to work with `GetWorld()` instead, or `mVenue` needs to be
     reassigned to the merged dir at the right point in the lifecycle

### Suggested plan

**Phase A: Diagnostic** — Add logging to confirm whether `world_panel->mDir->DrawShowing()`
fires during `TheUI->Draw()` on native. If it does, we're already double-drawing.

**Phase B: mVenue reassignment** — In `OnFileMerged` or a `PostMerge` handler, set
`mVenue = GetWorld()` (the merged persistent dir). This makes `mVenue` valid for the
rest of the lifecycle without the `IsWorldLoaded()` const_cast hack.

**Phase C: Remove explicit draw** — Delete App.cpp:1264-1296. Verify venue renders
through panel path. If black screen, debug the panel entry/draw chain.

**Phase D: Component loading** — Determine if `_buildings`/`_sky`/etc. are loaded by
DTA flow or need to be merged separately. If separate, move the merge into a proper
callback rather than the main loop.

**Phase E: Cleanup** — Remove `gNativeVenueDir`, `IsWorldLoaded()` recovery hack,
HX_WEB diagnostic logging, Kinect mesh hiding (or move to a proper venue setup callback).

## Files Referenced

- `src/App.cpp` — main loop, explicit venue draw (lines 1264-1296), component loading
- `src/system/hamobj/HamDirector.cpp` — OnFileLoaded (1214), IsWorldLoaded (1686), SetNewWorld (1762), PlayNextShot (2557)
- `src/system/world/Dir.cpp` — WorldDir::DrawShowing (479), SetTheWorld (37)
- `src/system/ui/UIPanel.cpp` — UIPanel::Draw (96)
- `src/system/char/FileMerger.cpp` — PostMerge, MergeDirs, temp dir deletion
- `src/lazer/game/GamePanel.cpp` — PollForLoading (929), world_panel lookup (939)
- `docs/native/FILEMERGER_CONVERGENCE.md` — convergence plan, hack removal tracker
