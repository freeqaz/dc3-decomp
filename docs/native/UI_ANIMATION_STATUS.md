# UI Animation System — Native Port Status

## Current State

**Unwind plan complete.** All removable hacks removed. Most remaining items are native infrastructure, but panel loading shortcuts are still under active investigation.

| Step | Status | Session |
|------|--------|---------|
| 1. Restore animation lifetime | Done | [Session 52](../sessions/2026-03-12-session52-animation-lifetime.md) |
| 2. Stop synthetic panel entry | Done | [Session 53](../sessions/2026-03-12-session53-step2-paneldir.md) |
| 3. Remove visibility masks | Done (2/3 removed) | [Session 54](../sessions/2026-03-12-session54-step3-visibility.md) |
| 4. Revisit parity bridges | Done (1 removed) | [Session 55](../sessions/2026-03-12-session55-step4-parity-bridges.md) |

## Session Log

| Session | Date | Summary |
|---------|------|---------|
| 48 | 2026-03-11 | [Initial native UI hacks](../sessions/2026-03-11-session48-native-ui-hacks.md) — Flow activation, PropAnim forcing, label alpha force |
| 49 | 2026-03-11 | [Camera fixes](../sessions/2026-03-11-session49-camera-fixes.md) — Scene camera selection, XYZ uniform bug, entering=true removal |
| 51 | 2026-03-11 | [Validation](../sessions/2026-03-11-session51-validation.md) — Screenshot A/B sweeps, GPU captures, flow tracing |
| 52 | 2026-03-12 | [Animation lifetime fix](../sessions/2026-03-12-session52-animation-lifetime.md) — Timer-based enter anim, transition waits, alpha hack removal |
| 53 | 2026-03-12 | [Step 2: PanelDir refactor](../sessions/2026-03-12-session53-step2-paneldir.md) — PropAnim forcing removed, flow activation narrowed to startMode=0 |
| 54 | 2026-03-12 | [Step 3: Visibility masks](../sessions/2026-03-12-session54-step3-visibility.md) — Overlay filter + voicetip hide removed; alpha floor + Kinect filter kept |
| 55 | 2026-03-12 | [Step 4: Parity bridges](../sessions/2026-03-12-session55-step4-parity-bridges.md) — PanelDir dir-hide removed; remaining bridges classified as permanent |
| 56 | 2026-03-12 | [Alpha floor + panel unload investigation](../sessions/2026-03-12-session56-alpha-floor-investigation.md) — Traced 29 zero-alpha meshes; confirmed panel unload hangs in FlowSwitchCase destruction |

## Unwind Plan

### Step 1: Restore animation lifetime (DONE)

- HamNavList enter animation runs via timer instead of dummy AnimTask
- UIManager transition waits re-enabled with frame-based timeouts
- Text alpha force hacks removed (alpha now driven by PropAnim forcing)

### Step 2: Stop synthetic panel entry (DONE)

- PropAnim end-frame forcing removed entirely (was a no-op after Step 1's timer fix)
- Flow activation narrowed: `startMode>0` flows auto-start via `Flow::Enter()`; only `startMode==0` (game-code-triggered) flows still get blanket-activated
- Remaining flow activation is necessary — `startMode=0` flows drive visual state that DTA enter scripts would trigger on Xbox

### Step 3: Remove visibility masks (PARTIAL)

- Overlay filter (small white texture check) removed — flows now animate PropAnim overlays correctly
- HelpBarPanel voice-tip hiding removed — redundant with MeshFilter
- **Remaining (still load-bearing):**
  - Zero-alpha floor — background meshes stay invisible without it (DTA scripts needed)
  - Kinect mesh filter — white opaque overlays appear without it

### Step 4: Revisit broad parity bridges (DONE)

- PanelDir Kinect/tutorial dir-hide block removed — redundant with MeshFilter
- Most other bridges remain native infrastructure:
  - Boot flow auto-advance (DTA handlers don't work on native)
  - Controller mode forcing (no Kinect gesture input)
  - Panel loading shortcuts (`UnloadPanels()` still too slow to re-enable)
  - Tutorial suppression (gesture UI conflicts with controller mode)

### Panel Unload Revalidation (NEW)

- `MILO_NATIVE_UNLOAD_PANELS=1` now forces the real `UIScreen::UnloadPanels()` path on native for direct testing.
- Forced-unload boot runs no longer reproduce the old "immediate ObjRef SIGSEGV" claim:
  - `attract_screen -> autosave_warning_screen` unloads cleanly
  - `autosave_warning_screen` itself enters and runs after unloading `attract_screen`
- The current blocker appears later on `autosave_warning_screen -> title_screen`:
  - `UIPanel::Unload()` reaches `RELEASE(mDir)` for `autosave_warning_panel`
  - teardown then spends 40s+ inside nested `ObjectDir::DeleteObjects()` / `PanelDir` deletion and does not finish within a 40-70s timeout
- The same issue is now isolated below the screen/panel layer:
  - `ObjectLifetimeTest.DeleteAutosaveWarningRawDir` loads `ui/title/gen/autosave_warning.milo_xbox` in ~2ms, then hangs on `delete dir`
  - `ObjectLifetimeTest.DeleteAutosavingIconSubdirOnly` detaches the `autosaving_icon` child dir and still hangs on `delete subdir`
- Current conclusion: the shortcut is still load-bearing, but the reason is now **pathological unload latency / deletion churn**, not a proven immediate ObjRef crash.

## Hack Inventory

### A. UI animation/state forcing

| File | Hack | Status |
|------|------|--------|
| `PanelDir.cpp` | Flow activation for `startMode=0` flows via `ShouldActivateNativeFlow()` | Narrowed (S53) — only game-code-triggered flows |
| ~~`PanelDir.cpp`~~ | ~~Force `"enter"` PropAnims to end frame (+ nested RndDirs)~~ | **Removed (S53)** |
| ~~`PanelDir.cpp`~~ | ~~Hide tutorial/gesture/silhouette subdirs on enter~~ | **Removed (S55)** — redundant with MeshFilter |
| ~~`HamListRibbon.cpp`~~ | ~~Label alpha force to 1.0~~ | **Removed (S52)** |
| ~~`HamNavList.cpp`~~ | ~~Native StopAnimation + frame reset~~ | **Replaced (S52)** |
| ~~`HamNavList.cpp`~~ | ~~Input bypass of IsAnimating check~~ | **Fixed (S52)** |
| `UIListMesh.cpp` | Temporarily force hidden template meshes visible | Keep |

### B. Renderer/material heuristics

| File | Hack | Status |
|------|------|--------|
| ~~`MaterialSetup.cpp`~~ | ~~Text-only alpha force~~ | **Removed (S52)** |
| `Mesh_Wgpu.cpp` | SrcAlpha zero-alpha floor (0.20) | Keep — still load-bearing (S54) |
| `MeshFilter.cpp` | Skip Kinect/speech/tutorial meshes | Keep — still load-bearing (S54) |
| ~~`MeshFilter.cpp`~~ | ~~Skip tiny white srcAlpha overlays~~ | **Removed (S54)** — flows now animate overlays |
| `MaterialSetup.cpp` | Auto-prelit, specular clamp, emissive guard, etc. | Keep (renderer heuristics) |
| `MeshGpuCache.cpp` | `FixZeroAlpha()` vertex alpha fix | Keep (compatibility) |

### C. Camera/composition

| File | Hack | Status |
|------|------|--------|
| `HamNavList.cpp` | Scene camera selection (`turbo_shell.cam`) | **Real fix** — keep |
| `Rnd_Wgpu.cpp` | XYZ camera change detection | **Real fix** — keep |
| `UI.cpp` | `MILO_UI_CAM_MODE` debug overrides | Debug-only |
| `UI.cpp` | Single-pass camera/environment selection | Architectural bridge |

### D. Controller/Kinect bypasses

| File | Hack | Status |
|------|------|--------|
| `GestureMgr_Native.cpp` | Force controller mode | Permanent — no Kinect |
| `HamScreen.cpp` | Force controller mode on first enter | Permanent — no Kinect |
| `ShellInput.cpp` | Skip Kinect init, never exit controller mode | Permanent — no Kinect |
| `CursorPanel.cpp` | Skip gesture cursor logic | Permanent — no Kinect |
| ~~`HelpBarPanel.cpp`~~ | ~~Hide voice-tip drawables~~ | **Removed (S54)** — redundant with MeshFilter |

### E. Screen/panel lifecycle

| File | Hack | Status |
|------|------|--------|
| `UI.cpp` | Auto-advance stuck boot/tutorial screens | Permanent — DTA handlers fail |
| ~~`UI.cpp`~~ | ~~Skip transition exit/enter waits~~ | **Replaced (S52)** — timeouts |
| `UI.cpp` | Set `mSink = current screen` | Permanent — input routing |
| `UIPanel.cpp` | Synchronous panel loading | Permanent — LoadMgr queue issue |
| `UIPanel.cpp` | Force-finish panels without loader | Permanent — no DLC/save state |
| `UIPanel.cpp` | Block tutorial panel enter | Permanent — gesture UI conflicts |
| `UIScreen.cpp` | Skip null panels in SetTypeDef | Permanent — defensive |
| `UIScreen.cpp` | Always load all panels | Keep for now — unload workaround still active |
| `UIScreen.cpp` | Hide previous screen instead of unload | Keep for now — forced unload no longer instantly crashes, but `autosave_warning_panel` teardown is still too slow |
| `UIScreen.cpp` | Skip tutorial panels on enter | Permanent — gesture UI conflicts |

## Follow-Up Items

- **Panel unload teardown**: the older Session 56 note that blamed `FlowSwitchCase` and hash-table deletion order is no longer the best-supported explanation. Current forced-unload runs show `UnloadPanels()` survives early transitions and later spends 40s+ in nested `ObjectDir::DeleteObjects()` during `autosave_warning_panel` teardown. The new minimal repro is lower-level: deleting `ui/title/gen/autosave_warning.milo_xbox` hangs even outside `UIPanel::Unload()`, and deleting the detached `autosaving_icon` subdir alone also hangs. Because `Hmx::Object::~Object()` already calls `ReplaceRefs(nullptr)` before free, plain dependency ordering is not yet proven to be the blocker. Treat this as an unresolved teardown-cost / producer-bug investigation, not a confirmed need for topological deletion.
- **Alpha floor refinement**: 29 meshes hit the zero-alpha floor (0.20) across 5 dirs. All are DTA-script-driven — no Flow objects target their material alpha. The `background` and `main_ribbon` dirs have NO associated flows at all; `letterbox` flows are filtered; `game_mode_icon` flows don't target alpha. Current floor is a reasonable permanent workaround. Could be refined with per-dir alpha values or by implementing a DTA property-set subset on native.
- **List widget draw traversal**: `list_choose_mode.milo` loads correctly (see `docs/sessions/NEXT_DTA_LIST_RENDERING.md`), but the populated list may not reach the renderer. Investigate whether `right_hand.hnl` is in the active drawable list and whether `HamNavList::DrawShowing()` is reached. Separate from the animation unwind — revisit after Step 4.

## References

- **Xbox reference**: `archive/screenshots/references/dc3_main_menu.jpg`
- **Session 56 screenshots**: `archive/screenshots/session56/`
- **Session 55 screenshots**: `archive/screenshots/session55/`
- **Session 54 screenshots**: `archive/screenshots/session54/`
- **Session 53 screenshots**: `archive/screenshots/session53/`
- **Session 52 screenshots**: `archive/screenshots/session52/`
- **Session 49 screenshots**: `archive/screenshots/session49/`
