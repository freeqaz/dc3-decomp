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

**Live telemetry from the original binary is the strategic goal** — not just for this IK bug, but for decomp parity assurance across the entire port. Any function's behavior can be compared Xbox-vs-native once gameplay is running.

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

xenia/src/xenia/dc3_hack_pack.cc:
  - IK telemetry instrumentation (7 capture points, behind --dc3_ik_telemetry)
  - (Note: hack pack stubs are now decomp-only, gated by caller)
```

## IK Bug Investigation

### Confirmed fixes (all at 100% match, committed-quality)

1. **`HamIKSkeleton::NeutralWorldXfm`** — 100%. Real bug: `mChar->Find(...)` should be `skelDir->Find(...)`.
2. **`MakeRotMatrix`** — 100%. Real sign error in `mtx.z.y`.
3. **`GetGroundHeight`** — 100%. Loop structure fix.

### Partially fixed

4. **`ComputeHandPullAndQuat`** — 86.4%. Two logic bugs fixed (inverted `dz`, wrong `LocalXfm` source). Remaining mismatch is likely register allocation or lowering.

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
