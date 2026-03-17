# HamDirector & hamobj Native Hack Audit — Cleanup Roadmap

**Date**: 2026-03-17
**Goal**: Inventory all `#ifdef HX_NATIVE` hacks in hamobj subsystem, classify by root cause, and define a cleanup roadmap that converges native behavior toward Xbox DTA-driven flow.

---

## Executive Summary

The `src/system/hamobj/` subsystem has **102 `#ifdef HX_NATIVE` blocks across 31 files** (excluding `.permuter_bak`). HamDirector.cpp alone has 14, HamNavList.cpp has 22. Most exist because the native port either (a) bypasses DTA script flow that Xbox relies on, or (b) adds defensive null guards because subsystems initialize in a different order.

After the FileMerger convergence work (phases 1-5), the core loading pipeline is correct — but the **game flow layer** still has significant divergence. The hacks cluster into clear categories with different cleanup strategies.

---

## Hack Categories (102 total)

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

## Deep Dive: HamDirector.cpp (14 hacks)

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
#ifdef HX_NATIVE
WorldDir *world = mVenue;
#else
WorldDir *world = dynamic_cast<WorldDir*>(mMerger ? mMerger->Dir() : nullptr);
#endif
```
- **Root cause**: Camera management routed differently on native (explicit venue in App.cpp vs FileMerger hierarchy)
- **Fix**: FileMerger pipeline IS fully wired after Phase 1-5 convergence. `mMerger->Dir()` should return the venue WorldDir on native too. Test by switching native to the Xbox path and verifying camera shots still fire.
- **Investigation step**: Add `MILO_LOG("PlayNextShot: mMerger=%p Dir=%p mVenue=%p", mMerger.Ptr(), mMerger ? mMerger->Dir() : nullptr, mVenue.Ptr())` — if `mMerger->Dir() == mVenue`, the hack is safe to remove.
- **Priority**: HIGH — directly validates FileMerger convergence

### Tier 3: Permanent / low-priority (platform differences)

**8. Platform includes (line 74)** — Permanent. Xbox XDK headers vs native stubs.

**9-10. Logging (lines 991, 1176)** — Keep, useful for debugging OnLoadSong and OnFileLoaded.

**11. OnFileLoaded — video_recorder stub (line 1223)** — Permanent. No Kinect hardware. Creates a stub `Hmx::Object` named `video_recorder.srec` so DTA scripts don't assert.

**12. SetPlayerSpotlightsEnabled — non-fatal Find (line 1893)** — **Likely removable now.** The DirLoader parent chain fix (Phase 5) resolved HUD flow target resolution (461→7 "couldn't find" warnings). HUD objects like phrase meters and spotlights are now found via the FindObject ProxyDir fallback.
- **Investigation step**: Change `false` to `true` in the `Find()` calls. If no asserts fire during a full song playthrough, the hack is safe to remove.

**13. UnloadMergers — accessor vs offset (line 2473)** — Refactoring improvement. The native path uses `Mergers()` accessor; Xbox uses raw offset `0x40`. The accessor is better code — make it available on both paths (add to `FileMerger.h` outside any `#ifdef`).

**14. STLport throw guard (line 3056)** — `#ifndef HX_NATIVE` around `stlpmtx_std::__stl_throw_out_of_range("vector")`. libstdc++ doesn't have this symbol. Permanent STL implementation difference. The throw guards an out-of-range clip key index — on native, falling through to the `Find<CharClip>` with a bad index is equivalent (Find returns null → no clip plays).

---

## Deep Dive: hamobj Subsystem (88 remaining hacks)

### HamNavList.cpp — 22 hacks (largest file)

The dominant pattern is **TheHamProvider null guards** (6 instances at lines 540, 750, 806, 1104, 1109, 1324) and **IsAnimating() bypass** (3 instances at lines 510, 1436, 1526). The remaining 13 are a mix of scroll behavior, ribbon state management, and DTA property access guards.

**Root cause**: `TheHamProvider` is set up by `ham_init.dta` during DTA flow. On native, subsystems poll before DTA initialization completes. The `IsAnimating()` bypasses exist because `transition_complete` DTA handlers never fire on native.

**Fix strategy**:
1. Add `MILO_LOG` to each TheHamProvider null guard to check if they fire during a clean boot. If none fire, they're safe to remove immediately. If some fire, log the call stack to identify which poll runs before init.
2. Check `UIScreen::FinishTransition()` — does it call `Handle(TRANSITION_COMPLETE_MSG, ...)`? If yes, trace why the message doesn't reach HamNavList (message routing issue). If no, add the send.
3. If `transition_complete` is fixed, the 3 `IsAnimating()` bypasses can be removed — but verify with a full menu→song→results navigation cycle first.

### MoveMgr.cpp — 7 hacks

Guards around move graph loading (null checks, assertion downgrades). Root cause: async loading means move data isn't ready when MoveMgr initializes.

**Fix**: FileMerger convergence should have fixed loading order. **Verification method**: Add `MILO_ASSERT` to each guard site on a test branch. Run a full song playthrough. If no asserts fire, the guards are redundant. If they fire, log the call stack to understand which poll runs before the move merger completes.

### HamCharacter.cpp — 6 hacks (+ 1 in .h)

Mix of clip loading guards and PPC codegen variants. The codegen variants (QuatXfm, Poll control flow) are permanent decomp guards. The clip loading guards may be removable once character loading pipeline converges.

### HamNavProvider.cpp — 5 hacks

Likely similar pattern to HamNavList — TheHamProvider null guards and DTA flow gaps. Investigate alongside HamNavList.

### OriginalChoreoRemixer.cpp (2) + SuperEasyRemixer.cpp (4) — 6 hacks combined

Guards around move graph/layout null derefs. The OriginalChoreoRemixer blanket return was already removed in the convergence work. Remaining guards are for move data not being fully loaded.

**Fix**: Same as MoveMgr — add `MILO_ASSERT` at guard sites, run full song playthrough, remove if clean.

### Other files (42 hacks across 20 files)

| File | Count | Category |
|------|-------|----------|
| HamWardrobe.cpp | 5 | Outfit loading / DTA flow |
| HamSongData.cpp | 4 | Data access guards |
| HamVisDir.cpp | 4 | Visual setup / camera |
| Ham.cpp | 2 | Init ordering |
| HamGameData.cpp | 2 | Player data guards |
| DanceRemixer.cpp | 2 | Move data guards |
| FreestyleMove.h | 2 | Struct field access |
| FreestyleMoveRecorder.cpp | 2 | Kinect/gesture |
| MoveDir.cpp | 2 | Async loading |
| MoveGraph.cpp | 2 | Move data |
| PoseFatalities.cpp | 2 | Gameplay flow |
| FilterVersion.cpp | 1 | Codegen variant |
| HamAudio.cpp | 1 | FileLoader guard |
| HamCamShot.cpp | 1 | Null guard |
| HamListRibbon.cpp + .h | 2 | LP64 pointer size |
| HamRibbon.cpp | 1 | Scroll behavior |
| HamScrollBehavior.cpp | 1 | Scroll behavior |
| MoveAsyncDetector.cpp | 1 | Async loading |
| MoveVariant.cpp | 1 | Move data |
| PhotoSpotlightPositioner.cpp | 1 | Camera/spotlight |
| PracticeSection.cpp | 1 | Practice mode |
| SongCollision.cpp | 1 | Collision guard |

---

## Cleanup Roadmap

### Phase A: DTA Flow Convergence (removes ~15 hacks)

**Goal**: Make DTA scripts fire the same handlers on native as Xbox.

**A1. Fix `set_type "world"` firing** (HamDirector hack #1)
- **Investigation**: Add `MILO_LOG` to `ObjectDir::SetType()`. Boot to gameplay. Three possible outcomes:
  - (a) `SetType("world")` is never called → `world_panel` DTA enter handler doesn't fire. Trace UIPanel enter flow for the venue panel.
  - (b) `SetType` is called but on the wrong object → `$this` resolves incorrectly in DTA scope.
  - (c) `SetType` is called correctly → the hack's `!dir->TypeDef()` guard is already a no-op (TypeDef was set by DTA). Safe to remove.
- **Removes**: 1 hack. **Unblocks**: all venue DTA handlers (`select_camera`, etc.)

**A2. Fix `transition_complete` messages** (HamNavList hacks at 510, 1436, 1526)
- **Investigation**: Check `UIScreen::FinishTransition()` — does it send `TRANSITION_COMPLETE_MSG`? If yes, trace why it doesn't reach HamNavList (missing sink registration, message routing). If no, add the send.
- **Verification**: Navigate main_menu → song_select → gameplay → results → main_menu. All transitions must complete without hanging animations.
- **Removes**: 3 `IsAnimating()` bypasses.

**A3. Fix player provider initialization** (HamDirector hack #2)
- **Investigation**: Log `HamPlayerData::Provider()`, `hpd->Crew()`, `hpd->Outfit()` at the top of OnLoadSong. If providers are correctly populated, the native reconstruction block is dead code. If not, trace `ham_init.dta` provider setup to find what's missing.
- **Risk**: HIGH — wrong outfits break gameplay visuals (as the GamePanel bug proved). Test with 2 different songs to catch outfit-specific issues.
- **Removes**: 1 hack (38 lines of defensive reconstruction).

**A4. Fix HamProvider init ordering** (HamNavList null guards at 540, 750, 806, 1104, 1109, 1324)
- **Investigation**: Add `MILO_ASSERT(TheHamProvider, __LINE__)` at each site. Boot to gameplay. If none fire → remove guards. If they fire → log timestamps to identify which poll runs before `ham_init.dta` completes, then fix ordering.
- **Risk**: MEDIUM — if guards are load-bearing, removing them without fixing ordering crashes immediately. The assert-first approach catches this safely.
- **Removes**: up to 6 null guards.

### Phase B: Loading Pipeline Validation (removes ~14 hacks)

**Goal**: Validate that FileMerger convergence fixed loading order, then remove defensive guards.

**B1. Move data loading** (MoveMgr 7, OriginalChoreoRemixer 2, SuperEasyRemixer 4)
- **Verification method**: Replace each `#ifdef HX_NATIVE` null guard with `MILO_ASSERT(ptr != nullptr, ...)` on a test branch. Play 1 full song. If no asserts fire → guards are redundant. If they fire → the move merger hasn't completed by that point and the guard is still needed.
- **Removes**: up to 13 hacks across 3 files.

**B2. PlayNextShot — venue vs merger dir** (HamDirector hack #7)
- **Investigation**: Add log: `MILO_LOG("PlayNextShot: mMerger=%p Dir=%p mVenue=%p", ...)`. If `mMerger->Dir() == mVenue`, switch native to the Xbox path.
- **Removes**: 1 hack.

**B3. Camera dircut data** (HamDirector hack #4)
- **Verification**: Replace early return with `MILO_ASSERT(entry, ...)`. Play 1 song with camera transitions. If no asserts → remove guard.
- **Removes**: 1 hack.

### Phase C: Init Sequence Alignment (removes ~10 hacks)

**Goal**: Align native initialization order with Xbox so subsystems don't see uninitialized state.

**Risk note**: Changing initialization order produces non-local effects. One subsystem's timing fix can break another's assumptions. This phase requires:
1. **Boot-order trace first** — Log timestamps for: App::Init, ham_init.dta eval, TheHamProvider creation, venue Enter, HamDirector Enter, first HamNavList Poll, first MoveMgr Poll, first GamePanel Poll.
2. **Dependency graph** — Map which subsystems depend on which init events.
3. **Rollback criteria** — If any unrelated subsystem crashes after an ordering change, revert and investigate before proceeding.

Items:
1. **HamCharacter clip guards** — Verify character clips are loaded before Poll/Enter access them
2. **HamAudio FileLoader guard** — Verify FileLoader is polled after Game::Restart
3. **HamCamShot null guard** — Verify characters are loaded before camera shot evaluation

### Phase D: Architectural Decisions + Trivial Refactors

Split into D-trivial (do anytime) and D-architectural (need design decision).

**D-trivial** (do alongside any other work):
- **UnloadMergers accessor** (hack #13) — Move `Mergers()` accessor outside `#ifdef HX_NATIVE` in `FileMerger.h`. 5-line change, zero risk. Removes 1 hack.
- **SetPlayerSpotlightsEnabled** (hack #12) — Change `Find(..., false)` to `Find(..., true)`. If asserts fire, put it back. Removes 1 hack.

**D-architectural** (need design decision before implementing):
1. **SetShot song.anim vs routine builder** (hack #6) — Decide whether native should drive the routine builder like Xbox, or keep its current direct song.anim approach. If routine builder: implement it, remove hack. If direct: document and mark permanent.
2. **Intro timing** (hack #3 at line 3093) — Decide whether to fix `Game::Poll()` to drive `SetSecondsAndBeat()` during intro, or keep wall-clock fallback. The wall-clock approach works but diverges from Xbox timing.

### Not Removable (~55 hacks)

These are permanent platform differences:
- Kinect/hardware stubs (8)
- LP64 pointer size (10)
- Xbox-only includes (9)
- PPC compiler codegen variants (6)
- Static field declarations (9)
- STL implementation diffs (2 — STLport throw + iterator compat)
- Debug logging (8) — keep for diagnostics
- Remaining guards for genuinely missing features (3)

---

## Priority Order

| Phase | Hacks | Investigate | Fix | Impact |
|-------|-------|-------------|-----|--------|
| **D-trivial**: Mergers accessor + SpotlightsEnabled | 2 | None | 10 min | Code quality, validates Phase 5 |
| **A1**: `set_type "world"` | 1 | Add MILO_LOG to SetType | Small if (c) | Unblocks all venue DTA handlers |
| **A4**: HamProvider init | 6 | Add MILO_ASSERT at each site | Small if none fire | Removes defensive null checks |
| **B2**: PlayNextShot unify | 1 | Log mMerger->Dir() vs mVenue | Small | Validates FileMerger convergence |
| **A2**: `transition_complete` | 3 | Trace UIScreen::FinishTransition | Medium | HamNavList animation flow |
| **B1**: Move data guards | 13 | MILO_ASSERT at each guard | Small if none fire | Removes defensive null checks |
| **A3**: Player provider init | 1 | Log provider state at OnLoadSong | Medium | Correct wardrobe loading |
| **B3**: Camera dircut | 1 | MILO_ASSERT at guard | Small | Camera robustness |
| **C**: Init sequence | ~10 | Boot-order trace + dep graph | Large | Requires careful ordering |
| **D-arch**: song.anim + intro timing | 2 | Design review | Large | Architectural decisions |

**Estimated removable: ~40 hacks (39%), bringing hamobj from 102 → ~62 native guards.**

### Verification protocol

Every hack removal follows the same pattern:

1. **Investigate**: Add `MILO_LOG` or `MILO_ASSERT` at the guard site. Boot to gameplay. Does it fire?
2. **If no** → guard is dead code. Remove the `#ifdef` block. Run smoke test (boot → play 1 song → results).
3. **If yes** → the guard is load-bearing. Log the call stack / timing to understand why. Fix the root cause, then remove.
4. **Regression check**: After removal, play 1 full song. Verify: characters visible, cameras switch, HUD renders, no crashes.

No hack is removed on faith — investigation proves the DTA path works first.

---

## Relationship to Prior Work

### Fix applied during review: GamePanel::StartGame() LoadCharacters removal

The most impactful hack wasn't in HamDirector — it was in **GamePanel.cpp:579**. `StartGame()` called `TheHamWardrobe->LoadCharacters(mo01, emilia01, ...)` inside `#ifdef HX_NATIVE`, based on the false assumption that "the DTA load_characters flow doesn't run on native." The DTA flow **does** run — `HamDirector::OnFileLoaded('song')` fires at t=25s and calls `LoadCharacters` with `mCharacterOutfits[]`. The GamePanel call was a **third redundant load** (after Rnd_Wgpu early load at t=7s and OnFileLoaded at t=25s) with different outfits, triggering a FileMerger `Clear→Merge` cycle during gameplay that destroyed character meshes and animation state. **Removed.** Also removed the diagnostic venue RndDir enumeration loop (noisy, not actionable).

This audit builds on:
- **FileMerger Convergence** (phases 1-5) — Fixed core loading pipeline, removed gNativeHudDir
- **HACK_AUDIT.md** — Cataloged all 745+ HX_NATIVE blocks project-wide
- **Song Anim Advancement** session — Fixed beat-sync timing, removed AdvanceFrame hack
- **DirLoader Parent Chain** session — Fixed parent dir resolution for flow subdirs. Also fixed pre-existing `ObjPtrVec::Node::RefOwner()` bug (wrong `static_cast` to `Hmx::Object*` on native).

The key insight from convergence work applies here too: **the engine already does this work via DTA**. Most hacks exist because we bypassed the DTA flow. Fixing the flow at the root (ensuring DTA handlers fire) cascades into removing many downstream guards.

### Post-Phase 5 impact on this roadmap

The DirLoader parent chain fix (Phase 5) has downstream effects on several hacks listed above:
- **Hack #7 (PlayNextShot)**: Already unified — no `#ifdef` exists. Remove from roadmap.
- **Hack #11 (SetPlayerSpotlightsEnabled)**: HUD objects are now found (461→7 warnings) — non-fatal Find may be removable
- **Phase B generally**: FindObject ProxyDir fallback means objects in proxy subdirs can now reach parent scope during loading, which may resolve some of the "objects not found" root causes behind null guards in MoveMgr, HamCharacter, etc.

---

## Design Review Notes

### Corrections to the audit

1. **Count mismatch**: hamobj has **103** `#ifdef`/`#ifndef` blocks across **31** files (including 2 `.h` and 2 `.permuter_bak` files), not 122 across 28. The document should exclude `.permuter_bak` files and re-count.

2. **Hack #7 (PlayNextShot) is a ghost**: Line 2548 has **no `#ifdef HX_NATIVE`**. The code is already `mMerger->Dir()` on both platforms. The comment above describes historical context only. This inflates the "removable" count by 1 and misrepresents the PlayNextShot path as divergent when it's already converged.

3. **HamNavList count is 22, not 20**: Lines 48, 510, 540, 663, 750, 806, 864, 1033, 1086, 1104, 1109, 1151, 1297, 1324, 1436, 1526, 1574, 1631, 1720, 1727, 1771, 1794.

4. **Missing hack #13 (STLport throw)**: The `#ifndef HX_NATIVE` at line 3052 guards `stlpmtx_std::__stl_throw_out_of_range` — an STL implementation difference. Wasn't listed.

5. **MoveMgr has 7 hacks, not 6**: Recount needed.

### Structural concerns with the roadmap

**Phase A is underspecified.** "Fix `set_type "world"` firing" and "fix `transition_complete` messages" are described at the symptom level, not the investigation level. Each needs a concrete first step:

- A1 (`set_type`): First step is tracing DTA evaluation during venue load — add `MILO_LOG` to `ObjectDir::SetType` to see if it's called at all on native, or if the DTA script simply never executes. The root cause could be: (a) `world_panel` DTA enter handler doesn't fire, (b) it fires but `$this` doesn't resolve to the venue, or (c) the venue TypeDef is set by a different mechanism entirely.

- A2 (`transition_complete`): First step is checking whether `UIScreen::FinishTransition()` sends the message. If it does, the issue is upstream (transitions not completing). If it doesn't, the fix is in UIScreen.

- A4 (HamProvider init): The 6 null guards may be **correct defensive code** if `TheHamProvider` genuinely isn't initialized yet. Removing them without fixing ordering would crash. The doc should specify: "Validate that TheHamProvider is non-null at each guard site during a clean boot. If any fire, the init ordering fix is a prerequisite."

**Phase B assumes loading convergence is validated but doesn't define how.** "Verify move mergers complete before MoveMgr access" — what's the verification? A `MILO_ASSERT(!mMoveMerger->HasPendingFiles(), ...)` at the guard sites? A log-based trace? This needs to be concrete.

**Phase C (init sequence alignment) has the highest risk.** Changing initialization order is a class of change that produces non-local effects — fixing one subsystem's timing can break another's assumptions. This phase should:
- Start with a boot-order trace (log timestamps for key init events)
- Map the dependency graph before making changes
- Have rollback criteria defined per change

**Phase D conflates "architectural decisions" with "effort."** The routine builder question (D1) is genuinely architectural — it changes how the native port drives animation. The `Mergers()` accessor (D3) is a 5-line refactor. These don't belong in the same phase.

### Risk assessment gaps

The doc doesn't discuss **regression testing strategy**. Each hack removal needs a verification plan:
- Minimum: boot to gameplay, play 1 song to completion, no crashes
- Ideal: capture specific behavior (camera shots fire, characters animate, HUD renders) before and after

The doc also doesn't address the **GamePanel::StartGame() LoadCharacters bug** that we just fixed — this was the most impactful hack in the system (caused visible gameplay breakage) and it wasn't in HamDirector. The audit scope should explicitly note that GamePanel.cpp and Rnd_Wgpu.cpp have related hacks.

### Prioritization feedback

The priority table is reasonable but should account for **validation cost**. Some "small effort" items (B2 PlayNextShot — already done!) have zero cost. Others (A1 set_type) are small to implement IF the root cause is found, but the investigation could be large. Separate "investigation effort" from "fix effort."

Suggested reordering:
1. **A1 (set_type)** + **A4 (HamProvider init)** — investigate together, both are DTA init ordering
2. **B2 (PlayNextShot)** — already done, just remove from list
3. **A2 (transition_complete)** — investigate UIScreen flow
4. **B1 (move data guards)** — validate with asserts, remove if clean
5. **A3 (player provider)** — complex, leave until DTA init is solid
6. **C (init sequence)** — only after A-B are stable
7. **D (architectural)** — split D3 out as trivial; D1-D2 defer
