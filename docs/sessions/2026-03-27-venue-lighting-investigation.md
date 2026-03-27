# Venue Lighting Investigation — 2026-03-27

## Summary

Investigated why venue lighting (light-catcher overlay meshes, TV screens, dynamic lighting transitions) doesn't work on native/web. Found three distinct rendering bugs and a major architectural discovery about how DC3's lighting system differs from RB3.

Follow-up investigation (2026-03-27, post-cascade-fix) confirmed that the **Enter/Poll pipeline is intact** — venue Flows, PropAnims, and the LightPresetManager all receive Enter() and Poll() correctly. The problem is in the **rendering layer**: PropAnim-driven property changes on RndLight/RndEnviron don't propagate to the GPU.

## White Block Bugs Found & Fixed

### 1. Light-catcher overlay meshes (Rink_overlay.mesh, StageCarpet_lightCatcher.mesh)
- **Root cause**: Multiply-blend materials with white color and no texture. Without DTA lighting scripts driving the color, the shader's lighting pipeline produces values > 1.0, which through multiply blend brightens the framebuffer → white blocks.
- **Fix**: Force multiply-blend materials to prelit mode in `MaterialSetup.cpp`. This skips lighting calculations and outputs `baseColor` directly. White × multiply = identity = invisible. Correct when DTA scripts aren't running.
- **Files**: `native/src/platform/MaterialSetup.cpp`, `native/src/platform/MeshFilter.cpp`

### 2. Screen.mat venue TV meshes (TopFloor_Back4, foodCourt4, Arcade_A.2)
- **Root cause**: Two sub-issues:
  - Failed texture upload fallback used `WhiteTexView` with `useTexture=0.0` — shader skipped texture sampling, used white material color
  - `Arcade_A_Screen.mat` has NULL diffuse (ScreenRecorder would assign at runtime on Xbox)
- **Fix**: Use `BlackTexView` with `useTexture=1.0` for failed uploads and screen materials with no texture. TVs render as dark "off" screens.
- **Files**: `native/src/platform/MaterialSetup.cpp`, `native/src/platform/MeshFilter.cpp`

### 3. Render target alpha initialization
- **Root cause**: `EnsureRenderTargetData` cleared new textures to `RGBA(0,0,0,0)` — transparent. With SrcAlpha blending, transparent pixels let the white HTML canvas bleed through on web.
- **Fix**: Set alpha channel to 255 (opaque) in the initial clear.
- **Files**: `native/src/platform/Tex_Wgpu.cpp`

## DC3 Lighting Architecture Discovery

### `world_event` PropKeys Do NOT Exist in DC3

The previous assumption was that `world_event` PropKeys in `song.anim` drive venue lighting (the RB3 model). After thorough investigation:

- **Zero DC3 songs** ship with `world_event` PropKeys. Searched all 65+ songs across all difficulty milos.
- The `world_event` property exists in code (inherited from RB3's engine) but was never used by DC3 content.
- RB3's `BandDirector` dynamically creates `world_event` keys from MIDI VENUE track data. DC3's `HamDirector` does not.
- DC3 MIDIs have only 3 tracks: song name, EVENTS, DRUMS. No VENUE or LIGHTING track.

### How DC3 Venue Lighting Actually Works

DC3 uses a completely different lighting pipeline from RB3:

| Aspect | RB3 | DC3 |
|--------|-----|-----|
| Trigger source | MIDI VENUE track → `world_event` PropKeys | Performance state (`low`/`high`/`awesome`) from scoring system |
| Event mapping | `BandDirector::OnFileLoaded` creates keys dynamically | No dynamic key creation |
| Lighting control | `LightPresetMgr::Interp(catA, catB, blend)` | `force_preset` messages from venue Flow system |
| State machine | Song-synced (PropKeys on timeline) | Venue-internal FlowAnimate objects (`performance.flow`, `battle_env.flow`) |
| MIDI tracks | VENUE track with note→lighting mappings | Only DRUMS for drum triggers |
| LightPresetManager | Full implementation (Interp, SetLighting, SelectPreset, etc.) | Stripped down (no Interp, no SetLighting, no SelectPreset) |

### DC3 Venue Lighting Chain (Xbox)

```
Scoring System
  → HamProvider::performance_state (low/high/awesome)
    → Venue Flow system (performance.flow, battle_env.flow)
      → force_preset message to LightPresetManager
        → LightPreset::Animate() drives lights/spotlights/environment
          → LightPresetManager::Poll() advances blend
```

Separately, venue-internal PropAnims run continuously:
```
Venue Enter
  → FlowAnimate::Start (start.flow)
    → PropAnims: env_char_lighting.anim, env_lights_environs.anim, environments_master.anim
      → Drive light colors, spotlight intensities, fog parameters
```

### DC3 Venues Have NO LightPreset Objects

Confirmed via binary inspection of `rollerrink.milo_xbox`: zero `LightPreset` class instances exist in the venue data. The `LightPresetManager` is effectively unused in DC3 venues. All lighting is driven by Flow objects and PropAnims directly.

## Pipeline Verification (2026-03-27)

### Enter Chain — CONFIRMED WORKING

```
UIPanel::Enter() [world_panel]
  → WorldDir::Enter() [parent world]
    → LightPresetManager::Enter() [parent — no-op, no presets]
    → PanelDir::Enter()
      → RndDir::Enter() → iterates mPolls/mEnters
        → HamDirector::Enter()
          → VenueEnter(mVenue)
            → mVenue->Enter() → WorldDir::Enter() [venue]
              → LightPresetManager::Enter() [venue — no-op, no presets]
              → PanelDir::Enter() [venue]
                → RndDir::Enter() → iterates venue mPolls/mEnters
                  → Flow::Enter() on each Flow → auto-starts mStartMode>0 flows
                → PanelDir native block → activates mStartMode==0 flows via ShouldActivateNativeFlow
```

- `HamDirector::VenueEnter()` at `HamDirector.cpp:688-694`
- `WorldDir::Enter()` at `Dir.cpp:592-615`
- `PanelDir::Enter()` at `PanelDir.cpp:423-453` (native flow activation)
- `ShouldActivateNativeFlow()` at `PanelDir.cpp:97-158` — venue flows pass the filter

### Poll Chain — CONFIRMED WORKING

```
UIPanel::Poll() [world_panel]
  → WorldDir::Poll() [parent]
    → RndDir::Poll() → iterates mPolls
      → HamDirector::Poll()
        → mVenue->Poll()
          → WorldDir::Poll() [venue — takes shortcut path since TheWorld is set]
            → RndDir::Poll() [venue children polled normally]
```

- Venue `mLightPresetMgr.Poll()` is intentionally skipped in the shortcut path (same on Xbox)
- Flow/PropAnim AnimTasks are polled globally via `TheTaskMgr`, not through the venue's Poll chain

### Flow Activation — CONFIRMED WORKING

Venue flows (`performance.flow`, `battle_env.flow`, `intro.flow`) pass `ShouldActivateNativeFlow`:
- No skip tokens match ("hide", "exit", "deactivate", etc.)
- Default filter returns true for non-"letterbox" dirs

### Not Related to HUD Cascade Fix

The ~ObjectDir cascade fix (commit d41f5bf72) and HUD workaround removal (commit c340c71ca) only affected the `game_hud` merger path in `OnFileMerged`. The venue loads through a completely separate path:
- `OnFileLoaded("venue")` at `HamDirector.cpp:1314` — stores venue as `mVenue`
- Venue uses proxy mode (not reparented), so the cascade fix is irrelevant
- VenueEnter/Poll were never touched by the HUD commits

## Roadmap: Venue Lighting Convergence (Revised)

~~Phase 1-3 from the original roadmap are largely unnecessary~~ — the Enter/Poll pipeline works, and LightPresets don't exist in DC3 venues. The real work is in the rendering layer.

### Phase 1: PropAnim Target Resolution (Critical)
**Goal**: Verify that venue PropAnim targets resolve to live objects.

- [ ] **Enumerate venue PropAnims**: List all PropAnim objects in a loaded venue via HTTP debug
- [ ] **Check target resolution**: For each PropAnim (e.g., `env_char_lighting.anim`), verify its PropKeys' `Target()` pointers are non-null and point to valid RndLight/RndEnviron objects
- [ ] **Check ObjPtr fallback**: During venue deserialization, PropKey targets are ObjPtrs that need to resolve within the venue dir. If resolution fails, animations run but affect nothing.
- [ ] **Test via HTTP debug**: `{$venue find "env_char_lighting.anim" list_keys}` — do targets exist?

### Phase 2: RndLight/RndEnviron GPU Propagation (Critical)
**Goal**: Property changes on RndLight/RndEnviron reach the GPU uniform buffer.

- [ ] **Trace RndLight::SetColor**: When a PropAnim sets a light's color, does `WgpuRnd::WriteSceneUniforms()` pick it up?
- [ ] **Trace RndEnviron ambient/fog**: PropAnims drive `mAmbientColor`, `mFogColor`, etc. — verify these are read by the shader
- [ ] **Check light list**: `RndEnviron::mLights` and `RndEnviron::mRealLights` — are venue lights registered?
- [ ] **GPU capture**: Use gpu-capture skill to capture a frame and inspect the light uniform buffer

### Phase 3: Spotlight Rendering (High Priority)
**Goal**: Venue spotlights render correctly.

- [ ] **Check SpotlightDrawer**: Is spotlight rendering implemented in the WGPU renderer?
- [ ] **Enumerate spotlights**: Venue data has objects like `mirrorball_spotlight_01.lit` — verify they load
- [ ] **Spotlight pipeline**: Spotlights need their own draw pass (separate from mesh rendering)

### Phase 4: Performance State Propagation (Medium Priority)
**Goal**: Scoring system drives venue lighting transitions via Flow.

- [ ] **Trace performance_state**: Add telemetry to confirm `performance_state` is set during gameplay
- [ ] **HamProvider → venue Flow**: Verify state changes propagate to `performance.flow` listeners
- [ ] **Flow state transitions**: When state changes, does the flow trigger `force_preset` or activate new PropAnims?

### Phase 5: Light-Catcher Material Integration (Low Priority)
**Goal**: Remove the multiply-blend prelit workaround.

- [ ] **Requires Phases 1-3**: Light-catchers only look correct when PropAnims drive material colors via lights
- [ ] **Verify material color drive**: When a PropAnim animates a light, check if light-catcher materials respond
- [ ] **Remove prelit force**: Once colors are properly driven, multiply blend with driven values < 1.0 creates correct shadow effects
- [ ] **Test across venues**: Each venue has different light-catcher configurations

### Phase 6: PostProc Transitions (Low Priority)
**Goal**: Post-processing changes with lighting states.

- [ ] **`postproc` PropKeys exist**: song.anim has `postproc` keys — verify they set `RndPostProc::Current()`
- [ ] **PostProc switching**: When postproc symbol changes, the active PostProc object should switch
- [ ] **Blend between presets**: PostProc transitions should blend smoothly

## Testing Strategy

### HTTP Debug Server Tests
The test at `scripts/tests/test_lighting_events.sh` covers the event dispatch chain. Extend it to:
- Enumerate venue PropAnims and check target resolution
- Query venue Flow objects and their state
- Manually trigger flow transitions and check if light properties change
- Set `performance_state` and verify flow transitions

### GPU Capture Analysis
- Capture frames with gpu-capture, inspect with gpu-inspect
- Check light uniform buffer contents — are venue lights present?
- Compare scene uniforms between "no venue lighting" and "with venue lighting" frames

### Visual Regression
- Capture screenshots at known frames with specific camera angles
- Compare native vs Xbox reference footage for lighting quality
- Track light-catcher visibility as a regression indicator

## Key Files Reference

| File | Role |
|------|------|
| `src/system/hamobj/HamDirector.cpp:688-694` | VenueEnter — calls mVenue->Enter() |
| `src/system/hamobj/HamDirector.cpp:3225-3226` | Venue Poll — calls mVenue->Poll() |
| `src/system/hamobj/HamDirector.cpp:1314-1315` | Venue assignment in OnFileLoaded |
| `src/system/world/Dir.cpp:554-590` | WorldDir::Poll — venue takes shortcut path |
| `src/system/world/Dir.cpp:592-615` | WorldDir::Enter — enters LPM, CameraMgr, PanelDir |
| `src/system/ui/PanelDir.cpp:423-453` | PanelDir::Enter — native flow activation |
| `src/system/ui/PanelDir.cpp:97-158` | ShouldActivateNativeFlow filter |
| `src/system/flow/Flow.cpp:465-474` | Flow::Enter — auto-starts mStartMode>0 |
| `src/system/flow/FlowAnimate.cpp:133-182` | FlowAnimate::Activate — creates AnimTasks |
| `src/system/rndobj/PropAnim.cpp` | Property animation, SetFrame, key evaluation |
| `src/system/world/LightPreset.cpp` | Preset animation (unused — no presets in DC3 venues) |
| `src/system/world/LightPresetManager.cpp` | Preset selection/blending (unused in DC3 venues) |
| `native/src/platform/MaterialSetup.cpp` | Material setup (prelit workaround lives here) |
| `native/src/platform/Rnd_Wgpu.cpp:WriteSceneUniforms` | GPU uniform buffer — where light data reaches the shader |
| `orig-assets/extracted/(..)/(..)/system/run/ham/ham_objects.dta` | world_event property definition (unused in DC3) |
