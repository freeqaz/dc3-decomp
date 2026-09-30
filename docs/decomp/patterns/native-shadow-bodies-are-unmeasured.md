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
triaged; many are defensive null checks and DTA-flow shortcuts, and some change behaviour
(see "notable" below for the ones that surfaced incidentally).

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
- **`GamePanel.cpp:618`** calls `Game::Start()` unconditionally; the image only when `HasIntro()`.
- **`HamPanel::Exiting` returns false**, dropping `UIPanel::Exiting()` and DTA `exiting`
  handlers; **`MetaPanel`** never creates `Campaign`/`MetaMusicManager`/`HAQManager`;
  **`UI.cpp:821`** force-completes a screen enter after 90 frames.
- **`HamNavList.cpp:529/1609`** ignore `IsAnimating()` (Poll select completion,
  `OnMsg(ButtonDownMsg)`), and **`HamPanel::Exiting`** returns false — all three rest on
  the belief that animations never settle natively, which the `native-menulist` fix above
  shows was caused by the native stop-handler. Likely removable once that lands;
  adjudicate against the image first. **`Game.cpp:1105/1158`**,
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
