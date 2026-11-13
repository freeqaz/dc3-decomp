# Manifest — 2026-11-13 Native-Port Documentation Audit

58 markdown files archived on 2026-11-13, all with `git mv` (every rename is
R100 — byte-for-byte). The layout under this directory **mirrors `docs/`**
(`native/`, `plans/`, `sessions/`), so links *between* archived files that sat
in the same tree keep resolving; links from archived files to live docs that
moved or were renamed are dead and are listed at the end.

The failure mode was the same one the
[2026-08-17 audit](../2026-08-17-doc-audit/MANIFEST.md) found on the decomp side:
**status snapshots, session-by-session logs and plans written without an expiry**,
left in the live tree and still pointed at as "the current plan" months after
the work they describe had landed or been abandoned. `docs/INDEX.md` sent readers
to a 2026-03-25 "Native port master plan" and a 2026-03-16 "Native port status";
memory notes named `docs/native/PLAN.md` + `TODO.md` as canonical; `PLAN.md` and
`CONVERGENCE_PLAN.md` named `docs/sessions/convergence/` as the live working
docs. None of the five was current.

**These files are preserved byte-for-byte. Every correction is in this manifest,
not in the file.** Durable content was lifted, re-verified against the tree, into
the live pages listed under *Superseded by* before the move.

---

## Ground truth as of 2026-11-13

Measured in worktree `np-docs` at `2d678db7e` (main). Each number carries the
command that produced it.

| Fact | Value | Command |
|---|---|---|
| Shared engine pin | `cb124a75ae5bb55f8bf32fc5661b2d4042a672a1` | `grep -n 'set(MILO_ENGINE_PIN' native/CMakeLists.txt` |
| Engine HEAD | `cb124a7` (2026-10-07) | `git -C ../milo-native-engine log -1 --format='%h %ad' --date=short` |
| Native skip budget | 70 | `cat native/tests/skip_budget.txt` |
| Native test sources | 75 `.cpp` files | `ls native/tests/*.cpp \| wc -l` |
| `HX_NATIVE` mentions in `src/` | 1,038 lines in 317 files | `git grep -nE HX_NATIVE -- src \| wc -l`; `git grep -lE HX_NATIVE -- src \| wc -l` |
| `HX_NATIVE` preprocessor conditionals in `src/` | 979 lines in 311 files | `git grep -nE '^\s*#\s*(if\|ifdef\|ifndef\|elif).*HX_NATIVE' -- src \| wc -l` (and `-l`) |

**Every `HX_NATIVE` count in this archive measured a different unit** — files
(298), blocks (745, 865), "guards" (408) — and none can be differenced against
the table above. A new count must say what it counts and print its command.

Durable structural facts the archived docs get wrong, for reference against the
rows below:

- **Every native target that builds links the engine's renderer, gfx and
  platform copies** (`../milo-native-engine/src/{gfx,platform}`): dc3-native,
  milo-viewer, render-test, milo-tests, the three export tools, and — since
  `2a51acd7e` (2026-10-08) — dc3-web. dc3's own `native/src/gfx/*` and
  `native/src/platform/{Rnd,Mesh,Tex,Part}_Wgpu.cpp`, `BoneSetup.cpp`,
  `MaterialSetup.cpp`, `File_Native.cpp` … are removed from the engine-linking
  lists (`list(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE …)` in
  `native/CMakeLists.txt`) and compile into no target that builds
  (`DC3_NATIVE_SOURCES` and `DC3_GFX_SOURCES` have no consumer;
  `DC3_GFX_CORE_SOURCES` feeds only `wgpu-window-test`, which does not compile).
  Any archived doc citing those dc3 paths is describing dead code.
- `MILO_FATAL_FAILS` defaults to **0** (non-fatal) — `src/system/os/Debug.cpp`.
- GPU rendering is on by default; `MILO_NORENDER=1` disables it.
- `MILO_FIRST_SCREEN`, `MILO_FORCE_DRAW_PANEL` and `DC3_SCREEN` do not exist.
- Link policy is `-Wl,--no-undefined` since `eb62d26a1` (2026-08-31), not
  `--unresolved-symbols=ignore-all`.
- Native save/persistence landed (`717e56182`, `c8570515d`, `eb9ab6140`,
  2026-11-05/06); native scoring is default-on; faithful foot IK is the default
  since `3fb97a370` (2026-07-02).

---

## `native/` (from `docs/native/`)

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `PLAN.md` | 2026-03-24 | "Priority 1..5" roadmap; every item is done or was re-measured dead (`VISUAL_BUG_PROBE_PLAN.md` §6.2/§6.9). Its test command passes `MILO_FATAL_FAILS=0`, already the default. | Coordinator's new `docs/native/PLAN.md` (not yet written) |
| `TODO.md` | 2026-09-30 (content 2026-05-25; the 09-30 touch was a sed) | Lists feet-in-floor IK as open (shipped `3fb97a370`), `RockCentral::ManageJob` open (`1e0ac77ca`/`f30105195`), "TheMoveMgr null" (`App.cpp` calls `MoveMgr::Init`); documents `MILO_FIRST_SCREEN`/`DC3_SCREEN` (absent) and `MILO_RENDER=1` as required; "4,880 AT_LIMIT" predates the 2026-08 ruler; §8.4 and §8.5 contradict each other. §8.5/§8.6 (web metamaterials root cause, smoke-test guard) are still true history — `scripts/web/smoke-test.mjs` now points here. | New PLAN (coordinator); env vars → [`../../debugging/native.md`](../../debugging/native.md) |
| `NATIVE_PORT_STATUS.md` | 2026-05-27 | Sessions 1–77 log under a "Current Status" header; engine pin `9a58e86` (actual `cb124a75`); "371/371" tests; ASan via "uncomment -fsanitize" (now `ENABLE_ASAN`); always-on 120-frame auto-advance (now `DC3_FAST_BOOT` only); `native/src/gfx/*` "files created" are now engine-owned. Porting traps lifted. | New STATUS (coordinator); [`../../native/reference/PORTING.md`](../../native/reference/PORTING.md) |
| `CONVERGENCE_PLAN.md` | 2026-05-26 | Its own banner says superseded 2026-05-25. `gNativeVenueDir` has 0 hits; `NativeVenueInit` survives only in a historical comment in `native/src/dc3_render_hook.cpp`. "865 blocks / 295 files". | [`../../native/reference/LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md) |
| `FILEMERGER_CONVERGENCE.md` | 2026-03-17 | All phases DONE 2026-03-17; Step 1's forced `async = true` is gone from `FileMerger.cpp`; "remains: gNativeVenueDir" is false. Loader topology lifted. | [`LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md) |
| `DTA_LOADING_BLOCKER.md` | 2026-03-25 | "Native sets mSink = screen" — removed in `0800c3ed2`; DTA drives the flow now. Boot DTA order lifted. | [`LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md) |
| `LOADING_ARCHITECTURE.md` | 2026-03-17 | Describes the App.cpp DirLoader bypass that `FILEMERGER_CONVERGENCE.md` removed. | [`LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md) |
| `DECOMP_GAPS.md` | 2026-10-07 (header says 2026-06-22; dates inside are mixed) | "Native score honestly 0 / `DC3_REAL_MOVE_PASSED` off / native never instantiates SkeletonUpdate" — default true in `Game.cpp`, SkeletonUpdate created natively since `35b30215e`; "`ShouldActivateNativeFlow` activates startMode=0 flows" — inert by default (`PanelDir.cpp`, `MILO_NATIVE_FLOW_FILTER`); "~2530 weak stubs"; `MILO_DEBUG_UI_CAM_HACK`, `gSuppressRefErase` (0 hits); PPC % tables that belong in `decomp.db`. AnimTask keepalive, key-function vtable, factory-registration and O(n²) HasDirPtrs lessons, BulkLoad and input-script recipes, audio chain lifted. | [`PORTING.md`](../../native/reference/PORTING.md), [`ANIMATION.md`](../../native/reference/ANIMATION.md), [`AUDIO_VIDEO.md`](../../native/reference/AUDIO_VIDEO.md), [`../../native/TESTING.md`](../../native/TESTING.md), [`../../debugging/native.md`](../../debugging/native.md) |
| `CROWD_ANIMATION.md` | 2026-03-16 | 3 of 4 "fixes" are gone: `Rand.h` `FastInt` uses the bounds-safe masked form (refuting "PPC FastInt is UB"); `CharClipGroup.cpp` has 0 `HX_NATIVE`; the `LoadCrowdClips("medium"…)` autoload and the 100k FileMerger timeout are gone. Pipeline/file layout lifted. | [`ANIMATION.md`](../../native/reference/ANIMATION.md) |
| `GRAPHICS_REFACTOR.md` | 2026-03-12 | Unexecuted roadmap anchored on dead dc3 copies (`native/src/platform/MaterialSetup.cpp`, `native/src/gfx/PipelineManager.cpp`). The engine instead ports retail passes (`e5c6880` NgDOFProc, `fd6441c` spot beams, `ShippedWgsl.h`) — the opposite of this doc's "not a faithful recreation" principle. | [`RENDERING.md`](../../native/reference/RENDERING.md); renderer direction lives in `../milo-native-engine` |
| `STUB_BURNDOWN.md` | 2026-06-22 | Its own banner: "historical … the native-relevant stub lever is EMPTY". "199 weak linker stubs / 1058 lines" vs `native/src/engine_stubs_generated.cpp` today. | [`../../native/MISSING_DEFINITIONS.md`](../../native/MISSING_DEFINITIONS.md) |
| `UI_ANIMATION_STATUS.md` | 2026-09-30 (content 2026-03-12; the 09-30 touch was a sed) | Hack inventory stale (`CursorPanel.cpp` now 0 `HX_NATIVE`); "performer corrupted" headline refuted later (`VISUAL_BUG_PROBE_PLAN.md` §6.3); missing-manager backlog superseded by the save landing. | — (history) |
| `VISUAL_BUG_PROBE_PLAN.md` | 2026-08-02 | Mixed RB3/DC3 bug list: #2 HUD workaround dropped `559d0deed`; #34 flare occlusion `ec8f4bf80`/`a449ca1b9`; #36 `DrawMode()==8` fixed in the engine; §0.4 "DC3 renderer is a fork" superseded. §6 is itself an earlier stale-doc audit, kept as evidence. Probing method lifted. | [`LIVE_STATE_PROBING.md`](../../native/reference/LIVE_STATE_PROBING.md) |
| `HACK_AUDIT.md` | 2026-08-19 | "~745 blocks" (2026-03-16); `gSuppressRefErase → SuppressEraseScope` (both 0 hits); `MILO_STRICT_GETOBJ` was never built. Guiding principles lifted; the 2026-08-19 inverse-audit (guard-leak scanner) section moved to the patterns tree. | [`PORTING.md`](../../native/reference/PORTING.md) § Guard policy, [`native-guard-leak-scan.md`](../../decomp/patterns/native-guard-leak-scan.md) |
| `CONSOLE_HW_FINDINGS.md` | 2026-08-03 | Self-described "scratch checkpoint"; §7 "xbdm sub-transport UNIMPLEMENTED" closed by its own §11; §13 is an incident log. Durable hardware sections merged; console IP/identity, RB3E build hashes and noise-floor numbers were not re-measured and stay only here. | [`../../tools/console/CONSOLE_DTA_EVAL.md`](../../tools/console/CONSOLE_DTA_EVAL.md) §10 |
| `ANIMATION_PIPELINE.md` | 2026-03-15 | Cites `native/src/platform/BoneSetup.cpp`, removed from every engine-linking target (the engine copy links); "Current Status (Session 74)". Data flow and checklist lifted. | [`ANIMATION.md`](../../native/reference/ANIMATION.md) |
| `UI_RENDER_DECOMP_GUIDE.md` | 2026-03-10 | Status table stale (UIList, HamNavList, UILabel::DrawShowing, RndText::UpdateText all 100% in report.json today); native-submission links point at dead dc3 copies. Render path and triage rule lifted. | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `RENDERING_SYSTEM.md` | 2026-03-16 | "Native Port Status" section stale (crowd impostor "stacks at origin"; "occlusion queries missing" — `ec8f4bf80` landed). Decomp-derived §§1–5 lifted. Verdict was UPDATE; merged instead so one rendering reference exists. | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `RENDER_TO_TEXTURE.md` | 2026-03-16 | Implementation paths are dead dc3 copies; "WorldCrowd Impostor Known Issue" superseded. API/format tables lifted. | [`RENDERING.md`](../../native/reference/RENDERING.md) |

---

## `plans/dc3-native/` (whole directory)

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `STATUS.md` | 2026-03-25 | `MILO_FATAL_FAILS` default 1 (code: 0); `MILO_FAIL_DTA` "non-fatal warn" (throws to `MILO_CATCH` since `390804c25`); `MILO_FIRST_SCREEN`/`MILO_FORCE_DRAW_PANEL` (absent); motion capture "NOT STARTED", scoring "needs MoveGraph", save "TODO" — all landed. Was linked from `docs/INDEX.md` and the root `README.md` as *the* native status. | New STATUS (coordinator); env vars → [`../../debugging/native.md`](../../debugging/native.md) |
| `PLATFORM_HACKS_ANALYSIS.md` | 2026-03-25 | "298 files"; every Category-2 "missing init" is now on the native path of `src/App.cpp`; Anim.cpp auto-null hack gone; "mUseMoveGraph never set" false; "DC3 doesn't use WorldCrowd" false. Only §1.3 (audio Suspend/Resume) is still live. Screen-flow reference lifted. | [`LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md), [`PORTING.md`](../../native/reference/PORTING.md) |
| `object-lifetime-guard-root-cause.md` | 2026-03-12 | All 5 fixes landed; `gSuppressRefErase`, `gSuppressDirPtrDelete`, `HmxObjectIsLive` have 0 hits (removed `b1945bd18`); ObjPtrVec erase reworked again `bf6ccd460`. | — (history; tests live in `native/tests/test_object_lifetime.cpp`) |
| `PHASE_A_MOVE_CARDS.md`, `PHASE_B_SCORE_HUD.md` | 2026-03-16 | "Blocked on Kinect / gesture detection" — `native/src/pose/` and the live-pose provider exist, scoring is default-on. | New PLAN / POSE-INPUT (coordinator) |
| `PHASE_C_WORLDCROWD.md` | 2026-03-16 | Direct 3D crowd plan + a SetFullness guard; the code took the billboard-impostor path and the guard is gone. | [`ANIMATION.md`](../../native/reference/ANIMATION.md) |
| `RENDERING_GAPS.md` | 2026-03-16 | Session-30 snapshot; stub counts predate the 2026-03-17 cleanup. | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `TEST_GAP_ANALYSIS.md` | 2026-03-16 | "25 test files" (75 today); proposed tests assume removed hacks; CI now exists. Still-open gaps carried. Same class as `TEST_VALUE_AUDIT.md` (archived 2026-08-17). | [`../../native/TESTING.md`](../../native/TESTING.md) |
| `VIEWER_STATUS.md` | 2026-03-06 | Single-file `milo_viewer.cpp` (now split across `native/src/viewer/`); "particles/lines/flares not implemented", "bloom future"; key-file pointers are dead dc3 copies. How-to-run lifted. | [`VIEWER.md`](../../native/reference/VIEWER.md) |

## `plans/web-port/` (whole directory)

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `PLAN.md` | 2026-03-25 | "246 files / 5.7 MB" bundle (boot is now one async bundle plus per-screen bundles, `9f6f777a2`/`8af3c71f7`); `native/web/build.sh` (now a delegator to `scripts/web/build.sh`, `6298c017c`); `scripts/web/test.mjs` (absent); WebAssets/GpuDevice_Web now in the engine; dc3-web links `libmilo-engine.a` since `2a51acd7e`. | New WEB page (coordinator); `docs/native/WEB_TESTING.md` |
| `AUDIO.md` | 2026-03-14 | "Web audio completely stubbed" — engine `src/audio/AudioDevice_Web.cpp` exists; SFX sidecars landed `9f6f777a2`. MOGG/channel material lifted. | [`AUDIO_VIDEO.md`](../../native/reference/AUDIO_VIDEO.md) |
| `CROWD.md` | 2026-03-16 | Not web-specific; most venues "untested". Two-crowd-patterns section lifted. | [`ANIMATION.md`](../../native/reference/ANIMATION.md) |
| `HUD_RENDERING.md` | 2026-03-16 | `phrase_meter0` SyncObjects fix and `skipUIDraw` have 0 hits; "score stuck at 0" obsolete. | — (history) |
| `UNIFY_WITH_APP.md` | 2026-03-17 | Finished (completion log 2026-03-17): `App::RunOneFrame` is shared with `main_web.cpp`. "main_web.cpp is 131 lines" has regrown. | [`LOADING_AND_BOOT.md`](../../native/reference/LOADING_AND_BOOT.md) |

## `plans/custom-graphics-engine/` (whole directory)

The 2026-02/03 design set for the native renderer/audio/input. WebGPU/Dawn was
chosen and built; the code then moved into the shared engine. Common stale
facts: Dawn "at `../dawn`" (it is a prebuilt dependency in `dc3-decomp-deps`,
found via `-DDawn_DIR` by `scripts/native_configure.sh`); gfx code "in
`native/src/gfx/`" (the engine's copy links); uniform sizes (the engine's
`UniformStructs.h` static-asserts are authoritative).

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `PLAN.md` | 2026-03-25 | Input "SDL2" (GLFW); Phase 5 mocap as MediaPipe-in-C++ via Bazel + `LiveCameraInput_Webcam.cpp` (never written — the live pipeline is `native/scripts/pose_server.py` → unix socket → `NativeSkeletonProvider`); web and save "not started" (both landed); "HX_NATIVE concentrated in a handful of files". Was linked from `docs/INDEX.md` as the "Native port master plan". | New PLAN (coordinator) |
| `INITIAL_IDEAS.md` | 2026-02-28 | Brainstorm; every question since decided. | — |
| `APPROACH_WEBGPU.md`, `APPROACH_BGFX_SOKOL.md`, `APPROACH_OPENGL.md`, `APPROACH_VULKAN.md`, `APPROACH_GODOT.md`, `APPROACH_BEVY.md`, `APPROACH_FORGE_DILIGENT_LLGL.md`, `APPROACH_YARG_COMMUNITY.md` | 2026-02-28 | Engine-selection analyses (WebGPU won). Kept for the rejected options. `APPROACH_WEBGPU`'s `../dawn` setup is wrong. | — |
| `IMPLEMENTATION_GUIDE.md` | 2026-03-02 | Build steps (`../dawn/install/Release`, no `Dawn_DIR`); "shadow mapping not yet wired up" (engine `ShadowPass` exists). Dawn API gotchas lifted. | [`PORTING.md`](../../native/reference/PORTING.md) |
| `PORTING_ANALYSIS.md` | 2026-03-01 | "24 thunk stubs" (`native/src/thunk_stubs.cpp` now says all moved out); audio "Stubbed"; critical path ends at "title screen". LP64 table, iterator shim, key-function lesson lifted. | [`PORTING.md`](../../native/reference/PORTING.md) |
| `GRAPHICS_SYSTEM_DESIGN.md` | 2026-03-16 | "Draft v5 / Tier 1.5 current"; `SceneUniforms`/`MaterialUniforms` sizes; Tier 2/3 files that do not exist; `Rnd_Wgpu` in dc3 (now engine, behind `GameRenderHook`). Texture/format/blend/archive reference lifted. | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `MATERIAL_RENDERING.md` | 2026-03-02 | All 15 phases DONE; uniform sizes stale; paths are dc3 copies; "bloom/DOF not planned" (both exist). Shader-variation semantics lifted. | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `SHADOW_MAPPING.md` | 2026-03-02 | Plan carried out (engine `gfx/ShadowPass`). | [`RENDERING.md`](../../native/reference/RENDERING.md) |
| `STREAM_DESYNC.md` | 2026-03-01 | The peek-and-unreread hack was removed from `DirLoader.cpp` in `6f688f1fc`; the `BinStream` `siz > 10000` cap is gone. Six-step diagnostic lifted. | [`PORTING.md`](../../native/reference/PORTING.md) |
| `BONE_GROUND_TRUTH_AND_CLIP_VALIDATION_PLAN.md` | 2026-03-04 | Gate log frozen at "Phase 6 hand parity IN PROGRESS"; `SolveAllTwists` moved to `CharTwistSolver`; percentages pre-`name_check`. Conventions and `--gtest_filter` lines lifted. | [`ANIMATION.md`](../../native/reference/ANIMATION.md), [`../../native/TESTING.md`](../../native/TESTING.md) |
| `LIP_SYNC.md` | 2026-03-05 | "Viewer has zero lip sync" (visemes and blink exist, `--visemes`); decomp-status table stale (`f12e7afbc`). Architecture lifted. | [`ANIMATION.md`](../../native/reference/ANIMATION.md) |
| `AUDIO_SYSTEM.md` | 2026-03-13 | `AudioDevice`/miniaudio now in the engine; "FX bypassed for MVP" (`FxSendNative`); `StreamReceiver` now runs the image body (`7e97a6942`). Architecture lifted. | [`AUDIO_VIDEO.md`](../../native/reference/AUDIO_VIDEO.md) |
| `VIDEO_PLAYBACK.md` | 2026-03-01 | Accurate for desktop; omits that FFmpeg is off under Emscripten (web uses `.webm` sidecars). Lifted. | [`AUDIO_VIDEO.md`](../../native/reference/AUDIO_VIDEO.md) |
| `MOTION_CAPTURE.md` | 2026-02-28 | Pre-implementation; the implementation went another way (see `PLAN.md` row). Joint table wrong (HandRight "21-24"). Gesture-subsystem map and open questions are inputs for the coordinator's POSE-INPUT page. | New POSE-INPUT (coordinator) |
| `POSE_REGRESSION_GUIDE.md` | 2026-03-04 | Accurate, but it is **character-animation screenshot goldens, not webcam pose**. Merged under a heading that says so. | [`../../native/TESTING.md`](../../native/TESTING.md) |
| `PHILOSOPHY.md` | 2026-03-05 | Timeless rules; paths stale. | [`CODE_STYLE.md`](../../native/reference/CODE_STYLE.md) |

## `sessions/`

| File | Last touched | The claim that went stale | Superseded by |
|---|---|---|---|
| `NEXT_NATIVE_PORT.md` | 2026-03-16 | Its 3 "active hacks" are gone (HamNavList gate restored `d8d6ed966`; CharClipGroup null guards; `WorldCrowd::BuildBillboard` is a real quad); Phase E save landed. Undated filename presented as the current next-steps. | New PLAN (coordinator) |
| `NEXT_DTA_LIST_RENDERING.md` | 2026-09-30 | Own banner: "RESOLVED 2026-09-30" (native-only `UITransitionCompleteMsg` handler removed; `native/tests/test_hamnavlist_transition.cpp`). The rest is dead hypotheses. | — (history) |

---

## Not archived, and why

| Candidate | Decision |
|---|---|
| `docs/native/MACOS_ARM_BUILD.md` (since rewritten as `MACOS.md`), `WEB_TESTING.md`, `SCORING_ENV_VARS.md`, `native/web/VIDEO_ASSETS.md` | Have UPDATE findings, but are being folded into new platform pages by the coordinator; left **unedited** on purpose. Known stale facts: MACOS_ARM_BUILD — "untested on hardware" (`7bc413130`, `0a8f5c73a` fixed Mac-hardware crashes), Linux link flags pre-`--no-undefined`, no mention of the engine; WEB_TESTING — `native/web/build/dc3-web.js` (now `release/`/`debug/`), `scripts/build/web.sh` (delegator), "headed-only"; SCORING_ENV_VARS — omits `DC3_POSE_MODELS`, `DC3_POSE_GPU`, `DC3_POSE=off`, `DC3_CONTROLLER_MODE`. |
| `docs/native/{ENDIANNESS_PORT_CHECKLIST_DC3,RENDER_TEST,TOOLS}.md` | Accurate (KEEP). |
| `docs/native/CONSOLE_DTA_EVAL.md`, `KINECT_CAPTURE.md` | Not stale — **moved** (not archived) to `docs/tools/console/`: they are Xbox-hardware tooling, not the port. |
| `docs/sessions/convergence/` (7 files) | Dated history (2026-03-21) by content; kept in `sessions/`. The live docs that called them "working docs" are the ones archived here. |
| `docs/plans/stub-burndown/PLAN.md`, `docs/plans/dc3-boot/FILE_INCLUDE_FIX.md` | Decomp-side, not native; out of this audit's scope. `stub-burndown/PLAN.md` is a PPC `is_stub` worklist of the class archived on 2026-08-17 and is a candidate for the next decomp doc audit. |
| `docs/runtime/*`, `docs/plans/XENIA_*.md` | Xenia, not native; out of scope. Only two headers were corrected (`XENIA_ORACLE.md` status line; `XENIA_ASYNC_COMPLETION_STALL.md` "OPEN" → solved). `runtime/BOOT_ANALYSIS.md` is contradicted by `runtime/XENIA_HEADLESS_STATUS.md` of the same date and is a candidate for a Xenia audit. |

---

## Links left frozen (inbound links to archived files)

Links **into** the archived files from dated session logs and investigations are
history and were not edited. They are dead after this move; resolve them by
inserting `archive/2026-11-13-native-doc-audit/` after `docs/` (e.g.
`docs/native/DECOMP_GAPS.md` → `docs/archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md`).

Markdown links that no longer resolve (12):

- `docs/sessions/2026-03-12-session52-animation-lifetime.md:3` → `../native/UI_ANIMATION_STATUS.md`
- `docs/sessions/2026-03-12-session53-step2-paneldir.md:3` → `../native/UI_ANIMATION_STATUS.md`
- `docs/sessions/2026-03-12-session54-step3-visibility.md:3` → `../native/UI_ANIMATION_STATUS.md`
- `docs/sessions/2026-03-12-session55-step4-parity-bridges.md:3` → `../native/UI_ANIMATION_STATUS.md`
- `docs/sessions/2026-03-12-session56-alpha-floor-investigation.md:3` → `../native/UI_ANIMATION_STATUS.md`
- `docs/sessions/2026-03-16-architecture.md:221` → `../native/FILEMERGER_CONVERGENCE.md`
- `docs/sessions/2026-03-16-architecture.md:274` → `../native/FILEMERGER_CONVERGENCE.md`
- `docs/sessions/2026-03-16-architecture.md:4` → `../native/LOADING_ARCHITECTURE.md`
- `docs/sessions/2026-03-17-convergence-review.md:17` → `../native/FILEMERGER_CONVERGENCE.md`
- `docs/sessions/2026-03-17-convergence-review.md:4` → `../native/FILEMERGER_CONVERGENCE.md`
- `docs/sessions/2026-03-19-merge-parity-p0.md:221` → `../native/FILEMERGER_CONVERGENCE.md`
- `docs/sessions/2026-03-19-venue-draw-investigation.md:7` → `../native/FILEMERGER_CONVERGENCE.md`

Plain-text path mentions (not links) of an archived file, in these dated logs and investigations:

- `docs/investigations/2026-06-10-roadmap-to-100/12-native-stub-intersection.md`
- `docs/investigations/2026-06-10-roadmap-to-100/99v-WAVE-24-RESULTS.md`
- `docs/investigations/2026-06-10-roadmap-to-100/99x-WAVE-26-RESULTS.md`
- `docs/sessions/2026-03-07-native-burndown-phases-0-3.md`
- `docs/sessions/2026-03-07-session29-animation-pipeline.md`
- `docs/sessions/2026-03-12-session52-animation-lifetime.md`
- `docs/sessions/2026-03-12-session53-step2-paneldir.md`
- `docs/sessions/2026-03-12-session54-step3-visibility.md`
- `docs/sessions/2026-03-12-session55-step4-parity-bridges.md`
- `docs/sessions/2026-03-12-session56-alpha-floor-investigation.md`
- `docs/sessions/2026-03-16-architecture.md`
- `docs/sessions/2026-03-17-convergence-review.md`
- `docs/sessions/2026-03-17-gNativeHudDir-removal.md`
- `docs/sessions/2026-03-17-hamobj-native-hack-audit.md`
- `docs/sessions/2026-03-17-native-long-term-roadmap.md`
- `docs/sessions/2026-03-19-merge-parity-p0.md`
- `docs/sessions/2026-03-19-venue-draw-investigation.md`
- `docs/sessions/2026-03-21-convergence-session.md`
- `docs/sessions/2026-06-08-feet-reverify-data.md`
- `docs/sessions/2026-09-15-two-month-native-impact-bug-review.md`
- `docs/sessions/2026-09-30-native-assert-harvest.md`
- `docs/sessions/SESSION41_PLAN.md`
- `docs/sessions/SESSION74_VENUE_LOADING_ARCHITECTURE.md`

**Outside this repository (not edited — owned elsewhere):**
`../milo-native-engine/CLAUDE.md` and
`../milo-native-engine/docs/native/README.md` link
`docs/native/NATIVE_PORT_STATUS.md`; `../milo-native-engine/docs/sessions/2026-05-27-phase-0-closure.md`
cites it. The agent memory notes (`convergence-progress.md`, `MEMORY.md`) name
`docs/native/PLAN.md` + `TODO.md` as canonical. The
[2026-08-17 manifest](../2026-08-17-doc-audit/MANIFEST.md) is itself frozen and
references `docs/native/` paths that moved here.

## Links inside archived files that are now dead

Recorded rather than corrected (rule 4 of [`../README.md`](../README.md)).
Two were already dead before the move:
`plans/custom-graphics-engine/GRAPHICS_SYSTEM_DESIGN.md:574` →
`../../docs/native/RENDER_TO_TEXTURE.md` and
`plans/custom-graphics-engine/PORTING_ANALYSIS.md:286` →
`docs/plans/custom-graphics-engine/STREAM_DESYNC.md` (both malformed paths).

The other 27 became dead with the move: a `../sessions/…`, `../debugging/…`, `../tools/…` or `../decomp/…` link from `native/` now needs two more `../` (resolve it against `docs/`, not against this directory); `plans/dc3-native/VIEWER_STATUS.md`'s `native/docs/` links need two more `../` as well.

- `native/DECOMP_GAPS.md:304` → `../sessions/2026-03-12-session58-ui-animation-verification.md`
- `native/DECOMP_GAPS.md:319` → `../sessions/2026-03-12-session61-merger-investigation.md`
- `native/DECOMP_GAPS.md:363` → `../sessions/2026-03-12-session61-merger-investigation.md`
- `native/DECOMP_GAPS.md:596` → `../debugging/native.md#controller-mode-policy`
- `native/FILEMERGER_CONVERGENCE.md:6` → `../sessions/2026-03-17-convergence-review.md`
- `native/FILEMERGER_CONVERGENCE.md:320` → `../sessions/2026-03-17-dirloader-parent-chain.md`
- `native/HACK_AUDIT.md:435` → `../decomp/patterns/rounded-100-hides-real-bugs.md`
- `native/HACK_AUDIT.md:447` → `../tools/BUILD_SYSTEM.md#a-fresh-worktrees-reportjson-describes-main-not-your-branch`
- `native/LOADING_ARCHITECTURE.md:3` → `../sessions/2026-03-16-architecture.md`
- `native/LOADING_ARCHITECTURE.md:5` → `../sessions/2026-03-17-convergence-review.md`
- `native/TODO.md:267` → `../sessions/2026-03-17-dirloader-parent-chain.md`
- `native/UI_ANIMATION_STATUS.md:26` → `../sessions/2026-03-12-session52-animation-lifetime.md`
- `native/UI_ANIMATION_STATUS.md:27` → `../sessions/2026-03-12-session53-step2-paneldir.md`
- `native/UI_ANIMATION_STATUS.md:28` → `../sessions/2026-03-12-session54-step3-visibility.md`
- `native/UI_ANIMATION_STATUS.md:29` → `../sessions/2026-03-12-session55-step4-parity-bridges.md`
- `native/UI_ANIMATION_STATUS.md:35` → `../sessions/2026-03-11-session48-native-ui-hacks.md`
- `native/UI_ANIMATION_STATUS.md:36` → `../sessions/2026-03-11-session49-camera-fixes.md`
- `native/UI_ANIMATION_STATUS.md:37` → `../sessions/2026-03-11-session51-validation.md`
- `native/UI_ANIMATION_STATUS.md:38` → `../sessions/2026-03-12-session52-animation-lifetime.md`
- `native/UI_ANIMATION_STATUS.md:39` → `../sessions/2026-03-12-session53-step2-paneldir.md`
- `native/UI_ANIMATION_STATUS.md:40` → `../sessions/2026-03-12-session54-step3-visibility.md`
- `native/UI_ANIMATION_STATUS.md:41` → `../sessions/2026-03-12-session55-step4-parity-bridges.md`
- `native/UI_ANIMATION_STATUS.md:42` → `../sessions/2026-03-12-session56-alpha-floor-investigation.md`
- `native/UI_ANIMATION_STATUS.md:44` → `../sessions/2026-03-12-session58-ui-animation-verification.md`
- `native/VISUAL_BUG_PROBE_PLAN.md:5` → `../tools/HTTP_DEBUG_SERVER.md`
- `plans/dc3-native/VIEWER_STATUS.md:85` → `../../../native/docs/scene-renderer.md`
- `plans/dc3-native/VIEWER_STATUS.md:86` → `../../../native/docs/viewer-internals.md`
