# HamDirector & hamobj Native Hack Audit — Cleanup Roadmap

**Date**: 2026-03-17
**Goal**: Inventory all `#ifdef HX_NATIVE` hacks in hamobj subsystem, classify by root cause, and define a cleanup roadmap that converges native behavior toward Xbox DTA-driven flow.

---

## Executive Summary

The `src/system/hamobj/` subsystem has **122 `#ifdef HX_NATIVE` blocks across 28 files**. HamDirector.cpp alone has 13. Most exist because the native port either (a) bypasses DTA script flow that Xbox relies on, or (b) adds defensive null guards because subsystems initialize in a different order.

After the FileMerger convergence work (phases 1-5), the core loading pipeline is correct — but the **game flow layer** still has significant divergence. The hacks cluster into clear categories with different cleanup strategies.

---

## Hack Categories (122 total)

| Category | Count | Removable? | Strategy |
|----------|-------|------------|----------|
| **DTA flow gaps** | 12 | Yes — fix root causes | Ensure DTA handlers fire naturally |
| **Null guards (late init)** | 27 | Mostly — fix init order | Converge init sequence |
| **Kinect/hardware stubs** | 8 | No — permanent | Platform difference |
| **LP64 pointer size** | 10 | No — permanent | Correct for x86_64 |
| **Debug/logging** | 8 | Keep — useful | Low priority |
| **Async loading workarounds** | 4 | Yes — loading is fixed | Remove post-validation |
| **Xbox-only includes** | 9 | No — permanent | Compile guard |
| **Compiler codegen variants** | 6 | No — PPC decomp | Keep for matching |
| **Struct field access** | 5 | Partially | Add accessors |
| **Static field declarations** | 9 | No — linker needs | Keep |
| **Unimplemented features** | 3 | Future work | Implement or permanent stub |
| **STL implementation diffs** | 1 | No — permanent | libstdc++ vs STLport |

**Actionable (removable): ~43 hacks across DTA flow, null guards, and async loading.**

---

## Deep Dive: HamDirector.cpp (13 hacks)

### Tier 1: Should Be Removed (DTA flow root causes)

**1. VenueEnter — force `SetType("world")` (line 575)**
```cpp
if (!dir->TypeDef()) {
    dir->SetType("world");
}
```
- **Root cause**: DTA `set_type` command doesn't fire on native during world_panel enter
- **Fix**: Trace why `world_panel`'s DTA enter handler skips `set_type`. The `world_objects.dta` defines the "world" type with handlers like `select_camera`. If `world_panel`'s DTA flow fires `{$this set_type world}`, this hack is unnecessary.
- **Priority**: HIGH — this is the gateway to all venue DTA handlers firing

**2. OnLoadSong — player presence / crew reconstruction (line 1004-1041)**
```cpp
// Native: check provider->player_present, clear stale slots, reconstruct crew/outfit
// Xbox: just mCharacterOutfits[i] = hpd->CharacterOutfit(mCrews[i])
```
- **Root cause**: Native single-player flow skips multiuser DTA initialization, leaving secondary player slot with stale data
- **Fix**: Ensure `ham_init.dta` provider initialization runs completely. The PropertyEventProvider for each player should have correct `player_present` and `crew`/`outfit` properties set by DTA before OnLoadSong fires.
- **Priority**: HIGH — stale state causes wrong wardrobes

**3. Poll — intro frame advancement fallback (line 3093-3109)**
```cpp
if (TheTaskMgr.Beat() == 0.0f) {
    float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    // ... manual AdvanceFrame with wall-clock time
}
```
- **Root cause**: During intro, `Game::Poll()` doesn't call `SetSecondsAndBeat()`, so beat stays at 0. DTA's `OnSelectCamera` computes frame=0 every tick.
- **Fix**: Investigate why `Game::Poll()` doesn't drive timing during intro. On Xbox, the beat system starts earlier or the intro uses a different timing source. The song.anim advancement session doc (`2026-03-17-song-anim-advancement.md`) may have more context.
- **Priority**: MEDIUM — works but diverges from Xbox timing

### Tier 2: Simplifiable (defensive guards with partial root cause)

**4. FindNextDircut — early null return (line 702)**
```cpp
if (!entry) return shot;
```
- **Root cause**: Camera dircut data may not be loaded yet on native
- **Fix**: Once loading pipeline is fully converged, this guard should be unnecessary. Keep as safety net for now, add `MILO_ASSERT(entry, ...)` on Xbox path.
- **Priority**: LOW

**5. FindNextShot — Area1_WIDE fallback (line 2297)**
- **Root cause**: Shot category not found in venue camera data
- **Fix**: Investigate why specific shot categories are missing. May be a venue loading completeness issue.
- **Priority**: LOW

**6. SetShot — skip SongAnim frame<0 guard (line 2319)**
- **Root cause**: Native drives original `song.anim`, not routine builder. `SongAnim(0)` returns routine builder whose frame is never set.
- **Fix**: This is an architectural difference in how song animation is driven. May require deeper work to converge routine builder setup.
- **Priority**: MEDIUM

**7. PlayNextShot — venue vs merger dir (line 2548)**
```cpp
// Native: WorldDir *world = mVenue;
// Xbox: WorldDir *world = dynamic_cast<WorldDir*>(mMerger ? mMerger->Dir() : nullptr);
```
- **Root cause**: Camera management routed differently on native (explicit in App.cpp vs FileMerger hierarchy)
- **Fix**: If FileMerger pipeline is fully wired, `mMerger->Dir()` should work on native too. Test removing the native override.
- **Priority**: HIGH — directly tests FileMerger convergence

### Tier 3: Permanent (platform differences)

**8-9. Logging (lines 991, 1176)** — Keep, useful for debugging.

**10. OnFileLoaded — video_recorder stub (line 1223)** — Permanent. No Kinect hardware.

**11. SetPlayerSpotlightsEnabled — non-fatal Find (line 1893)** — Keep until shared HUD is complete. Could be removed if all HUD objects are properly loaded.

**12. UnloadMergers — accessor vs offset (line 2473)** — Refactoring improvement. The native path uses `Mergers()` accessor; Xbox uses raw offset `0x40`. The accessor is better code. Consider making the accessor available on both paths.

**13. Platform includes (line 74)** — Permanent.

---

## Deep Dive: hamobj Subsystem (109 remaining hacks)

### HamNavList.cpp — 20 hacks (largest file)

The dominant pattern is **TheHamProvider null guards** (6 instances at lines 540, 750, 806, 1104, 1109, 1324) and **IsAnimating() bypass** (3 instances at lines 510, 1436, 1526).

**Root cause**: `TheHamProvider` is set up by `ham_init.dta` during DTA flow. On native, subsystems poll before DTA initialization completes. The `IsAnimating()` bypasses exist because `transition_complete` DTA handlers never fire on native.

**Fix strategy**:
1. Ensure `ham_init.dta` runs to completion before any HamNavList polling begins
2. Investigate why `transition_complete` messages don't fire — likely a UIScreen transition handler gap
3. If `transition_complete` is fixed, the 3 `IsAnimating()` bypasses can be removed

### MoveMgr.cpp — 6 hacks

Guards around move graph loading (null checks, assertion downgrades). Root cause: async loading means move data isn't ready when MoveMgr initializes.

**Fix**: FileMerger convergence should have fixed loading order. Validate that move mergers load before MoveMgr tries to use them, then remove guards.

### HamCharacter.cpp — 6 hacks

Mix of clip loading guards and PPC codegen variants. The codegen variants (QuatXfm, Poll control flow) are permanent decomp guards. The clip loading guards may be removable once character loading pipeline converges.

### OriginalChoreoRemixer.cpp, SuperEasyRemixer.cpp — 6 hacks combined

Guards around move graph/layout null derefs. The OriginalChoreoRemixer blanket return was already removed in the convergence work. Remaining guards are for move data not being fully loaded.

**Fix**: Same as MoveMgr — validate loading order post-convergence.

---

## Cleanup Roadmap

### Phase A: DTA Flow Convergence (removes ~15 hacks)

**Goal**: Make DTA scripts fire the same handlers on native as Xbox.

1. **Fix `set_type "world"` firing** — Trace `world_panel`'s enter DTA flow. If the TypeDef handler fires on Xbox via `{$this set_type world}`, ensure it fires on native too. This removes HamDirector hack #1 and unblocks all venue DTA handlers.

2. **Fix `transition_complete` messages** — UIScreen transitions need to send `TRANSITION_COMPLETE_MSG`. This removes 3 `IsAnimating()` bypasses in HamNavList.

3. **Fix player provider initialization** — Ensure `ham_init.dta` completely initializes player PropertyEventProviders before OnLoadSong. This removes the crew/outfit reconstruction hack.

4. **Fix HamProvider init ordering** — Ensure `TheHamProvider` is non-null before HamNavList polls. Either delay HamNavList polling or ensure `ham_init.dta` runs first. Removes 6 null guards.

### Phase B: Loading Pipeline Validation (removes ~10 hacks)

**Goal**: Validate that FileMerger convergence fixed loading order, then remove defensive guards.

1. **Move data loading** — Verify move mergers complete before MoveMgr/OriginalChoreoRemixer/SuperEasyRemixer access them. Remove null guards in MoveMgr (6), OriginalChoreoRemixer (2), SuperEasyRemixer (4).

2. **HamDirector::PlayNextShot** — Test `mMerger->Dir()` on native. If it returns the correct WorldDir, unify the camera path. Removes hack #7.

3. **Camera dircut data** — Verify dircut entries are loaded before FindNextDircut runs. Remove early-return guard.

### Phase C: Init Sequence Alignment (removes ~10 hacks)

**Goal**: Align native initialization order with Xbox so subsystems don't see uninitialized state.

1. **HamCharacter clip guards** — Verify character clips are loaded before Poll/Enter access them
2. **HamAudio FileLoader guard** — Verify FileLoader is polled after Game::Restart
3. **HamCamShot null guard** — Verify characters are loaded before camera shot evaluation

### Phase D: Architectural Decisions (removes ~8 hacks)

**Goal**: Decide permanent architecture for areas where native genuinely differs.

1. **SetShot song.anim vs routine builder** — Decide whether native should drive the routine builder like Xbox, or keep its current direct song.anim approach. If routine builder: implement it, remove hack #6. If direct: document and keep.

2. **Intro timing** — Decide whether to fix `Game::Poll()` to drive `SetSecondsAndBeat()` during intro, or keep wall-clock fallback. If fix: removes Poll hack #13.

3. **UnloadMergers accessor** — Make `Mergers()` accessor available on all platforms. Remove offset hack.

### Not Removable (~67 hacks)

These are permanent platform differences:
- Kinect/hardware stubs (8)
- LP64 pointer size (10)
- Xbox-only includes (9)
- PPC compiler codegen variants (6)
- Static field declarations (9)
- STL implementation diffs (1)
- Debug logging (8) — keep for diagnostics
- Remaining guards for genuinely missing features (16)

---

## Priority Order

| Phase | Hacks Removed | Effort | Impact |
|-------|---------------|--------|--------|
| **A1**: `set_type "world"` | 1 | Small | Unblocks all venue DTA handlers |
| **A2**: `transition_complete` | 3 | Medium | HamNavList animation flow |
| **A4**: HamProvider init | 6 | Medium | Removes defensive null checks |
| **B2**: PlayNextShot unify | 1 | Small | Tests FileMerger convergence |
| **A3**: Player provider init | 1 | Medium | Correct wardrobe loading |
| **B1**: Move data guards | 12 | Medium | Removes defensive null checks |
| **D3**: Mergers accessor | 1 | Small | Code quality |
| **C**: Init sequence | ~10 | Large | Requires careful ordering |
| **D1-D2**: Architectural | ~8 | Large | Design decisions needed |

**Estimated total removable: ~43 hacks (35% of total), bringing hamobj from 122 → ~79 native guards.**

---

## Relationship to Prior Work

This audit builds on:
- **FileMerger Convergence** (phases 1-5) — Fixed core loading pipeline, removed gNativeHudDir
- **HACK_AUDIT.md** — Cataloged all 745+ HX_NATIVE blocks project-wide
- **Song Anim Advancement** session — Fixed beat-sync timing, removed AdvanceFrame hack
- **DirLoader Parent Chain** session — Fixed parent dir resolution for flow subdirs

The key insight from convergence work applies here too: **the engine already does this work via DTA**. Most hacks exist because we bypassed the DTA flow. Fixing the flow at the root (ensuring DTA handlers fire) cascades into removing many downstream guards.
