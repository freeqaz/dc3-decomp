# Native-shadow bodies are unmeasured

**Established 2026-09-30** on branch `native-shadow`. Inventory tool:
`scripts/analysis/native_shadow_audit.py` (tests:
`scripts/analysis/tests/test_native_shadow_audit.py`). Native regression tests:
`native/tests/test_native_shadow*.cpp`.

## The class

The native port compiles the **same** `src/` tree as the PPC target, with `HX_NATIVE`
defined. Wherever the source says

```cpp
#ifdef HX_NATIVE
    <hand-written native body>   // what the playable build runs
#else
    <decompiled body>            // what objdiff measures
#endif
```

every decomp instrument in this repo — objdiff under all three rulers, `report.json`,
unicorn, every scanner under `scripts/analysis/` — measures **only the second branch, by
construction**. A semantic difference between the two is a native-only bug that no match
percentage can see. A function can read 100.0% with zero mismatch rows while the native
game runs a different algorithm.

Some guards are legitimate: LP64 pointer width, endianness, platform APIs (XAudio, D3D,
Kinect, XDK threads/files), the 64 KB Xbox stack, ObjRef ring-walk safety, compiler/STL
spelling. Others silently changed behaviour, usually because the native body was written
**before** the decompiled one existed, or because an LP64 fix changed more than the width.

Calibration case: `ObjPtrVec<T1,T2>::erase` (`src/system/obj/ObjPtr_p.h`). The Xbox body
honours `mEraseMode` (swap-last moves the last element into the hole); the native body
was a bare `mNodes.erase(it)`, so four swap-last vectors reordered differently on native.
Fixed on `fix-ptrvec-erase` (landed as `0a5594a27`). The tool reports it.

## What the tool sees

`native_shadow_audit.py` enumerates every conditional group in `src/` whose condition
names `HX_NATIVE`, resolves nesting and `#elif`/`#else` against enclosing guards (a
`#ifndef HX_NATIVE` nested inside `#ifdef HX_NATIVE` is dead in both builds and is dropped
as such), and classifies each group by **shape**:

| bucket | shape | meaning |
|---|---|---|
| (a) | REPLACES | both a native-only and a PPC-only branch carry code |
| (b) | ADDS | only the native-only branch carries code |
| (c) | REMOVES | only the PPC-only branch carries code — the decomp body is simply not compiled natively ("the guard swallows the definition") |
| (d) | overlay | the group is at file/class scope and its branches contain function **definitions** (`file-scope-defs`), or it covers ≥ 90% of the file (`whole-file`) |

and maps each group to its enclosing function (column-0 heuristic, including
`BEGIN_HANDLERS(X)`-style macros) or lists the functions defined in each branch.

It then **pairs functions across groups**. A function whose PPC definition sits in one
guard and whose native definition sits in another is a **split shadow** — two regions of
raw shape (c) and (b) that are semantically one (a). This is exactly the ObjPtrVec shape
(`#ifndef HX_NATIVE` block at line 797 holding the Xbox bodies, `#ifdef HX_NATIVE` block
at 906 holding the native ones); a per-region count files it under (b)+(c) and hides it.
Pairing also runs **cross-file** (qualified names only): `Edge::operator<` had its PPC body
in `rndobj/Utl.cpp` and a native-only copy in `rndobj/AmbientOcclusion.cpp`.

### Denominators on the tree this was written against (main @ `0e1dcb139`)

Universe: **1,164** `#if/#ifdef/#ifndef/#elif` lines naming `HX_NATIVE` in 2,600 files
under `src/`, counted by an independent regex pass before the parser runs.

| | count |
|---|---|
| examined (classified) | 1,146 |
| (a) REPLACES | 340 |
| (b) ADDS | 688 |
| (c) REMOVES | 118 |
| (d) overlay (file-scope-defs + whole-file) | 136 |
| function shadows | 54 (26 one guard, 23 split pairs, 5 cross-file) |
| dropped: vendored `src/xdk` | 9 |
| dropped: no code in either build | 9 |
| parser drops | 0 |

Every directive is either classified or dropped with a reason; a parser that loses one
leaves the books unbalanced and exits 4, and any *parser* drop (unbalanced file,
unparseable condition) also exits 4 rather than printing a smaller clean census.

## What it cannot see

A zero from this tool is not "no shadows". It is blind to:

- **Build-level shadows.** A `src/` file the native CMake does not compile at all
  (`native/CMakeLists.txt` source lists, `REMOVE_ITEM`, the excluded `synth_xbox/`,
  `rnddx9/`, `*_Xbox.cpp`), replaced wholesale by `native/src/**` or
  `../milo-native-engine`. No preprocessor guard exists there.
- **Replacements that live outside `src/`.** A (c) region whose native counterpart is in
  `native/src/` (e.g. `RndTex::PreLoad`/`PostLoad` → `native/src/platform/RndTex_Native.cpp`,
  `StreamReceiver::New` → `StreamReceiver_Native.cpp`) is reported as a bare REMOVES; the
  tool does not find the other half.
- **Other platform macros.** Only `HX_NATIVE` is modelled; `_XBOX`, `__EMSCRIPTEN__`,
  `HX_WEB`, `_WIN32` … are assumed equal in both builds.
- **Non-guard divergences** that are nonetheless native-only, e.g. a container whose
  element size changed under LP64 feeding code with a hard-coded stride (the `Locale`
  sort below: the *search* was guarded, the *sort* was not).
- **Semantics.** It is an inventory. Deciding legitimate-vs-divergent is manual triage
  against the **target listing** (`build/373307D9/asm/**`) — the decompiled branch is not
  ground truth either.

## Triage recognizer

Legitimate (no gameplay-observable change): LP64 width (`size_t` operator new,
`intptr_t`, full-pointer compares), ENDIAN (host-relative swaps), API (host replacement
of an Xbox facility with the same contract), RENDERER (WebGPU replacing D3D), STACK,
RING (ObjRef ring-walk safety), COMPAT (STL/compiler spelling, orphan-instantiation
probes), DEBUG, STUB (hardware absent natively: Kinect camera, voice, online).

The tells of a **drifted** native body — each seen at least once in this audit:

1. **The native body predates the decompiled one.** `git log -S` on the guard shows an
   early "progress" / "Added stubs" / "native port" commit, and the `#else` body landed
   months later with its own matching work. Nobody re-derived the native side
   (`HamCharacter::Poll`, `AppLabel::SetTimeElapsedSince`, `LoadMgr::PollFrontLoader`).
2. **A container type swap that changes a default.** `std::list<Object*>` → `ObjPtrList`
   picks up `kObjListNoNull`, whose `insert` drops null — and null was a meaningful queue
   entry (`FlowQueueable::mListeners`).
3. **A literal re-typed for u16/LP64 and re-chosen instead of copied.** `L" .,"` became
   `{' ', '\t', '\n'}` (`RndText::FitTextEllipsis`).
4. **An LP64 fix that also changed the predicate.** Widening `(int)obj > 0xfffff060` to
   `uintptr_t` is right; adding `!obj ||` is not (`JsonConverter::LoadFromString`).
5. **Bitfield order under a compiler change.** MSVC/Xenon allocates MSB-first, clang
   LSB-first; the same declaration puts the field in different bits
   (`kdTreeNode::mData.index` landed in the float's sign/exponent bits).
6. **One half of a pair fixed for LP64, the other not.** A search compared full pointers
   while the sort it depends on still read a 32-bit stride (`Locale`).
7. **A stray duplicate definition in another TU wins the native link** once the real one
   is guarded out (`ToggleDrawSkeletons` in `HamUI.cpp`).
8. **Debug scaffolding removed together with semantics.** The glitch watchdog and ark
   logging are debug; the `mLoaderPos` save/set/restore next to them is not.
9. **A removed-API replacement with different side effects.** `std::random_shuffle` →
   `std::shuffle(..., default_random_engine(RandomInt()))` consumed a draw from the game
   RNG that the image never takes.
10. **A native test encodes the native behaviour.** A test written against the native
    body pins the bug; adjudicate the test against the listing too.

Workflow that produced every fix here: read the region, compare to the listing at the
decisive instructions, write a native gtest, **watch it fail against the current native
body**, fix the native body (platform-correct *and* faithful), watch it pass, and prove
PPC neutrality (edits inside `#ifdef HX_NATIVE`: rebuild the `.obj` with and without the
edit — same hash; PCH-reached headers: full `ninja` + `report.json` comparison).

## Results of the 2026-09-30 audit

**Read: 464 of 464 triage regions** — every (a) REPLACES and (c) REMOVES region (458) plus
the 6 (b) regions that shadow a function defined elsewhere, split by area across six
read-only triage passes (obj/math/utl 112, hamobj/game/flow/meta 83, char/meta_ham/ui/world
71, rndobj 79, audio/movie 62, os/gesture/net/App 57). The 688 (b) ADDS regions were **not**
triaged by this pass; many are defensive null checks and DTA-flow shortcuts, and some change
behaviour (see "notable" below for the ones that surfaced incidentally). They were triaged
the same day on `native-additions` -- see "(b) ADDS triage" at the end of this file.

### Divergences fixed (one commit each on `native-shadow`; test watched failing first)

| file : function | wrong native behaviour | test |
|---|---|---|
| `flow/FlowQueueable.h` `mListeners` | `ObjPtrList` in `kObjListNoNull` mode dropped the NULL entries that are listener-less queued triggers — a kQueue flow re-triggered while running never re-ran | `NativeShadowTest.FlowQueueableQueuesListenerlessTriggers` |
| `rndobj/Text.cpp` `RndText::FitTextEllipsis` | trim set `{' ','\t','\n'}` instead of the image's `L" .,"` — "Hello,..." instead of "Hello..." | `NativeShadowUnit.TextEllipsisTrimSetIsTheImagesLiteral` |
| `gesture/DrawUtl.cpp` `ToggleDrawSkeletons` | image body guarded out; a stray `HamUI.cpp` copy won the link and could never turn skeleton drawing on | `NativeShadowTest.ToggleDrawSkeletonsFlipsShowing` |
| `utl/Locale.cpp` `LocaleChunkSort::Sort` / `FindDataIndex` | sort keyed on the signed low 32 bits of the symbol pointer (16-byte DataNode under an 8-byte stride), search on the full unsigned pointer — ASLR-dependent missing UI text | `NativeShadowUnit.LocaleChunkSortOrdersByTheSearchKey` (+ tie-break control) |
| `math/Rand.h` `RandomShuffle` | `std::shuffle(..., default_random_engine(RandomInt()))` consumed a game-RNG draw per shuffle; image uses CRT `rand()` | `NativeShadowUnit.RandomShuffleLeavesTheGameRngAlone`, `...IsDrivenByCrtRand` |
| `utl/Loader.cpp` `LoadMgr::PollFrontLoader` | dropped the `mLoaderPos` save/set/restore, so StayBack sub-dir loads jumped the queue | `NativeShadowTest.PollFrontLoaderPublishesTheLoadersPosition` |
| `obj/ObjPtr_p.h` `ObjPtrList::Unlink` | erase of the tail returned `end()`; the image returns the predecessor | `NativeShadowTest.ObjPtrListEraseTailReturnsPredecessor` |
| `obj/Dir.h` `operator<<(BinStream&, ObjDirPtr)` | wrote `dir->Name()`; the image writes the file path its reader reopens | `NativeShadowTest.ObjDirPtrSavesTheFilePathNotTheName` |
| `os/Debug.h` `MILO_LOG` | evaluated its arguments twice | `NativeShadowUnit.MiloLogEvaluatesArgumentsOnce` |
| `math/kdTree.h` `kdTreeNode::mData` | clang LSB-first bitfield put the split axis in the float's sign/exponent bits | `NativeShadowUnit.KdTreeSplitAxisLivesInTheLowMantissaBits` |
| `rndobj/Utl.cpp` / `AmbientOcclusion.cpp` `Edge::operator<` | native copy keyed max-major; image min-major (latent: find/insert only) | `NativeShadowUnit.AmbientOcclusionEdgeOrderIsMinMajor` |
| `net/JsonUtils.cpp` `JsonConverter::LoadFromString` | LP64 rewrite also rejected NULL (payload `null`), which the image wraps | `NativeShadowUnit.JsonLoadFromStringWrapsANullPayload` |
| `synth/StandardStream.cpp` `ConsumeData` (3 commits) | capped every jump (forward jump stalled the stream: practice `set_loop`, forward crossfades); ignored `mFloatSamples` (int16 readers read as float); channel table / remap / 0x800 chunk differed | `test_native_shadow_stream.cpp` (11 tests) |
| `rndobj/Cam.cpp` `RndCam::WorldToScreen` / `ScreenToWorld` | mirrored screen y; ortho depth/scale wrong; forced `UpdatedWorldXfm()` — flares drawn mirrored, CamShot follow filter off. Two expectations in `test_rndcam_projection.cpp` had encoded the native bug | `test_native_shadow_cam.cpp` (4 tests) |
| `rndobj/Ribbon.cpp` `ConstructMesh` / `UpdateMesh` / `UpdateChase` | empty on native | `test_native_shadow_bodies.cpp` (3 tests) |
| `lazer/meta_ham/AppLabel.cpp` `SetTimeElapsedSince` | empty stub on native — "last played" never updated | `test_native_shadow_bodies.cpp` |
| `hamobj/HamCharacter.cpp` `HamCharacter::Poll` | early native rewrite dropped the `bone_prop0`/`spot_prop0` prop-attach blend and the `robot_face.mat` viseme texture swap (every frame, every dancer) | `test_native_shadow_hamchar.cpp` (6 tests) |

The calibration case (`ObjPtrVec::erase`) was found by the tool as a split-pair shadow
(`ObjPtr_p.h` 797 REMOVES + 906 ADDS) and was fixed separately on `fix-ptrvec-erase`.

**A (b) ADDS region is not safe to skip.** A concurrent lane (`native-menulist`) root-caused
the "menu list items never draw" bug to a native-only `HamNavList::OnMsg(UITransitionCompleteMsg)`
handler that called `StopAnimation()` and froze every list's `enter.anim` at frame 0
(alpha 0). The inventory holds it — `HamNavList.cpp:148` (the `HANDLE_MESSAGE` line) and
`HamNavList.cpp:1654` (the definition), both **(b) ADDS** — but this audit triaged only
(a) and (c), so it was not caught here. It is fixed by `native-menulist`, not on this
branch. The 688 (b) regions are the next triage pass.

### Legitimate but behaviour-changing (NOTABLE) — not fixed, recorded

- **Tombstones in NoNull containers.** Native `ReplaceRefs`/`ReplaceList` set
  `gInReplaceList`, and `ReplaceNode` then skips the erase for `kObjListNoNull`
  lists/vectors (`ObjPtr_p.h:276/562`, `Object.cpp:553`, `Object.h:233`); the image erases
  unconditionally (`ReplaceNode@?$ObjPtrList@VUILabel` 8278C364 `bl erase`). Deleting an
  object leaves a null node in every NoNull container that held it (`size()` wrong,
  iteration hits null). Ring-walk safety is the reason; a faithful fix needs deferred
  compaction after the walk. `native/tests/test_object_lifetime.cpp` accepts the
  tombstone. **Highest-value open item in `obj/`.**
- **Native is pinned in controller mode** (`GestureMgr.cpp:43/423`): `SetInControllerMode(false)`
  is ignored, so every `InControllerMode()` reader takes the controller branch.
- **`HamPlayerData::IsPlaying` always true** (`HamPlayerData.cpp:185`) — player 2 always scored.
- **`MoveDir` async detector never fed** (`MoveDir.cpp:2413`) — phrase meter / `rating_frac`
  never update from live play.
- **`FreestyleMoveRecorder` Poll/Start/StopRecording emptied** (`:148`, `:414`) — including
  the parts that need no camera (timers, dancer-take recording).
- **RESOLVED on `native-animbypass` (2026-09-30): the "animations never settle" family.**
  `HamNavList::Poll` select completion and `HamNavList::OnMsg(ButtonDownMsg)` now check
  `IsAnimating()`; `HamPanel::Exiting` runs the image body (`UIPanel::Exiting()` + nav-list
  animation); `UI.cpp`'s 90-frame enter force-complete is gone; `MetaPanel::Exiting` waits
  on `TheMetaMusic->IsActive()` again; `GamePanel::StartGame` calls `Game::Start()` only when
  `HasIntro()`. Each was adjudicated against the image and runtime-checked (perform, dance
  battle, practice, 25x main<->choose_mode): the belief came from the native
  `transition_complete -> StopAnimation()` handler removed by `native-menulist`, and every
  wait now observed ends (enter anim ~20 UI frames, panel exit <=24, metamusic fade 2 s wall
  clock). One consequence is faithful and visible to tooling: a press during a nav list's
  enter animation is dropped, so input scripts wait +30 on nav-list screens and
  `DC3_FAST_BOOT` keeps title_screen's 60-frame delay. Tests:
  `native/tests/test_native_animbypass.cpp`.
- **`MetaPanel`** never creates `Campaign`/`MetaMusicManager`/`HAQManager` (not part of the
  family above; still open). **`Game.cpp:1105/1158`**,
  **`GameMode.cpp:24`**, **`PreloadPanel.cpp:64`** are DTA-flow shortcuts.
- **`MoggClip::LoadNumChannels`** (`MoggClip.cpp:323`) calls `SynthPoll()` instead of
  `Play(0)`, so `mNumChannels` is always -1 and stereo moggs never disable pan;
  **`StreamReceiver::Poll`** (`:90`) counts polls-after-drain instead of silence buffers,
  so `kFinished` arrives early; native **`VorbisReader::Poll`** passes `startSamp=-1`, so
  `mCurrentSamp` never resyncs to the granule position.
- **`App::App`** native boot skips `audio_mixer.milo`, `EnableKeyCheats(true)`,
  `SaveLoadManager::Init` and others; **`Geo.cpp:1094` `MakeBSPTree`** returns false, so
  `kVolumeBSP` meshes lose collision; **`Draw.cpp:110`** frustum culling off;
  **`Text.cpp:2731`** re-lays out text every draw; **`Mesh.cpp:1291`** native `OnSync`
  does not reorder faces / rebuild patches; **`Tex.cpp:137`** native Pre/PostLoad drops
  revision gating; **`FileMerger.cpp:489`** passes a parent dir the image does not.
- **`Song.cpp:291`** defers the unpause to `Poll`, so Play-then-Pause ends up playing.
- **`Dir.cpp:921`** three-phase delete skips owner `Replace()` callbacks during teardown;
  **`ObjPtr_p.h:129`** resolves owner-less refs by walking parent dirs and `Main()`.

### Open leads (what would decide each)

- **`HamCharacter::Poll` force-show** — native `SetShowing(true)`s any hidden character;
  the image does so only with `mPollWhenHidden`. Kept as a labelled workaround (tried and
  reverted once). Measured in one gameplay flow: only `iconman` is hidden at Poll time
  (8,400/8,400 polls), the four dancers never; with the image's gate swapped in, 48/48
  gameplay tests passed. Decide by running the same counter over menus, practice,
  campaign and crew select.
- **`Dir.cpp:662` `HasDirPtrs`** — address-keyed DirPtr counter can go stale when
  `NullifyObj` clears a DirPtr without decrementing; assert counter == ring walk in a
  native debug run.
- **`UIListWidget.cpp:189` `DisplayColor`** — native returns the default colour for
  out-of-range states, blamed on `HamListRibbon::DrawRibbon` field reuse; check whether
  the image's DrawRibbon writes draw-state offsets 0x28/0x2c.
- **Ogg CRC skip (`oggvorbis/framing.c:571`) + `NativeDecrypt` HMXA scan
  (`VorbisReader.cpp:531`)** — the image *does* validate Ogg CRCs; count CRC failures and
  mid-buffer HMXA hits over every v0xE mogg.
- **`HamDirector.cpp:741` `SongAnim`** (an (b) ADD) — falls back to the expert
  choreography when the routine-builder anim is empty; decide whether the remixer now
  populates it natively.
- **`audio_mixer.milo` not loaded at native boot** — compare fader/send parameters at song
  start with and without it.
- **`CharPollGroup.cpp`** — `DC3_POLL_ORDER_FIX=0` flips to a polarity its comment
  wrongly calls "the PPC polarity" (the default is correct).

## (b) ADDS triage (branch `native-additions`, 2026-09-30)

**Read: 689 of 689 (b) regions at body level** (the inventory on `3f59e5b4c`; 682 after the
fixes below removed 7). **Adjudicated against the target listing: 97** -- 92 of the 301
pre-classified SUSPECT regions, plus 5 others -- by this lane and two read-only
sub-agents. The other **209 suspect regions were judged by class from their bodies, not
against the asm**: mostly null/bounds guards that only differ where the image would read
its zero-mapped page 0 or has already failed a fatal `MILO_ASSERT`/`MILO_FAIL`, absent-
hardware stubs (Kinect, voice, sign-in, LIVE), and diagnostics the classifier could not
prove inert. Read those as unexamined, not clean.

### The pre-classifier

`native_shadow_audit.py` now puts every ADDS region into exactly one of 17 buckets
(`classify_addition`, `B_CLASSES`; `--list b-suspect`): `handled:*` (owned by
`native-animbypass`, or already judged above), `plumbing:*` (decl, diag, debug-optin,
ring, lp64-endian, platform, native-def) and `suspect:*` (handler, timeout, hw-stub,
null-guard, early-return, forced-state, extra-call, unrecognised). It is a triage aid,
not a verdict: it reads shapes, not semantics. On `3f59e5b4c`: handled 13, plumbing 375,
suspect 301 (of 689). After this branch's fixes, rebased on `7add51c15` (native-animbypass
landed and removed its regions, so `handled:animbypass` is now empty): handled 11,
plumbing 375, suspect 295 (of 681). Known confusions: a region with an unrecognised diagnostic line
lands in `suspect:extra-call`; the `hw-stub` bucket keys on comment words. Contract:
the bucket counts ride the coverage JSON as `b_classes` and must sum to the ADDS count or
the run exits 4; tests in `scripts/analysis/tests/test_native_shadow_audit.py` fail on
the pre-extension tool (3 of 8).

### Divergences fixed (one commit each; test watched failing first)

| file : function | wrong native behaviour | test |
|---|---|---|
| `synth/StandardStream.cpp` `UpdateTime` | a second, never-paused, never-reset wall clock switched the stream to "timer fallback" whenever audio lagged it 10x -- after any long pause or any `Resync`, with a real device too: song time jumped forward by the pause, drift correction off for good. Now: no `AudioDevice` -> `mTimer` (the image's clock) | `NativeAdditionsStreamTest.PauseDoesNotAdvanceSongTime` (+ headless control) |
| `world/CameraShot.cpp` `CamShotFrame::BuildTransform` | early return for every targetless keyframe skipped the path, the whole parent block and the dynamic offsets -- parented/path shots froze | `NativeAdditionsCamShotTest.TargetlessFrameFollowsItsParent` |
| `ui/UI.cpp` `UIManager::Poll` | `mSink = trans` on every transition: the screen saw every `ui` message before the typedef and C++ handlers, and got unhandled ones twice | `NativeAdditionsUITest.TransitionDoesNotMakeTheScreenTheSink` |
| `meta_ham/Overshell.cpp` `OvershellSlot::SetPlaying` | unconditional `return;` -- `player_join`/`player_quit` never exported (reset_detection, drop-in grace, flashcards) | `NativeAdditionsOvershellTest.SetPlayingExportsJoinAndQuit` |
| `ui/UIScreen.cpp` / `ui/UIPanel.cpp` `Enter` | any panel named `*tutorial*` refused to enter -- Options/Pause -> Tutorials opened empty | `NativeAdditionsTutorialTest.*` (+ ordinary-name control) |
| `game/Game.cpp` `Game::Restart` | `mLoadState = 0` on a false premise re-ran the whole song load chain (PostLoad recreating `mOvershell`, LoadMoveData, ...) at every song start; telemetry read `gameLoadState=0` all through gameplay | `DtaFlowTest.SongLoadChainRunsOncePerSong` |

### Legitimate but notable

- `StandardStream::Play` pump/prefill, `Rnd::PreInit` 1280x720 (the image's
  `DxRnd::InitBuffers` also forces 720p), `Splash` non-threaded path,
  `WaveToTurnOnLight::EnableWaveState` (the image's own NUI-failure branch),
  `ShellInput::SyncVoiceControl`, `SkeletonChooser::GetPlayerSide`, the signin fictions
  (`GetSignedInProfiles`, `SetAssociatedPadNum` -- the latter also drops a -1
  re-association blip), `HamInit` provider defaults (no-ops on real assets:
  `flow.dta` defines them), `GameModeInit gameplay_mode` (cleared by the first
  `SetMode`), `LoadingPanel` ready gate (web only), `HamMaster::Poll` MIDI with no stream
  (web only), `Game::PostWaitStart` audio-failed branch (web only).
- Dead code: `Synth::NewStreamDecoder` mogg branch (NativeSynth overrides it),
  `PropKeys::Replace` (the owner dispatch reaches `RndPropAnim::Replace` natively too),
  `RndText::OnComputeCharWidths` reset (the image resets in `AcquireFontMap`),
  `Splash::EndSplasher` camera clear (`~RndCam` clears it).
- `DataNode::Evaluate` null property returns `sNullNode(0)` -- the same value the 360
  reads from page 0 after the Continue dialog.

### Open leads (divergent, not fixed here -- with what blocks each)

- ~~**`MultiUserGesturePanel` `mNativeEnterPending`**~~ -- **FIXED on `native-gameflow`**
  (see "Game-flow leads" below): the screen is driven by the controller, as on the 360.
- ~~**`GamePanel::StartGame`** forces `game_stage playing`~~ -- **FIXED on
  `native-gameflow`**; `DtaFlowTest.GameplayReachesPlayingState` re-pointed at the real
  state.
- ~~**`PoseFatalities::Poll`** unconditional return~~ -- **FIXED on `native-gameflow`**;
  what still cannot run without skeleton input is recorded there.
- **`MetaPanel::Init` `sUnlockAll = true`** -- everything unlocked, campaign reads
  finished, profile reads cheated. Its premise (asserts on an empty profile list) is
  stale since `InitNative`; removing it is a product decision (no save system natively).
- **`HamDirector::OnFileMerged` HUD block + `GamePanel::SetTypeDef` `common_reset`** --
  repositions score/flashcard transforms and snaps the show-score anims; the root is the
  native HUD camera / draw path, not the merge.
- **`UIManager::GotoScreenImpl` refuses `*campaign*` screens** -- Story mode unreachable;
  blocked on `MetaPanel` never creating `Campaign`.
- ~~**`UIScreen::OnMsg(ButtonDownMsg)` -> `skip_selected`**~~, ~~**`MoviePanel::Poll`
  `IsOpen` guard**~~ -- **FIXED on `native-gameflow`** (with `FFmpegMovieImpl::Poll`);
  **`HamDirector::FindNextShot` Area1_WIDE fallback** (image keeps the shot and notifies),
  **`HamNavList::RealRefresh`** recreates every widget on every refresh,
  **`UIList::Refresh`** display recount, **`UI.cpp OnGotoScreen`** null -> main_screen
  (kept on `native-gameflow` but now a `MILO_WARN` on every hit),
  **`WorldCrowd::DrawShowing`** static additive impostor cache,
  **`SkeletonChooser::DoesRequireHandRaise`** always false.
- **`ObjectDir::FindObject`** proxy/parent-loader fallback binds pointers the image leaves
  null -- log every non-null fallback hit over a boot and a song.
- **`RndDir::SyncObjects`** moves `*ikfoot*`/`*feetandhands*` polls last **by default**
  (`DC3_FEET_PLANT_FIX_OFF` to disable) although every sibling of that experiment is
  opt-in and `Dc3FeetPlantFix()` calls itself non-functional; the image does not reorder.
- **UNSURE:** `HamDirector::Poll` drives `songAnim->SetFrame` from the beat (double drive
  if `select_camera` also fires -- count `OnSelectCamera` calls); `SetupRoutineBuilderAnims`
  `mLoop=false` (read the routine anim's EndFrame after `ResetRemixer`);
  `CharForeTwist`/`CharUpperTwist` write `mLocalXfm` (compare Xenia twist-bone telemetry).
- **`AnimTask::Poll` (unowned -- flagged to `native-animbypass`, not in its merge):** it nulls `mAnimTarget` before the `ended`
  listener runs; the task is deleted on the same frame either way and `IsAnimating()` does
  not observe it, but a new AnimTask started from `ended` on the same target no longer
  finds the finishing task as its `mBlendTask`.

## Game-flow leads (branch `native-gameflow`, 2026-09-30)

Five of the (b) ADDS open leads above were menu / gameplay FLOW shortcuts. Each was
adjudicated against the target listing, restored, and runtime-checked on the perform and
dance-battle routes (`scripts/native_assert_harvest.py`, now pressing through
multiuser_screen). All five are `HX_NATIVE`-only on the decomp side: the six touched PPC
objects were rebuilt with and without the branch and hash identically (`tree_sha256`
`329cf14c...` both ways).

| lead | why it existed | image | now | test (watched failing first) |
|---|---|---|---|---|
| `PoseFatalities::Poll` early `return;` | March: "LP64 struct mismatch" in Player/Side lookups | `82496DA0`: whole body from `bl InStrikeAPose` | image body | none in-process (needs director, venue, HUD); probe A/B below |
| `GamePanel::StartGame` sets `game_stage playing` | March: "the intro may be skipped" | `8287AE28`: HasIntro/Start, SetInGame, `mState=2`; no property | removed | `DtaFlowTest.GameplayReachesPlayingState` re-pointed: first `gameStage=playing` telemetry sample must follow `Game::Poll: intro timer expired` (pre-fix: frames 843-845 read `playing` before it) |
| `MultiUserGesturePanel` auto-fires `enter_gameplay` | March: "no Kinect skeleton chooser" | `82942EB8`: `UpdateNavLists` x2, `UpdateProviderPlayerIndices`, `TexLoadPanel::Poll` | removed; `SetAssociatedPadNum(0,0)` kept at Enter as the enrollment stand-in | `DtaFlowIdleMultiuserTest.MultiuserScreenWaitsForInput` (`idle-multiuser.txt`, no presses: pre-fix it left for loading_screen) |
| `UIScreen::OnMsg(ButtonDownMsg)` fires `skip_selected` on any button | March: "movie panels aren't functional (no BINK)" | `827A3AF0`: only Cancel -> `go_back_screen`, only with `mBack` | removed | `NativeGameflowUITest.ButtonOnAMovieScreenIsNotSkipSelected` |
| `MoviePanel::Poll` returns when the movie is not open; `FFmpegMovieImpl::Poll` returned `true` when not open | March stub era: "avoid infinite movie_done loop" | `82E0F970`: no IsOpen test; `BinkMovieImpl::Poll` `82E25DE0` returns false with no HBINK | movie_done fires for a movie that did not open (attract advances by itself -- no `.bik` ships) | `NativeGameflowMovieTest.UnopenedMovieReportsDone`, `.PanelFiresMovieDoneWhenTheVideoDidNotOpen` |

**multiuser_screen with a controller** (native is pinned in controller mode, so the image's
own controller path is what runs): perform -- `right_hand_p1` `beginner` (seldiff_pane) ->
`play` (startgame_pane) -> `skip_waiting` (readywait_pane) -> `start_game`. Dance battle
(`requires_2_players`: readywait's list is disabled, `can_enter_game` needs both sides) --
p1 difficulty, `play`, **DLeft** (`OnMsg(ButtonDownMsg)` moves focus to `right_hand_p2`),
p2 difficulty, `play_title` -> `start_game`. Presses 70 frames apart: each pane change
replays the list's enter animation. Party mode's `crew_throwdown_multiuser_screen` used to
be auto-fired too (gameplay with no song, bounced to main_screen through the silent
`OnGotoScreen` fallback); it now waits, and both captains can pick a crew with the
controller. It then waits in readywait: party mode's ready flags / `is_team_signed_in` were
not pursued.

**Fatalities: what runs and what cannot.** Nothing natively ACTIVATES a fatality: dance
battle rates moves from MoveDir's async detector (`last_detector_result`), which returns 0
without a skeleton feed (`MoveAsyncDetector::MoveRatingFrac`'s `SkeletonUpdate` guard), so
no final-pose move is ever rated <= 1 and `HamDirector::CheckBeginFatal` never fires.
`scripts/native_fatality_probe.sh` stands in for exactly that one event
(`{meta_performer move_passed 0 Finishing_Move_macarena.move 0 1.0}`, what
`dance_battle.dta`'s handler calls on a perfect rating) and turns on Autoplay, which
`UpdateMatchingPose` accepts as matching (its skeleton compare reads 0 natively). Injected
at beat ~200:

| probe beat | pre-fix (Poll returns) | image body |
|---|---|---|
| +7 | fatal_active=1, stage=`none` | fatal_active=1, stage=`playing` (OnBeat) |
| +12 | same | p0 score 20000 |
| +18 | same | p0 score 96000, fatal_active=0 |
| +24 | same | stage=`outro` (fatals_over) -> endgame -> complete -> song_select |
| end | gameover at 267, then stuck on game_screen (game_won waits on `fatal_active`) until the run died at beat 646 (std::bad_alloc in Dawn's shader compiler) | 0 crashes |

A mid-song activation is artificial (the final pose ends the battle): after it the song
ends through fatals_over, so `GamePanel` never reaches `gameover` and the harvester's
`gameover` checkpoint reads NOT REACHED although every post-song screen is entered. Injected
at beat ~250 the run reaches gameover normally. Strike-a-pose (a party-mode minigame) was not
driven.

**Web is not covered.** `WebMovieImpl::Poll` keeps its "not open = still playing"
convention and was not built here; with the `UIScreen` shortcut gone a stalled web movie is
left the image's way (confirm on `movie_overlay_panel`'s list), not by any button.

