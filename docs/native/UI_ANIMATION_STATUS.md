# UI Animation System — Native Port Status

## Current State

**Unwind plan complete.** All removable hacks removed. Remaining items are permanent native infrastructure.

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
- All other bridges classified as permanent native infrastructure:
  - Boot flow auto-advance (DTA handlers don't work on native)
  - Controller mode forcing (no Kinect gesture input)
  - Panel loading shortcuts (ObjRef lifecycle issues)
  - Tutorial suppression (gesture UI conflicts with controller mode)

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
| `UIScreen.cpp` | Always load all panels | Permanent — skipped UnloadPanels |
| `UIScreen.cpp` | Hide previous screen instead of unload | Permanent — ObjRef SIGSEGV |
| `UIScreen.cpp` | Skip tutorial panels on enter | Permanent — gesture UI conflicts |

## Follow-Up Items

- **List widget draw traversal**: `list_choose_mode.milo` loads correctly (see `docs/sessions/NEXT_DTA_LIST_RENDERING.md`), but the populated list may not reach the renderer. Investigate whether `right_hand.hnl` is in the active drawable list and whether `HamNavList::DrawShowing()` is reached. Separate from the animation unwind — revisit after Step 4.

## References

- **Xbox reference**: `archive/screenshots/references/dc3_main_menu.jpg`
- **Session 55 screenshots**: `archive/screenshots/session55/`
- **Session 54 screenshots**: `archive/screenshots/session54/`
- **Session 53 screenshots**: `archive/screenshots/session53/`
- **Session 52 screenshots**: `archive/screenshots/session52/`
- **Session 49 screenshots**: `archive/screenshots/session49/`
