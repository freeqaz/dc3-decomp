# UI Render Decomp Guide

This document identifies the most important shared-engine functions to decompile correctly for native UI rendering parity, and separates them from native-only renderer code.

## Core Point

The native port is already leveraging the original game UI engine for almost all high-value behavior:

- screen / panel orchestration
- list layout
- ribbon composition
- text layout and glyph generation
- helpbar / letterbox / controller-mode shell logic

The native renderer mostly takes the final draw requests and submits them through WebGPU.

If the shared-engine UI functions are wrong, native backend fixes alone will not recover retail layout.

## High-Level Render Path

Current shared-engine UI render flow:

1. [`HamUI::Draw()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/HamUI.cpp)
2. [`UIManager::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UI.cpp)
3. [`UIScreen::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIScreen.cpp)
4. [`UIPanel::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIPanel.cpp)
5. [`PanelDir::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/PanelDir.cpp)
6. [`RndDir::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/rndobj/Dir.cpp)
7. Panel-local drawables:
   - [`UIList::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIList.cpp)
   - [`UIListDir::BuildDrawState()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIListDir.cpp)
   - [`UIListDir::DrawWidgets()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIListDir.cpp)
   - [`HamNavList::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/hamobj/HamNavList.cpp)
   - [`UILabel::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UILabel.cpp)
   - [`RndText::UpdateText()`](/home/free/code/milohax/dc3-decomp/src/system/rndobj/Text.cpp)
   - [`RndText::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/rndobj/Text.cpp)
8. Native submission:
   - [`RndMesh::DrawShowing()`](/home/free/code/milohax/dc3-decomp/native/src/platform/Mesh_Wgpu.cpp)
   - [`WgpuRnd::EnsureSceneUniformsCurrent()`](/home/free/code/milohax/dc3-decomp/native/src/platform/Rnd_Wgpu.cpp)
   - transparent flush in [`Rnd_Wgpu.cpp`](/home/free/code/milohax/dc3-decomp/native/src/platform/Rnd_Wgpu.cpp)

## Tier 1: Must Match

These are the highest-value decompilation targets for retail UI parity.

### [`UIListDir::BuildDrawState()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIListDir.cpp)

This is likely the single most important pure layout function in the path.

It computes:

- per-element positions
- list fade behavior
- highlight position
- sublist offsets
- scrolling presentation

If this function is wrong, menu layout and highlight behavior will be wrong even if rendering is otherwise correct.

### [`UIListDir::SetElementPos()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIListDir.cpp)

This is the low-level position mapping primitive for list elements.

It turns logical list position into actual UI-space coordinates. Small mistakes here can shift entire lists, headers, and child widgets.

### [`UIList::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIList.cpp)

This feeds sublist offsets and current draw state into `UIListDir`.

If this is wrong, even a correct `BuildDrawState()` may be asked to lay out the wrong thing.

### [`HamNavList::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/hamobj/HamNavList.cpp)

This is the DC3-specific list composition layer.

It ties list state to:

- ribbons
- headers
- swell / scroll animation state
- final widget drawing

This is one of the main functions that makes DC3 menu composition look like DC3 rather than generic Milo UI.

### [`PanelDir::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/PanelDir.cpp)

This is the camera / environment boundary for panels.

It controls:

- camera override selection
- panel-local environment selection
- back / main / front panel ordering

If this is wrong, panel-local drawables may still be individually correct but appear in the wrong camera pass or wrong composition order.

### [`HamUI::Draw()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/HamUI.cpp)

This controls the shell draw order:

- main UI
- overlay
- helpbar
- letterbox
- blacklight
- debug
- shell input

If this function is wrong, the app can render the right individual assets in the wrong overall order.

### [`UIManager::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UI.cpp)

This is the top-level UI pass entry point.

It is the main shared-engine boundary between screen selection and actual panel drawing.

### [`UILabel::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UILabel.cpp)
### [`RndText::UpdateText()`](/home/free/code/milohax/dc3-decomp/src/system/rndobj/Text.cpp)
### [`RndText::DrawShowing()`](/home/free/code/milohax/dc3-decomp/src/system/rndobj/Text.cpp)

These are the critical text functions.

They control:

- localized / formatted label content
- style colors
- text wrapping / scrolling / fitting
- glyph mesh construction
- final text draw submission

If the app has “text exists but is misplaced / unreadable / missing in certain states,” these functions remain top-tier suspects.

## Tier 2: Important Shared-Engine Functions

These are not as central as Tier 1, but they strongly affect what UI should appear on screen.

### [`UIScreen::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIScreen.cpp)
### [`UIPanel::Draw()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIPanel.cpp)

These gate which panels actually draw and in which pass.

### [`UIListDir::DrawWidgets()`](/home/free/code/milohax/dc3-decomp/src/system/ui/UIListDir.cpp)

This is the bridge between computed list draw state and actual widget drawing.

### [`ShellInput::SyncToCurrentScreen()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/ShellInput.cpp)
### [`ShellInput::EnterControllerMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/ShellInput.cpp)

These are major shell-state functions. They do not directly place geometry, but they decide which shell/helpbar/controller-mode state should be active.

### [`HelpBarPanel::Draw()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/HelpBarPanel.cpp)
### [`HelpBarPanel::EnterControllerMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/HelpBarPanel.cpp)
### [`HelpBarPanel::SyncToPanel()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/HelpBarPanel.cpp)

These matter because the helpbar is one of the most obvious broken/missing parts of current native composition.

### [`LetterboxPanel::Draw()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/LetterboxPanel.cpp)
### [`LetterboxPanel::EnterBlacklightMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/LetterboxPanel.cpp)
### [`LetterboxPanel::ExitBlacklightMode()`](/home/free/code/milohax/dc3-decomp/src/lazer/meta_ham/LetterboxPanel.cpp)

These are important for shell framing and blacklight-mode presentation.

## Tier 3: Native Backend Functions

These matter for correctness, but they are mostly renderer/backend plumbing rather than UI layout authority.

### [`RndMesh::DrawShowing()`](/home/free/code/milohax/dc3-decomp/native/src/platform/Mesh_Wgpu.cpp)

This is the native backend mesh submission point.

It handles:

- visibility checks
- transparent deferral
- immediate draw for text meshes
- material/pipeline selection

### [`WgpuRnd::EnsureSceneUniformsCurrent()`](/home/free/code/milohax/dc3-decomp/native/src/platform/Rnd_Wgpu.cpp)

This keeps camera/environment scene uniforms current across mixed camera passes.

### Transparent queue / flush logic in [`Mesh_Wgpu.cpp`](/home/free/code/milohax/dc3-decomp/native/src/platform/Mesh_Wgpu.cpp) and [`Rnd_Wgpu.cpp`](/home/free/code/milohax/dc3-decomp/native/src/platform/Rnd_Wgpu.cpp)

This matters for composition correctness, but it is still downstream of the game deciding what to draw and where.

## What Native Is Leveraging Today

For UI rendering, native is already leveraging these shared systems:

- `src/system/ui` for screens, panels, lists, labels, and widget draw state
- `src/lazer/meta_ham` for DC3 shell composition, helpbar, letterbox, overlay, controller mode
- `src/system/hamobj` for DC3-specific ribbon/list presentation
- `src/system/rndobj/Text.cpp` for text layout and glyph generation

Native-specific code is mostly responsible for:

- GPU draw submission
- pipeline state selection
- scene uniform uploads
- transparent queue management
- screenshot / frame-capture diagnostics

That means decomp priority should stay centered on shared-engine functions first, not on replacing more behavior in native code.

## Current Status (2026-03-10)

### Tier 1

| Function | Match% | Status |
|----------|--------|--------|
| `UIListDir::BuildDrawState` | WIP | Being worked on separately |
| `UIListDir::SetElementPos` | **100%** | Complete |
| `UIList::DrawShowing` | **90.1%** | Improved from 84.3% (struct fix + mScrolling param + float init) |
| `HamNavList::DrawShowing` | **90.7%** | AT_LIMIT (register swaps, scheduling). Improved from 64.5% (IsScrolling reorder, kFocused literal, unsigned mElemDrawState, float unk24 store) |
| `PanelDir::DrawShowing` | **100%** | Complete |
| `HamUI::Draw` | **100%** | Complete |
| `UIManager::Draw` | **100%** | Complete |
| `UILabel::DrawShowing` | 95.6% | AT_LIMIT (Style() scheduling, cmpwi vs cmplwi) |
| `RndText::UpdateText` | **95.0%** | AT_LIMIT (ObjPtr offset folding, store scheduling) |
| `RndText::DrawShowing` | 70.1% | AT_LIMIT (address relocation noise, behaviorally equivalent) |

### Tier 2

| Function | Match% | Status |
|----------|--------|--------|
| `UIScreen::Draw` | **100%** | Complete |
| `UIPanel::Draw` | **100%** | Complete |
| `UIListDir::DrawWidgets` | **100%** | Complete |
| `ShellInput::SyncToCurrentScreen` | **100%** | Complete |
| `HelpBarPanel::Draw` | **100%** | Complete |
| `HelpBarPanel::EnterControllerMode` | **100%** | Complete |
| `HelpBarPanel::SyncToPanel` | **100%** | Complete |
| `LetterboxPanel::Draw` | **100%** | Complete |
| `LetterboxPanel::EnterBlacklightMode` | **100%** | Complete |
| `LetterboxPanel::ExitBlacklightMode` | **100%** | Complete |

### Key Fixes (Session 41)

**UIList struct layout fix** — Discovered UIList.h had incorrect member ordering:
- Reordered to: `mDrawManuallyControlledWidgets` (0x15c), `mAllowHighlight` (0x15d), `mLimitCircularDisplayNumToDataNum` (0x15e), `mUncappedNumDisplay` (0x160), `mScrolling` (0x164)
- **UIList::PostLoad**: 76.7% → **100%**

**UIList::DrawShowing source fixes**:
- Changed `false` → `mScrolling` as last param to `BuildDrawState()` (target passes scroll state)
- Changed `float offset = 0.0f` to deferred if/else init (target initializes 0.0f in else branch only)
- Combined with struct fix: 84.3% → **90.1%**

## Remaining Work

Functions still needing improvement:
1. `UIListDir::BuildDrawState` (WIP, separate effort)
2. `UIList::DrawShowing` (90.1% — register cascade, ICF symbol noise)
3. ~~`UIListDir::DrawWidgets`~~ (100% — Complete)
4. ~~`RndText::UpdateText`~~ (95.0% — AT_LIMIT, ObjPtr offset folding, store scheduling)
5. ~~`RndText::DrawShowing`~~ (70.1% — AT_LIMIT, addr reloc noise, behaviorally equivalent)
6. `HamNavList::DrawShowing` (64.5% — register swaps, bool mask)

## Practical Rule

If a problem looks like:

- wrong menu placement
- wrong highlight position
- wrong list spacing
- missing or misframed helpbar / shell chrome
- text present but positioned badly

then the first assumption should be: shared-engine UI logic is still wrong or incomplete.

If a problem looks like:

- wrong blend behavior
- wrong transparency ordering
- wrong camera uniform at draw time
- text meshes created correctly but rendered with the wrong GPU state

then the first assumption should be: native backend logic is wrong.
