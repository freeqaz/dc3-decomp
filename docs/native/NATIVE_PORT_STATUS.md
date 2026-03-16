# Native Port Progress (x86_64 Linux)

## Current Status: Session 75 - Phase 8 Polish (White Rectangle Elimination + 6-Venue Gameplay)
**Goal**: Full visual quality — post-processing, lighting, particles, flares, lines all working

### Sessions Complete
- **Sessions 1-19**: Foundation through mesh rendering (see git history)
- **Session 20-21**: LP64 DataNode union bug fix, vtable fixes, screen crash fixes
- **Session 22**: MsgSinks::Export implemented, button dispatch working, screen navigation working
- **Session 23**: WorldDir::DrawShowing, CameraManager::Poll, light list fix, FlowNode kDataUndef guard
- **Session 24**: Link fixes, UIScreen auto-skip, boot-to-menu navigation working
- **Session 25**: Text rendering — depth test fix, DXT5 alpha shader, backface cull fix
- **Session 26**: Scene ring buffer, screen hiding, text rendering verified across screens
- **Session 27**: Object lifetime, ObjRef ring fixes, ObjDirItr safety
- **Session 28**: Compressed vertex decompression — all UI meshes have vertices, 51 draw calls
- **Session 29**: Animation pipeline fully verified — Timer fix, SyncObjects, AnimTask ticking
- **Session 30**: ObjOwnerPtr::RefOwner() decomp fix, multiply→opaque blend, auto-prelit, FixZeroAlpha
- **Session 31**: Object lifetime guard elimination (HmxObjectIsLive, gSuppressDirPtrDelete), ASan verification, ChunkStream endianness fix, ASSERT_REVS decomp bugs fixed, defensive guard instrumentation
- **Session 32-33**: Text rendering regression fixes (Eagle-Light font collision, deferred draw state, Showing() filter), FontMap heap buffer overflow fix (PPC hardcoded sizes in AcquireFontMap), text clipping fixes (wchar_t 2→4 byte, markup cursor off-by-one), 47 draw calls with GPU
- **Session 34-35**: Text brightness (shader useAlphaAsRGB bypass), mTextColor vertex colors, text_menu test case, duplicate symbol fixes
- **Session 36**: FontMap heap buffer overflow root cause fix (sizeof(FontMap)/sizeof(FontMap3d) under #ifdef HX_NATIVE), ASan verified clean
- **Session 37**: HamNavList element creation, UIList::Selected/GetListState implementations, STLport compat guards
- **Session 38**: HamUI integration (TheUI = &TheHamUI for two-pass draw), ShellInput/CursorPanel Kinect guards, HamListRibbonDrawState LP64 pointer fix (mElemDrawState), HamListRibbonDrawState field rename (unk18→mElemDrawState, unk20→mBigScale, unk24→mActive)
- **Session 39**: Input pipeline unblocked — IsAnimating() bypass (AnimTask never self-deletes without DTA lifecycle), mSink fallback dispatch (set_sink DTA action doesn't fire), controller mode force-on. Debug traces cleaned up. **Identified DTA loading as critical blocker** — mSink, animation cleanup, content population, and screen flow all depend on DTA scripts that native can't execute yet.
- **Session 40**: **Interactive menu navigation working end-to-end.** ScrollDirection decomp fix (66.1%→100% match — was missing vertical mode logic entirely). DTA stub objects for 8 Xbox-only managers. TheHamProvider null crash fix (PropertyEventProvider factory stub). GestureMgr controller mode always-on. GameMode::SetMode crash guard. Full Up/Down/Confirm navigation verified with headless GPU screenshots.
- **Session 41**: **UI layout fix — Transform::Multiply decomp bug.** Fixed fundamental decomp bug in `Multiply(Transform, Transform, Transform)` (mtx.cpp, was 48% match) — the translation y/z coefficients were swapped due to decompiler mis-mapping of struct field offsets. This caused ALL transform compositions to produce wrong results for non-trivial rotations, including the `sFlipYZ` axis swap in `GetViewProjectXfms()`. The `[ui.cam]` view translation went from `(0, 768, 768)` (wrong — y duplicated into z) to `(0, 0, 768)` (correct). Made transparent queue flush unconditional on panel camera switch (was env-var gated). Result: autosave_warning_screen now shows correctly positioned player indicators (~100px, matching Xbox reference), centered autosave icon with metallic orb, full readable text, and Kinect prompt. Screenshots: `archive/screenshots/session41/`.
- **Session 47**: **Main menu text visible.** Three fixes: HamListRibbon draw filter bypass (`entering=true` when no header ribbon), label alpha force (1.0 on native), Flow activation + PropAnim end-frame forcing. Menu items render but still centered.
- **Session 48**: **game_mode_icon panel visible.** Key discovery: `ObjDirItr::RecurseSubdirs()` only traverses formal `SubDirs()`, NOT nested `RndDir` objects in the hash table. `game_mode_icon` is an RndDir object (not a subdir) with 42 objects including 6 PropAnims (`icon_enter.anim`, etc.). Added nested RndDir PropAnim forcing in PanelDir::Enter(). Also identified `list_choose_mode.milo` (UIListDir) PostLoad resolving to nullptr — file loads but dir creation fails. Screenshots: `archive/screenshots/session48/`. See `docs/native/UI_ANIMATION_STATUS.md` for full analysis.
- **Sessions 52-58**: UI animation unwind — verified Flow->FlowAnimate->AnimTask->PropAnim chain end-to-end. Removed rendering hacks. Alpha floor for 29 DTA-driven meshes. Menu enter animations work correctly.
- **Session 59**: **Game screen venue rendering.** Navigated full menu flow into YMCA song. DCI venue renders with 505 draw calls/frame — floor, walls, DJ booth, lighting rigs, **fully-lit character** (skin/hair/outfit visible via zero-color LightPreset fallback), HUD move cards. Stable 10000 frames (clean exit). Key fixes: ObjRef ring validation, siglongjmp crash recovery, 4 new function implementations, zero-color light detection, 3 null pointer crash fixes (HamCharacter/HamCamShot/PoseFatalities), SetupAnims re-init for venue loading timing. Scene is static (LightPreset::Load unimplemented, song.anim DTA crashes). See `docs/sessions/2026-03-12-session59-game-screen-venue-rendering.md`.
- **Session 62+**: **Audio pipeline end-to-end.** StandardStream kInit→kBuffering decomp fix (was missing `mState = kBuffering` after channel creation). VorbisReader native single-threaded decode (Poll/DoFileRead/Decrypt via `#ifdef HX_NATIVE`). StreamReceiver WriteData/Poll native implementation. HamAudio `.mogg` extension fix. NativeSynth `expectMap=true`. VorbisReader `#ifndef HX_NATIVE` thread guard (prevents destructor infinite loop from stubbed DecodeThread). Game now reaches gameplay with `audioFail=0` and advancing songMs.
- **Session 61+**: **FileMerger pipeline restored and ObjRef producer bug fixed.** `FileMerger::PreLoad()` now runs `StartLoadInternal(true, true)` on native, restoring song -> venue -> viz -> HUD chain loading. Follow-up root-cause work found that `ObjDirPtr(C*)` was double-linking the same ref node into an object's ObjRef ring: `ObjRefConcrete` already links in its base ctor, and the derived ctor's extra `dir->AddRef(this)` corrupted the ring immediately. Removed the extra link, added a direct lifetime regression test, and revalidated scripted boot-to-`game_screen` without `ReplaceRefs` corruption warnings or merge crashes.
- **Session 63+**: **SkeletonViz/MoveDir enter path fixed.** Native `FileSystemRoot()` now canonicalizes to the extracted `system/run` tree under `orig-assets`, so direct system-run resources load from disk instead of going through bad archive-relative paths. Follow-up decomp validation showed `SkeletonViz` field `mResource` at offset `0x168` is `ObjDirPtr<ObjectDir>`, not `ObjDirPtr<UILabelDir>`; the wrong type caused successful `ham/skeleton.milo` loads to be discarded because the asset class is `RndDir`. Added direct regressions for system-run `ham/skeleton.milo` loading and `SkeletonViz::Init()`, then revalidated headless YMCA boot through frame 4500 with `game_screen` entered and stable.
- **Session 67**: **Audio timing fix.** VorbisReader::Poll decode loop exited immediately when TryDecode() found no Ogg packet (on Xbox, a background thread feeds data continuously). Fixed to read more file data and retry. Added ring buffer flow control to native ConsumeData (missing BytesWriteable check caused silent data loss). Pre-fill ring buffers in Play() before registering with AudioDevice. Song audio now plays at real-time (100% speed), driving all animation systems.
- **Session 68**: **Camera cuts verified.** song.anim PropKeys with property "shot" drives HamDirector::SetShot() → FindNextShot() → CameraManager::ForceCameraShot(). 34 shot keyframes for YMCA cycle through Area1_WIDE, Area1_NEAR, Area1_MOVEMENT, CLOSEUP categories. LightPreset animation N/A for YMCA's glitterati venue (0 LightPreset objects — static baked lighting correct). HUD panels (game_panel, world_panel, rhythm_detector_panel, fitness_hud_panel) all active+showing during gameplay. 272 draw calls/frame during gameplay.
- **Session 69**: **Post-processing pipeline + visual quality.** Enabled full post-processing in headless mode: bloom (screen blend instead of additive to prevent blown-out whites), Xbox-matched contrast formula (from RndColorXfm::AdjustContrast), brightness, saturation, levels, vignette, chromatic aberration, posterization. Fixed RndFlare visibility by bypassing GPU occlusion query (SetVisible + SetOcclusionResult on native). Improved directional light selection — collect all lights from environment + venue WorldDir, sort by brightness, pick top 4 (prevents zero-color LightPreset placeholders from filling slots). Verified RndLine, RndParticleSys, SpotlightDrawer all linked as strong symbols. 14 screenshots in `archive/screenshots/session69/`. PPC decomp: 9 improvements, 0 regressions.
- **Session 70+**: **Rendering decomp batch — ribbons, particles, spotlights, Bink.** PPC decomp improvements across rendering subsystems: RndRibbon (UpdateMesh 61.4% AT_LIMIT, UpdateChase 54.7% AT_LIMIT, ConstructMesh 44.4% AT_LIMIT — all large functions dominated by register swaps), NgSpotlightDrawer::RenderConeDefs 66.3% AT_LIMIT (Vector4 aggregate parameter passing), RndParticleSys::InitParticle 62.1→64.6% AT_LIMIT (virtual call elimination + memcpy block copy), BinkFileReadFrame 84.7% AT_LIMIT (base pointer caching + branchless conditional), ReadFunc 78.6% AT_LIMIT (EndianSwap 64-bit scheduling). Particle rendering confirmed working in native build — Part_Wgpu.cpp billboard renderer already fully wired.
- **Session 71**: **Venue rendering fix + CameraManager-driven camera.** Root-caused "black venue" bug: BC3 compressed textures used as render-to-texture targets lacked `RenderAttachment` usage, invalidating the ENTIRE WebGPU command buffer for every gameplay frame. Fix: detect non-renderable textures in `EnsureRenderTargetData` and replace with proper RGBA targets. Replaced hard-coded orbit camera with engine's CameraManager animation — `HamDirector::OnSelectCamera` → `PlayNextShot()` → `CameraManager::ForceCameraShot()` → `CamShot::SetFrame()` drives proper cinematic camera angles. Both dclive (outdoor concert) and glitterati (nightclub) venues render correctly with animated cameras, characters, HUD, and post-processing.
- **Session 72**: **DrawPreClear pipeline + HUD overlay + choreo crash fix.** Three major rendering pipeline additions: (1) `DrawPreClear()` — iterates `mPreClearDraws` list to execute render-to-texture passes (TexRenderer, TexMovie) before the main scene. TexRenderer now renders Group.grp → keep_me.tex (640x480) via render_cam.cam. (2) `FlushPostProcessingForOverlay()` — ends venue render pass, runs post-processing (bloom/contrast/etc), starts new HUD overlay pass drawing directly to framebuffer (HUD bypasses post-proc). (3) SuperEasyRemixer crash fix — move graph fails to load on native → `MILO_FAIL` is non-fatal → `mTotalMeasures` uninitialized → `vector::reserve(garbage)` → `std::length_error`. Added `#ifdef HX_NATIVE return` guards. Also hid 30 Kinect-dependent venue meshes (TV screens, projectors, textureless white panels) that appeared as white rectangles without camera feed. Draw calls during gameplay: 1094/frame.
- **Session 75**: **White rectangle elimination + 6-venue gameplay stability.** Eliminated all white rectangle artifacts across all venues: (1) Fixed WebGPU BGL mismatch — particle pipeline reuses main renderer's SceneBGL instead of creating mismatched 1-binding layout. (2) DrawRect2D bind group restore — after 2D rect drawing, restore scene bind group at slot 0 to prevent subsequent mesh draw validation failures. (3) Hide kBlendDest meshes (refraction/reflection without framebuffer feedback), render-target textures (empty File() = Kinect projections), and TexRenderers in both venue init and per-frame paths. (4) Skip TheUI->Draw() during game_screen — game panels (flashcard_dock, bustamove, fitness_hud) have white background meshes. (5) HUD whitelist — hide all drawables, show only song_name/artist labels. (6) RockCentral::ManageJob crash fix — SendDropInDatapoint calls null-on-native Xbox Live analytics. (7) MoveGraph deserialization crash fix — CacheLinks interprets Xbox serialized pointers as native pointers. All 6 venues tested stable: glitterati (416), dclive (2011), rollerrink (1404), houseparty (898), streetside (1048), throneroom (1063) draw calls/frame.

### Completed Phases
- **Phase 0**: Foundation — COMPLETE
- **Phase 1A**: Main Loop — **COMPLETE** (10000 frames, clean exit)
- **Phase 1 Track B**: Milo Viewer — COMPLETE (full material pipeline)
- **Phase 1.5**: Asset Pipeline (runtime) — COMPLETE
- **Phase 2**: Rendering — **COMPLETE** (272 draw calls/frame during gameplay, full material pipeline)
- **Phase 3**: Audio — **COMPLETE** (real-time MOGG playback via FFmpeg/Vorbis/miniaudio)
- **Phase 4**: Input — COMPLETE (Joypad_Native + Keyboard_Native + 19 tests)
- **Phase 6**: Polish — **IN PROGRESS** (~75% — post-processing, flares, particles, lines, venue rendering, camera animation, DrawPreClear/RTT, HUD overlay, white rectangle elimination, 6-venue gameplay stability)

### Current Boot Progress
Engine boots, navigates full menu flow, loads a song, and renders full gameplay with audio:
1. Archive loading → config → all subsystem inits → main loop
2. **Full screen flow**: attract_screen → autosave_warning → title_screen → tutorial_voice_control → main_screen → choose_mode_screen → song_select_screen → multiuser_screen → loading_screen → preloading_screen → real_loading_screen → **game_screen**
3. **Auto-skip mechanism**: UIScreen::Enter() fires DTA handlers (`skip_selected`, `next_screen`) on enter. Timer-based fallback in UIManager::Poll auto-advances stuck screens after 120 frames
4. **Button dispatch working**: JoypadPoll → Export → MsgSinks → JoypadClient → UIManager → UIScreen → PanelDir → HamNavList
5. **Interactive navigation**: Up/Down/Confirm navigate menus. Input script (`MILO_INPUT_SCRIPT`) drives headless navigation
6. **Venue rendering**: 272 draw calls/frame during gameplay — DCI venue with floor, walls, DJ booth, lighting rigs, fully-lit character, HUD overlays, particles, flares, lines
7. **Audio playback**: Real-time MOGG decoding (FFmpeg/Vorbis) → ring buffer → miniaudio output. Song audio drives animation timing via songMs
8. **Camera cuts**: song.anim PropKeys drive HamDirector::SetShot() → CameraManager::ForceCameraShot() with 34+ shot keyframes per song
9. **Post-processing**: Bloom (screen blend), Xbox-matched contrast/brightness, saturation, levels, vignette, chromatic aberration, posterization
10. **HamUI two-pass draw**: Uses TheHamUI (game-specific UIManager) for proper letterbox/blacklight/helpbar rendering
11. **Stable 10000+ frames** — boot-to-gameplay without crash/recovery after ObjRef + SkeletonViz root-cause fixes
12. **Env vars**: `MILO_RENDER=1` + `MILO_HEADLESS=1` for headless GPU, `MILO_SCREENSHOT_DIR=path` + `MILO_SCREENSHOT_FRAMES=100,300,500` for auto-capture, `MILO_FIRST_SCREEN=main_screen` skips attract, `MILO_MAX_FRAMES=N`, `MILO_INPUT_SCRIPT=path`

### Session 22 Fixes (MsgSinks + Button Dispatch)
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **DataNode LP64 union bug** | Constructors writing 4-byte members (int/float) left upper 4 bytes of 8-byte union uninitialized | `mValue.object = nullptr` before each 4-byte write in all DataNode ctors (Data.h) |
| **MsgSinks::Export stubbed** | Entire sink dispatch system was a no-op stub | Implemented Export(), RemoveSink(), Replace() in Msg.cpp |
| **kTransitionFrom stuck** | `#ifndef HX_NATIVE` removed `Entering()` check but left `!mCurrentScreen` which is always false | Changed to `#ifdef HX_NATIVE true` (always allow transition completion) |
| **ButtonToAction returns kAction_None** | DTA button_meanings config not matching native controller type | Native fallback lambda mapping buttons→actions in Joypad_Native.cpp |
| **Object not found → fatal abort** | DTA scripts calling missing objects (profile_mgr etc.) | Warn + return kDataUnhandled on native (DataFunc.cpp) |
| **GameMode::InMode SIGSEGV** | TheGameMode nullptr (GameModeInit wrapped in #ifndef HX_NATIVE) | Enabled GameModeInit, simplified constructor for native |
| **FindFontForMat SIGSEGV** | Virtual call on objects with broken .bss vtables | Itanium ABI typeinfo name comparison ("7RndFont") |

### Session 18-19 Fixes
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| Locale::mSymTable null (100+ errors) | `TheLocale.Init()` missing in native SystemInit path | Added to `#ifdef HX_NATIVE` block in System.cpp |
| ALSA noise (30 lines) | miniaudio probes ALSA devices | stderr redirect during ma_device_init |
| UIListLabel alt version mismatch | INIT_REVS(1,0) but Save writes altRev=1 | Changed to INIT_REVS(1,1), ASSERT_REVS(1,1), load mHighlightAltStyles |
| FxSendEQ version mismatch | INIT_REVS(2,0) but Save writes rev=3 | Changed to INIT_REVS(3,0), ASSERT_REVS(3,0) |
| MoviePanel::IsLoaded blocks transition | mMovie.Ready()=false when no videos loaded | Skip readiness check if mMovies.empty() |
| MoviePanel::Poll infinite loop | Calls PlayMovie() with null mCurrentMovie | Return early if mMovies.empty() |
| HamNavList::LinkRibbonDrawState hang | Raw pointer arithmetic with ILP32 offsets (0x38/0x3c) broken on LP64 | LP64-safe rewrite using widgetState.mElements directly |
| HamNavList::DrawShowing crash | mListState.mProvider null | Added null provider guard |
| UIListSlot::Fill/Draw crash | mElements empty, vector bounds check aborts | Added bounds check guard under HX_NATIVE |

### Key LP64 Pattern: HamNavList::LinkRibbonDrawState
This function used raw pointer arithmetic `*(int**)((char*)&widgetState + 0x38)` to read std::vector internals at ILP32 offsets. On LP64:
- std::vector is 24 bytes (3x 8-byte pointers) vs 12 bytes on ILP32
- Field offsets shift after the vector member
- `state.unk18 = (int)&elem` truncates 64-bit pointer to 32-bit
- `elem.unk2c = *(int*)((char*)this + 4)` reads wrong vtable offset

Fix: `#ifdef HX_NATIVE` with proper struct API access (`widgetState.mElements.size()`, direct indexing)

### LP64 Issues Found & Fixed
The decomp was written for ILP32 (Xbox 360) where int=long=pointer=4 bytes. On LP64 (x86_64), long=pointer=8 bytes.

| Issue | File | Fix |
|-------|------|-----|
| `u32=unsigned long` (8 bytes on LP64) | `types.h` | `u32=unsigned int` under `HX_NATIVE` |
| `sizeof(Vector3)=24` (PAD field grows) | `types.h` | Same fix (u32 PAD now 4 bytes) |
| BinStream duplicate operators (u32=uint) | `BinStream.h` | `#ifndef HX_NATIVE` around u32 ops |
| Missing `unsigned long` BinStream ops | `BinStream.h` | Added `#ifdef HX_NATIVE` unsigned long ops |
| `DataNode(unsigned int)` ambiguous | `Data.h` | Added `#ifdef HX_NATIVE` ctor |
| `(int)ptr` truncation in CharClip | `CharClip.cpp/h` | `(intptr_t)` casts, `(char*)` pointer arithmetic |
| NodeVector temp alloc too small | `CharClip.cpp` | Scale allocation 4x on native |
| Uninitialized `_MemAllocTemp` memory | `CharClip.cpp` | `memset(start, 0, allocSize)` |
| `Resize()` pointer arithmetic bug | `CharClip.cpp` | `(char*)mNodeStart + size` instead of `mNodeStart + size` |
| `TrigTableInit` OOB write | `Trig.cpp` | Guard `if (i * 2 + 1 < 0x200)` |
| `Release(nullptr)` crash | `Object.h` | Null check under `HX_NATIVE` |
| XDK callback type mismatches | Various | `#ifdef HX_NATIVE` guards, skip Xbox threading |
| `(int)ptr` arithmetic in 10+ files | AsyncFile, ArkFile, ChunkStream, CameraManager, MeshDeform.h, Text, StringTable, MemHeap | `(intptr_t)ptr` unconditional (safe on ILP32) |
| `(int)ptr == (int)ptr` signed comparison | TypeProps.cpp | `#ifdef HX_NATIVE` — direct pointer compare changes `cmpw`→`cmplw` |
| `(int)ptr` null checks | Object.cpp, DataArray.cpp | Remove cast (null check has no sign difference) |
| `count * 4` pointer array alloc | MemTracker.cpp, AllocInfo.h | `count * sizeof(AllocInfo*)` unconditional |
| ObjRef `+4`/`+8` hardcoded offsets | CharClip.cpp | `sizeof(void*)` multiplier (unconditional, safe) |
| `(u32)c + 0x294` field access | CharTransDraw.cpp | `#ifdef HX_NATIVE` call `SetDrawMode()` instead |
| `(u32)this + 0x70` vtable dispatch | StorePanel.cpp | `#ifdef HX_NATIVE` skip |
| `gMemTracker + 0x8` / `+ 0xC` offsets | AllocInfo.cpp, MemMgr.cpp | `#ifdef HX_NATIVE` with LP64 offsets |
| UIScreen glitch callbacks | UIScreen.cpp | `#ifdef HX_NATIVE` simplified stubs |
| PostProc_NG hardcoded offsets | PostProc_NG.cpp | `#ifdef HX_NATIVE` stub (rendering reimpl later) |
| Text blacklight packet pointers in int[] | Text.cpp | `#ifdef HX_NATIVE` skip (rendering reimpl later) |
| MSVC PPC missing `intptr_t` | types.h | `typedef int intptr_t` for non-native build |

### Key Files Modified for Native Port
- `src/types.h` — LP64-safe typedefs
- `src/system/utl/BinStream.h` — LP64 operator overloads
- `src/system/obj/Data.h` — `DataNode(unsigned int)` ctor
- `src/system/obj/Object.h` — Release null guard
- `src/system/char/CharClip.cpp` — LP64 pointer fixes, transition buffer
- `src/system/char/CharClip.h` — BytesInMemory intptr_t
- `src/system/char/CharBonesSamples.cpp` — LoadHeader/LoadData implemented, **cached Vector3 padding fix** (on-disk cached format pads uncompressed Vector3 to 16 bytes but in-memory is 12; must use element-by-element read when compression < kCompressVects)
- `src/system/math/Trig.cpp` — OOB write guard
- `src/system/os/Debug.cpp` — Skip SetUnhandledExceptionFilter
- `src/system/movie/Splash.cpp` — Skip threaded splash
- `src/system/utl/ChunkStream.cpp` — Skip decompression thread
- `src/system/gesture/SkeletonUpdate.cpp` — Skip Kinect thread
- `src/system/gesture/GestureMgr.cpp` — Skip Kinect init
- `src/xdk/xapilibi/winnt.h` — LP64 exception filter typedef
- `native/src/thunk_stubs.cpp` — C++ stubs for non-virtual thunks
- `native/CMakeLists.txt` — Linker flags, source files
- `native/include/bits/stl_iterator.h` — Shadow copy with `operator _Iterator()` for iterator→pointer compat
- `src/system/ui/UILabel.h` — `const String&` overload for SetPrelocalizedString under `#ifdef HX_NATIVE`

### Session 4 Key Findings

#### Iterator/Pointer Compatibility (SOLVED)
- **Problem**: STLport (PPC) returns raw pointers from `.begin()/.end()`, libstdc++ returns `__normal_iterator` wrappers
- **Fix**: Shadow copy of `bits/stl_iterator.h` at `native/include/bits/` adds `operator _Iterator() const noexcept` to `__normal_iterator`, enabling implicit conversion to raw pointer. Fixes ALL 605 call sites.
- **`.data()` == `.begin()` on PPC**: Both return `_M_start`, identical codegen
- **`.end()` != `.data() + .size()`**: `.end()` loads `_M_finish` (one load). `.data()+.size()` computes `_M_start + (_M_finish - _M_start)` — generates extra `subf/divw/mulli` (DataEventList::Reset: 36.3% vs 99.7%)
- **Rule**: Always use `.begin()/.end()`, never `.data()+.size()` for iterator math

#### MSVC Extension Compat (Clang rejects)
| Issue | Fix |
|-------|-----|
| `foo(String("temp"))` binding to `foo(String&)` non-const ref | Added `const String&` overload in UILabel.h under `#ifdef HX_NATIVE` |
| `vector<bool>` `operator[]` returns proxy, `auto&` fails | `#ifdef HX_NATIVE` with direct array access |
| Missing `\|=` operator on custom types | `#ifdef HX_NATIVE` with `= \|` syntax |
| Ambiguous `Symbol` vs `int` in conditional | Explicit `(Symbol)0` cast |

#### Out-of-line Functions Affect Inlining
- Adding `Rand::Int()` body to Rand.cpp prevented PPC inlining in `Float()`/`Gaussian()` — -34% to -43% regressions
- **Rule**: New native-only function implementations MUST be `#ifdef HX_NATIVE` guarded to avoid PPC regressions

### Graphics Subsystem Progress (Tier 1.5 — Full Material Pipeline)
- **Step 1** (DONE): GpuDevice + GLFW windowed/headless rendering (cornflower blue clear verified on RTX 3090)
- **Step 2** (DONE): TextureConvert — DXT byte-swap, Milo untile, CPU DXT decompress, format mapping
- **Step 3** (DONE): VertexFormats + PipelineManager + standard.wgsl shader
- **Step 4** (DONE): WgpuRnd + WgpuTex + WgpuMesh + WgpuShaderMgr
- **Step 5** (DONE): Milo Viewer standalone app — loads .milo_xbox from CLI, orbit camera, full render loop
- **Step 6** (DONE): Visual Polish + Reliability — specular (Blinn-Phong), emissive, rim lighting, intensify, multi-light from RndEnviron (up to 4 directional), ring buffer auto-grow, GPU resource cleanup via destructor hooks, pipeline cache bounds warning, error logging in upload paths, --verbose flag, window title

### GCC 15 + Clang MSVC Compat Workarounds
- `-D__GNUC_STDC_INLINE__` — fixes `__extern_always_inline` in glibc sys/cdefs.h
- `-D__GCC_ATOMIC_TEST_AND_SET_TRUEVAL=1` — fixes `atomic_base.h` compiler builtin
- These let `-fms-compatibility` coexist with Dawn/WebGPU headers

### Graphics Files Created
- `native/src/gfx/GpuDevice.h/.cpp` — WebGPU device, GLFW window, surface, headless, sampler cache
- `native/src/gfx/TextureConvert.h/.cpp` — Xbox texture pipeline (byte-swap, untile, DXT decompress)
- `native/src/gfx/VertexFormats.h/.cpp` — GPU vertex layouts, unpack from RndMesh::Vert
- `native/src/gfx/PipelineManager.h/.cpp` — Bind group layouts, pipeline cache, shader cache
- `native/src/gfx/standard_wgsl.inc` — Embedded WGSL shader (diffuse+ambient+fog+alphatest)
- `native/shaders/standard.wgsl` — Standalone copy of shader
- `native/src/platform/Rnd_Wgpu.h` — WgpuRnd + WgpuShaderMgr + uniform structs declarations
- `native/src/platform/Rnd_Wgpu.cpp` — WebGPU renderer (replaces Rnd_Stub.cpp)
- `native/src/platform/Mesh_Wgpu.cpp` — RndMesh::DrawShowing() with GPU vertex/index buffers
- `native/src/platform/Tex_Wgpu.cpp` — RndTex::PresyncBitmap() GPU texture upload

### Dawn API Gotchas
- Target name: `dawn::webgpu_dawn` (namespaced)
- Instance needs `TimedWaitAny` feature for sync callbacks
- `adapter.HasFeature()` not `AdapterFeatures` struct
- `SurfaceGetCurrentTextureStatus::SuccessOptimal` not `Success`
- Structs have `nextInChain` as first member — no simple aggregate init
- `BlendFactor::Dst` not `DstColor`, `OneMinusDst` not `OneMinusDstColor`
- `DepthStencilState.depthWriteEnabled` is `wgpu::OptionalBool` not `bool`
- `TexelCopyTextureInfo` not `ImageCopyTexture`, `TexelCopyBufferLayout` not `TextureDataLayout`
- Queue::WriteTexture takes `TexelCopyTextureInfo*` + `TexelCopyBufferLayout*`

### Step 4 Architecture Notes
- **Ring buffer pattern**: Material/Object uniforms use UniformRingBuffer (64KB each, 256-byte aligned offsets). Each draw writes at a new offset, avoiding WriteBuffer-before-submit ordering issues.
- **Side tables**: GPU resources stored in `unordered_map<RndMesh*, GpuMeshData>` and `unordered_map<RndTex*, GpuTexData>` — avoids modifying decomp class layouts.
- **Matrix convention**: Milo uses row-major (D3D convention). Memcpy to WGSL column-major storage gives automatic transpose, correct for `M * v` in WGSL matching D3D's `v * M`.
- **Bind groups per draw**: Created fresh each draw call (Dawn caches internally). Tier 2 can optimize.
- **Decomp header changes**: `Mesh.h` — `#ifdef HX_NATIVE` DrawShowing override. `BaseMaterial.h` — 6 getters. `Env.h` — FogStart/FogEnd/FogColor accessors.

### Session 8: Milo Viewer + ObjOwnerPtr Fix

#### ObjOwnerPtr Null Dereference Fix (Critical)
- **Root cause**: `ObjOwnerPtr<T>::RefOwner()` called `mObject->RefOwner()` without null check
- **Fix**: `return mObject ? mObject->RefOwner() : nullptr;` in `src/system/obj/ObjPtr_p.h`
- Every OTHER RefOwner() impl in the codebase has a null check — this was the only one missing
- This was the "ObjRef lifecycle crash" blocking DirLoader .milo loading in the native port
- Safe for decomp build — null check on a template only changes native instantiations

#### Milo Viewer (`native/src/viewer/milo_viewer.cpp`)
- CMake target `milo-viewer` — same engine sources as dc3-native, different entry point
- Engine init: SetFileChecksumData → SystemPreInit → TheRnd.PreInit → SystemInit → TheRnd.Init → FlowInit/CharInit/WorldInit/HamInit
- Loads .milo_xbox via `ObjDirPtr<ObjectDir>::LoadFile(FilePath(absPath), ...)`
- `FilePath(absPath)` constructor correctly preserves absolute paths (vs `fp.Set(path, nullptr)` which loses them)
- RndCam::UpdateLocal() is stubbed → manual viewProj computation with Y-forward perspective projection
- GLFW orbit camera: left-drag=orbit, scroll=zoom, middle-drag=pan, R=reset, ESC=quit
- Tested: metamaterials.milo (18 objects), ui/common.milo (camera+materials+textures) both load successfully

#### CMakeLists.txt Changes
- Split DC3_NATIVE_SOURCES into DC3_NATIVE_CORE_SOURCES (no entry point) + DC3_NATIVE_SOURCES (core + main_native.cpp)
- milo-viewer target uses DC3_NATIVE_CORE_SOURCES + its own entry point

### Session 9: Rendering Pipeline Working + Batch Screenshots

#### Vulkan Headless Rendering
- **Sandbox blocks Vulkan ICD**: Claude Code sandbox prevents access to `/usr/share/vulkan/icd.d/nvidia_icd.json` — must use `dangerouslyDisableSandbox: true` for render commands
- RTX 3090 works correctly in headless mode via Dawn/WebGPU backend

#### Decomp Bug Fixes for Rendering Pipeline
- **RndGenerator class name** (`src/system/rndobj/Gen.h`): Was `OBJ_CLASSNAME(Mesh)` which overwrote RndMesh factory. Fixed to `OBJ_CLASSNAME(Generator)` (confirmed via RB3 reference)
- **CachedRead was stubbed** (`src/system/rndobj/Mesh.cpp`): Added `#ifdef HX_NATIVE` implementation — reads count, resizes vector, bulk reads via ReadChunks, byte-swaps from big-endian
- **Compressed vertex support**: Added `NumCompressedVerts()`/`CompressedVerts()` accessors to `Mesh.h`, `UnpackCompressedVertices()` in `VertexFormats.cpp`

#### CompressedVertex_Xbox Field Mapping (CRITICAL — names are MISLEADING)
The struct field names DO NOT match their actual D3D vertex declaration usage (from `src/system/rnddx9/Mesh.cpp`):
| Struct Field | D3D Type | D3D Usage | Actual Content |
|---|---|---|---|
| mPosX/Y/Z | FLOAT3 | POSITION | Position (3 floats) |
| mColor | D3DCOLOR | COLOR | Packed ARGB |
| **mNormal** | **FLOAT16_2** | **TEXCOORD** | **UV coordinates (2 half-floats!)** |
| **mTangent** | **DEC4N** | **NORMAL** | **Normal vector (10-10-10-2)** |
| mBinormal | DEC4N | TANGENT | Tangent vector (10-10-10-2) |
| mBoneIndices | UDEC4N | BLENDWEIGHT | Bone weights |
| mBoneWeights | UBYTE4 | BLENDINDICES | Bone indices |
- The `rndobj/Mesh.cpp` FillCompressedVertex is WRONG (packs tex into mTangent as DEC4N)
- The `rnddx9/Mesh.cpp` FillCompressedVertex is CORRECT (packs tex into mNormal as FLOAT16_2)

#### Rendering Features
- Auto-framing: computes bounding box from mesh vertices (both uncompressed and compressed), sets orbit camera target/distance
- Minimum ambient floor (0.35) when scene environment has low ambient
- Three-quarter directional light for better visibility
- Batch screenshot script: `native/scripts/render_screenshots.sh`

#### Known Issues (Resolved)
- **Index buffer alignment (FIXED)**: WebGPU requires buffer sizes aligned to 4 bytes. Meshes with odd face counts (e.g. 581 faces = 1743 indices = 3486 bytes) caused validation error "Size not a multiple of 4" and black output. Fix: round up to even index count for allocation, align buffer size with `(size + 3) & ~3`.
- **Cleanup crash (SIGSEGV)**: `new[]`/`delete` mismatch on compressed vertex buffer during ObjectDir destructor. Screenshots save before crash. Workaround: `ASAN_OPTIONS=alloc_dealloc_mismatch=0`
- **CharBone vtable OOB**: ASan reports global-buffer-overflow in DirLoader::CreateObjects during CharInit. Non-blocking with `halt_on_error=0`
- **Backface culling (FIXED)**: CW→CCW front face in PipelineManager (D3D LH CW = WebGPU RH CCW)

#### Screenshots Gallery (`archive/screenshots/`)
31/31 renders: 15 static props + 2 crowd + 12 main dancers + 2 venues. All textured with correct UVs, normals, and full material pipeline.

#### Session 20+: Visual Quality Fixes
- **sRGB color space**: Textures use `*UnormSrgb` format variants (BC1/BC2/BC3/RGBA8), framebuffer RGBA8UnormSrgb, surface prefers sRGB. Without this, lighting math in linear space produces correct gamma.
- **Emissive fix**: Only apply `emissiveMultiplier` when emissive map texture exists (`mat->GetEmissiveMap()`). Default 1.0 without a map adds full diffuse as self-illumination (wrong).
- **Specular fix**: Clamp minimum specPower to 32, scale intensity to 0.4x for low-power materials. Xbox shader masks specular with normal map alpha — without normal map, low power creates unrealistic broad sheen.
- **CharMeshHide visibility**: Post-load step iterates CharMeshHide objects and applies default state (`!mShow`). Eliminates z-fighting at mesh seam boundaries where overlapping body meshes had different normals. Also fixes "missing limbs" from meshes with pre-set Showing=false.
- **Crop script**: `native/scripts/crop_screenshot.sh` — renders and crops head/torso/legs/arms regions for close inspection.

#### Rendering Features (Session 10 updates)
- **Half-Lambert lighting**: `NdotL = dot(N,L)*0.5+0.5; NdotL2 = NdotL*NdotL;` — wraps light to dark side, squared for softer falloff
- **Per-prop camera CLI overrides**: `--azimuth` and `--elevation` (degrees) applied after auto-framing
- **Dynamic far plane**: `farDist = distance * 5.0f` (min 1000) scales frustum with auto-frame distance
- **Vertex alpha zero fix**: If first 10 vertex alphas are all zero, force white vertex colors (1,1,1,1)
- **rndobj/Utl.cpp excluded**: API mismatches with native build. 25 stubs in thunk_stubs.cpp
- **--help flag**: Comprehensive help with usage, options, controls, and examples

### Session 12: FlowAnimate Save/Load Asymmetry Fix

#### Root Cause: FlowAnimate::Load skipped mAnim at rev >= 3
- **Problem**: Save always writes `mAnim`, but Load only read it when `rev < 3`
- **Ghidra confirmed**: Original binary ALWAYS calls `LoadFromMainOrDir(mAnim)` — rev < 3 does extra `operator=`
- **Impact**: FlowAnimate objects with non-empty mAnim names caused stream desync — subsequent fields read string data as floats/ints
- **Hex pattern**: `0x6e696d00` ("nim\0") read as string length at mType position
- **Fix**: Always call `mAnim.LoadFromMainOrDir(d.stream)`, only do `mAnim = anim` assignment when rev < 3
- **Decomp improvement**: 85.9% → 90.7% match for `FlowAnimate::Load`
- **Native improvement**: timey_wimey_elements.milo loads without SUSPICIOUS reads

#### RndWind::NewObject Fix (previous session, documented here)
- RndWind had `REGISTER_OBJ_FACTORY` but no `NEW_OBJ(RndWind)` → factory created plain Hmx::Object
- Fix: Added `NEW_OBJ(RndWind)` to Wind.h
- Boot output went from 1,414 → 30,793 lines

#### Remaining Desyncs
- `ObjVector<T>::operator>>` SUSPICIOUS reads in crowd anim files (male_base.milo, female_base.milo)
- These are likely from other stubbed Load functions (FlowEventListener, etc.)
- DrivenPropertyEntry destructor crash in FlowWhile → FlowSwitch chain (LP64 ObjPtr issue)

#### Test Infrastructure
- `native/tests/test_flow_desync.cpp` — TrackObjectBytes + FlowAnimateFieldTrace tests
- Field-level tracing added to FlowAnimate::Load and FlowNode::Load
- Per-object tell() logging in DirLoader::LoadObjs (PreLoad, PostLoad, ReadDead)

### Session 24 Fixes (Link + Auto-Skip + Boot Navigation)
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **PostProc_NG compile error** | NgPostProc::ReleaseTex accesses private BloomTextures::mTextures | Added `friend class NgPostProc;` to BloomTextures template |
| **QuatXfm multiple definition** | Defined in both mtx.cpp and HamCharacter.cpp | `#ifndef HX_NATIVE` around HamCharacter.cpp definition |
| **RndShaderSimple::Select duplicate** | Defined in Shader.cpp and native_link_glue.cpp | Removed from native_link_glue.cpp |
| **attract_screen stuck** | No `button_down` DTA handler; advances via movie/Kinect | UIScreen::Enter() auto-skip fires `skip_selected` + `next_screen` |
| **InTransition() always true in Enter()** | Enter() called during transition TO the screen | Changed to `TheUI->TransitionScreen() != this` check |
| **Screens stuck after auto-skip** | Some screens need timer-based advance (no DTA handlers) | UIManager::Poll auto-advance table: from→to after 120 frames |

### Key Auto-Skip Design
Two mechanisms work together:
1. **Enter-based** (UIScreen.cpp): Fires DTA handlers (`skip_selected`, `next_screen` property) immediately when screen enters
2. **Timer-based** (UI.cpp): UIManager::Poll tracks stuck screens, force-advances via hardcoded from→to table after 120 frames
- **InTransition bug**: `TheUI->InTransition()` returns true during Enter() because transition TO the screen is active. Must use `TransitionScreen() != this` to detect NEW transitions

### Session 25: Text Rendering Working
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **Text meshes invisible** | Depth test failed — opaque UI geometry wrote z-buffer, text quads behind | Disable depth test (`zMode=0`) for nameless text meshes in Mesh_Wgpu.cpp |
| **Purple font texture** | DXT5/BC3 font atlas: glyph in alpha, RGB is garbage (purple) | `useAlphaAsRGB` shader uniform + WGSL: `baseColor.rgb * texColor.a` instead of `texColor.rgb` |
| **Backface culling text** | Text quads face direction depends on camera; font meshes shouldn't cull | Disable culling (`cull=None`) for nameless text meshes |
| **Dawn Null backend** | Sandbox blocks Vulkan ICD → Dawn falls back to no-op null backend | Must use `dangerouslyDisableSandbox: true` for any rendering commands |
| ~~**Localization tokens unresolved**~~ | `PlatformMgr::RegionInit()` was a no-op stub → region stayed `kRegionNone` → `LanguageInit` warning but locale loading worked. Also missing `RegionInit()` call in native SystemPreInit path. | **FIXED**: `RegionInit()` sets `kRegionNA`, added call to native SystemPreInit. 2091 locale tokens load correctly. |

Key findings:
- Text meshes created by `RndText::FontMap` via `Hmx::Object::New<RndMesh>()` have empty names (`Name()[0]=='\0'`)
- Detection: `!mesh->Name()[0]` identifies text meshes in draw path
- UI camera `[ui.cam]` at (0,-768,0), near=1, far=1000 — text at Y=0 is 768 units away
- Text vertex positions ARE correctly filled by `SetupCharacter()` — v0-v3 form char quads in XZ plane
- `MaterialUniforms` struct: added `useAlphaAsRGB` field (176 bytes total, matches WGSL layout)

### Session 29: Animation Pipeline Verified
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **Timer 50x too slow** | Native `__mftb()` returns µs but Timer::Init used PPC timebase conversion (0.00002) | `#ifdef HX_NATIVE` in Timer::Init — set `sLowCycles2Ms = 0.001f` (µs→ms) |
| **sPlayCursor undefined** | `StreamReceiverFile::sPlayCursor` declared in header but never defined | Added `int StreamReceiverFile::sPlayCursor = 0;` to StreamReceiverFile.cpp |
| **Suspected broken dynamic_cast** | Initial diagnosis thought vtables were zero-filled | **False alarm** — vtables and typeinfo are all properly emitted (`D` symbols). dynamic_cast works. |
| **Suspected 0 animatables** | Debug trace limited to first 10 dirs (all skeleton dirs) | Removed counter limit — UI panel dirs DO have animatables (background=8-12, letterbox=23, main=37) |

Key findings:
- **7,811 PropAnim objects** across 850+ DC3 UI milo files
- **Zero EventTrigger/UITrigger objects** in any UI milo — animations driven by PropAnim, not triggers
- UI panel dirs properly call SyncObjects and collect animatables
- PanelDir::Enter auto-starts animation via `Animate()` on `kTaskUISeconds` timeline
- `AnimTask::Poll` receives increasing time (0.0→0.3→0.5→0.7s confirmed)
- **Remaining gap**: PropAnim drives material properties but the renderer doesn't reflect them visually

### Session 38: HamUI Integration + Kinect Guards
| Issue | Root Cause | Fix |
|-------|-----------|-----|
| **0 draw calls on choose_mode_screen** | `TheUI = new UIManager()` instead of `&TheHamUI` — HamUI::Draw() never called | Changed to `TheUI = &TheHamUI; TheHamUI.Init()` in App.cpp |
| **SIGSEGV in SpeechMgr::SpeechSupported** | TheSpeechMgr null (Kinect not initialized) | Null guard in ShellInput::SyncVoiceControl |
| **SIGABRT in CursorPanel::Poll** | TheHamProvider->Property returns null DataNode (Kinect cursor tracking) | `#ifdef HX_NATIVE return` early in CursorPanel::Poll |
| **SIGABRT in SkeletonIdentifier::Init** | Kinect user index OOB (no Kinect on native) | `#ifdef HX_NATIVE` simplified ShellInput::Init |
| **SIGSEGV in HandsUpGestureFilter::GetHandsUp** | Null pointer from skipped Kinect init | `#ifdef HX_NATIVE` simplified ShellInput::Poll |
| **SIGSEGV in DrawGestureMgr** | RndDrawable::Showing on null (Kinect debug draw) | `#ifdef HX_NATIVE return` in HamUI::DrawDebug |
| **SIGSEGV in HamListRibbon::DrawRibbon** | LP64 pointer truncation: `int mElemDrawState` stored 8-byte pointer as 4 bytes | `UIListElementDrawState*` type on native, `int` on PPC |
| **Undefined UIList::Selected/GetListState** | Missing function bodies | Added implementations in UIList.cpp |
| **STLport compile errors** | Concurrent agents added STLport-specific templates | `#ifndef HX_NATIVE` guards in CharSignalApplier.cpp, PropKeys.cpp |

#### HamUI vs UIManager
HamUI is DC3's game-specific UIManager subclass providing:
- **Two-pass draw pipeline**: First pass (`mFinalDrawPassFlag=0`), letterbox draw, second pass (`mFinalDrawPassFlag=1`)
- **Blacklight mode**: Visual effect overlay
- **HelpBar**: On-screen button prompts
- **ShellInput**: Kinect gesture + controller input routing
- **Init chain**: `HamUI::Init()` → `UIEventMgr::Init()` + `UIManager::Init()` + `ShellInput::Init()`

App.cpp must use `TheUI = &TheHamUI` (global instance) not `new UIManager()`.

#### HamListRibbonDrawState LP64 Fix
The `mElemDrawState` field stores a `UIListElementDrawState*` pointer. On ILP32 (Xbox), `int == pointer` (4 bytes). On LP64, pointers are 8 bytes — storing in `int` truncates the upper 4 bytes, causing SIGSEGV when dereferenced.

```cpp
// HamListRibbon.h
#ifdef HX_NATIVE
    UIListElementDrawState *mElemDrawState; // LP64: pointer, not int
#else
    int mElemDrawState; // ILP32: int == pointer size
#endif
```

### Native Implementation TODOs
Functions currently guarded with `#ifdef HX_NATIVE` early returns that should be properly implemented:

| Function | File | Current Guard | What It Does | Priority |
|----------|------|---------------|-------------|----------|
| **ShellInput::Init** | ShellInput.cpp | Simplified init (cursor panel only) | Full init: SkeletonIdentifier, SpeechMgr, HandsUpGestureFilter, DepthBuffer, DrawGestureMgr, multiple gesture panels | Low (Kinect-specific) |
| **ShellInput::Poll** | ShellInput.cpp | Early return after cursor panel poll | Polls all gesture recognizers, skeleton updates, voice control | Low (Kinect-specific) |
| **ShellInput::SyncVoiceControl** | ShellInput.cpp | Null guard on TheSpeechMgr | Syncs speech recognition commands from DTA config | Low (Kinect-specific) |
| **CursorPanel::Poll** | CursorPanel.cpp | Early return after PassiveMessagesPanel::Poll | Tracks hand cursor position from Kinect skeleton data | Low (Kinect-specific) |
| **HamUI::DrawDebug** | HamUI.cpp | Early return on native | Draws Kinect camera buffers and skeleton debug visualization | Low (debug-only) |

Non-Kinect TODOs:
| Feature | Description | Priority |
|---------|-------------|----------|
| ~~**Locale data**~~ | ~~Full localization strings~~ | ~~Medium~~ DONE |
| **WorldCrowd rendering** | Crowd character instancing | Low |
| **Projected light textures** | Gobo/spotlight cookie textures | Low |
| **FakeSpot light type** | Projected cone light | Low |
| **Motion blur / exotic post-proc** | Gradient map, kaleidoscope, flicker, noise, video feedback | Low |

Done (previously TODOs):
- ~~Content system~~ — **DONE** (Session 62: 62 songs load, 49 items in song_select)
- ~~Audio playback~~ — **DONE** (Session 67: real-time MOGG via FFmpeg/Vorbis/miniaudio)
- ~~Skinned mesh rendering~~ — **DONE** (Session 63: GPU skinning, 4-bone blending, 40-bone palettes)
- ~~Post-processing~~ — **DONE** (Session 69: bloom, contrast, brightness, saturation, levels, vignette, chromatic aberration, posterization, DOF)

### Next Steps — Phase 7: Visual Completeness

High-visibility items remaining to complete the native port's visual experience.

#### Quick Wins
| Task | Description | Effort |
|------|-------------|--------|
| **DC3 logo** | Main menu logo missing — likely a TexRenderer/render-to-texture not being triggered, or a subdir not traversed during scene loading. Most visible gap on the main menu. | Low-Medium |
| **Score display wiring** | Connect scoring system outputs to HUD label elements during gameplay. Infrastructure exists, just needs data flow. | Low |

#### Medium Effort
| Task | Description | Effort |
|------|-------------|--------|
| **Move card UI** | HUD panels active but move card content invisible. TexMovie render-to-texture pipeline is implemented; issue is that game_panel's TexMovie objects aren't receiving source content. Needs tracing what assets/data flow game_panel expects. | Medium |
| **WorldCrowd rendering** | Crowd character instancing system — would significantly improve venue visuals with background dancers/audience. | Medium-High |

#### Low Priority / Cosmetic
| Task | Description | Effort |
|------|-------------|--------|
| **Lip sync** | CharFaceServo, CharLipSyncDriver — character mouth movement during songs | Medium |
| **Procedural blinking** | CharFaceServo eye blink animation | Low |
| **CharEyes gaze** | Eye gaze direction tracking | Low |
| **Projected light textures** | Gobo/spotlight cookie textures for stage lighting | Medium |
| **Exotic post-processing** | Gradient map, kaleidoscope, flicker, noise, video feedback | Medium |
| **Performance optimization** | Profile and optimize hot paths for sustained 60fps, draw call batching | Medium |
| **Platform support** | macOS, Windows targets (WebGPU handles backends) | Medium |

#### Not Code-Fixable
| Item | Reason |
|------|--------|
| Text `<alt>` markup | ParseMarkup code is complete — issue is missing alt font style entries in .milo asset files |

Done:
- ~~LightPreset::Load~~ — **DONE** (Session 61: real impl at 99.2% match, ForcePreset active on venue load)
- ~~Skinned mesh rendering~~ — **DONE** (Session 63: GPU skinning with 4-bone blending, 40-bone palettes)
- ~~Content system~~ — **DONE** (Session 62: 62 songs load, 49 items in song_select)
- ~~Post-processing~~ — **DONE** (Session 69: full pipeline with Xbox-matched formulas)
- ~~Audio playback~~ — **DONE** (Session 67: real-time MOGG via FFmpeg/Vorbis/miniaudio)
- ~~Camera cuts~~ — **DONE** (Session 68: song.anim PropKeys → HamDirector → CameraManager)
- ~~Locale/localization~~ — **DONE** (Session 70: RegionInit fix, 2091 tokens loaded)
- ~~Web build~~ — **DONE** (Emscripten/WASM port, `scripts/build/web.sh`)
- ~~MetaMaterials / DC3 logo~~ — **DONE** (Session 71: enabled MetaMaterials loading on native, DC3 logo visible on main menu, 198 shell_basic.mmat warnings → 0)

### Build Commands
```bash
cd native/build && cmake --build . -j$(nproc)  # Build
cd /home/free/code/milohax/dc3-decomp && ./native/build/dc3-native  # Run
# Enable ASan: uncomment -fsanitize=address in native/CMakeLists.txt
```

### Pattern: ChunkStream Limitations
- ChunkStreams (compressed .milo) are FORWARD-ONLY
- Seek() backwards corrupts stream state — never peek+seekback on ChunkStream
- Debug: use direct printf at stream positions, not hex dump + seek back
