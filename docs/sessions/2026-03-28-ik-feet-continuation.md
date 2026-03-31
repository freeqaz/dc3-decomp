# IK Feet-in-Floor Bug — Continuation Notes (2026-03-28)

## Status: Calibration Bypass Partially Working — GestureMgr Consistency Needed

Two of three calibration bypass patches are working:
1. **NOP'd `IsTrackingAllSkeletons` guard** in `SetPlayerPresent` (0x8290834C) — allows `player_present` property to propagate during multiuser panel.
2. **Continuous tracking ID force-write** via `NuiSkeletonGetNextFrame` handler — writes tracking IDs 1/2 to both players' `HamPlayerData.mSkeletonTrackingID` (offset 0x60) every frame.

**Result**: The game loads `venue_select_screen.cfg` (confirming `enter_gameplay` fires) but then asserts at `SkeletonChooser.cpp:464 pPlayer2Skeleton` — the `ChoosePlayerSides` function calls `GetSkeletonByTrackingID` with the forced ID, which returns null because the GestureMgr has no skeleton with that tracking ID. The forced IDs create an inconsistency.

**Resolution**: Added patches 3 and 4 — stubbed `ChoosePlayerSides` (0x82909968) and `SetPlayerSkeletonWarningData` (0x82907880) to `blr`. These functions validate skeleton pointers that don't exist with forced IDs. With all 4 patches:
1. NOP'd `IsTrackingAllSkeletons` guard in `SetPlayerPresent`
2. Continuous tracking ID force-write via `NuiSkeletonGetNextFrame` handler
3. Stubbed `ChoosePlayerSides` to `blr`
4. Stubbed `SetPlayerSkeletonWarningData` to `blr`

The game now boots through splash → main menu without assert crashes. Grammar files for `gameplay_settings_screen`, `startgame_screen`, `venue_select_screen`, and `song_select_screen` all load — confirming the `enter_gameplay` DTA flow fires.

**Current blocker**: Scripted input timing needs tuning to navigate through menus to actual gameplay.

**Correct menu flow** (matches native port YMCA script):
1. attract_screen → A (skip intro movie)
2. title_screen → A
3. main_screen → A (selects "Dance", first item)
4. choose_mode_screen → A (selects "Perform")
5. song_select_screen → DOWN×2, A (selects YMCA)
6. seldiff_screen → A (select difficulty)
7. character_select_screen → A (confirm character)
8. startgame_screen → A (Play Solo)
9. multiuser_screen → auto-advances via calibration bypass
10. loading → game_screen

Transitions on Xenia are ~5-10× slower than the native port. Each screen change needs ~5-8 seconds of dwell time. The A-button spam approach doesn't work well because it fires on the wrong screen.

**Pre-existing issue**: XMA audio SIGSEGV at `0x82E7732C` (`XMAHALAllocateContexts`) is non-fatal — doesn't block game progression but may mean no audio during gameplay.

### Patch 6: PPC Code Cave for enter_gameplay (2026-03-28 late)

Wrote 36 PPC instructions (144 bytes) into the `ProtocolDebugString` dead zone (0x825EF4D8) to auto-call `enter_gameplay` from `MultiUserGesturePanel::Poll` after 60 frames. The code cave:
1. Calls `TexLoadPanel::Poll` (parent) every frame
2. After 60 frames, constructs `Symbol("enter_gameplay")` → `DataArrayPtr` → calls `Execute()`
3. Cleans up DataArrayPtr

Patched `MultiUserGesturePanel::Poll` (0x82942EB8) to branch to the cave.

**Status**: Code cave compiles and runs without crashes, but the `enter_gameplay` DTA call doesn't visibly transition the game. Possible issues:
- DataNode type constant (kDataSymbol = 5) might be wrong for this engine version
- DataArrayPtr offset for mData might not be at +0
- The "Start the Party" mode might need song state set up before enter_gameplay works
- The code cave might be calling the wrong function (need to verify PPC instruction encoding)

**Update (late session)**: Root cause found for player_present not stabilizing — `SetPlayerSkeletonNavData` was CLEARING the forced tracking IDs to -1 each frame. Fixed with Patch 4b (stub that always calls SetPlayerPresent(0/1, true)). Player indicators now show **colored boxes** (cyan/magenta), confirming player_present=TRUE.

**Remaining blocker: enter_gameplay DTA evaluation from PPC code cave**

Three approaches tried for the PPC code cave in MultiUserGesturePanel::Poll:
1. **DataArrayPtr/Symbol/Execute** — crashed (82 SIGSEGVs) due to PPC ABI parameter save area corruption. Fixed stack layout, but DTA evaluation still didn't transition.
2. **UIManager::GotoScreen("loading_screen")** — no crash, but no transition either. The loading_screen UIScreen may not exist in ObjectDir, or the screen stack state prevents the goto.
3. **push=false GotoScreen** — same result, no transition.

**What DOES work**:
- Game navigates menus correctly via A-button spam (splash → main menu → Perform → song select with YMCA visible → difficulty select → character select)
- player_present property is TRUE on both sides (colored boxes confirm)
- No assert crashes (all skeleton-related functions properly stubbed)

**What DOESN'T work**:
- The multiuser panel cannot be exited because `enter_gameplay` requires DTA script evaluation, which is extremely difficult to trigger from PPC code caves or host-side handlers

**Graphics bug (2026-03-30 investigation)**: Xenia Vulkan renderer on Linux shows **framebuffer ghosting** — previous screen content bleeds through during transitions. Rendering itself IS working (DC3 logo, 3D menus, character models all render correctly with `--dump_frames_path` + `dangerouslyDisableSandbox`). Investigation notes:
- MUST skip sandbox for ALL Xenia GPU runs (Vulkan needs ICD access)
- MUST include `--dump_frames_path=` for headless capture (`headless_frame_dump_` flag)
- First ~5 VdSwaps always black (game still booting, expected)
- Draw counts vary: 134→1017→868→927→856 — confirms different content per frame
- Ghosting likely from deferred draw system replaying one frame but EDRAM state persisting from previous frames (LOAD_OP_LOAD by design). Frames captured at interval=300 accumulate 300 game frames of resolve data in shared_memory, then the deferred draws add ONE frame's rendering on top — producing the overlay/ghosting appearance.

**Recommended next approaches (in order of feasibility)** *(Note: approaches 1-4 below are for the multiuser panel bypass specifically; these were superseded by Patch 6 in the Boot3 session which successfully uses in-place PPC rewrite to call GotoScreen("loading_screen") after 400 frames)*:
1. **Patch the multiuser.dta bytecode in guest memory** to auto-fire enter_gameplay when player_present is set (requires understanding DTA bytecode format)
2. **Use processor->ExecuteRaw()** from the NuiSkeletonGetNextFrame host handler to call the GotoScreen code cave on the game's main thread
3. **Find and call the C++ function backing initialize_gameplay_data + the UI transition** instead of going through DTA
4. **Skip the multiuser panel entirely** by patching the DTA flow that pushes it (find where the screen transition TO multiuser_screen happens and redirect to loading_screen)

**Live telemetry from the original binary is the strategic goal** — not just for this IK bug, but for decomp parity assurance across the entire port. Any function's behavior can be compared Xbox-vs-native once gameplay is running.

## SaveLoadManager Crash Fix (2026-03-28 continued)

### Root cause

`wait_main_after_saveload_screen` calls `{saveload_mgr activate}` on enter. `SaveLoadManager::Activate()` sets up XContent cache operations via `CacheXbox::GetFileSizeAsync`. Xenia's XAM `GetFileSize` stub returns 0 (success), but `CacheXbox::ThreadGetFileSize` has a bug where the success path never stores the file size into `mData`. The garbage `mCacheFileSize` causes `_MemAllocTemp(536876708)` which crashes with "Allocation failure, heap 'main', want 536876708 bytes".

### Fix

Added `SaveLoadManager::Activate` stub to `emulator.cc` in the original-XEX else-branch (runs when `!dc3_is_decomp_layout`). Patches `0x82894A10` to `blr`. With `mActivated=0`, `IsIdle()` returns true immediately, `wait_for_saveload_panel.poll` fires `saveload_complete`, and the game advances to `title_screen_to_voice_control_tutorial_screen` or `main_screen`.

### XamAlloc non-zero unk fix

`XamAlloc_entry` in `src/xenia/kernel/xam/xam_info.cc` had `assert_true(unk == 0)`. DC3's DLC content queries pass non-zero unk values, causing assertion crash after ~190 seconds. Fixed by replacing the assert with a warning log.

### Current state (post-fix)

- Game runs 190+ seconds without "Allocation failure" or "Program Ended"
- Scripted input navigates past attract_screen → autosave_warning → title_screen → wait_main_after_saveload_screen → voice_control_tutorial (or main_screen)
- Null GPU test shows 5.5M draw calls in 190s (vs 576K in 70s without scripted input), confirming complex 3D scenes are rendering
- Vulkan renderer still crashes in `Rnd::DoWorldEnd` (null world pointer) when loading main_screen 3D background — only affects frame capture, not null GPU path

### Scripted input timing (wall-clock seconds from Xenia start)

```
15s:A      # skip attract movie (fires attract_screen.skip_selected → autosave_warning_screen)
100s:A     # select MAIN MENU on title_screen (appears after ~100s)
115s:A,120s:A  # voice_control_screen_0 → screen_1 (A to advance dialog)
125s:Y     # skip voice control tutorial (Y = debug shortcut to exit_screen)
130s:A     # voice_control_screen_4 outro → main_screen
135s:A     # select first item on main_screen (Dance)
140s:A     # choose_mode_screen → select Perform It
145s:A,150s:A  # song_select_screen navigation
```

NOTE: These are approximate — frame captures from Vulkan show title_screen at ~100s, but transition-screen durations vary. Need further refinement with more frame captures once Vulkan renderer is fixed.

## Xenia Boot Progress (2026-03-28)

### What was fixed

1. **Hack pack disabled for original XEX** — The hack pack was incorrectly applying decomp-only stubs (`FlowManager::Poll`, `UIManager::GotoFirstScreen`, `Splash::PrepareNext`, etc.) to the original binary. These stubs address `/FORCE:MULTIPLE` linker corruption that only exists in the decomp build. Gated the entire `ApplyDc3HackPack` call behind `if (dc3_is_decomp_layout)` in `emulator.cc`. Only NUI overrides remain active for the original XEX.

2. **Fake skeleton handler implemented** — New `Dc3NuiFakeSkeletonGetNextFrameExtern` in `emulator.cc` that returns S_OK with a tracked skeleton (20 joints, raised-hands pose). Registered when `--fake_kinect_data=true`. Writes all data in big-endian via `xe::store_and_swap`. Floor clip plane set to Y-up.

### What boots now

Full rendering pipeline works:
- DC3 splash screen (animated neon logo)
- Harmonix logo splash
- Intro cutscene (3D characters dancing in club, close-up shots)
- Autosave prompt
- Main menu (`TITLE_SCREEN`) with controller mode active
- Grammar files load for `startgame_screen`, `campaign_song_select_screen`, `practice_choose_screen` — confirming the game internally reaches the song selection flow

### Remaining blocker: Kinect calibration

After selecting "Start the Party" from the main menu, the game enters the `SkeletonChooser` / `MultiUserGesturePanel` flow. This screen shows the NUI space (depth camera view) and waits for players to be assigned to sides via hand-raise gesture detection.

**What works:**
- Fake skeleton provides tracked skeleton data (both player position indicators light up blue)
- The game loads `startgame_screen.cfg` grammar, confirming it briefly reaches the startgame pane

**What doesn't work:**
- `NuiImageStreamGetNextFrame` returns -1 (no depth frame). Returning S_OK crashes because the output `NUI_IMAGE_FRAME` struct has a texture pointer that would be garbage. A proper handler needs to allocate guest memory for a depth image buffer.
- The `SkeletonChooser` hand-raise filter (`mSkeletonHandRaisedFilters`) may need more than just skeleton position data — it may require the full `GestureMgr` processing pipeline including depth validation.
- The `MultiUserGesturePanel` on Xbox fires `enter_gameplay` from DTA scripts after `s0_ready` and `s1_ready` are set. These require the full skeleton assignment flow to complete.

### Next steps for Xenia gameplay

Two approaches, in order of expected difficulty:

**Approach A: Fake depth image handler (preferred)**
Implement `Dc3NuiFakeDepthImageGetNextFrameExtern` that:
1. Allocates a persistent guest memory buffer for a 320x240 depth image (153,600 bytes)
2. Fills the `NUI_IMAGE_FRAME` output struct with valid pointers to that buffer
3. Writes depth values corresponding to a person at 2m (depth ~2000mm)
4. Handles `NuiImageStreamReleaseFrame` as a no-op

This would let the depth camera view render something and allow the calibration to validate player position.

**Approach B: Patch SkeletonChooser auto-assign (faster but less general)**
Write a targeted guest extern override for `SkeletonChooser::AssignSkeleton` or the DTA handler that sets `s0_ready`/`s1_ready` to force player assignment when `InControllerMode()` is true. This bypasses calibration entirely but doesn't give us a working depth camera for other debugging.

**Approach C: DTA flow bypass**
Modify the game's flow to skip the multiuser panel entirely. The `enter_gameplay` DTA function (in `ui/global.dta`) can be triggered directly, but it requires valid player/song state to be set up first.

### Commands

**Build Xenia:**
```bash
cd /home/free/code/milohax/xenia
git checkout headless-vulkan-linux
make -C build -f Makefile xenia-headless -j$(nproc)
# Binary: build/bin/Linux/Checked/xenia-headless (~200 MB)
```

**Boot original XEX (Vulkan, frame capture):**
```bash
XENIA=/home/free/code/milohax/xenia/build/bin/Linux/Checked/xenia-headless
DC3=/home/free/code/milohax/dc3-decomp

mkdir -p /tmp/claude-1000/xenia-frames
$XENIA \
  --target=$DC3/orig-assets/debug.xex \
  --gpu=vulkan \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --fake_kinect_data=true \
  --dump_frames_path=/tmp/claude-1000/xenia-frames \
  --headless_capture_interval=300 \
  --scripted_input="5s:A,10s:A,15s:A,50s:A,55s:A,60s:A" \
  --headless_timeout_ms=120000
```

**Boot with IK telemetry (once gameplay is reachable):**
```bash
$XENIA \
  --target=$DC3/orig-assets/debug.xex \
  --gpu=vulkan \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --fake_kinect_data=true \
  --dc3_ik_telemetry=true \
  --headless_timeout_ms=300000 2>&1 | tee /tmp/xenia_ik.log

# Filter ankle IK data:
grep 'DC3:IK \[frame' /tmp/xenia_ik.log | grep 'type=ankle' > /tmp/ik_ankle.txt
```

**Convert captured frames to PNG:**
```bash
for f in /tmp/claude-1000/xenia-frames/frame_*.ppm; do
  [[ "$f" == *_raw.ppm ]] && continue
  magick "$f" "${f%.ppm}.png"
done
```

**Headless boot (no GPU, faster, for log analysis):**
```bash
$XENIA \
  --target=$DC3/orig-assets/debug.xex \
  --dc3_nui_patch_layout=original \
  --dc3_crt_skip_nui=true \
  --fake_kinect_data=true \
  --headless_timeout_ms=30000
```

**Scripted input button names:** `A`, `B`, `X`, `Y`, `START`, `BACK`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `LB`, `RB`, `LS`, `RS`, `GUIDE`. Combine with `+` (e.g. `A+START`). Format: `<time>:<button>[:<hold_duration>]`, default hold 200ms.

### Key addresses for calibration bypass

**SkeletonChooser (controls player assignment):**
| Function | Address | Notes |
|----------|---------|-------|
| `SkeletonChooser::Poll` | `0x8290A280` | Main loop — calls CheckToSwitch, UpdateTrackedSkeletonsElective, HighFiveFilter |
| `SkeletonChooser::SetPlayerPresent` | `0x82908320` | Sets `player_present` property on PlayerProvider → triggers DTA flow |
| `SkeletonChooser::SetPlayerSkeletonID` | `0x82904BB0` | Assigns a tracking ID to a player slot |
| `SkeletonChooser::ChoosePlayerSides` | `0x82909968` | Decides left/right side assignment |
| `SkeletonChooser::EnterMultiPlayerUpdateMode` | `0x82904A60` | Starts tracking all skeletons, inits hand-raise filters (500ms threshold) |
| `HamGameData::AssignSkeleton` | `0x82451D90` | Low-level skeleton→player assignment |
| `HamGameData::AutoAssignSkeletons` | `0x82451E88` | Auto-assign from SkeletonUpdateData |

**NUI image stream (depth camera):**
| Function | Address | Notes |
|----------|---------|-------|
| `NuiImageStreamOpen` | `0x829C9330` | Opens depth/color stream, returns HANDLE |
| `NuiImageStreamGetNextFrame` | `0x829C86F0` | Returns `NUI_IMAGE_FRAME` with locked texture ptr |
| `NuiImageStreamReleaseFrame` | `0x829C8A18` | Releases frame lock |

**NUI_IMAGE_FRAME struct** (from `LiveCameraInput.h`):
The frame contains a `NUI_LOCKED_RECT` with `{ uint32_t mPitch; void* mBits; }`. The `mBits` pointer must point to valid guest memory — returning S_OK with a null/garbage pointer crashes at `0x82E7732C`. A proper handler needs to allocate ~154KB of guest memory for a 320x240x16bit depth buffer.

**DTA flow (`ui/multiuser/multiuser.dta`):**
- `s0_ready` / `s1_ready` — boolean state vars, both must be TRUE to call `start_game`
- `start_game` handler calls `enter_gameplay` (defined in `ui/global.dta`)
- `set_ready($side, $ready)` — handler at line 767 that sets these vars
- `startgame_pane` `on_select_play` calls `set_ready $side TRUE` then `start_game`

**DC3 source reference:**
- `src/lazer/meta_ham/MultiUserGesturePanel.cpp` — On `HX_NATIVE`, fires `enter_gameplay` directly in Poll (line 73-77)
- `src/lazer/meta_ham/SkeletonChooser.cpp` — Hand-raise filter at line 258 (500ms, 0.82 forward-facing cutoff)
- `src/system/gesture/LiveCameraInput.h` — `NUI_IMAGE_FRAME`, `LockedRect`, `Buffer` structs
- `src/system/gesture/GestureMgr.h:140` — `mInControllerMode` flag (always 1 on HX_NATIVE)

### Xenia files modified (in xenia repo, branch `headless-vulkan-linux`)

```
xenia/src/xenia/emulator.cc:
  - Gated ApplyDc3HackPack behind is_decomp_layout
  - Dc3NuiFakeSkeletonGetNextFrameExtern handler (lines 317-451)
  - fake_kinect_data routing for NuiSkeletonGetNextFrame override
  - DEFINE_bool(dc3_ik_telemetry)
  - Calibration bypass: NOP at 0x8290834C + continuous tracking ID force-write
  - Calibration bypass: stub ChoosePlayerSides (0x82909968) + SetPlayerSkeletonWarningData (0x82907880) to blr

xenia/src/xenia/dc3_hack_pack.cc:
  - IK telemetry instrumentation (7 capture points, behind --dc3_ik_telemetry)
  - (Note: hack pack stubs are now decomp-only, gated by caller)
```

## IK Bug Investigation

### Confirmed fixes (all at 100% match, committed-quality)

1. **`HamIKSkeleton::NeutralWorldXfm`** — 100%. Real bug: `mChar->Find(...)` should be `skelDir->Find(...)`.
2. **`MakeRotMatrix`** — 100%. Real sign error in `mtx.z.y`.
3. **`GetGroundHeight`** — 100%. Loop structure fix.

### Major improvements (2026-03-28 session)

4. **`HamIKEffector::Poll`** — 83.3% → **99.9%** (+16.6%). Key fixes: mSkeleton-first check order, ObjPtr& ternary for finger, pelvis mLocalXfm.v.x direct access, struct Vector3 assignment, Max(0,blend) argument order.
5. **`DoFancyElbow`** — 84.5% → **99.5%** (+15.0%). Key fix: combined pullAccum+quatAccum into single QuatXfm struct (prevents FPR register promotion). Also reused rotMat variable.
6. **`ComputeHandPullAndQuat`** — 86.4% → **93.5%** (+7.1%). Offset swap fixes. Remaining 6.5% is unfixable volatile register allocation + compiler scheduling.

### HX_NATIVE hacks (to be evaluated with Xbox data)

- **Ground clamp** (HamIKEffector.cpp lines 432-461) — Raises ankle to keep toe above ground. Works for L-foot but R-foot still has issues.
- **mLocalXfm sanity check** (lines 464-485) — 50-unit threshold band-aid.
- **Foot inversion guard** (lines 492-520) — Diagnostic logging only.

These should be evaluated once we have Xbox ground truth. If Xbox ankle positions are naturally correct without a ground clamp, the real divergence is upstream (constraint targets, animation data, or character world transform).

### IK telemetry instrumentation (ready, waiting for gameplay)

| Slot | Field | What it captures |
|------|-------|-----------------|
| totalWeight | ApplyConstraints f1 | Sum of constraint weights |
| groundHeight | GetGroundHeight f1 | Floor Z reference |
| effectorType | GetType r3 | ankle=2, hand=3, etc. |
| posWeight | ApplyPosConstraints f1 | Pelvis constraint weight |
| pollThisPtr | Poll r3 | HamIKEffector instance address |
| ikElbowZ | IKElbow entry v.z | Ankle Z before elbow modifies parents |
| fancyWeight | DoFancyElbow entry f1 | Hand effector weight |

## Key Insight

Live Xenia telemetry is not just for this IK bug — it's infrastructure for **decomp parity assurance**. Any function's runtime behavior can be compared Xbox-vs-native once gameplay is running. The investment in getting past the Kinect calibration pays dividends across the entire project.

The decomp logic bugs (NeutralWorldXfm, MakeRotMatrix, ComputeHandPullAndQuat) are genuine fixes. The HX_NATIVE hacks are workarounds for symptoms. **Get the Xenia data first, then fix the actual divergence.**

**Lesson learned (2026-03-30)**: The incremental "peel one layer at a time" patching strategy was inefficient. The animation freeze is caused by a SINGLE root cause (mogg FileLoader stall in Xenia) with cascading effects. Each narrow patch (HandleWait stub, memory writes, timer reset) fixed one symptom while missing others. The correct approach is either (a) fix the root cause (FileLoader), or (b) replicate the native port's complete audio-fail handling via a single JIT override on HandleWait that runs synchronously on the guest thread.

## Xenia Boot2 Session (2026-03-29)

### Goal
Navigate from song_select through to gameplay on Xenia to capture live IK telemetry from the original Xbox binary.

### What was accomplished

#### 1. Native port gameplay confirmed working
The native port successfully reaches gameplay with the YMCA input script. Full rendering with venue, characters dancing, camera shots active. Screenshots captured in `archive/screenshots/xenia-boot2/`.

#### 2. RB3Enhanced DTA injection research
Analyzed RB3Enhanced's DTA injection mechanisms for ideas:
- **ExecuteDTA on game thread** — we already have this via native port `/api/dta/eval`
- **DataReadFile hook** — intercept DTA loads and redirect to patched files. DC3's DataReadFile is at `0x825C1AD0`. Could redirect multiuser.dta to a version that auto-fires enter_gameplay, but this is overkill for the immediate goal.
- **Preprocessor #define injection** — RB3Enhanced hooks the DTA preprocessor to inject `#ifdef RB3E` defines.

#### 3. Code cave JIT discovery — in-place rewriting works
**Key finding**: Xenia's JIT does NOT follow branches to addresses outside the function table. The code cave at `ProtocolDebugString` (0x825EF4D8) was never executed because the JIT couldn't resolve it as a function.

**Fix**: Write PPC code **in-place** at `MultiUserGesturePanel::Poll` (0x82942EB8), overwriting the original function. The JIT compiles this as part of the original function and executes it correctly.

Confirmed by diagnostic: `pollInsn=7C0802A6` (our `mflr r0` prologue, not the original).

#### 4. UIManager diagnostics
Added host-side diagnostics to the NuiSkeletonGetNextFrame handler:
- **Screen name reading**: `Hmx::Object::mName` is at offset `0x20` (const char*). Successfully reads screen names like `attract_screen`, `title_screen`, `song_select_screen`, etc.
- **Transition state monitoring**: Reads `mTransitionState` (0x2c), `mCurrentScreen` (0x48), `mTransitionScreen` (0x4c) from UIManager.
- **Code cave counter**: Reads counter from ProtocolDebugString zone to verify code cave execution.

#### 5. enter_gameplay analysis
`enter_gameplay` is a **pure DTA function** (not a C++ DataFunc):
```dta
{func enter_gameplay
   ...
   {if_else {== {ui bottom_screen} {ui current_screen}}
      {ui goto_screen loading_screen}
      {ui pop_screen loading_screen}}
   ...}
```

Key finding: **mPushedScreens is empty** at multiuser_screen (begin=0, end=0). The DTA would use `goto_screen`, not `pop_screen`. The previous session's PopScreen theory was wrong — there are no pushed screens.

### Current blocker: song_select_screen input

**song_select_screen does not respond to controller A-button presses.** Confirmed across 5+ test runs with different timings, including:
- Single A presses at various wall-clock times
- 5-second A-spam for 5+ minutes
- DOWN + A combinations

The game navigates through attract → title → main → choose_mode → song_select via A-spam, but becomes unresponsive on song_select_screen. This is likely a **Kinect-only input screen** that uses gesture-based song browsing (swipe to scroll, point to select) without controller fallback.

**Screen flow observed** (5s A-spam):
```
attract_screen → autosave_warning_screen → choose_mode_screen → song_select_screen (STUCK)
```

### Approaches for next session

1. **Fix song_select controller input** — find where the song_select DTA or C++ filters out controller input and patch it. Check if `InControllerMode()` gates input differently on song_select panels.

2. **Host-side screen navigation** — extend the NuiSkeletonGetNextFrame handler to scan ObjectDir for target UIScreen objects and force-write UIManager transition state. The scan range needs to be wider than 0x20000 bytes around curScreen.

3. **DTA file bypass** — overlay a modified song_select_screen DTA that auto-selects the first song when entering. Use the same mechanism as the native port's `native/dta/` overlay directory, but for Xenia via DataReadFile hook.

4. **Skip song_select entirely** — find the C++ function that `on_select_play` calls in the startgame/multiuser DTA flow and invoke it directly from the host handler, bypassing the song select → difficulty → character → startgame chain.

### Xenia files modified (in xenia repo, branch `headless-vulkan-linux`)

```
xenia/src/xenia/emulator.cc:
  - Patch 6 rewritten: in-place PPC code at MultiUserGesturePanel::Poll
    (30 instructions, 120 bytes, 8-byte overflow into SetRandomCharacter)
  - Host-side UIManager diagnostic logging in NuiSkeletonGetNextFrame
  - Host-side memory scan for multiuser_screen/loading_screen UIScreen objects
  - Screen name reading via Object+0x20 (mName)
```

### Key addresses confirmed

| Symbol | Address | Source |
|--------|---------|--------|
| `MultiUserGesturePanel::Poll` | `0x82942EB8` | map file |
| `MultiUserGesturePanel::Enter` | `0x82942D38` | map file |
| `UIManager::PopScreen` | `0x8277C760` | map file |
| `UIManager::GotoScreen(char*,b,b)` | `0x8277B378` | map file |
| `HamUI::ForceLetterboxOffImmediate` | `0x8288FF40` | map file |
| `DataReadFile` | `0x825C1AD0` | map file |
| `ObjectDir::sMainDir` | `0x82F63B28` | map file |
| `TheUI` (UIManager* ptr) | `0x82F1A8E0` | decomp |
| `TheHamUI` (HamUI value) | `0x831179E8` | decomp |
| `Hmx::Object::mName` | `this+0x20` | decomp header |
| `UIManager::mTransitionState` | `this+0x2c` | decomp header |
| `UIManager::mCurrentScreen` | `this+0x48` | decomp header |
| `UIManager::mTransitionScreen` | `this+0x4c` | decomp header |
| `UIManager::mPushedScreens` | `this+0x34` (std::vector) | decomp header |
| `kTransitionPop` | `3` | decomp enum |

## Xenia Boot3 Session (2026-03-29)

### Goal
Fix the controller mode timeout that prevents song_select_screen from accepting A-button input, and navigate through to actual gameplay.

### Root cause identified and fixed

**Problem**: `ShellInput::Poll()` has a controller mode timeout. When a fake skeleton is present but no real Kinect gesture input arrives, after ~1 second the game calls `ExitControllerMode(true)`, switching to gesture-only mode. In Kinect mode, only gesture-based UI input works (swipe, point-to-select), so A-button presses are ignored on song_select_screen and all subsequent screens.

**Fix**: Added **Patch 7** to the Xenia calibration bypass chain — stubs `ShellInput::ExitControllerMode` at `0x82902748` to `blr` (immediate return). This keeps controller mode permanently active, allowing A-button input on all screens.

### Strategy 1 (ExitControllerMode stub) -- SUCCESS

The single-function stub was sufficient. No need for Strategy 2 (per-frame mInControllerMode force-write) or Strategy 3 (aggressive input timing).

### Screen transitions observed

```
autosave_warning_screen  (nuiCalls=600)
song_select_screen       (nuiCalls=1200, 1800, 2400)
game_screen              (nuiCalls=3000, 3600, 4200, 4800)  <-- GAMEPLAY REACHED
```

The game skipped several intermediate screens (title, choose_mode, seldiff, character_select, startgame) -- rapid A-button presses blast through them before the diagnostic interval fires. The multiuser_screen was also bypassed via Patch 6 (auto-GotoScreen("loading_screen") after 400 frames).

### Vulkan frame capture results

10 key screenshots archived to `archive/screenshots/xenia-boot3/`:

| Frame | Screen | Description |
|-------|--------|-------------|
| 0300 | splash | DC3 neon logo animation |
| 0600 | attract | Harmonix logo, "EXIT CONTROLLER MODE" + "SELECT" buttons visible |
| 0900 | loading/transition | Debug text: `DIAMOND.LBL (UI/ENDGAME/RESULTS_CLUSTER.MILO):SCORE_FMT CHAR ('S' 0x53) MISSING FROM VIBRATIO...` + debug lines |
| 1200 | title | "DANCE CENTRAL 3" + "MAIN MENU / START THE PARTY" |
| 1800 | song_select | Song select with blurry content, "EXIT CONTROLLER MODE" visible |
| 2400 | loading/transition | Mostly black, player indicators (cyan + magenta boxes) |
| 3000 | game_screen | Split-screen layout: P1 left (black bg), P2 right (blue bg) |
| 4800 | mode_select overlay? | Blurry menu overlay during game |
| 7200 | game_screen | Gameplay with player score/info panels visible |
| 9000 | game_screen | Score panels with bordered display areas for both players |

### Game screen observations

The game_screen shows:
- **Split-screen 2-player layout** with player 1 (cyan indicator, top-left) and player 2 (magenta indicator, top-right)
- **Player score/info panels** visible at each side with bordered display areas
- **No visible dancers or dance moves UI** -- the venue background renders (blue) but character models and move cards are absent. This is expected: without skeleton animation data flowing through the IK system, characters have nothing to render.
- **The XMA audio SIGSEGV at 0x82E7732C continues** -- non-fatal, count increases slowly (6 at 12s to 50 at 117s). Audio is likely broken but doesn't affect gameplay state.

### Scripted input that works

```
--scripted_input="5s:A,10s:A,15s:A,20s:A,25s:A,30s:A,35s:A,40s:A,45s:A,50s:A,55s:A,60s:A,65s:A,70s:A,75s:A,80s:DOWN,82s:A,85s:DOWN,87s:A,90s:DOWN,92s:A,95s:A,100s:A,105s:DOWN,107s:A,110s:DOWN,112s:A,115s:A,120s:A,125s:A,130s:A,135s:A,140s:A,145s:A,150s:A,155s:A,160s:A,165s:A,170s:A,175s:A"
```

Key insight: 5-second A-spam from the start works better than carefully timed presses, because Xenia's emulation speed varies. The game processes inputs whenever it polls, and rapid A-presses blast through menus as they appear. The DOWN presses (80s, 85s, 90s, 105s, 110s) help navigate song_select by scrolling to ensure a song is highlighted.

### Patches now active (original XEX, `--fake_kinect_data=true`)

| # | Target | Action | Purpose |
|---|--------|--------|---------|
| 1 | `SetPlayerPresent` bne at 0x8290834C | NOP (0x60000000) | Skip IsTrackingAllSkeletons guard |
| 2 | NuiSkeletonGetNextFrame handler | Force tracking IDs | Write tracking IDs 1/2 to HamPlayerData every frame |
| 3 | `ChoosePlayerSides` at 0x82909968 | blr | Skip null-skeleton asserts |
| 4 | `SetPlayerSkeletonWarningData` at 0x82907880 | blr | Skip null-skeleton asserts |
| 4b | `SetPlayerSkeletonNavData` at 0x82909340 | SetPlayerPresent(0/1, true) stub | Force player_present=TRUE |
| 5 | `ShouldWaitForRecovery` at 0x82904CD0 | li r3, 0; blr | Return false (prevent recovery block) |
| 6 | `MultiUserGesturePanel::Poll` at 0x82942EB8 | In-place rewrite (30 insns) | After 400 frames: GotoScreen("loading_screen") |
| 7 | `ExitControllerMode` at 0x82902748 | blr | **NEW** -- prevent controller mode timeout |
| - | `SaveLoadManager::Activate` at 0x82894A10 | blr | Prevent CacheXbox crash |

### IK telemetry results (run 2-3)

IK telemetry instrumentation was enabled (`--dc3_ik_telemetry=true`). PPC code caves for 7 instrumentation points installed successfully (ApplyConstraints, GetGroundHeight, GetType, ApplyPosConstraints return caves; Poll, IKElbow, DoFancyElbow entry caves).

**Findings**: IK effectors fire exactly **once** during game_screen initialization (frame ~2940), then go dormant:
```
DC3:IK [nuiFrame 2940] type=hand totalWeight=0.0000 groundHeight=-0.0000
  posWeight=0.0000 ikElbowZ=55.4311 fancyWeight=0.0000
  this=40A29C28 mEffector=8200AE9C mGround=40A46478 mWeight=0.0000
```

**Root cause**: Character animation pipeline is frozen. Traced through multiple layers:

1. **XMA audio in limbo** — Xenia's XMA decode crashes (SIGSEGV in `XMAHALAllocateContexts`). Audio is stuck: `HamAudio::Fail()` returns false, `HamAudio::IsReady()` returns false.
2. **HandleWait blocks** — `Game::HandleWait()` loops forever on `!audio->IsReady()`, calling `TheSynth->Poll()` and returning false each frame. PostWaitStart never runs.
3. **Game stays paused** — Without PostWaitStart, `mPaused` stays true and song timeline doesn't advance (songMs stays 0).
4. **No beat advancement** — Even if we force `mPaused=false` and `mRealTime=true` from the host (Patch 8), the `GameInput::mTimeOffset` was never set by `SetTimeOffset()`. With `mTimeOffset=0` and `mRealTime=true`, `CurrentMs()` returns raw timer since construction (~100s = 100000ms), mapping to a beat far past the end of the song.
5. **Animation frozen** — `HamDirector::OnSelectCamera()` sets `songAnim->SetFrame(frame)` using the beat. With a stuck beat (0 or past-end), the animation system evaluates no clips, the character rig stays static, and IK effectors have nothing to correct.

**Patches applied (runs 4-5)**:

| # | Target | Instruction | Purpose |
|---|--------|-------------|---------|
| 8a | `HandleWait` at 0x82867318 | bne→b unconditional | Bypass `!IsReady()` → return false loop |
| 8b | `PostWaitStart` at 0x8286514C | NOP (0x60000000) | Remove `Fail()` guard, body always runs |
| 8c | Host-side NUI handler | Direct memory write | Force `mPaused=false`, `mRealTime=true` at Game+0x5E/0x60 |

Result: PostWaitStart runs (confirmed), mPaused/mRealTime force-written, but **song timeline still frozen** because mTimeOffset was never calibrated. The IK effectors still only fire once at frame 2940.

**Useful data captured**:
- HamIKEffector object at `0x40A29C28` (hand type)
- mEffector member at `+0x1C` = `0x8200AE9C` (vtable/code)
- mGround member at `+0x38` = `0x40A46478` (ground plane object)
- IKElbow z=55.4311 captured via entry cave
- ApplyConstraints has no simple `blr` return (uses bctr or tail-call), so totalWeight cave doesn't fire
- `TheGamePanel` global at `0x83117410`, `mGame` at GamePanel+0x38

### Key addresses discovered (original XEX)

| Address | Function/Symbol |
|---------|----------------|
| 0x82867288 | `Game::HandleWait` |
| 0x82865128 | `Game::PostWaitStart` |
| 0x8252B9E0 | `HamAudio::IsReady` (virtual) |
| 0x825291F0 | `HamAudio::Fail` |
| 0x83117410 | `TheGamePanel` global pointer |
| 0x82342E80 | `HamDirector::Poll` (ICF-merged) |
| 0x824C21E8 | `HamIKEffector::Poll` |

### Xenia Boot3 Continued (2026-03-30)

**XMA crash fixed**: Stubbed `XMAHALAllocateContexts` at `0x82E77250` to return S_OK. Root cause: XDK HAL code uses `MmMapIoSpace` for XMA hardware MMIO that Xenia doesn't fully support. Returning S_OK (not E_FAIL, which triggered FailAppendCallback crash) lets XAudio2 proceed. Song audio uses Vorbis (.mogg), not XMA — XMA is only for SFX.

**HandleWait stubbed**: Patched prologue at `0x82867288` to `li r3, 1; blr` (always return true). Middle-of-function bytepatches were being missed by JIT cache.

**Timer reset**: Zeroed `LiveInput::mTimer.mCycles` and set `mStart` to current PPC timebase via `Clock::QueryGuestTickCount()`. This makes `CurrentMs(true)` start from 0.

**Beat still frozen at -4.87**: Despite HandleWait returning true, mPaused=false, mRealTime=true, and timer reset, the beat stays at -4.87 (pre-roll position from Game::Reset). Likely cause: `GamePanel::Poll` gate at line 430: `if (!mPauseCountInTimer->Running()) { mGame->Poll(); }` — the pause timer may be running, preventing Game::Poll from being called. Or `GamePanel::StartGame()` (which calls `mGame->Start()`) hasn't fired yet because `TheTaskMgr.Seconds(kRealTime) > -0.025f` fails.

**Animation pipeline — single root cause with cascading effects**:
```
CDReadDone() returns false forever (Xenia OVERLAPPED.Internal stays PENDING)
  → BlockMgr::Poll never completes async tasks
    → ArkFile::ReadAsync never finishes (mNumOutstandingTasks > 0)
      → mogg FileLoader stays in LoadFile state
        → HamAudio::IsReady() returns false forever
          → Game::HandleWait() returns false
            → Beat frozen → animation frozen → IK dormant
```

**ROOT CAUSE IDENTIFIED (2026-03-30)**: `CDReadDone()` at `0x826026E0` calls `GetOverlappedResult()` on the global OVERLAPPED used by `CDRead` for async ark file reads. Xenia completes all `NtReadFile` calls synchronously, but the XAPILIB `ReadFile` wrapper sets `OVERLAPPED.Internal` to `STATUS_PENDING` before calling `NtReadFile`, and Xenia writes the result to a separate stack `IO_STATUS_BLOCK` — so `OVERLAPPED.Internal` stays PENDING forever. The decomp build's hack pack already stubs CDReadDone to return true, but this stub was NOT applied to the original XEX path.

**FIX**: Added `CDReadDone` stub (`li r3, 1; blr` at `0x826026E0`) to the original XEX path in `emulator.cc`. Also removed the now-harmful HandleWait stub and mRealTime force-write, since the natural audio pipeline (StreamNull VarTimer) should work once mogg loading completes.

### Remaining blocker: song timeline

To get continuous IK data from the original binary, the song timeline must advance.

### Why previous patches failed (post-mortem)

The incremental patching approach (fix one layer, discover the next) failed because each patch was too narrow:

1. **HandleWait → `li r3, 1; blr`** (always return true): This skips the **entire function body** including the dispatch switch that calls `PostWaitStart()`. So `mPaused` stays true, `mRealTime` stays false, and `CurrentMs(false)` reads `audio.GetTime()` = 0. Beat stuck at `MsToBeat(0)` ≈ -4.87 (pre-roll).

2. **Force mPaused/mRealTime from NUI handler**: These writes happen asynchronously on a host thread while the guest thread is mid-execution. Race conditions mean the guest may read stale values on the critical frame. Also, `SetTimeOffset()` CANNOT be replicated by a memory write — it needs to invoke guest code (`TheTaskMgr.Seconds()`, `mTimer.SplitMs()`, `TheProfileMgr.GetSongToTaskMgrMs()`).

3. **Timer reset from host**: Even with timer zeroed, if `SetTimeOffset()` was never called, `mTimeOffset` is uninitialized/stale. And `MetaPerformer::StartGameplayTimer()` was never called (it runs inside `PostWaitStart`), leaving gameplay subsystems uninitialized.

4. **mPauseCountInTimer hypothesis was likely wrong**: The timer only starts on `SetGamePaused(false)` when `mState == kGamePlaying` AND `pause_count_in != 0`. During initial startup, it's not running. The real issue was items 1-3 above.

The chain of blockers diagram was misleading — it suggests 5 independent problems, but the root cause is ONE: **the mogg FileLoader never completes**, and all downstream effects stem from that.

### Root cause analysis

On the original Xbox 360 binary, `Synth::NewBufStream()` creates a `StreamNull` (XMA decoding happens at hardware level, outside the Stream class). `StreamNull::IsReady()` always returns true and `Stream::Fail()` always returns false. So the game **should** work silently on Xenia — the audio path is designed for this.

**The actual blocker is the file loading pipeline**, not audio failure detection:
- `HamAudio::IsReady()` checks `mFileLoader->IsLoaded()` → returns false forever
- The mogg `FileLoader` either never started (MIDI loading stuck) or is stuck in Xenia's ark I/O

**Diagnostic needed**: Read `HamAudio+0x30` (mFileLoader) from guest memory:
- If **null**: `HamMaster::LoaderPoll` never reached `mAudio->Load()` — MIDI loading is stuck upstream
- If **non-null**: `FileLoader` exists but `IsLoaded()` is false — Xenia's file I/O for the ark/mogg is broken

### Correct approaches (in order of preference)

**Approach A: Fix the mogg FileLoader (root cause)**
If we fix WHY the mogg FileLoader stalls in Xenia, the ENTIRE timing chain works naturally with zero additional patches. `FinishLoad()` creates a `StreamNull`, `IsReady()` returns true, `HandleWait()` dispatches `PostWaitStart()`, game unpauses, beat advances, animation plays.

Diagnostic steps:
1. Add host interceptor on `HamAudio::IsReady` (0x8252B9E0) to log `mFileLoader` / `mSongStream` / `mRawBuffer` state
2. If mFileLoader is null → trace `HamMasterLoader::PollLoading` and `HamSongData::Poll`
3. If mFileLoader is non-null → trace `FileLoader::PollLoading` and Xenia's `File::ReadAsync` for ark files

**Approach B: JIT override on HandleWait (replicate native port)**
Register a `RegisterGuestFunctionOverride` on `Game::HandleWait` (0x82867288) that runs **synchronously on the guest thread** (no race conditions):

```cpp
// Pseudocode for JIT override
void HandleWait_Override(PPCContext* ctx) {
    Game* game = (Game*)ctx->r[3];  // this pointer
    if (game->mWaitState == 0) { ctx->r[3] = 1; return; }

    HamAudio* audio = game->mMaster->GetAudio();
    if (audio->IsReady()) {
        // Audio ready — call original HandleWait to dispatch normally
        CallOriginal(ctx);
        return;
    }

    // Audio stalled — replicate native port's PostWaitStart else-branch
    static int stallCount = 0;
    if (++stallCount > 120) {  // ~2s at 60fps
        game->mPaused = false;
        game->mRealTime = true;
        game->mWaitState = 0;
        // Call SetTimeOffset on guest thread (synchronous, no race)
        game->mGameInput->SetTimeOffset();  // via guest function invocation
        MetaPerformer::Current()->StartGameplayTimer();
        ctx->r[3] = 1;  // return true
        return;
    }
    ctx->r[3] = 0;  // return false (still waiting)
}
```

This is ~30 lines of host C++ and cleanly handles everything the native port's `#ifdef HX_NATIVE` blocks do.

**Approach C: Host-driven TaskMgr write (bypass everything)**
If approaches A and B are too complex, directly write to `TheTaskMgr.mTimelines[0].mTime` (seconds) and `mTimelines[1].mTime` (beats) from the NUI handler, incrementing each frame. This bypasses the entire Game::Poll → CurrentMs → SetSecondsAndBeat chain. See `docs/sessions/2026-03-30-host-driven-beat-design.md` for the full memory layout.

**~~Approach D: Use native port~~** ← WRONG
The native port cannot provide Xbox ground truth data. It already has the `#ifdef HX_NATIVE` audio fallback — that's the code under test. Only Xenia running the original XEX gives us the real IK bone positions to compare against.
