# Native Port TODO — UI Fully Working

## Current State (Session 38)
- **450+ draw calls/frame** on choose_mode_screen (up from 47 before HamUI integration)
- Full boot flow renders: autosave_warning → title_screen → tutorial_voice_control → main_screen → choose_mode_screen
- Text rendering, mesh rendering, material pipeline all working
- HamUI two-pass draw pipeline active (letterbox + main draw pass)
- 10000 frames stable, zero crashes

## CRITICAL BLOCKER: DTA Loading Subsystem

**The native port cannot fully function without a DTA content/scripting system.** DTA (Data Array) files are the game's primary configuration and scripting format. They drive:

### What DTAs control
1. **UIManager::mSink** — The only way `mSink` gets set is via a `set_sink` DTA message handler (`HANDLE_ACTION(set_sink, mSink = _msg->Obj<Hmx::Object>(2))`). Without this, button messages don't route to screens. We have a native-only fallback that forwards ButtonDown/ButtonUp directly to `mCurrentScreen`, but this is incomplete.
2. **Screen transitions** — DTA scripts define `next_screen`, screen flow logic, and transition triggers
3. **Content population** — List providers, mode definitions, song lists all come from DTA configs
4. **Animation lifecycle** — `StopAnimation()` calls that clean up `AnimTask` objects after enter animations complete are triggered by DTA event handlers. Without these, `IsAnimating()` returns true forever (fixed with native bypass, but the root issue persists)
5. **UI initialization** — Panel enter/exit handlers, focus management, component wiring
6. **Object properties** — Material colors, animation ranges, timing parameters

### Where DTAs live
- Compiled into binary `.dta`/`.dtb` files inside the game's `.ark` archives
- Loaded at runtime by `ObjectDir::Load` and `DataArray::Load`
- Our archive loader (`BlockMgr`) CAN read `.ark` files and extract content
- The `DataArray` parser exists and works for loading binary DTAs

### What's needed
- [ ] **DTA autoload on Dir/Object load** — When an ObjectDir loads a `.milo`, also load its associated `.dta` if present in the ark
- [ ] **DTA init message broadcast** — After loading, broadcast `"init"` to trigger `set_sink` and other init handlers
- [ ] **SystemConfig DTA loading** — Ensure `system.dta` / `ui.dta` configs are loaded from ark
- [ ] **Screen-level DTA hooks** — Screen enter/exit/transition DTAs that wire up mSink, animation callbacks, content providers
- [ ] **Test: verify mSink gets set naturally** — Remove the native fallback once DTA loading works

### Current workarounds (native-only guards)
- `UI.cpp`: Fallback button dispatch when `mSink` is null
- `HamNavList.cpp`: Bypass `IsAnimating()` check (AnimTask lifecycle not managed by DTA scripts)
- `GestureMgr.cpp`: Force `mInControllerMode = true` (no Kinect init DTA)
- `UI.cpp`: Screen auto-advance timer (replaces DTA-driven transitions)

## Phase 1: Interactive Menu Navigation (HIGH PRIORITY)
Goal: Navigate menus with keyboard/controller, see selections change

### 1.1 Controller Input on Menus
- [x] Verify joypad input reaches UIManager (ButtonDownMsg dispatch)
- [x] Fix mSink null — added fallback dispatch to mCurrentScreen
- [x] Fix controller mode gate — forced mInControllerMode on native
- [x] Fix IsAnimating() blocker — bypassed on native (AnimTask never self-deletes)
- [ ] Test keyboard arrow keys → menu highlight movement (needs GPU window)
- [ ] Verify HamNavList responds to nav input (highlight changes, scroll)
- [ ] Test select/back buttons trigger screen transitions

### 1.2 Menu Selection Visual Feedback
- [ ] Verify HamListRibbon swell/slide/select animations play on highlight change
- [ ] Test that PropAnim-driven material changes (color, alpha) reach GPU uniforms
- [ ] Verify HamLabel text updates when list selection changes

## Phase 2: DTA/Content System (HIGH PRIORITY — BLOCKER)
Goal: Load and execute DTA scripts so the game's event system works natively

### 2.1 DTA Loading Infrastructure
- [ ] Identify which `.dtb` files are in the ark archives
- [ ] Trace Xbox DTA load path: where/when does `system.dta` get loaded?
- [ ] Implement DTA autoload in ObjectDir (load .dta alongside .milo)
- [ ] Verify DataArray binary parser handles DC3's DTB format

### 2.2 Content Population
- [ ] Trace what provides data to choose_mode_screen's HamNavList
- [ ] Identify which HamNavProvider subclass populates mode list items
- [ ] Check if DTA scripts define fallback/hardcoded list content
- [ ] Understand `main_menu_wait_for_content_panel` — why it force-finishes with no loader

### 2.3 Content System Stubs (fallback if DTA loading is too complex)
- [ ] Implement minimal content provider that returns hardcoded mode list
- [ ] Or: find DTA-level content definitions that work without Xbox DLC system
- [ ] Verify list items appear in HamNavList after content is provided
- [ ] Test scrolling through populated list

## Phase 3: Visual Polish (MEDIUM PRIORITY)
Goal: Match Xbox visual quality as closely as possible

### 3.1 Kinect UI Cleanup
- [ ] Hide Kinect player indicator panels (purple corner boxes) on native
- [ ] Hide "Tip: Say Xbox, Dance!" overlay on native
- [ ] Hide voice control tutorial screens (auto-skip is fine, but they flash visually)

### 3.2 PropAnim → Material → GPU Path
- [ ] Trace PropAnim::Poll() → which material properties change
- [ ] Verify changed material properties are re-read in Mesh_Wgpu.cpp each frame
- [ ] Check if material color/alpha is read from RndMat at draw time (not cached at load)
- [ ] Test: add printf for material alpha changes on main_screen background animations

### 3.3 Text Quality
- [ ] Check localization: are token names showing instead of real strings?
- [ ] Verify text wrapping/alignment matches Xbox layout
- [ ] Check "jump n'" truncation on choose_mode_screen — might be list item text

### 3.4 Letterbox / Blacklight
- [ ] Verify HamUI's two-pass draw (mFinalDrawPassFlag) produces correct layering
- [ ] Test blacklight mode activation (enter_blacklight_mode / exit_blacklight_mode handlers)
- [ ] Verify letterbox draws between the two passes

## Phase 4: Screen-by-Screen Verification (MEDIUM PRIORITY)
Goal: Each major screen renders and functions correctly

### 4.1 Main Screen
- [ ] "START THE PARTY" / "PLAYERS" / etc. text visible and positioned
- [ ] Main menu list items navigable
- [ ] Background animation playing

### 4.2 Choose Mode Screen
- [ ] Mode thumbnails (Perform, Dance Battle, etc.) visible with correct textures
- [ ] List scrolling works
- [ ] Select transitions to next screen

### 4.3 Song Select (stretch)
- [ ] Song list populates (requires content system)
- [ ] Album art thumbnails render
- [ ] Difficulty selection works

## Phase 5: Audio (LOW PRIORITY)
Goal: Menu sound effects play

- [ ] UI click/select/scroll sounds via miniaudio backend
- [ ] Background music (if accessible from .milo assets)
- [ ] Sound volumes respond to game settings

## Phase 6: Advanced Rendering (LOW PRIORITY)
Goal: Full visual parity

- [ ] Skinned mesh rendering (bone transforms in vertex shader)
- [ ] Post-processing: bloom, color correction
- [ ] Multiply blend mode (needs bright destination — venue background)
- [ ] Motion blur (NgPostProc::DoVelocity)

---

## Known Issues to Fix
| Issue | File | Status |
|-------|------|--------|
| HamRibbon::UpdateChase resize-before-copy UB | HamRibbon.cpp | **FIXED** (copy first on native) |
| UIListWidget::DisplayColor assert on corrupted mElementState | UIListWidget.cpp | **FIXED** (bounds check on native) |
| IsAnimating() blocks input forever | HamNavList.cpp | **FIXED** (bypassed on native) |
| mSink null — button dispatch broken | UI.cpp | **FIXED** (set mSink = screen on transition) |
| Controller mode gate blocks input | GestureMgr.cpp | **FIXED** (force on native) |
| Kinect player indicators visible on native | ShellInput panels | TODO — hide panels |
| "jump n'" text truncation | choose_mode_screen | TODO — investigate |
| Empty lists (no content) | Content system | TODO — Phase 2 |

## Crashes Fixed
1. HamRibbon::UpdateChase — vector OOB from resize-before-copy (STLport UB)
2. UIListWidget::DisplayColor — HamListRibbon data overlay corrupts mElementState
3. SpeechMgr::SpeechSupported — TheSpeechMgr null
4. CursorPanel::Poll — Kinect cursor tracking null
5. SkeletonIdentifier::Init — Kinect user index OOB
6. HandsUpGestureFilter::GetHandsUp — null from skipped init
7. DrawGestureMgr — null drawable
8. HamListRibbon::DrawRibbon — LP64 pointer truncation (mElemDrawState)
