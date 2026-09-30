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
- ~~**`FreestyleMoveRecorder` Poll/Start/StopRecording emptied** (`:148`, `:414`) — including
  the parts that need no camera (timers, dancer-take recording).~~ **FIXED on
  `native-partyplay`**: it crashed Make Your Move (see "Party mode" below).
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
- ~~**`Dir.cpp:921`** three-phase delete skips owner `Replace()` callbacks during teardown~~
  (owner-control holders stepped since `native-lifetime2`, see the last section);
  ~~**`ObjPtr_p.h:129`** resolves owner-less refs by walking parent dirs and `Main()`~~
  (removed on `native-suspects`).

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
- ~~**`CharPollGroup.cpp`** — `DC3_POLL_ORDER_FIX=0` comment~~ -- corrected on
  `native-engineleads` (the default is the image's polarity; the opt-out is not).

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
  ~~**`HamDirector::FindNextShot` Area1_WIDE fallback**~~,
  ~~**`HamNavList::RealRefresh`** recreates every widget on every refresh~~ -- **FIXED on
  `native-engineleads`** (see "Engine leads" below),
  **`UIList::Refresh`** display recount, **`UI.cpp OnGotoScreen`** null -> main_screen
  (kept on `native-gameflow` but now a `MILO_WARN` on every hit),
  **`WorldCrowd::DrawShowing`** static additive impostor cache (KEPT, see "Engine
  leads"), ~~**`SkeletonChooser::DoesRequireHandRaise`** always false~~ -- **FIXED on
  `native-engineleads`** with the `GameMode` ctor defect it hid.
- ~~**`ObjectDir::FindObject`** proxy/parent-loader fallback~~, ~~**`RndDir::SyncObjects`**
  foot-IK reorder~~, ~~`HamDirector::Poll` SetFrame~~, ~~`SetupRoutineBuilderAnims`
  `mLoop=false`~~, ~~`CharForeTwist`/`CharUpperTwist` `mLocalXfm`~~, ~~**`AnimTask::Poll`**
  nulls `mAnimTarget`~~ -- **all FIXED on `native-engineleads`**; see "Engine leads" below.
  ~~The `ObjPtr_p.h` `ObjRefConcrete::Load` parent-walk / `Main()` fallback~~ -- **measured
  (0 hits) and removed on `native-suspects`**; see "Suspect pass" below.

## Engine leads (branch `native-engineleads`, 2026-09-30)

The engine-side (b) ADDS leads. Each was adjudicated against the target listing and
measured at runtime before the native block was removed; every touched PPC object hashes
identically with and without its change (the edits are `HX_NATIVE`-only, or remove an
`#ifndef HX_NATIVE` around code the image runs). Tests live in
`native/tests/test_native_engineleads.cpp` unless noted; each was watched failing first.

| lead | why it existed | image / measurement | now | test |
|---|---|---|---|---|
| `AnimTask::Poll` nulls `mAnimTarget` on completion | 2026-03 bulk commit: "DTA callbacks that would null mAnimTarget never fire" | its trigger is already a term of the end test below it, so it only changed ORDER: the target left the ring before `ended`, and an AnimTask chained from `ended` on the same target (TransAnim/MatAnim/CamAnim) lost its `mBlendTask` | removed | `AnimTaskEndedKeepsTargetForBlendChaining` |
| `ObjectDir::FindObject` ProxyDir/ParentDir fallback | `bca8a792f`: "MergeDirs flattens all objects on Xbox" | FindObject 100% matched, no fallback. Instrumented perform route: 6,266 hits, all mid-load; 6,138 via `FlowPtrBase::LoadObject` (one level deep = what the image's own `FlowPtrGetLoadingDir` finds; a proxy of a proxy -- results_cluster in perform_endgame -- bound `bg_*_color.anim` the image cannot reach), ~100 `gLoadingProxyFromDisk` ObjPtr loads into discarded temporaries, 27 `CharacterTest::mDriver` bound to the PARENT's `main.drv` | removed (`DirLoader::SetParentDir` kept: the `ObjPtr_p.h` fallback reads it) | `FindObjectDoesNotSearchALoadingProxysParent` |
| `RndDir::SyncObjects` moves `*ikfoot*`/`*feetandhands*` last, default ON | `d5b9a6911` opt-in foot-plant experiment ("belt-and-suspenders") | fires only on HamCharacter dirs, which `Character::SyncObjects` re-sorts right after: sorted orders identical in 53/53 syncs; toe/ankle telemetry identical on, off, removed | removed; the sibling comments that said "opt-out" now say opt-in | `RndDirSyncObjectsKeepsHarvestOrder` |
| `HamDirector::Poll` sets the song anim frame | `d24e35534`: "select_camera never dispatched natively" | image Poll has no SetFrame; measured `OnSelectCamera` 7553 drives + Poll 7553, same frame each time = double drive | removed; telemetry counts the image path (`selectCameraSetFrameCount`) | `GameplayTelemetryTest.SongAnimIsDrivenOnlyBySelectCamera` (replaces `NativeSetFrameDrivesAnimation`) |
| `SetupRoutineBuilderAnims` `mLoop=false` | `d93b8d09b`: "EndFrame shrinks to ~0" | no mLoop store in the image; routine anim EndFrame 4466 (song-length), source song.anim Loop()=0 | removed | none -- inert on the only measurable asset, so no test could fail |
| `FindNextShot` Area1_WIDE retry | `7487524f8`: "abstract shot names don't match venue" | image stores null + notifies; 0 fallbacks / 0 notifies over two perform routes | removed | none -- no native input reaches it |
| `HamNavList::RealRefresh` CreateElements(NumShowing) | `bb9725429`: "provider set after Update() which created 0 elements" | no CreateElements in the image's RealRefresh; `96d39b2d5` already fixed Update's sizing (NumDisplay) | removed; `HideItem` bounds guard kept | route screenshots only (needs a UIListDir resource) |
| `SkeletonChooser::DoesRequireHandRaise` `return false` | `68d404dd9`: null DataNode deref off the perform path | removing it crashes on attract_screen: `gamemode: property [raise_hand_to_join] not found`. The real defect: the image's `GameMode` ctor calls `SetMode("init", "none")`; native skipped it (`72538161a`: "modes/HamProvider not ready") | both restored; `pose_scoring_gate.sh` PASS | `NewGameModeInstallsTheInitModeProperties` |
| `CharForeTwist`/`CharUpperTwist` rewrite `mLocalXfm` | `301d5aab8`: "UpperTwist polls after ForeTwist and dirties it" | image writes world only; the order came from the reversed sorter polarity (fixed 2026-07-02) and no CharUpperTwist exists at runtime (edit-mode CharacterTest only) | removed; hand telemetry within run-to-run noise | `ForeTwistPollWritesWorldNotLocal` |
| `CharPollGroup.cpp` comment: opt-out is "the PPC polarity" | 2026-07-02 | the image's inlined ChangedBy stores curDep to mTarget (`8235A138`) and recurses on the other: producer-first = the native DEFAULT | comment corrected; opt-out labelled non-image | -- |
| `WorldCrowd::DrawShowing` static additive impostor cache | `836c07024`: crowd chars "don't animate" + materials don't write alpha into the RT | image renders every impostor every frame with alpha cut | **KEPT**: the alpha half is a WebGPU render-to-texture limitation, not game code. Cache is keyed by `Character*` with no eviction on dir unload -- a lifetime hazard until the backend writes alpha and the block can go | -- |

Also measured along the way: `HamDirector::SongAnim`'s native "routine builder empty ->
expert anim" fallback did not fire on the perform route (the driven anim was
`player_1_routine_builder.anim` throughout), so the remixer does populate it natively.

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
not pursued. (Pursued on `native-partyplay`: see "Party mode" below.)

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


## Suspect pass (branch `native-suspects`, 2026-09-30)

The (b) ADDS triage above adjudicated 92 of its 301 `suspect:*` regions against the
image and judged the rest from their code. This pass took the whole current suspect list.

**Denominator.** `native_shadow_audit.py --list b-suspect` on `731b6137f`: **279** suspect
regions (of 665 ADDS). **35** are already judged, fixed or kept in the sections above,
or belong to the controller-mode pin they name. That leaves **244** regions. All 244 were read
and given a verdict. The per-region ledger, with its class and a one-line reason, is
`docs/analysis/2026-09-30-native-suspects-ledger.tsv`, which carries all 279 rows.

The evidence was not the same for every region, so the kinds are listed separately:

- **Matched body.** Every verdict names the enclosing function's decompiled body as the
  image's stand-in. For **206 of 279** regions that function reads 100.0 normalized in
  `report.json`, and for most of the rest it reads ≥ 97.
- **Target listing.** **7** regions were read in the asm directly: `Spotlight::BuildBoard`,
  the two `UIListMeshElement::Draw` blocks against `DxMesh::DrawShowing`,
  `WorldCrowd3DCharHandle::SyncProperty`, `RndParticleSys::UpdateRelativeXfm`,
  `SampleData::Dealloc`, and `MoviePanel::IsLoaded` against `BinkMovieImpl::Ready`. The
  ObjRef loads (not among the 279) were checked against their 100%-matched instantiations.
- **Runtime.** **28** native branches were instrumented and counted over the perform and
  dance-battle harvest routes (boot → menus → gameplay → results, every stage reached).
- **Code only.** The `feet` class (17 regions) is foot-plant/IK compensation owned by the
  feet-in-floor work. It was **not** adjudicated here.

| verdict | n | meaning |
|---|---|---|
| FIXED | 7 | below (plus `ObjRefConcrete::Load`, which is `handled:already-judged` and not in the 279) |
| divergent-held | 1 | `SetupRoutineBuilderAnims` retarget, reverted: unit held by another wave |
| image-fatal | 72 | differs only after the image has failed a fatal `MILO_ASSERT` / `MILO_FAIL`, or where the image dereferences null |
| image-page0 / image-ub / equivalent | 11 | the image reads zeroed page 0 or past a container's end; or the native branch is observably the same |
| inert-measured | 25 | the probe fired **0** times on both routes |
| inert | 8 | the guarded condition cannot occur natively; the reason is named per row |
| stub / stub-consequence | 42 | absent Kinect / voice / LIVE / save system, or a consequence of the doc's `SaveLoadManager` / `ProfileMgr` / `Campaign` notables |
| plumbing / diag / debug-optin / renderer / legit / dead / tombstone | 59 | inert by default or platform plumbing |
| feet | 17 | not adjudicated (see above) |
| renderer-workaround | 2 | open lead (below) |

### Divergences fixed (one commit each; test watched failing first)

Tests are in `native/tests/test_native_suspects.cpp`. Every edit is `HX_NATIVE`-only, or
under `native/src/`, or removes a native block around code the image runs. The PPC tree
hashes identically with and without the branch; the measurement is below.

| file : function | wrong native behaviour | image | test |
|---|---|---|---|
| `obj/ObjPtr_p.h` `ObjRefConcrete` / `ObjPtrVec` / `ObjPtrList::Load` (+ `Dir.cpp` `SetParentDir`, `Font3d.cpp`) | a missing name was searched up `Dir()` / the loader's ParentDir and then `Main()`, and an owner-less ref resolved against an explicit dir | every instantiation is 100% matched: `FindObject(name, false, true)`, resolved only when `refOwner && dir` | `ObjPtrLoadDoesNotSearchParentDirs`, `ObjPtrVecAndListLoadDoNotSearchParentDirs`, `OwnerlessObjPtrLoadResolvesNothing` |
| `world/Spotlight.cpp` `BuildBoard` | returned at the top ("no renderer"), so `sDiskMesh` stayed null. Lens disks and floor spots had no mesh, and their draw paths dereference it | `8282DDF8 bl BuildBoard` from Init; `8282BFF8` `New<RndMesh>` → `sDiskMesh` | `SpotlightInitBuildsTheDiskMesh` |
| `meta_ham/HamUI.cpp` `IsTimelineResetAllowed` | returned true whenever `TheSkeletonIdentifier` was null, which is always the case natively. A UI timeline reset was therefore allowed with a passive message queued or the help bar busy | 100% body: every term is required | `TimelineResetRefusedWhileAPassiveMessageIsQueued` |
| `native/src/platform/FFmpegMovieImpl.cpp` `Ready` + `meta/MoviePanel.cpp` `IsLoaded` | `Ready()` was false until a successful open, and no `.bik` ships. `MoviePanel::IsLoaded` skipped `Ready()` and the subtitles-loader wait | `BinkMovieImpl::Ready` `82E221C8`: loader `IsLoaded()`, else true | `MovieWithNoPendingLoadIsReady` |
| `world/Crowd3DCharHandle.cpp` `SyncProperty` | returned false for every path, including the resolved one | ICF-folded empty `BEGIN_PROPSYNCS` (`82711F80` → `827118E0`) | `CrowdCharHandleSyncPropertyResolvesTheEmptyPath` |

Two findings were landed and then reverted on this branch. Both sit in units held by a
concurrent wave (`~/tmp/dc3-wells/w8/phase4-w9-*.txt`), and lanes were asked to report
findings in held units rather than edit them.

- **`hamobj/HamDirector.cpp` `SetupRoutineBuilderAnims`** re-points copied PropKeys at
  `this` director. The image has no such walk, and a probe counted **0** retargets on both
  routes. Remove it when the unit is free.
- **`rndobj/Font3d.cpp` `RndFont3d::Load`**: the native explicit-`Dir()` `CharInfo::mMesh`
  load is equivalent to the image. The owner is the font, so the premise is stale. The
  removal did **not** hash neutrally, as the next subsection shows.

### PPC neutrality, measured

- **Method.** Full `ninja` in this worktree for three trees, comparing
  `patch_state.json`'s `tree_sha256` and the per-object sha256:
  - the branch as landed;
  - the branch with `main`'s copy of every touched `src/` file;
  - the branch plus a control edit.
- **Result.** The first two agree: `ef03e651…`, with **0 of 989** objects differing in
  content.
- **Control.** One PPC-visible literal in `Spotlight::BuildBoard` moved `Spotlight.obj`
  (`6dd8c8ea` → `d7738027`) and the tree hash (→ `1b531a41…`). Reverting restored both.
- **One deletion moved one object; the cause is UNDETERMINED.** Removing Font3d's
  `#ifdef HX_NATIVE` / `#else` lines changed `Font3d.obj` (`0e675c83` → `dc6d0998`),
  although every `report.json` score was identical. That edit was reverted. The
  HamDirector removal hashed neutrally. **Deleting an `HX_NATIVE` block is not
  PPC-neutral by construction; hash the object.**

  > ⚠ **CORRECTED 2026-09-30.** This paragraph first said *"Something in that TU
  > encodes a line number."* That was a guess, and both candidate mechanisms were then
  > checked and refuted: (1) `__LINE__` in assert macros -- `os/Debug.h` contains no
  > `__LINE__`; `MILO_ASSERT(cond, line)` takes the line as an explicit argument; neither
  > `Font3d.cpp` nor `HamDirector.cpp` uses `__LINE__`; the one PCH-wide user,
  > `utl/Std.h`'s `FOREACH_CONST_`, token-pastes (`container_##__LINE__`) so it never
  > expands, and would only name a local. See
  > [comments-are-inert-except-at-__LINE__.md](comments-are-inert-except-at-__LINE__.md)
  > (a comment prepended to all 1,188 sources moved 1 function of 48,365). (2) CodeView
  > line tables -- `Font3d.obj`'s `.debug$S` is 160 B, compile-unit metadata only. So
  > the Font3d byte change is real and unexplained; do not infer a line-number rule from
  > it (the HamDirector deletion, 40 asserts downstream, moved nothing). The misconception
  > is already in the tree once: `src/system/obj/Dir.cpp:209` pads a deletion "to keep PPC
  > `__LINE__` values fixed", which is unnecessary.

### `ObjRefConcrete::Load`: the verdict and what was measured

Its fallback rested on the same premise as the `ObjectDir::FindObject` fallback removed on
`native-engineleads`: "FileMerger flattens on Xbox, and the native merge is incomplete".

- **Arms instrumented (6).** The parent walk and the `Main()` fallback in all three Loads
  (ObjRefConcrete, ObjPtrVec, ObjPtrList), plus ObjRefConcrete's owner-less arm.
- **Hits: 0 of 6, on both routes.** The fallbacks did run: every "couldn't find" notify
  from `ObjPtr_p.h` on the route passed through them first. They found nothing.
- **Why the image cannot reach it.** The image never walks parents. With no owner it
  leaves the ref null.
- **Owner-less premise.** Its one named user, Font3d's `CharInfo::mMesh`, is constructed
  `CharInfo(this)`, so it has an owner. The premise is stale.
- **Result.** Removed, together with the three native-only `DirLoader::SetParentDir` calls
  the walk read, and Font3d's explicit-dir spelling. That spelling was equivalent: the
  owner's `Dir()` is the font's `Dir()`.
- **Post-fix perform route.** 24 of 24 stages, **96 distinct / 400 total** harvest messages,
  identical to the pre-fix probe run.

### Open leads (what would decide each)

- **Native `RndMesh::DrawShowing` (`native/src/platform/Mesh_Wgpu.cpp`) drops every hidden
  *named* mesh, and every `*_lod*` mesh.**
  - The image's `DxMesh::DrawShowing` (`826229B0`) tests only `CanDraw()`. `Draw()`, not
    `DrawShowing()`, is what gates on showing.
  - `UIListMeshElement::Draw`'s native show/restore undoes the skip for list meshes. The
    probe counted between 400 and 599 forced draws per route, every one in
    `list_choose_mode.milo`.
  - **To decide:** move the viewer's direct mesh iteration onto `Draw()`, then remove both
    skips. Also check whether `Character::DrawLod` ever reaches a `_lod` mesh natively.
- **`UIManager::Poll` boot advance under `DC3_FAST_BOOT`.** Every harvest route sets
  `DC3_FAST_BOOT`.
  - It still force-advances `attract` / `autosave_warning` / `wait_main_after_saveload`, and
    skips the tutorial screens.
  - `title_screen` skips `NAV_SELECT_MSG` if no confirm arrives within 60 frames. The
    harvester confirms, so its routes do take the image path.
  - The attract movie now reports `movie_done` by itself, so that entry is likely dead.
  - **To decide:** measure which entries still fire.
- **`UIListSlot::EnsureElements`** exists because `RootTrans()` was null at `UIList::Update`.
  - It created **0** elements on both routes, and its `Draw` / `Fill` / `StartScroll` guards
    fired 0 times. The gap it covered appears gone.
  - **To decide:** remove it after a menu-heavy route (options, song select, crew select)
    also reads 0.
- **`RndMat::CreateMetaMaterial`'s `!sMetaMaterials` guard is dead.** `RndMat::Init`
  always loads `metamaterials.milo` natively.
- **The `feet` class (17 regions).** `Dc3RunPostPollFootPlant`, the CharBonesMeshes plant
  guard, `CharLocalIKScope`, `PreEvalClipWeights` and friends compensate for a native knee
  under-bend whose root is not identified. `PreEvalClipWeights`' premise ("IK polls before
  `song.hdrv`") is worth re-checking now that the poll-order polarity is the image's.

### Gate and routes (final branch)

- **Native gate.** `scripts/native_test.sh`: **584 registered / 515 executed / 515
  passed / 0 failed / 69 skipped** (budget 69), exit 0.
- **Harvest routes**, run with `scripts/native_assert_harvest.py` on the final binary:

| route | stages reached | harvest messages | crashes | same run on the pre-fix probe binary |
|---|---|---|---|---|
| perform, `--mode-downs 0` | 24 of 24 (gameover at beat 267) | 96 distinct / 400 total | 0 | 96 / 400 |
| dance battle, `--mode-downs 2` | 23 of 23 (gameover at beat 267) | 154 / 471 | 0 | 154 / 471 |
| practice, `--mode-downs 1` | `game_screen` reached; ran to beat 1806 with no crash, then stopped by the run's own 1800 s timeout | not recorded | 0 | the same run on the baseline binary stalls on `practice_welcome_screen`: the documented practice route lacks that confirm, so pass `--confirm-screens ...,practice_welcome_screen` |


## Party mode (branch `native-partyplay`, 2026-09-30)

Crew Throwdown now plays end to end natively: crew select -> both teams' photo sign-in ->
hub -> every event (3 rounds + showdown) -> final standings -> rematch -> `main_screen`.
Route: `scripts/native_assert_harvest.py --route party`.

**Why it waited in readywait.** Nothing was broken there. The image readies a side of
crew select only while a skeleton stands on it (`multiuser.dta
update_crew_throwdown_waiting_text` reads the side's `player_present`, which
`SkeletonChooser::SetPlayerSkeletonNavData` derives from tracked skeletons). Headless
native feeds one static dummy skeleton, so side 0 read ready and side 1 read
`step_up_to_play`. The rest of the party is gated the same way: sign-in by a raised hand
(`HandRaisedGestureFilter`), every event by a high five (`HighFiveGestureFilter`). The image
has no controller path around any of it; the only other way is the ctrl+alt+S dev cheat
`crew_throwdown_skip_step`. Pad association (`SetAssociatedPadNum`) does not apply:
party sign-in is team membership (`PartyModeMgr::AddPlayerToTeam`), not a profile/pad
binding.

**The stand-in** is for the sensor, not for game logic: `scripts/synthetic_kinect.py`
serves two static people over the existing external pose-provider socket (`DC3_POSE=external
DC3_POSE_NO_SPAWN=1`, the `pose_server.py` v2 / DC3_20 protocol); one raises a hand, or both
meet hands. Every decision stays with the decompiled gesture code. Pad presses that must
wait on UI timers (photo confirm, final standings, rematch) go over `/api/input/press`,
which was dead and is fixed (`HttpServer::DispatchInjectedButtons`).

**Divergences fixed on the way** (tests in `native/tests/test_native_partyplay.cpp`, each
watched failing; every PPC object touched hashes identically):

| symptom on the route | cause | fix |
|---|---|---|
| SIGSEGV entering round 3 (perform, throneroom), every run | native cascade (`NullifyAllRefs`) never called `TypeProps::Replace`: a HamCharacter's cached `vo_bank` property kept a freed character_vo dir; `play_character_vo` called it | `NullifyAllRefs` runs the image's owner step for TypeProps and RndEnviron (`OwnerControlCascadeTest`) |
| second `party_mode_signin_screen` rendered black; `RndEnviron::FogEnable` SIGSEGV every draw | `~ObjectDir`'s pre-nullify nulled the parent's `ObjDirPtr` in `mSubDirs` instead of releasing it, so a file-loaded subdir (`ui/augmented_photo.milo`) was never destroyed; the next panel load got the zombie back, fog owner cut | unlink/relink those DirPtrs around the pre-nullify so `mSubDirs.clear()` destroys the subdir (`CascadeSubDirTest`) |
| SIGSEGV at Make Your Move's first score | `FreestyleMoveRecorder` Start/Stop/Poll emptied; `mFrames` never allocated | image bodies, camera/palette touches guarded (`MakeYourMoveRecorderTest`) |

**What runs and what cannot.** Strike a Pose runs to its end (pose windows, `strikeapose_over`,
outro, results 0-0): the synthetic people stand still and match no pose, and the sensor
stand-in cannot produce the target poses. (Superseded by `native-posesynth`: with
`--perform` the sensor strikes each pose and the round scores; see "The performing
sensor" below.) Every camera photo (sign-in, hub, standings,
the in-game party HUD photo) renders as a white quad: there is no `LiveCameraInput`
natively, and `HamUI`'s texture-store calls (100% matched) do nothing without it.
`RhythmBattle::Poll` warns `bustajack recordings are getting big` thousands of times per
Keep the Beat round once player 2 has a skeleton (100% matched; image behaviour).

**Open leads** (all three taken by `native-lifetime2`, see the next section).
`$elem = <null>` from a sound_group's `get_group_children` is the same
bypass (`RndGroup::Replace` never erases the child's node); running it inside the walk
double-freed a list node in `MergeScopeParityTest.RepeatedVenueMergeAfterClear`, so it is
not fixed. Other owner-control owners bypassed by the cascade (Task, LightPreset,
CharBonesMeshes, DefaultPhysicsManager) are unmeasured. A subdir that still holds a native
"survivor" (an object with external DirPtrs) keeps the old leak, because
`MergeLifecycleTest.CascadeSkipsObjectsWithExternalDirPtrs` pins that survival; in the image
it would be deleted.

## Cascade owner steps (branch `native-lifetime2`, 2026-09-30)

The three leads the party lane left open, all in the native delete cascade
(`ObjectDir::DeleteObjects` / `~ObjectDir` pre-nullify / `Hmx::Object::NullifyAllRefs`).
Tests: `native/tests/test_native_lifetime2.cpp`, plus the rewritten
`MergeLifecycleTest.CascadeDeletesNamedObjectDespiteExternalDirPtr`.

**1. The RndGroup double free was the walk, not the group.** `NullifyAllRefs` walks the dying
object's ring and self-loops each ref it nulls without repairing neighbours, so the next
ref's `prev` still names the ref just processed. `RndGroup::Replace` erases its node, whose
destructor unlinks it (`SafeReleaseFromRing`: `prev->next = next`) -- a write into the
earlier holder; when that holder was a `kObjListNoNull` list node, `NullifyObj` had already
`delete`d it, and the write corrupted glibc's bins. Reproduced with the group step restored:
both `MergeScopeParityTest` merge tests abort `free(): chunks in smallbin corrupted`, and
`GroupStepLeavesEarlierRefsSelfLooped` shows a plain `ObjPtr` ahead of the group node left
linked into the freed object's ring. The group's ownership of its child is the image's.

**2. Every owner-control holder, not four.** The party lane named Task, LightPreset,
CharBonesMeshes and DefaultPhysicsManager. A temporary audit of owner-control refs whose
owner outlived the cascade with its step skipped found, besides those, TypeProps,
CharDriver/HamDriver (clip drivers left on a NULL clip), HamCamShot, AnimTask,
PropertyEventProvider, RndMesh geometry owners, Spotlight colour owners, RndTransformable
parents, RndPropAnim keys, NgEnviron and CharServoBone (a CharBonesMeshes). So the fix is the
image's call for all of them: `ObjRef::ControlOwner()` (native virtual) identifies the
holders; the walk moves them onto a local head -- the image's `ObjRef other` in
`ReplaceRefs` -- and, once the dying ring is empty, drains it exactly like `ReplaceList`.
The TypeProps / RndEnviron / RndGroup special cases are subsumed. Two rules came from
failures: the dying object's OWN members are stepped too (a self-looped member broke
`CharClip::Transitions::RemoveNodes`, which memmoves refs and patches their ring neighbours:
`ObjectLifetimeTest.MergeKeepCharClipSetRootDoesNotCorruptRefs` aborted), and a dying dir's
own `DirLoader` is not (its `Replace` deletes the loader `~ObjectDir` deletes again: the image
deletes `mLoader` in the body, before `ReplaceRefs`; the native pre-nullify runs before the
body). Still divergent: `AnimTask::Replace` queues its delete and `TaskMgr::QueueTaskDelete`
refuses to queue during any cascade, so an AnimTask whose anim dies in a cascade is nulled
but never deleted (a leak; its `Poll` returns early on the NULL anim).

**3. The subdir leak: the test encoded it.** `MergeLifecycleTest.CascadeSkipsObjectsWith-
ExternalDirPtrs` asserted that an ObjectDir named in a subdir of a dir being deleted, with an
outside `ObjDirPtr`, survives. The image's `DeleteObjects` deletes every object it names (no
DirPtr test; `?DeleteObjects@ObjectDir@@QAAXXZ`), and `HasDirPtrs()`'s `sDeleting` guard
keeps the outside pointer's `Replace` from deleting it twice: the pointer reads NULL.
Natively the object "survived" only because the subdir leaked (probed: `anon_sub` never
destroyed, `hud_left` alive with `Dir() == NULL`); the same object named directly in the
deleted dir was already deleted, as in the image. The pre-nullify now always releases
subdirs; the test asserts the image's outcome. A subdir SHARED into the dying tree still
survives by refcount, as in the image.

The removed bail-out never fired for a non-root dir on the perform, dance-battle or party
routes (temporary audit, with a positive control: the same hook fires on the unit test's
`anon_sub`); every leak the party lane saw was a cascade ROOT, destroyed anyway.

**Route symptom of lead 1.** `$elem = <null>` did not reproduce on main's binary: 0 in two
party runs (one full, one through event 2), and 0 on this branch. The unit-level faults above
reproduce deterministically.

**Also on the way:** `FreestyleMoveRecorder::GetScore` indexed player frame scores with the
Xbox's 0x10 stride (natively 0x20): player 1 read a heap pointer as its score count, so with
about half of all heap layouts the sum loop ran off the heap -- the party route SIGSEGV'd in
Make Your Move once on this branch before the fix.

**Open, pre-existing, not a lifetime bug:** Make Your Move sometimes never ends -- still
`playing` past beat 3200 after 27 minutes, `StandardStream::SetJump` notifying "Trying to set
loop points when we're already past the point of no return!" ~160 times (13 in a whole
completed party). Seen once on main's binary (1 of 2 runs) and once on this branch's final
binary (1 of 2 runs, with the stride fix); the other run of each completed all 106 stages.


## The performing sensor (branch `native-posesynth`, 2026-09-30)

`scripts/synthetic_kinect.py` now PERFORMS: it reads, once per game frame, what each
player is being asked to do (`GET /api/pose/target`, `native/src/platform/
PoseTarget_Native.cpp`, read-only: the choreography's reference skeleton for the
player's scheduled or current move, or the fatality / Strike a Pose target) and
streams it back through the external pose socket as that player's person. Every
decision is the decompiled scorer's, on a LIVE `Skeleton`. Harvester: `--perform`;
negative control: `--stand-and-watch`; pose gate: third config `sensor`. Reference in
`docs/native/SCORING_ENV_VARS.md`.

**What a scoring native player exposed** (each found only because a move could score;
tests in `native/tests/test_native_posesynth.cpp`, watched failing first):

| symptom | cause | fix |
|---|---|---|
| perform/battle moves always rated at the bottom, whatever the player did; no battle fatality could start | two `HX_NATIVE` guards switched the async detectors off (`PostUpdateFilters` never enqueued them; `MoveRatingFrac` returned 0 without `SkeletonUpdate`), justified by a null `mMoveFrame` the detector ctor rules out | guards removed; image code (`a2c255ed8`) |
| SIGSEGV on `perform_endgame_screen` after the first real score | rank-up -> `HamProfile::GetHamUser` -> `HamUser::GetPadNum` read the null native `TheSkeletonIdentifier` | no identifier = no enrolled pad = -1, the image's unenrolled answer (`HamUserPadNumTest`) |
| the sensor fed the fatality its OWN target and the match still read 0.0: no pose could ever be struck (fatality, Strike a Pose) | `CompareSkeletonPositions` compares NormPos in the four limb coordinate systems, which only `Skeleton::Poll` derives; the native provider hand-filled camera space | every live frame goes through the image's `Skeleton::Poll` (`NativeSkeletonPollTest`) |
| SIGSEGV in `HttpServer::ProcessCommands` under a polling client | a timed-out request left its stack `Command` in the queue | withdraw on timeout; notify under the lock |

**Shown working** (`kinect.json` in each harvest dir records every score/rating/fatality
change): perform (Starships, Beginner, async `move` source) 73/73 moves `move_perfect`,
1,867,920, through `xp_reward_screen` (rank-up) to `song_select` -- and the negative
control, `--stand-and-watch` (same sensor, people standing), scores 0 with every move
`move_bad`; dance battle: the
Finishing_Move rates `move_perfect`, `CheckBeginFatal` starts the fatality, both players
strike all eight poses (570,000 -> 666,000) -- `scripts/native_fatality_probe.sh` is
superseded; `pose_scoring_gate.sh` on Better Off Alone / Easy: selftest 1.0000, sensor
1.0000 (77/77 perfect), dummy 0.1374..0.7812 (the 0.78 is Roxbury, a head bob a standing
body nearly does). Party (`--route party --perform`, Crew Throwdown): every event and the
showdown, back to `main_screen`, 0 crash lines; **Strike a Pose awards points** -- both
players match one pose per beat (`fatalMatch` 1.0, combo +2000 each) to 1,740,000 each,
and `strikeapose_over` ends the round. One native gap shows there too: the round's
`HamPartyJumpData` stream jump never happens (below), so from beat ~56 PoseFatalities'
beat bookkeeping (`SetJump` -> `mCurrentBeat = mJumpEnd`) runs ahead of the unjumped song
clock and no pose resolves (hold progress climbs past 100 s against a 0.5 s hold) until
the clock catches up at beat ~168; scoring then resumes.

**What still cannot run: practice's gameover (and any stream jump).** Practice never
loops natively. Its song
starts mid-song (the section start), and `StandardStream`'s decode position
(`mCurrentSamp`) stays at that start for the whole session -- 5752.4 ms on YMCA,
measured at every `SetJump` -- so `IsPastStreamJumpPointOfNoReturn()` ("decoded behind
played") reads true, `practice.dta` queues every loop (`queued_start_beat`) and never
sets one, and song time runs on past the end (beat 2,300 on a 167 s song) with the
session stuck in `review`. The same freeze holds under a real-time song clock with a
thread pulling the mix as a device would, so it is not the missing headless audio
device alone; the lead is the native `VorbisReader` after a start-position seek
(`DoSeek` / `mSamplesToSkip`), not pursued here. Until it is, `--route practice` cannot
reach `practice_endgame_screen`, performing or not.
