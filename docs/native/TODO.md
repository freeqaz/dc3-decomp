# Native Port TODO — UI Fully Working

## Current State (Session 40)
- **Interactive menu navigation working** — Up/Down/Confirm on choose_mode_screen
- Full boot flow renders: autosave_warning → title_screen → tutorial_voice_control → main_screen → choose_mode_screen
- Text rendering, mesh rendering, material pipeline all working
- HamUI two-pass draw pipeline active (letterbox + main draw pass)
- ScrollDirection decomp fixed to 100% match (was 66.1%)
- 3800+ frames stable with navigation input, zero crashes
- Screenshots: `archive/screenshots/session40/`
- Reference: `archive/screenshots/references/dc3_main_menu.jpg`

## Headless GPU Rendering

dc3-native supports fully headless GPU rendering via Dawn/WebGPU (no display server needed):

```bash
# Headless render with auto-screenshots
MILO_RENDER=1 MILO_HEADLESS=1 \
  MILO_SCREENSHOT_DIR=archive/screenshots/session40 \
  MILO_SCREENSHOT_FRAMES=500,3500,3700 \
  MILO_INPUT_SCRIPT=path/to/input.txt \
  MILO_MAX_FRAMES=4000 \
  native/build/dc3-native

# Input script format (one "frame button" per line):
#   3500 down
#   3550 confirm
# Button names: start, confirm/a, cancel/b, up, down, left, right, option/back, x, y
```

Env vars:
- `MILO_RENDER=1` — enable GPU rendering (otherwise headless no-GPU mode)
- `MILO_HEADLESS=1` — skip window creation, render to offscreen buffer
- `MILO_SCREENSHOT_DIR=<dir>` — auto-capture frames as PNG
- `MILO_SCREENSHOT_FRAMES=<csv>` — which frames to capture (default: 100,600,900,1500)
- `MILO_INPUT_SCRIPT=<path>` — text file with timed button presses
- `MILO_MAX_FRAMES=<N>` — exit after N frames
- `MILO_FIRST_SCREEN=<name>` — skip attract/boot screens

## NEXT UP: UI Layout Fix (Session 41)

### Problem
UI elements render but are not positioned correctly. Comparing our output (`archive/screenshots/session40/frame_03500.png`) to the Xbox reference (`archive/screenshots/references/dc3_main_menu.jpg`):

| Element | Xbox Reference | Our Native | Issue |
|---------|---------------|------------|-------|
| **Player icons** | Top-left and top-right corners, ~100x100px, white outlined | Top-left and top-right, smaller, pink/magenta filled | Size, color, position offset |
| **Nav ribbon** | Right half of screen, "MAIN MENU" text with arrow | Center of screen, horizontal band with selection box | Position shifted, text missing |
| **Selection box** | N/A (main_screen has no selection box) | Center square with icon | Different screen content |
| **Logo** | "DANCE CENTRAL 3" left-center, large cyan text | Not visible | Missing or not rendered |
| **Copyright text** | Bottom center, white text | Not visible | Missing or not rendered |
| **Help bar** | Top bar: "EXIT CONTROLLER MODE" + "SELECT" | Not visible | Missing or not rendered |
| **Background** | Flowing blue/cyan neon lines | Plain dark gray | No venue/background rendering |
| **Kinect icon** | Bottom-right, "Say Xbox" | Bottom-right, small icon | Present but different style |

Note: The reference shows `main_screen` while our native shows `choose_mode_screen` — need to compare equivalent screens.

### Investigation Plan
1. **Camera/projection setup** — Is [ui.cam] positioned correctly? Check RndCam transform, FOV, aspect ratio
2. **RndTransformable world transforms** — Are mesh/group transforms being applied? Check if xfm matrices are loaded from .milo
3. **Coordinate system** — Milo uses a different coordinate convention (Y-forward?). Check if our projection matches
4. **Screen resolution** — Xbox renders at 1280x720. Are our viewport/projection matrices set up for this?
5. **Missing text** — Are RndText objects loading? Are font meshes being created? Check visibility/Showing state
6. **HelpBar rendering** — HamUI has a help bar system — is it entering/drawing?

### Key Files to Investigate
- `native/src/platform/Rnd_Wgpu.cpp` — Camera selection, projection setup, draw loop
- `native/src/platform/Mesh_Wgpu.cpp` — Transform application in DrawShowing
- `src/system/rndobj/Cam.cpp` — RndCam::UpdateLocal, projection matrix
- `src/system/rndobj/Trans.cpp` — RndTransformable::WorldXfm
- `src/system/ui/PanelDir.cpp` — Panel draw, camera setup
- `src/system/hamobj/HamUI.cpp` — HamUI::Draw, two-pass pipeline

## CRITICAL BLOCKER: DTA Loading Subsystem

**The native port cannot fully function without a DTA content/scripting system.** DTA (Data Array) files are the game's primary configuration and scripting format. They drive:

### What DTAs control
1. **Screen transitions** — DTA scripts define `next_screen`, screen flow logic, and transition triggers
2. **Content population** — List providers, mode definitions, song lists all come from DTA configs
3. **Animation lifecycle** — `StopAnimation()` calls that clean up `AnimTask` objects after enter animations complete
4. **UI initialization** — Panel enter/exit handlers, focus management, component wiring
5. **Object properties** — Material colors, animation ranges, timing parameters

### Current workarounds (native-only guards)
- `App.cpp`: 8 stub objects for Xbox-only managers (`platform_mgr`, `profile_mgr`, etc.)
- `App.cpp`: TheHamProvider fallback via PropertyEventProvider::NewObject()
- `UI.cpp`: Fallback button dispatch + mSink = screen on transition
- `HamNavList.cpp`: Bypass `IsAnimating()` check + TheHamProvider null guards
- `GestureMgr.cpp`: Force `mInControllerMode = true`
- `GameMode.cpp`: Skip full SetMode property evaluation on native
- `UI.cpp`: Screen auto-advance timer (replaces DTA-driven transitions)

## Phase 1: Interactive Menu Navigation — COMPLETE
- [x] Joypad input reaches UIManager (ButtonDownMsg dispatch)
- [x] mSink fallback dispatch to mCurrentScreen
- [x] Controller mode gate always-on
- [x] IsAnimating() bypass
- [x] ScrollDirection vertical mode fix (Up/Down navigation)
- [x] SetSelecting crash fix (TheHamProvider null)
- [x] GameMode::SetMode crash fix
- [x] Keyboard arrows → menu highlight movement (verified headless)
- [x] Confirm button → screen transition (verified headless)

## Phase 2: UI Layout & Visual Fidelity (NEXT)
Goal: UI elements positioned and sized correctly, matching Xbox reference

### 2.1 Camera & Projection
- [ ] Verify [ui.cam] transform matches Xbox (position, FOV, near/far)
- [ ] Check aspect ratio (1280x720 → 16:9)
- [ ] Verify projection matrix (orthographic vs perspective for UI?)

### 2.2 Transform Hierarchy
- [ ] Check RndTransformable::WorldXfm is populated from .milo data
- [ ] Verify parent-child transform chain (group → mesh)
- [ ] Check if RndDir/PanelDir camera is being selected

### 2.3 Text Rendering
- [ ] Verify RndText objects load and create font meshes
- [ ] Check if font .milo files load (default.milo_xbox)
- [ ] Verify text positioning in world space
- [ ] Fix missing "MAIN MENU", copyright, help bar text

### 2.4 Help Bar & Overlays
- [ ] Verify HamUI help bar rendering (ShellInput overlay)
- [ ] Check InlineHelp component visibility

## Phase 3: DTA/Content System (HIGH PRIORITY)
Goal: Remove C++ workarounds and let real DTA screen-flow scripts drive the native port.

**Full plan**: [DTA_FLOW_V2_PLAN.md](DTA_FLOW_V2_PLAN.md)

- [ ] **Smart stubs** (Phase 1): SaveLoadManager, ProfileMgr, PlatformMgr return sensible defaults
- [ ] **Remove auto-advance** (Phase 2): DTA handlers drive screen transitions naturally
- [ ] **Animation lifecycle** (Phase 3): Fix `anim_done` → StopAnimation chain
- [ ] **Remove multiuser auto-skip** (Phase 4): Real venue/char/difficulty selection flow
- [ ] **Cleanup** (Phase 5): Remove mSink hack, GameMode guard, controller force-on

## Phase 4: Visual Polish
- [ ] PropAnim → material → GPU uniform path verification
- [ ] Letterbox / blacklight two-pass draw verification
- [ ] Background venue rendering (or fallback)

## Phase 5: Audio (LOW PRIORITY)
- [ ] UI click/select/scroll sounds via miniaudio backend
- [ ] Background music playback

## Phase 6: Advanced Rendering (LOW PRIORITY)
- [ ] Skinned mesh rendering (bone transforms in vertex shader)
- [ ] Post-processing: bloom, color correction
- [ ] Multiply blend mode (needs bright destination)

---

## Known Issues to Fix
| Issue | File | Status |
|-------|------|--------|
| HamRibbon::UpdateChase resize-before-copy UB | HamRibbon.cpp | **FIXED** |
| UIListWidget::DisplayColor assert on corrupted mElementState | UIListWidget.cpp | **FIXED** |
| IsAnimating() blocks input forever | HamNavList.cpp | **FIXED** (bypassed) |
| mSink null — button dispatch broken | UI.cpp | **FIXED** (set on transition) |
| Controller mode gate blocks input | GestureMgr.cpp | **FIXED** (force on) |
| TheHamProvider null crash | HamNavList.cpp + App.cpp | **FIXED** (factory stub) |
| GameMode::SetMode crash | GameMode.cpp | **FIXED** (skip eval on native) |
| ScrollDirection vertical mode missing | Utl.cpp | **FIXED** (100% match) |
| UI elements mispositioned | Rendering pipeline | TODO — Phase 2 |
| Text labels missing | Text/Font pipeline | TODO — Phase 2 |
| Empty lists (no content) | Content system | TODO — Phase 3 |

## Crashes Fixed (Session 40)
1. HamNavList::SetSelecting → TheHamProvider null (virtual inheritance vbtable at offset 0x8)
2. GameMode::SetMode → Property("battle_mode")->Sym() null DataArray evaluation
3. All previous session crashes (see NATIVE_PORT_STATUS.md for full history)
