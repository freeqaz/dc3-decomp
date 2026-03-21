# HX_NATIVE Audit: Complete Block Inventory

**Date**: 2026-03-21
**Scope**: All `#ifdef HX_NATIVE`, `#ifndef HX_NATIVE`, `#if defined(HX_NATIVE)` blocks across `src/` and `native/`
**Total blocks**: 865 across 295 files

## Summary Table

| Subsystem | SCAFFOLD | PLATFORM | STUB | BUGFIX | DEBUG | Total |
|---|---|---|---|---|---|---|
| App | 2 | 3 | 0 | 1 | 1 | 7 |
| Audio | 4 | 20 | 9 | 2 | 0 | 35 |
| Character | 2 | 9 | 1 | 5 | 0 | 17 |
| Flow/UI | 21 | 5 | 2 | 11 | 40 | 79 |
| Game | 10 | 2 | 0 | 7 | 6 | 25 |
| Gesture/Kinect | 0 | 4 | 15 | 3 | 1 | 23 |
| Ham | 18 | 3 | 14 | 17 | 28 | 80 |
| Math | 0 | 1 | 0 | 3 | 0 | 4 |
| Meta | 3 | 2 | 4 | 1 | 1 | 11 |
| Milo Core | 5 | 30 | 2 | 55 | 8 | 100 |
| Movie | 0 | 3 | 4 | 0 | 0 | 7 |
| Native Port | 0 | 4 | 0 | 0 | 0 | 4 |
| Network | 0 | 3 | 2 | 1 | 0 | 6 |
| OS/Platform | 0 | 18 | 4 | 1 | 1 | 24 |
| Rendering | 2 | 30 | 6 | 12 | 6 | 56 |
| Utilities | 2 | 29 | 2 | 3 | 3 | 39 |
| World | 2 | 5 | 2 | 9 | 2 | 20 |
| XDK Compat | 0 | 6 | 0 | 0 | 0 | 6 |
| **Total** | **71** | **177** | **65** | **131** | **97** | **543** |

**Note**: Many blocks contain multiple logically distinct guards (e.g., `Dir.h` has 17 `#ifdef` lines but they serve 5 distinct categories). The summary counts logical purposes; the raw 865 count includes headers, closing braces, STLport template instantiations, and other mechanical guards not individually classified.

## Classification Legend

- **SCAFFOLD**: Temporary hack to bypass Xbox-only flow (DTA scripts, boot sequence, stuck screens). These need to be replaced with proper convergent implementations.
- **PLATFORM**: Legitimate platform difference (64-bit pointers, endianness, wchar_t size, allocator, threading, graphics API). Expected to remain permanently.
- **STUB**: Stubbed-out Xbox-specific functionality (Kinect, Xbox Live, Bink video, Holmes debugger). Expected to remain permanently.
- **BUGFIX**: Fix for a crash/corruption that only manifests on native due to differences in memory layout, ABI, or lifecycle. Many are cascade-related (ObjectDir::DeleteObjects). Critical path.
- **DEBUG**: Diagnostic printf/logging code gated behind env vars. Low priority for convergence.

---

## Critical Path Assessment

For venue/gameplay convergence, the most impactful blocks are:

1. **SCAFFOLD blocks in Flow/UI** (21 blocks) -- auto-advance stuck screens, force transitions, skip boot sequence
2. **SCAFFOLD blocks in Game** (10 blocks) -- force-start gameplay, bypass audio gating
3. **SCAFFOLD blocks in Ham** (18 blocks) -- force controller mode, bypass Kinect flows, null-guard globals
4. **BUGFIX blocks in Milo Core** (55 blocks) -- cascade destruction safety, ring corruption prevention
5. **BUGFIX blocks in Ham** (17 blocks) -- null-guard missing objects during gameplay

---

## Detailed Per-Subsystem Inventory

### App (src/App.cpp, src/App.h, src/types.h)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `App.cpp:2` | PLATFORM | No | Native-specific includes (GLFW, telemetry, algorithm) |
| `App.cpp:270` | SCAFFOLD | **Yes** | Native boot sequence -- replaces Xbox init (no Kinect, no splash threading, different subsystem init order) |
| `App.cpp:471` | PLATFORM | No | Xbox boot sequence (MagnuInit, Splasher, RockCentral) excluded on native |
| `App.cpp:631` | SCAFFOLD | **Yes** | `RunOneFrame()` -- native main loop body (polls UI, TaskMgr, FlowMgr, LoadMgr, renders) |
| `App.cpp:719` | PLATFORM | No | `std::find_if(isdigit)` vs PPC loop for symbol text parsing |
| `App.cpp:1000` | SCAFFOLD | **Yes** | Native main loop with GLFW window management, frame limiting, telemetry |
| `App.cpp:1142` | SCAFFOLD | **Yes** | Pre-game venue drawing when no HamDirector (menu/attract mode) |
| `App.h:19` | PLATFORM | No | `RunOneFrame()` declaration |
| `types.h:5` | PLATFORM | No | POSIX equivalents of MSVC functions (stricmp, _snprintf, sprintf_s) |
| `types.h:46` | PLATFORM | No | LP64 type definitions (u32=unsigned int, not unsigned long) |
| `types.h:72` | PLATFORM | No | cstdint include for intptr_t/uintptr_t |

### Audio (src/system/synth/*, src/system/synth_xbox/*, src/system/oggvorbis/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `Faders.cpp:3` | PLATFORM | No | `__fsel` polyfill (PPC float-select intrinsic) |
| `Faders.cpp:246` | BUGFIX | No | Skip fader cleanup during cascade destruction |
| `FxSend.h:51` | PLATFORM | No | Makes FxSend members public for native access |
| `FxSend*.h` (9 files) | PLATFORM | No | Makes FxSend subclass members public for native access |
| `MidiInstrument.cpp:112` | BUGFIX | No | Skip voice cleanup during cascade destruction |
| `SampleData.cpp:29` | BUGFIX | No | Guard against null `sFree` function pointer |
| `SampleData.h:63` | PLATFORM | No | `DataPtr()` accessor for native audio pipeline |
| `Sequence.cpp:28` | BUGFIX | No | Skip null/destroying instruments during sequence poll |
| `StandardStream.cpp:11` | PLATFORM | No | Include native StreamReceiver header |
| `StandardStream.cpp:21` | PLATFORM | No | `sAudioOffsetMs` static definition |
| `StandardStream.cpp:68` | PLATFORM | **Yes** | Synchronous Vorbis header pump (Xbox uses background thread) |
| `StandardStream.cpp:84` | PLATFORM | **Yes** | Pre-fill ring buffers before audio callback registration |
| `StandardStream.cpp:133` | PLATFORM | **Yes** | Audio time with offset correction |
| `StandardStream.cpp:449` | PLATFORM | **Yes** | Wall-clock fallback for headless audio mode |
| `StandardStream.cpp:574` | PLATFORM | No | Skip buffer alignment assert on native |
| `StandardStream.cpp:714` | PLATFORM | **Yes** | `ConsumeData()` -- native audio callback data provider |
| `StandardStream.h:103,162` | PLATFORM | No | Audio offset + wall-clock fallback member declarations |
| `StreamReceiver.cpp:3,55,72,87` | PLATFORM | **Yes** | Native stream receiver implementation (ring buffer, play cursor) |
| `StreamReceiver.cpp:158` | PLATFORM | No | Xbox factory function excluded on native |
| `StreamReceiver.h:51` | PLATFORM | No | Makes `sFactory` public on native |
| `Synth.cpp:45` | PLATFORM | No | Native-specific includes |
| `Synth.cpp:189` | STUB | No | Skip DTA obfuscation letter-function registration |
| `Synth.cpp:241,252` | PLATFORM | **Yes** | Native stream creation (StandardStream instead of Xbox stream) |
| `Synth.cpp:261` | PLATFORM | **Yes** | Native mogg file resolution from ark/filesystem |
| `Synth.cpp:284` | PLATFORM | **Yes** | VorbisReader creation for mogg files |
| `Synth.cpp:606,617` | PLATFORM | **Yes** | `CreateNativeSynth()` -- native audio device factory |
| `SynthSample.h:29` | PLATFORM | No | Virtual `NewInst` with different signature |
| `VorbisReader.cpp:51,69` | STUB | No | Skip Xbox decode thread creation/teardown |
| `VorbisReader.cpp:134` | PLATFORM | **Yes** | Direct masterKey initialization (bypass DTA pointer-math obfuscation) |
| `VorbisReader.cpp:399` | PLATFORM | **Yes** | Native AES-CTR decryption + mogg demux implementation |
| `WavMgr.cpp:5` | PLATFORM | No | WavMgr alloc/free function pointer definitions |
| `GranularSynth.cpp:3` | PLATFORM | No | Xbox-only DSP code excluded on native |
| `GranularSynth.h:2` | PLATFORM | No | Xbox-only DSP header excluded on native |
| `StreamReceiver360.h:39` | PLATFORM | No | `std::list` vs STLport list for pending voices |
| `Synapse_dsp.cpp:17,35,60,197` | PLATFORM | No | Constructor signatures differ (std::vector vs STLport vector) |
| `Synapse_dsp.h:3,38,56` | PLATFORM | No | Member types differ (std::vector vs STLport vector) |
| `codec.h:32` | PLATFORM | No | Skip `alloca` wrapper on native (already in stdlib) |
| `framing.c:571` | PLATFORM | No | Skip Ogg CRC validation (Xbox mogg anti-tamper) |

### Character (src/system/char/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `CharBones.cpp:96` | PLATFORM | No | Direct member access instead of `offset[-7]` pointer arithmetic (LP64 safety) |
| `CharBonesSamples.cpp:126,226` | PLATFORM | **Yes** | Big-endian to little-endian byte-swap for bone sample data |
| `CharBonesSamples.cpp:214` | PLATFORM | No | `WaitUntilReady()` for synchronous stream reads |
| `CharClip.cpp:26` | BUGFIX | **Yes** | Skip ObjRef removal during `ReplaceList` to prevent use-after-free |
| `CharClip.cpp:57` | PLATFORM | No | `intptr_t` for pointer arithmetic (LP64) |
| `CharClip.cpp:218` | PLATFORM | No | Larger alloc size for LP64 NodeVector |
| `CharClip.cpp:252` | BUGFIX | **Yes** | Fix ObjRef ring corruption after memcpy of NodeVectors |
| `CharClip.h:155,164` | PLATFORM | No | `size_t` operator new signatures (LP64) |
| `CharClip.h:246` | PLATFORM | No | Const accessors for `mFull`/`mOne` |
| `CharClipGroup.cpp:13` | PLATFORM | No | Skip STLport explicit template instantiation |
| `CharDriver.cpp:266` | BUGFIX | No | Null-guard clip before use |
| `CharDriver.cpp:743` | PLATFORM | No | Skip STLport map template instantiation |
| `CharEyes.cpp:85,1372` | BUGFIX | No | Skip null eye entries |
| `CharEyes.cpp:743,760` | PLATFORM | No | LP64-safe offset computation using `data()` |
| `CharEyes.cpp:1405` | PLATFORM | No | Standalone `NormalizeScale` implementation |
| `CharEyes.h:85` | PLATFORM | No | Setter/accessor methods |
| `CharHair.cpp:232` | PLATFORM | No | `sqrtf` instead of PPC `frsqrte` intrinsic |
| `CharIKHead.cpp:177` | PLATFORM | No | Proper null-terminated loop condition |
| `CharSignalApplier.cpp:204` | PLATFORM | No | Skip STLport `__uninitialized_fill_n` specialization |
| `Character.cpp:671` | BUGFIX | No | Null-guard character pointer |
| `Character.cpp:933` | SCAFFOLD | **Yes** | Native character draw implementation (shadow, LOD, draw modes) |
| `Character.cpp:1002` | BUGFIX | No | Null-guard iterator dereference |
| `Character.cpp:1027` | PLATFORM | No | Skip `CharPollableSorter` (STLport dependency graphs) |
| `FileMerger.cpp:21` | PLATFORM | No | `sDisableAll` static definition |
| `FileMerger.cpp:54,65,78` | BUGFIX | **Yes** | Guard against cascade destruction during merger cleanup |
| `FileMerger.cpp:240` | SCAFFOLD | **Yes** | Post-merge registration of subdir objects for flat-scope Find |
| `FileMerger.cpp:448` | PLATFORM | No | Pass merger Dir as parent for ObjPtr resolution |
| `Waypoint.cpp:13` | PLATFORM | No | Static `sWaypoints` definition |

### Flow/UI (src/system/flow/*, src/system/ui/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| **Flow subsystem** | | | |
| `Flow.cpp:48` | BUGFIX | **Yes** | Clear running nodes during cascade instead of deactivating |
| `FlowNode.cpp:25` | BUGFIX | **Yes** | Clear running nodes during cascade to prevent null deref |
| `FlowNode.cpp:36` | BUGFIX | **Yes** | Skip null/destroying child nodes during cleanup |
| `FlowNode.cpp:137` | BUGFIX | No | Sanity check on numEntries during Load |
| `FlowQueueable.cpp:9` | BUGFIX | **Yes** | ObjPtrList constructor with owner tracking |
| `FlowQueueable.cpp:54,87` | BUGFIX | **Yes** | Pop-before-release listener pattern to avoid ring-modified iteration |
| `FlowQueueable.cpp:112` | PLATFORM | No | Iterator type difference (ObjPtrList vs raw pointer) |
| `FlowQueueable.cpp:141` | PLATFORM | No | `insert(begin)` vs raw pointer insert |
| `FlowQueueable.h:33` | BUGFIX | **Yes** | `ObjPtrList<Hmx::Object>` member type (ring-tracked, auto-nullifies) |
| `FlowSwitch.cpp:119` | PLATFORM | No | Proper member accessors instead of hardcoded struct offsets |
| `FlowTimer.cpp:97` | BUGFIX | No | Null-guard mTask cast |
| **PanelDir** | | | |
| `PanelDir.cpp:17,26` | PLATFORM | No | Includes, `sAlwaysNeedFocus` static |
| `PanelDir.cpp:32` | SCAFFOLD | **Yes** | Native flow activation filter system -- curated list of flows to auto-activate |
| `PanelDir.cpp:391,412` | PLATFORM | No | `FlushTransparentDraws()` calls after draw phases |
| `PanelDir.cpp:428` | SCAFFOLD | **Yes** | Auto-activate game-triggered Flows that normally fire from DTA enter scripts |
| **UI** | | | |
| `UI.cpp:45` | DEBUG | No | Debug UI flow logging utilities |
| `UI.cpp:75` | SCAFFOLD | No | Native UI camera mode selection (HD scaling) |
| `UI.cpp:180` | SCAFFOLD | **Yes** | `MILO_FIRST_SCREEN` env override to skip attract/tutorial |
| `UI.cpp:207` | DEBUG | No | Detailed camera/screen debug printf (gated by `if(false)`) |
| `UI.cpp:257` | PLATFORM | No | Restore previous camera/environment after UI draw |
| `UI.cpp:301` | DEBUG | No | Debug log for UseJoypad |
| `UI.cpp:387` | SCAFFOLD | **Yes** | Skip campaign screens, debug log for GotoScreenImpl |
| `UI.cpp:424` | DEBUG | No | Debug log for screen transitions |
| `UI.cpp:484` | DEBUG | No | Debug log for goto_screen DTA handler |
| `UI.cpp:503` | SCAFFOLD | **Yes** | Fallback to main_screen when goto_screen resolves to null |
| `UI.cpp:568` | SCAFFOLD | **Yes** | Auto-advance stuck screens (boot splash, tutorial, Kinect) |
| `UI.cpp:635` | DEBUG | No | Transition state monitoring |
| `UI.cpp:666` | SCAFFOLD | **Yes** | Exit animation timeout (~3s safety net) |
| `UI.cpp:683` | SCAFFOLD | No | Extra condition check for screen exit completion |
| `UI.cpp:693` | SCAFFOLD | **Yes** | Force-set `mSink` to current screen for button input routing |
| `UI.cpp:734` | SCAFFOLD | **Yes** | Enter animation timeout (~3s safety net) |
| `UI.cpp:750` | SCAFFOLD | No | Extra condition check for screen enter completion |
| `UI.cpp:770` | DEBUG | No | Debug log for transition completion |
| **UIPanel** | | | |
| `UIPanel.cpp:12` | DEBUG | No | Debug utilities (DebugUIFlow, DebugSharedPanelDirs) |
| `UIPanel.cpp:30` | PLATFORM | No | `sMaxPanelId`, `sIsFinalDrawPass` static definitions |
| `UIPanel.cpp:64,71` | DEBUG | No | Panel load state logging |
| `UIPanel.cpp:118,139` | DEBUG | No | Shared panel dir logging |
| `UIPanel.cpp:225,236` | DEBUG | No | Panel load path logging |
| `UIPanel.cpp:282` | SCAFFOLD | **Yes** | Block Kinect tutorial panels from entering |
| **UIScreen** | | | |
| `UIScreen.cpp:20` | PLATFORM | No | `sMaxScreenId` static definition |
| `UIScreen.cpp:24` | DEBUG | No | DebugUIFlow utility |
| `UIScreen.cpp:93` | DEBUG | No | Panel resolution diagnostic |
| `UIScreen.cpp:148` | DEBUG | No | Panel loading blocker diagnostic |
| `UIScreen.cpp:213` | DEBUG | No | Screen enter logging |
| `UIScreen.cpp:225` | SCAFFOLD | **Yes** | Skip Kinect tutorial panels during screen enter |
| `UIScreen.cpp:284` | DEBUG | No | TypeDef handler dump on enter |
| `UIScreen.cpp:359` | DEBUG | No | Screen exit logging |
| `UIScreen.cpp:391` | DEBUG | No | Exit blocker diagnostic |
| `UIScreen.cpp:570` | BUGFIX | No | Skip null panels in iteration |
| **UIList** | | | |
| `UIList.cpp:277,286` | BUGFIX | No | Null-guard mListDir before use |
| `UIList.cpp:396` | DEBUG | No | PreLoad diagnostic |
| `UIList.cpp:429` | SCAFFOLD | **Yes** | Re-evaluate circular display count for async providers |
| `UIList.cpp:648` | BUGFIX | No | Null-guard mListDir |
| **UIListSlot** | | | |
| `UIListSlot.cpp:9,15` | DEBUG | No | Includes + DebugChooseModeSlot utility |
| `UIListSlot.cpp:89` | SCAFFOLD | **Yes** | Lazy element creation for async loading |
| `UIListSlot.cpp:118,226,238` | SCAFFOLD | No | EnsureElements calls + bounds checks |
| `UIListSlot.cpp:124` | BUGFIX | No | Early return when elements empty |
| `UIListSlot.cpp:173` | DEBUG | No | Choose mode slot diagnostic |
| `UIListSlot.h:62` | PLATFORM | No | EnsureElements declaration |
| **UIListDir** | | | |
| `UIListDir.cpp:8,21` | DEBUG | No | Includes + DebugChooseMode utility |
| `UIListDir.cpp:207` | DEBUG | No | DrawWidgets diagnostic |
| `UIListDir.cpp:417,485` | BUGFIX | No | Zero-init UIListElementDrawState to prevent garbage reads |
| **UIListSubList** | | | |
| `UIListSubList.cpp:9` | PLATFORM | No | `sNextFillSelection` static definition |
| `UIListSubList.cpp:49` | BUGFIX | No | Bounds-check index before element access |
| `UIListSubList.cpp:71` | BUGFIX | No | Skip null UIList entries |
| **UIListLabel, UIListMesh, UIListCustom** | | | |
| `UIListLabel.cpp:8,14` | DEBUG | No | Includes + debug utility |
| `UIListMesh.cpp:9,15` | DEBUG | No | Includes + debug utility |
| `UIListMesh.cpp:117` | DEBUG | No | Mesh element draw diagnostic |
| `UIListMesh.cpp:159,173` | BUGFIX | No | Force-show hidden meshes during draw, then restore |
| `UIListCustom.cpp:6,12` | DEBUG | No | Includes + debug utility |
| `UIListCustom.cpp:103` | DEBUG | No | Custom element draw diagnostic |
| **UILabel** | | | |
| `UILabel.cpp:32,37` | DEBUG | No | Includes + ChooseMode diagnostic utility |
| `UILabel.cpp:56` | PLATFORM | No | Static member definitions |
| `UILabel.cpp:558` | DEBUG | No | Detailed label draw diagnostic |
| `UILabel.cpp:811` | SCAFFOLD | No | DefaultAllowEditText check via TheUI |
| `UILabel.h:69` | PLATFORM | No | Const-ref SetPrelocalizedString overload |
| **UIListWidget** | | | |
| `UIListWidget.cpp:189` | BUGFIX | **Yes** | Bounds-check element_state/list_state against enum ranges (HamListRibbon corrupts these fields) |
| **UITransitionHandler** | | | |
| `UITransitionHandler.cpp:12` | BUGFIX | No | Skip during cascade destruction |
| **UIFontImporter** | | | |
| `UIFontImporter.cpp:373` | PLATFORM | No | Itanium ABI typeinfo-based font identification (vs MSVC vtable layout) |
| **ResourceDirPtr** | | | |
| `ResourceDirPtr.h:44` | PLATFORM | No | BinStream operator<< template for ResourceDirPtr |

### Game (src/lazer/game/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `Game.cpp:54` | PLATFORM | No | AudioDevice include |
| `Game.cpp:60` | SCAFFOLD | **Yes** | Native audio init state tracking variables |
| `Game.cpp:219` | SCAFFOLD | **Yes** | Find MoveDir via world->Find instead of DTA assignment |
| `Game.cpp:258` | BUGFIX | No | Warn on missing audio data instead of crashing |
| `Game.cpp:274,282` | BUGFIX | No | Null-guard mMoveDir |
| `Game.cpp:292` | BUGFIX | No | Null-guard world pointer |
| `Game.cpp:305,315` | PLATFORM | **Yes** | Suspend/resume audio device around stream destruction |
| `Game.cpp:323` | SCAFFOLD | **Yes** | Reset mLoadState after StopAllSounds for reload |
| `Game.cpp:364` | BUGFIX | No | Null-guard mGameInput |
| `Game.cpp:391` | BUGFIX | No | Null-guard TheHamDirector |
| `Game.cpp:518,608` | BUGFIX | No | Null-guard mMoveDir/TheMoveMgr |
| `Game.cpp:619` | BUGFIX | No | Warn on missing song audio data |
| `Game.cpp:626` | SCAFFOLD | No | Set practice mode false on song load |
| `Game.cpp:740` | BUGFIX | No | Null-guard TheMoveMgr |
| `Game.cpp:786` | SCAFFOLD | **Yes** | Native MoveGraph loading (find move_data dir, load moves) |
| `Game.cpp:822,832` | SCAFFOLD | **Yes** | Native audio initiation during PollForLoading |
| `Game.cpp:883` | BUGFIX | No | Null-guard mMoveDir |
| `Game.cpp:977` | SCAFFOLD | **Yes** | Find and enter MoveDir via HamDirector world |
| `Game.cpp:1095` | SCAFFOLD | No | Hardcoded autoplay states (native test mode) |
| `GameMode.cpp:5` | PLATFORM | No | `TheGameMode` global definition |
| `GameMode.cpp:26` | SCAFFOLD | **Yes** | Skip initial SetMode (depends on uninitialized SystemConfig) |
| `GameMode.cpp:241` | BUGFIX | No | Null-guard TheGameMode |
| `GamePanel.cpp:60` | PLATFORM | No | cstdio include |
| `GamePanel.cpp:407` | SCAFFOLD | **Yes** | Force-advance past stuck kGameInIntro state |
| `GamePanel.cpp:548` | PLATFORM | No | Frame time sample with different indexing |
| `GamePanel.cpp:571` | SCAFFOLD | **Yes** | Skip intro gating, start game directly |
| `GamePanel.cpp:584` | SCAFFOLD | **Yes** | Explicit game_stage=playing property set |
| `GamePanel.cpp:930,956` | DEBUG | No | PollForLoading diagnostics |

### Gesture/Kinect (src/system/gesture/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `DepthBuffer3D.cpp:182` | STUB | No | Empty DrawShowing/Save/Copy/Load stubs |
| `DrawUtl.cpp:47` | STUB | No | `ToggleDrawSkeletons` excluded (no SkeletonViz on native) |
| `GestureMgr.cpp:43` | STUB | No | Force `mInControllerMode=1` (no Kinect) |
| `GestureMgr.cpp:50` | BUGFIX | No | Skip LiveCamInput assert (null on native) |
| `GestureMgr.cpp:84` | STUB | **Yes** | Safe defaults for LiveCameraInput DTA queries |
| `GestureMgr.cpp:163` | STUB | **Yes** | Native GestureMgr init (no Kinect, calls GestureMgr_NativeInit) |
| `GestureMgr.cpp:191` | STUB | No | Native terminate |
| `GestureMgr.cpp:201` | STUB | No | Native poll |
| `GestureMgr.cpp:406` | STUB | No | Force controller mode on exit_controller_mode |
| `LiveCameraInput.cpp:1112` | STUB | No | Skip NUI camera property calls |
| `LiveCameraInput.cpp:1139` | STUB | No | No-op camera debug DataNode handlers |
| `Skeleton.h:76` | PLATFORM | No | `NativeSkeletonProvider` friend declaration |
| `SkeletonQualityFilter.cpp:11` | PLATFORM | No | Skip sprintf_s template instantiation |
| `SkeletonUpdate.cpp:27` | BUGFIX | No | Skip mInst assert |
| `SkeletonUpdate.cpp:39-84` (7 blocks) | BUGFIX | No | Null-guard mInst in all accessors |
| `SkeletonUpdate.cpp:133` | STUB | No | Skip Xbox skeleton update thread creation |
| `SkeletonUpdate.cpp:156` | BUGFIX | No | Skip sInstance assert |
| `SkeletonViz.cpp:166,475,483` | BUGFIX | No | Guard against unloaded skeleton resource |
| `SpeechMgr.cpp:12` | PLATFORM | No | mbstowcs_s POSIX shim |
| `StreamRecorder.cpp:281` | STUB | No | Skip CompressTextures/recording/playback |
| `StreamRecorder.h:33` | PLATFORM | No | intptr_t parameter in TextureCompressed |
| `StreamRenderer.cpp:282` | STUB | No | Empty DrawToTexture + SetCrewPhotoPlayerCenters |

### Ham (src/system/hamobj/*, src/lazer/meta_ham/*)

Due to the large number of blocks (182 raw, 80 logical), this section groups by file.

**HamNavList (src/system/hamobj/HamNavList.cpp, .h) -- 20 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 48 | PLATFORM | No | Static member definitions (sSlideTrendAmount, etc.) |
| 81 | BUGFIX | **Yes** | Clear list widgets during cascade instead of normal dtor |
| 141 | BUGFIX | No | Handle UITransitionCompleteMsg |
| 663 | BUGFIX | **Yes** | Resize ribbon draw states for provider growth |
| 858 | PLATFORM | No | mElemDrawState init to nullptr |
| 1027 | SCAFFOLD | No | Active display check with skeleton tracking |
| 1080 | SCAFFOLD | **Yes** | Recreate elements when NumShowing changes post-Update |
| 1139 | PLATFORM | No | DataVariable cheat check |
| 1285 | DEBUG | No | Select diagnostic |
| 1550 | BUGFIX | No | OnMsg(UITransitionCompleteMsg) handler |
| 1558 | DEBUG | No | Debug overlay skipped |
| 1615 | PLATFORM | **Yes** | LP64-safe struct access for ribbon draw state computation |
| 1704 | BUGFIX | No | Null-guard provider |
| 1711,1755,1788 | SCAFFOLD | **Yes** | Scene camera selection for HamNavList draw (PanelDir cam) |
| 150 (`.h`) | PLATFORM | No | UITransitionCompleteMsg handler declaration |

**HamDirector (src/system/hamobj/HamDirector.cpp) -- 5 blocks**

| Line | Classification | Critical | Description |
|---|---|---|---|
| 546 | SCAFFOLD | **Yes** | Post-merge choreography init (Init + ResetRemixer for move loading) |
| 713 | BUGFIX | No | Null-guard entry |
| 1008 | SCAFFOLD | **Yes** | Native player presence checking and stale crew state clearing |
| 1220 | SCAFFOLD | No | Register stub "video_recorder.srec" for DTA compatibility |
| 2244 | SCAFFOLD | No | Fallback camera shot (Area1_WIDE) |
| 2984 | PLATFORM | No | Skip STLport `__stl_throw_out_of_range` |

**ClipPlayer (src/system/hamobj/ClipPlayer.cpp, .h) -- 9 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 280,306,314,335,347,485 | SCAFFOLD | **Yes** | `GetRoutineCrossoverClips` -- different return type (bool vs void), null guards, fallback to master clip keys |
| 567,573 | DEBUG | No | PlayNormal diagnostics |
| 38 (`.h`) | PLATFORM | No | Function signature change (bool return) |

**HelpBarPanel (src/lazer/meta_ham/HelpBarPanel.cpp) -- 11 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 22 | PLATFORM | No | sInstance static definition |
| 55,66,83,117,126 | BUGFIX | **Yes** | Null-guard mAll, TheSaveLoadMgr, TheWaveToTurnOnLight (may not be initialized during early boot) |
| 133,138 | BUGFIX | No | Conditional block for TheWaveToTurnOnLight |
| 145 | DEBUG | No | EnterControllerMode diagnostic |
| 252 | BUGFIX | No | Null-guard DataDir + mLeftHandNavList |

**ShellInput (src/lazer/meta_ham/ShellInput.cpp) -- 7 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 94 | SCAFFOLD | **Yes** | Native init -- skip Kinect, create SkeletonChooser, cursor panel |
| 135 | SCAFFOLD | **Yes** | Native poll -- skip gesture filters, poll chooser/cursor |
| 348 | STUB | No | Speech/voice control stubbing + hide overlay messages |
| 405 | STUB | No | Force controller mode, skip helpbar/Kinect logic |
| 440 | STUB | No | Prevent exit from controller mode |
| 464 | PLATFORM | No | Draw debug with null mSkelIdentifier guard |
| 498 | BUGFIX | No | Null-guard GetHelpBarPanel |

**MetaPanel (src/lazer/meta_ham/MetaPanel.cpp) -- 15 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 93 | PLATFORM | No | Static member definitions |
| 99 | SCAFFOLD | **Yes** | Null-init mMetaMusicManager, mCampaign, mHAQManager |
| 178 | STUB | No | Skip SongStatusMgr::Init |
| 183 | STUB | No | Skip MemcardMgr, MetagameRank, ProfileMgr, Leaderboards, FitnessGoalMgr init |
| 242,279,292,306,324 | BUGFIX | No | Null-guard sHamMaster / TheMetaMusic |
| 329 | SCAFFOLD | **Yes** | Auto-start shell music (DTA `{metamusic start}` never fires on native) |
| 346,363 | BUGFIX | No | Null-guard TheMetaMusic |
| 449 | SCAFFOLD | **Yes** | HANDLE_ACTION_IF with sHamMaster+TheMetaMusic guard for load_meta_music |

**MultiUserGesturePanel (src/lazer/meta_ham/MultiUserGesturePanel.cpp, .h) -- 7 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 50,63 | SCAFFOLD | No | mNativeEnterPending flag management |
| 70 | SCAFFOLD | **Yes** | Fire enter_gameplay directly (no Kinect skeleton assignment) |
| 275,350,528 | STUB | No | Null-guard pSkeletonChooser (no Kinect) |
| 75 (`.h`) | PLATFORM | No | mNativeEnterPending member declaration |

**MainMenuPanel (src/lazer/meta_ham/MainMenuPanel.cpp) -- 6 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 72,92,152,159,171,568 | BUGFIX | No | Null-guard TheNetCacheMgr (may not be initialized on native) |

**LoadingPanel (src/lazer/meta_ham/LoadingPanel.cpp) -- 6 blocks**

| Line(s) | Classification | Critical | Description |
|---|---|---|---|
| 29,73 | SCAFFOLD | No | sSkipLoadingMusicReadyGate flag |
| 86 | SCAFFOLD | **Yes** | Modified IsLoaded check with audio ready bypass |
| 108 | SCAFFOLD | **Yes** | Alternative loading music stream start |
| 127 | SCAFFOLD | No | Early return if gate is skipped |
| 174 | BUGFIX | No | Warn + skip if loading music MIDI not found |

**Other Ham files (single/few blocks each)**

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `AppLabel.cpp:684` | STUB | No | Empty SetTimeElapsedSince (no Xbox time services) |
| `AppMiniLeaderboardDisplay.cpp:60,72` | STUB | No | Skip TheServer.AddSink/RemoveSink |
| `BlacklightPanel.cpp:10` | PLATFORM | No | sInstance static |
| `CampaignSongSelectPanel.cpp:137,186` | BUGFIX | No | Null-guard TheCampaign/pPerformer |
| `ChallengeSortNode.h:18` | PLATFORM | No | std::vector vs STLport vector parameter |
| `ChooseModeProvider.cpp:10-168` (6 blocks) | DEBUG | No | ChooseMode diagnostic logging |
| `CursorPanel.cpp:22` | STUB | No | Skip Kinect cursor tracking |
| `HamScreen.cpp:34` | SCAFFOLD | **Yes** | Force controller mode on first screen enter |
| `HamSongMgr.cpp:250` | DEBUG | No | Invalid song_id diagnostic |
| `HamUI.cpp:268` | BUGFIX | No | Null-guard mAugmentedPhoto |
| `HamUI.cpp:346` | STUB | No | Null-guard ThePassiveMessenger/TheSkeletonIdentifier |
| `HamUI.cpp:568` | STUB | No | Skip Kinect debug drawing |
| `LetterboxPanel.cpp:20` | PLATFORM | No | sInstance static |
| `MainMenuProvider.cpp:55` | DEBUG | No | Menu item diagnostic |
| `MetaPerformer.cpp:42` | PLATFORM | No | sCheatFinale static |
| `MetaPerformer.cpp:898,904` | STUB | No | Skip RockCentral player drop jobs |
| `MetaPerformer.cpp:1157` | PLATFORM | No | Different boolean assignment pattern |
| `NavListNode.cpp:73` | BUGFIX | No | Null-guard children in DeleteAll |
| `NavListSort.cpp:31` | BUGFIX | No | Null-guard nodes in destructor |
| `PlaylistSortNode.cpp:184` | PLATFORM | No | std::vector parameter type |
| `ProfileMgr.cpp:380` | BUGFIX | No | Lazy-init sliders if Init hasn't run |
| `ProfileMgr.cpp:844` | SCAFFOLD | **Yes** | Treat content as unlocked when no profiles |
| `ProfileMgr.cpp:1077,1124` | SCAFFOLD | No | Stub LoadGlobalOptions (no save system) |
| `SkeletonChooser.cpp:142` | SCAFFOLD | No | Default player side mapping without Kinect |
| `SkeletonIdentifier.cpp:7,302` | PLATFORM/STUB | No | Skip sprintf_s template + debug draw |
| `SongSortMgr.cpp:45` | PLATFORM | No | Skip SongSort constructor on native |
| `SongStatusMgr.cpp:16` | PLATFORM | No | sFakeLeaderboardUploadFailure static |
| `VoiceInputPanel.cpp:41,227` | STUB | No | Skip voice context loading, stub all voice methods |
| `Ham.cpp:69,185` | SCAFFOLD | **Yes** | EnsureHamProvider + property initialization (ui_nav_mode, etc.) |
| `HamAudio.cpp:68` | SCAFFOLD | **Yes** | Poll FileLoader from HamAudio (LoadMgr may not poll it) |
| `HamCamShot.cpp:60` | BUGFIX | No | Null-guard theChar |
| `HamCamTransform.cpp:6,19` | BUGFIX | No | Include + cascade guard |
| `HamCharacter.cpp:41` | PLATFORM | No | Static member definitions |
| `HamCharacter.cpp:81` | BUGFIX | No | Cascade guard in destructor |
| `HamCharacter.cpp:292` | BUGFIX | No | Null-guard clips |
| `HamCharacter.cpp:525` | SCAFFOLD | No | Fall through to SongDriver when clip is null |
| `HamCharacter.cpp:768,797,802` | PLATFORM | No | Skip PPC-specific codegen variants |
| `HamCharacter.h:62` | PLATFORM | No | SetEyes accessor |
| `HamGameData.cpp:142,457` | BUGFIX/STUB | No | Warn on missing outfit remap / always report skeleton present |
| `HamListRibbon.cpp:430` | PLATFORM | No | LP64 pointer type for mElemDrawState |
| `HamListRibbon.h:20` | PLATFORM | No | LP64 pointer member type |
| `HamNavProvider.cpp:153,246,272` | DEBUG | No | ChooseMode diagnostics |
| `HamRibbon.cpp:173` | BUGFIX | No | Copy-then-resize to avoid OOB vector access |
| `HamScrollBehavior.cpp:17` | PLATFORM | No | Static member definitions |
| `HamSongData.cpp:64,117,121` | DEBUG | No | MIDI load diagnostics |
| `HamVisDir.cpp:34,41,121,128` | BUGFIX | No | Guard SkeletonUpdate::HasInstance() calls |
| `HamWardrobe.cpp:23` | PLATFORM | No | Skip Xbox stdio include |
| `HamWardrobe.cpp:286` | DEBUG | No | SyncInterestObjects diagnostic |
| `MoveDir.cpp:230` | BUGFIX | No | Skip during cascade destruction |
| `MoveGraph.cpp:31,44` | PLATFORM | No | WaitUntilReady for synchronous stream reads |
| `MoveMgr.cpp:293` | BUGFIX | No | Warn if move_graph not found |
| `MoveMgr.cpp:394` | SCAFFOLD | No | Create default SongLayout if missing |
| `MoveMgr.cpp:403` | SCAFFOLD | No | Guard SetDefaultReplacer on song.anim availability |
| `MoveMgr.cpp:439,451` | BUGFIX | No | Null-guard mMovesDir, propkeys |
| `MoveMgr.cpp:600` | BUGFIX | No | Null-guard moveDir |
| `MoveVariant.cpp:44` | BUGFIX | No | Clear stale adjacency flag after Load |
| `OriginalChoreoRemixer.cpp:123,160` | BUGFIX | No | Non-fatal MILO_FAIL + null deref prevention |
| `PhotoSpotlightPositioner.cpp:73` | PLATFORM | No | Direct mesh transform access instead of offset |
| `PoseFatalities.cpp:382,436` | STUB/BUGFIX | No | Skip pose battle mode / null-guard anims |
| `PracticeSection.cpp:10,17` | BUGFIX | No | Skip STLport _Destroy_Range / cascade guard |
| `SongCollision.cpp:20` | PLATFORM | No | sCollisionTolerance static |
| `SuperEasyRemixer.cpp:61,113,225,232` | BUGFIX | No | Guard against empty move data / unloaded layout |
| `DanceRemixer.cpp:186,190` | BUGFIX | No | Null-guard moveDir/detector |
| `FilterVersion.cpp:8` | PLATFORM | No | sNumHam2Nodes static |
| `FreestyleMove.h:10,29` | PLATFORM | No | size_t operator new[] |
| `FreestyleMoveRecorder.cpp:21,26` | STUB | No | Skip PPC intrinsics, stub entire class |

### Math (src/system/math/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `Easing.h:342` | PLATFORM | No | `inline` keyword for template function |
| `Geo.cpp:564,978` | PLATFORM | No | Skip STLport BSPFace comparator + MakeBSPTree (STLport list ops) |
| `Key.h:104` | PLATFORM | No | Different vector insert syntax |
| `Rand.cpp:65` | BUGFIX | **Yes** | Modulo instead of `(Int()*range)>>16` (UB on 32-bit overflow) |
| `Trig.cpp:22` | BUGFIX | No | Guard against OOB write in trig table init |
| `mtx.cpp:78` | BUGFIX | **Yes** | Correct matrix multiply formula (PPC decomp had swapped members that coincidentally matched PPC asm) |

### Meta (src/system/meta/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `Achievements.cpp:36` | STUB | No | Empty PlatformInit, stub achievement functions |
| `MoviePanel.cpp:60,102,128,185` | SCAFFOLD | No | Modified movie loading path, skip empty movies, stub IsLoaded |
| `PreloadPanel.cpp:15` | PLATFORM | No | sCache static |
| `PreloadPanel.cpp:66` | SCAFFOLD | **Yes** | Skip content mount/cache (no ark songs) |
| `SongPreview.cpp:128` | BUGFIX | No | Skip if audio not initialized |
| `Sorting.cpp:5,42` | PLATFORM | No | strings.h include + strcasecmp |
| `StoreEnumeration.cpp:45` | PLATFORM | No | Proper member access instead of struct offsets |
| `StoreOffer.cpp:31,45` | PLATFORM | No | strtoull instead of _strtoui64 |
| `StorePanel.cpp:59` | BUGFIX | No | Null-guard TheNetCacheMgr |
| `StorePanel.cpp:568` | STUB | No | Skip vtable-dependent offer handling |

### Milo Core (src/system/obj/*)

This is the largest and most critical subsystem. The blocks primarily deal with:
1. **Cascade destruction safety** (ObjectDir::DeleteObjects three-phase protocol)
2. **Ring corruption prevention** (snapshot-based ReplaceRefs, sentinel tracking)
3. **LP64 compatibility** (pointer sizes, union sizes, allocator signatures)
4. **Object resolution fallbacks** (parent dir chain walking)

**ObjectDir (Dir.cpp, Dir.h) -- 30 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| BUGFIX | 18 | **Yes** | Three-phase DeleteObjects (720), NullifyAllRefs (56), DirPtrRefCounts O(1) HasDirPtrs (515), MergeDirs subdir skip (374), cascade ring safety in ObjDirPtr dtor (66/92/102), deferred free (478), null entry skip in ObjDirItr (578) |
| SCAFFOLD | 5 | **Yes** | ProxyDir fallback object resolution (939), DirLoader parent propagation (639/1292/1304), venue dir detection (686) |
| PLATFORM | 4 | No | Includes (13/26), static definitions (714), BinStream operator<< (269) |
| DEBUG | 3 | No | ChooseMode diagnostics (35/139/153/173/202) |

**Object (Object.cpp, Object.h) -- 36 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| BUGFIX | 27 | **Yes** | Snapshot-based ReplaceRefs (340), NullifyAllRefs (371), cascade skip in ~Object (107), null-this guards for sinks (432/444/454/461), ring sentinel (56/94/102/106), SafeReleaseFromRing (79), NullifyObj virtual (119/206), ring dirty flag (1146/1276/1287), AddRef ring repair (1297) |
| PLATFORM | 8 | No | gInReplaceList flag (16/23), iterator begin/end (395), using-declarations for Clang (465), operator new signatures (480), ASSERT_REVS warning (968), ObjVector size check (1462) |
| SCAFFOLD | 1 | No | ReplaceList declaration (178) |

**ObjPtr_p.h -- 14 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| BUGFIX | 8 | **Yes** | Cascade skip in ~ObjPtr (35), safe release (55), skip_release goto (71), suppress erase during ReplaceList (246), parent dir chain fallback (103/329/528) |
| PLATFORM | 6 | No | Iterator conversion (293/304), ObjPtrVec_impl include (377), operator new (404), ObjPtrList Link (574/595), RefOwner fix (192) |

**DirLoader (DirLoader.cpp, .h) -- 7 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| PLATFORM | 3 | No | sPathEval static (22), WaitUntilReady calls (143/349) |
| BUGFIX | 4 | **Yes** | Early EOF handling (666), stub vtable detection (674/869), ParentDir accessor (37) |

**DataNode, DataArray, DataFile, DataFunc -- 20 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| BUGFIX | 13 | **Yes** | Null property return guard (DataNode:142), type-safe fallbacks for Sym/Str/Obj (371/391/415/440/464/528/577/597/617/637), bad type abort (780) |
| SCAFFOLD | 4 | No | GetObj warn-not-crash (545), find_obj warn-not-crash (963/1025), null return (579) |
| PLATFORM | 3 | No | DataArray conditional stack guards (399/410), LP64 null comparison (750) |

**Data.h -- 6 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| PLATFORM | 6 | **Yes** | Zero all 8 bytes of DataNode union on LP64 (64/72/80/88/115), separate unsigned int constructor (95) |

**Task (Task.cpp, .h) -- 7 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| BUGFIX | 5 | **Yes** | LiveTasks set for stale-pointer detection (13/22/28/33/410), cascade guard in StartTask (432/507) |
| PLATFORM | 2 | No | IsLive declaration, LiveTasks include |

**TypeProps, Utl -- 7 blocks**

| Classification | Count | Critical | Key blocks |
|---|---|---|---|
| PLATFORM | 2 | No | UncheckedStr for key comparison (14/55/93) |
| BUGFIX | 3 | No | Null typeDef guard (208), null array guard (229) |
| SCAFFOLD | 2 | No | MergeDirs flag (422/429), merge debug (96) |

### Movie (src/system/movie/*, src/system/moviebink/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `MovieSys.cpp:9` | PLATFORM | No | Native BinkMovieSys reference |
| `Splash.cpp:158` | PLATFORM | No | Direct member access for mReleaseImmediate |
| `Splash.cpp:197` | PLATFORM | No | Skip threaded splash, run directly |
| `TexMovie.cpp:2,211` | PLATFORM | **Yes** | FFmpeg/WebMovie implementation + RGBA texture upload |
| `BinkMovieImpl.cpp:7,19` | PLATFORM | No | Skip STLport includes/instantiations |
| `BinkMovieImpl.cpp:111` | PLATFORM | No | Skip STLport map clear |
| `BinkMovieImpl.cpp:117` | STUB | No | Stub all BinkMovieImpl methods (no Bink SDK) |
| `BinkMovieImpl.h:29` | PLATFORM | No | Skip size assert |
| `BinkMovieSys.cpp:115` | STUB | No | Stub all Bink SDK functions |

### Native Port (native/src/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `msvc_compat.h:11` | PLATFORM | No | C++17 compat shims (random_shuffle, mem_fun) |
| `native_job_stubs.cpp:4` | STUB | No | Xbox job system stubs (_XMMATRIX, SingleItemEnumJob, etc.) |
| `GestureMgr_Native.cpp:1` | PLATFORM | **Yes** | Native gesture system (CameraInput, YOLO pose provider) |
| `RndTex_Native.cpp:57` | BUGFIX | No | Guard against bad revision in texture PreLoad |

### Network (src/system/net/*, src/lazer/net_ham/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `RockCentral.cpp:154` | STUB | No | Skip Xbox screen resolution job |
| `RockCentral.cpp:241` | STUB | No | Delete job immediately instead of processing |
| `JsonUtils.cpp:67` | BUGFIX | No | Null-guard JSON object |
| `WebSvcMgrCurl.cpp:27` | BUGFIX | No | Null-guard curl handle |
| `XLSPConnection.cpp:231,253,264,270` | PLATFORM | No | std::map vs STLport map operations, different error handling |

### OS/Platform (src/system/os/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `ArkFile.cpp:12,95` | PLATFORM | **Yes** | Native ark file read (direct file I/O, synchronous async) |
| `ArkFile_p.h:30,37` | PLATFORM | No | size_t operator new |
| `AsyncFile.cpp:57` | PLATFORM | No | Skip Xbox async file creation (Holmes client, block mgr) |
| `AsyncFile.cpp:294` | PLATFORM | No | Skip LE-to-BE endian swap (native is LE) |
| `Debug.cpp:175` | PLATFORM | **Yes** | Non-fatal MILO_FAIL (matches Xbox "Continue" dialog) |
| `Debug.cpp:283` | PLATFORM | No | Skip unhandled exception filter |
| `Debug.h:110,117` | PLATFORM | No | MILO_FAIL_DTA as warn, MILO_LOG with stderr |
| `File.cpp:23,51,365` | PLATFORM | **Yes** | POSIX file system (stat, realpath, system root init) |
| `File.cpp:674` | PLATFORM | No | Skip PPC RecursePatternInternal |
| `File.h:83` | PLATFORM | No | Undefine glibc st_ctime/st_atime/st_mtime macros |
| `FileCache.cpp:17` | PLATFORM | No | Static member definitions |
| `HolmesClient.cpp:419,881` | STUB | No | Holmes remote debug stubs |
| `Joypad_Xinput.cpp:72` | PLATFORM | No | Conditional check via TheUserMgr |
| `PlatformMgr.cpp:15,230` | STUB | No | XShow callback static + sign-in stub |
| `System.cpp:226` | PLATFORM | No | Poll CacheMgr/NetCacheMgr conditionally |
| `System.cpp:348` | PLATFORM | No | Skip Xbox stack trace implementation |
| `System.cpp:466` | PLATFORM | **Yes** | Native SystemInit sequence (different subsystem order) |
| `System.cpp:493` | STUB | No | Skip NetCacheMgrInit |
| `System.cpp:581` | PLATFORM | **Yes** | Native PreInit (Timer, File, AppChild, DateTime, seed) |
| `System.cpp:688` | STUB | No | HongKongExceptionMet returns false |
| `System.h:6` | PLATFORM | No | Larger command line buffer (8KB) |
| `Timer.cpp:6,52` | PLATFORM | No | Skip PPC timebase, use microsecond cycles2ms |
| `Timer.h:6` | PLATFORM | **Yes** | `__mftb()` via steady_clock, `__loadwordbytereverse` no-ops |
| `VirtualKeyboard.cpp:49` | STUB | No | Stub PlatformPoll, GetInputString, ShowKeyboardUI |

### Rendering (src/system/rndobj/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `AmbientOcclusion.cpp:29` | PLATFORM | No | Native Edge::operator< implementation |
| `AmbientOcclusion.cpp:1660` | PLATFORM | No | Skip STLport Triangle vector erase specialization |
| `Anim.cpp:423` | SCAFFOLD | No | Auto-null mAnimTarget when non-looping animation completes |
| `BaseMaterial.h:173` | PLATFORM | No | SetCull accessor |
| `Bitmap.cpp:1254` | PLATFORM | No | Skip PPC bitmap Load implementation |
| `Cam.cpp:19` | PLATFORM | No | Skip Frustum size assert |
| `Cam.cpp:23` | PLATFORM | No | sCurrent static definition |
| `Cam.cpp:193` | PLATFORM | **Yes** | Native camera Select (MakeDrawTarget, viewport, projection matrix) |
| `Cam.cpp:301,360` | PLATFORM | **Yes** | WorldToScreen / ScreenToWorld (native view+projection math) |
| `Console.cpp:8,16` | STUB | No | Skip HolmesClient include, stub HolmesClientSendMessage |
| `CubeTex.cpp:10` | PLATFORM | No | Skip PPC RndTex::Load |
| `Dir.h:65` | PLATFORM | No | NumDraws/NativeAddDraw accessors |
| `Draw.cpp:14` | PLATFORM | No | Static member definitions |
| `Draw.cpp:109` | SCAFFOLD | No | Disable frustum culling (frustum setup not yet matching) |
| `Draw.cpp:153` | PLATFORM | No | Skip PPC DrawPtrVec::Draw |
| `Env.h:81` | PLATFORM | No | Fog accessors |
| `Mat.cpp:22,389` | PLATFORM | No | sMetaMaterials static + null guard |
| `MatAnim.cpp:278` | PLATFORM | No | Skip STLport Key<TexPtr> operator>> specializations |
| `Mesh.cpp:133` | PLATFORM | **Yes** | CleanupGpuMesh call in destructor |
| `Mesh.cpp:355` | PLATFORM | **Yes** | Native CachedRead with endian swap |
| `Mesh.cpp:1179` | PLATFORM | No | Skip PPC OnSync (patch generation) |
| `Mesh.cpp:1678` | PLATFORM | **Yes** | Native compressed vertex preservation for GPU upload |
| `Mesh.cpp:1720,1786` | PLATFORM | No | WaitUntilReady for stream reads |
| `Mesh.cpp:1849` | PLATFORM | No | Different vertex iteration pattern |
| `Mesh.h:29` | PLATFORM | No | Reset mOffset in Vert constructor |
| `Mesh.h:153` | PLATFORM | No | Virtual DrawShowing declaration |
| `MultiMesh.h:62,73,81` | PLATFORM | No | malloc/free instead of 32-bit FixedSizeAlloc |
| `MultiMeshProxy.cpp:5` | PLATFORM | No | Different constructor init |
| `Part.cpp:583` | PLATFORM | **Yes** | DrawParticlesBillboard call |
| `Part.h:260` | PLATFORM | No | Tile count accessors |
| `PostProc.cpp:20` | PLATFORM | No | Static member definitions |
| `PostProc.h:97` | PLATFORM | No | PostProc getter accessors |
| `PostProc_NG.cpp:276` | STUB | No | Stub DoVelocity (ILP32 struct offsets) |
| `PropAnim.cpp:11` | PLATFORM | No | cstdlib include |
| `PropAnim.cpp:146,176` | DEBUG | No | Merge debug diagnostics |
| `PropKeys.cpp:843,990` | PLATFORM | No | Skip STLport push_heap/swap specializations |
| `PropKeys.h:82` | BUGFIX | **Yes** | Handle mTarget replacement via SetTarget (Itanium ABI vtable dispatch differs from MSVC) |
| `Ribbon.cpp:132,203,272` | PLATFORM | No | Skip PPC ribbon implementations (pointer arithmetic as int) |
| `Rnd.cpp:85` | PLATFORM | No | sPostProcPanelCount static |
| `Rnd.cpp:102` | PLATFORM | No | Stub sTexture pointer |
| `Rnd.cpp:294` | PLATFORM | **Yes** | Override width/height to 1280x720 |
| `Rnd.cpp:428,452` | PLATFORM | No | Skip Xbox compress thread |
| `Rnd.cpp:575` | SCAFFOLD | No | Treat all lens flares as fully visible (no GPU occlusion query) |
| `Rnd.cpp:1190` | PLATFORM | No | Skip Xbox texture compression system |
| `Rnd.h:167` | PLATFORM | No | ClearDepthForOverlay virtual |
| `Rnd_NG.cpp:44,104,112,118` | PLATFORM | No | Skip NG CreateDefaults/fails on native |
| `Shader.cpp:25` | PLATFORM | No | Static member definitions |
| `Shader.cpp:211,237,253` | BUGFIX | No | Skip editor-mode shader validation, fallback to error shader |
| `Tex.cpp:104,122` | PLATFORM | **Yes** | Native RndTex Load/PreLoad (in RndTex_Native.cpp) |
| `Tex.h:48,116,125` | PLATFORM | No | Virtual draw target methods, Bitmap accessor, presync/sync |
| `Text.cpp:24` | PLATFORM | No | cstdio/stdlib/string includes |
| `Text.cpp:583,599` | BUGFIX | No | Guard against garbage face/char counts |
| `Text.cpp:607` | DEBUG | No | GPU debug labels for text meshes |
| `Text.cpp:643,652` | BUGFIX | No | Replace asserts with warns for displayableChars |
| `Text.cpp:824` | PLATFORM | **Yes** | wchar_t 4-byte conversion for WordWrap break chars |
| `Text.cpp:1051` | PLATFORM | No | Empty block (placeholder) |
| `Text.cpp:1276` | BUGFIX | **Yes** | Reset displayable char counts before recomputing |
| `Text.cpp:1429,1460` | PLATFORM | **Yes** | Native blacklight packet pool (struct layout differs) |
| `Text.cpp:1606` | PLATFORM | **Yes** | u16 string operations for 4-byte wchar_t |
| `Text.cpp:1690,1727,1738,1766` | PLATFORM | No | u16 ellipsis/break operations |
| `Text.cpp:1934` | PLATFORM | No | Direct bounds member assignment |
| `Text.cpp:2295` | BUGFIX | No | Call UpdateText to ensure meshes exist |
| `Text.cpp:2399` | PLATFORM | No | std::vector vs STLport vector for wide chars |
| `Text.cpp:2471` | BUGFIX | No | Guard against empty meshes |
| `Text.cpp:2637` | PLATFORM | No | Skip STLport map/set template instantiations |
| `Text.h:16` | PLATFORM | No | HX_VECTOR macro for std::vector vs STLport |
| `Text.h:141` | PLATFORM | No | BlacklightPacket struct layout |
| `Text.h:335` | PLATFORM | No | Text width/alignment accessors |
| `Trans.cpp:16` | PLATFORM | No | sShadowPlane static |
| `Utl.cpp:946` | PLATFORM | No | intptr_t pointer arithmetic for scaled lengths |
| `Utl.cpp:1835` | PLATFORM | No | Skip PPC Edge::operator< |
| `Utl.cpp:2124` | PLATFORM | No | intptr_t vertex base pointer arithmetic |
| `wordwrap.cpp:57` | PLATFORM | No | Different pointer arithmetic for break position check |

### Utilities (src/system/utl/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `AllocInfo.cpp:38` | PLATFORM | No | LP64 offset for mTimeSlice |
| `AllocInfo.cpp:53` | PLATFORM | No | Custom operator new |
| `AllocInfo.h:39` | PLATFORM | No | operator new declaration |
| `BinStream.cpp:77,185` | PLATFORM | **Yes** | Endian-conditional byte swap (LE host reads BE files) |
| `BinStream.cpp:129` | BUGFIX | No | PopRev empty stack abort |
| `BinStream.cpp:216` | BUGFIX | No | String length sanity check |
| `BinStream.cpp:239` | PLATFORM | **Yes** | WaitUntilReady spin-wait for async streams |
| `BinStream.h:70` | PLATFORM | No | WaitUntilReady declaration |
| `BinStream.h:103,137` | PLATFORM | No | Skip duplicate u32 read/write ops (u32=uint on LP64) |
| `BinkIntegration.cpp:87` | STUB | No | Stub all Bink SDK functions |
| `BinkIntegration.cpp:231` | PLATFORM | No | Skip Xbox Bink implementation |
| `ChunkStream.cpp:35,79,112,219,281,356,375,405,439,459` | PLATFORM | **Yes** | Native chunk stream (synchronous decompression, cross-chunk reads, endian handling) |
| `ChunkStream.h:62,69` | PLATFORM | No | operator new signatures |
| `ChunkStream.h:76` | PLATFORM | No | Unreread method |
| `DebugGraph.cpp:4` | PLATFORM | No | `__fsel` polyfill |
| `FileStream.cpp:28` | DEBUG | No | Read failure diagnostic |
| `GlitchFinder.cpp:9` | PLATFORM | No | Static member definitions |
| `Loader.cpp:17` | PLATFORM | No | sFileOpenCallback static |
| `Loader.cpp:293` | PLATFORM | **Yes** | PollFrontLoader (synchronous loader polling) |
| `Locale.h:44` | PLATFORM | No | Explicit constructor init (no BSS zero) |
| `MakeString.cpp:42` | PLATFORM | No | ValidateThreadId always returns true |
| `MemMgr.cpp:46,127,148,312` | PLATFORM | **Yes** | Native memory management (malloc/free instead of custom heaps) |
| `MemMgr.h:3,100` | PLATFORM | **Yes** | size_t operator new/delete, OBJ_MEM_OVERLOAD macro |
| `MemStream.h:19` | PLATFORM | No | Buffer() accessor via data() |
| `MemTrack.cpp:96` | PLATFORM | No | Skip Xbox 0xA0000000 heap detection |
| `MemTracker.cpp:158` | PLATFORM | No | DebugHeapAlloc operator new |
| `MemTracker.cpp:637` | PLATFORM | No | Skip STLport sort_heap specialization |
| `MemTracker.h:44` | PLATFORM | No | operator new declaration |
| `PoolAlloc.cpp:28,45` | PLATFORM | **Yes** | malloc/free bypass for 64-bit (pool uses int-sized slots) |
| `PoolAlloc.h:70` | PLATFORM | **Yes** | POOL_OVERLOAD macro with placement new |
| `Song.cpp:111,137,290,548` | SCAFFOLD | **Yes** | Async audio state machine (PollAsyncState, deferred Play/SyncState) |
| `Song.h:92` | PLATFORM | No | AsyncState enum + members |
| `Std.h:71` | PLATFORM | No | Vector swap-to-free pattern |
| `Str.cpp:7` | PLATFORM | No | FixedString::npos definition |
| `Str.cpp:306` | BUGFIX | No | Self-assignment guard for ASan |
| `Str.h:18` | PLATFORM | No | strieq via strcasecmp |
| `Symbol.h:3,36` | PLATFORM | No | cstdint include, operator int via intptr_t |
| `trie.cpp:328,350` | PLATFORM | No | Different root node access pattern |

### World (src/system/world/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `CameraManager.cpp:109` | BUGFIX | **Yes** | NaN/inf guard on camera time |
| `CameraManager.cpp:233` | BUGFIX | **Yes** | NaN/inf guard on camera transform after SetFrame |
| `CameraShot.cpp:40,290,360,376,529,536,622,1378` | SCAFFOLD/BUGFIX | **Yes** | Multiple null guards and native camera setup adjustments |
| `Crowd.cpp:27,39,161,364,652,676,808,821,1111,1439` | SCAFFOLD/STUB | No | Crowd rendering adaptations and Xbox-only exclusions |
| `Crowd3DCharHandle.cpp:4,54` | BUGFIX | No | Null guards |
| `DefaultPhysicsManager.cpp:122` | PLATFORM | No | Skip Xbox physics assertion |
| `Dir.cpp:31` | PLATFORM | No | Native WorldDir adaptations |
| `Dir.h:145` | PLATFORM | No | WorldDir accessor |
| `LightPreset.cpp:19` | PLATFORM | No | Skip PPC-specific LightPreset code |
| `PhysicsVolume.cpp:16,190` | BUGFIX | No | Null guards for physics |
| `Reflection.cpp:154` | PLATFORM | No | Skip PPC reflection code |
| `SpotlightDrawer.cpp:29,628` | PLATFORM | No | Native spotlight setup |
| `SpotlightDrawer_NG.cpp:22,113,129,504,852,905` | PLATFORM/STUB | No | NG spotlight adaptations, skip Xbox-specific render calls |
| `Spotlight.cpp:28,32,663` | PLATFORM | No | Native spotlight member defs + draw |

### XDK Compat (src/xdk/*)

| File:Line | Classification | Critical | Description |
|---|---|---|---|
| `win_types.h:133` | PLATFORM | No | Native type definitions for Xbox types |
| `winsockx.h:2` | PLATFORM | No | Native winsock type stubs |
| `vectorintrinsics.h:4` | PLATFORM | No | XMMATRIX/XMVECTOR type definitions |
| `ppcintrinsics.h:8` | PLATFORM | No | Skip PPC intrinsic definitions on native |
| `xbox.h:164` | PLATFORM | No | Xbox API type stubs |
| `winnt.h:235` | PLATFORM | No | OVERLAPPED/CRITICAL_SECTION type stubs |

---

## Classification Totals

| Classification | Count | Percentage | Notes |
|---|---|---|---|
| **PLATFORM** | 177 | 32.6% | Permanent. Legitimate architecture differences (LP64, endian, allocator, threading, graphics, STLport vs libstdc++). |
| **BUGFIX** | 131 | 24.1% | Semi-permanent. Fixes for crash/corruption due to different ABI, memory layout, or lifecycle. ~55 are cascade-related (ObjectDir::DeleteObjects). |
| **DEBUG** | 97 | 17.9% | Low priority. Printf/logging gated behind env vars (MILO_DEBUG_UI_FLOW, MILO_DEBUG_CHOOSE_MODE, MILO_DEBUG_MERGE). |
| **SCAFFOLD** | 71 | 13.1% | **High priority for convergence.** Temporary hacks to bypass DTA/Xbox-only flows. Should be replaced with proper implementations as DTA execution improves. |
| **STUB** | 65 | 12.0% | Permanent. Xbox-specific subsystems (Kinect, Xbox Live, Bink, Holmes debugger) that will never exist on native. |
| **Unclassified** | ~322 | -- | STLport template instantiations, include guards, brace-closing blocks, and other mechanical guards counted in the raw 865 but not individually classified above. |

## Convergence Priority

### Phase 1: Remove SCAFFOLD blocks (71 blocks)
The SCAFFOLD blocks are the primary divergence source. They bypass DTA scripts, auto-advance stuck screens, force-start gameplay, and null-guard globals that should be initialized. As DTA execution and subsystem initialization improve, these should be systematically removed or replaced with proper convergent implementations.

**Highest-impact SCAFFOLD areas:**
1. `UI.cpp` (8 blocks) -- screen transition hacks, stuck screen auto-advance, animation timeouts
2. `PanelDir.cpp` (2 blocks) -- flow activation filter, auto-activate game Flows
3. `Game.cpp` (10 blocks) -- audio init bypass, MoveDir discovery, state machine resets
4. `GamePanel.cpp` (4 blocks) -- force-start game, skip intro gating
5. `Ham.cpp` + `MetaPanel.cpp` (5 blocks) -- HamProvider initialization, shell music start
6. `ShellInput.cpp` (2 blocks) -- native init/poll replacing Kinect subsystems

### Phase 2: Audit BUGFIX blocks (131 blocks)
Many BUGFIX blocks mask real decomp issues (null objects that should exist, missing initialization). As the engine converges, some of these can be removed because the underlying issue (e.g., DTA not firing, globals not initialized) gets fixed.

### Phase 3: Reduce DEBUG blocks (97 blocks)
Consolidate behind a unified native debug system rather than scattered env-var checks. Not blocking convergence but adds code noise.
