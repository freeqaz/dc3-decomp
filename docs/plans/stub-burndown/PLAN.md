# Stub Burndown Plan

Resolve the ~733 ALTERNATENAME stubs in `link_glue.cpp` by implementing the
actual functions in their correct source files.

## Background

Each stub is a `#pragma comment(linker, "/ALTERNATENAME:...")` line that
redirects an unresolved symbol to a no-op function. These exist so the linker
succeeds without real implementations. The functions were previously marked
COMPLETE in the orchestrator DB (since the *linker* symbol resolved), but have
been reset because they have no source-level implementation.

**Impact**: Resolving all 733 stubs would push COMPLETE from ~94.4% to ~96.6%.

## What NOT to Work On

1. **`link_glue` unit (43 functions)** — Template instantiations (`PropSync<T>`,
   `operator<<(BinStream, ObjPtrVec<T>)`, etc.) that get emitted when the TU
   that *uses* them is compiled. These auto-resolve as parent units are
   implemented. Work them last if any remain.

2. **SDK/external stubs (~21)** — `BinkClose`, `D3DTexture_*`, `wmemcpy`, etc.
   These reference external Xbox SDK libraries. Leave as stubs permanently.

3. **`??__E` dynamic initializers** — Auto-resolve when the parent TU's static
   variables are correctly defined. Not regular functions you implement.

## Priority Tiers

### Tier 1: Quick Wins — Single-Stub Units (~33 units)

Units that become 100% complete with a single function. Highest ROI.

| Unit | Function |
|------|----------|
| system/char/CharLipSyncDriver | `CharLipSyncDriver::Poll` |
| system/char/ClipCollide | `Transform::LookAt` |
| system/char/FileMerger | `ObjDirPtr<ObjectDir>::ObjDirPtr` |
| system/char/FileMergerOrganizer | `FileMergerSort::operator()` |
| system/char/CharClip | `CharClip::Transitions::AddNode` |
| system/flow/FlowSound | `FlowSound::OnMarkerEvent` |
| system/flow/FlowMultiSetProperty | `ObjPtrVec<Hmx::Object>::unique` |
| system/flow/FlowSlider | `FlowSlider::UpdateActivations` |
| system/flow/DrivenPropertyEntry | `DrivenPropertyEntry::Load` |
| system/hamobj/HamListRibbon | `HamListRibbon::EndFrame` |
| system/hamobj/HamScrollSpeedIndicator | `HamScrollSpeedIndicator::Update` |
| system/hamobj/HamMove | `HamMove::PSNRToDetectFrac` |
| system/hamobj/HamRibbon | `HamRibbon::UpdateChase` |
| system/hamobj/HollaBackMinigame | `HollaBackMinigame::OnBeat` |
| system/hamobj/FilterQueue | `FilterQueue::Poll` |
| system/midi/MidiReader | `pow(float, int)` |
| system/obj/Dir | `DirLoader::New` |
| system/os/File | `FileRecursePattern` |
| system/os/UsbMidiKeyboard | `UsbMidiKeyboard::GetSustain` |
| system/os/HolmesKeyboard | `HolmesInput::SendJoypadMessages` |
| system/rndobj/PropAnim | `RndPropAnim::ForeachKeyframe` |
| system/rndobj/DOFProc_NG | `NgDOFProc::DoPost` |
| system/rndobj/MetaMaterial | `MetaMaterial::IsEquivalent` |
| system/rndobj/TexBlender | `RndTexBlender::DrawShowing` |
| system/synth/MidiInstrument | `MidiInstrument::SynthPoll` |
| system/synth/Emitter | `SynthEmitter::Poll` |
| system/synth/Utl | `CacheWav` |
| system/ui/UILabel | `UILabel::LabelStyle::~LabelStyle` |
| system/ui/LabelShrinkWrapper | `LabelShrinkWrapper::UpdateAndDrawWrapper` |
| system/utl/Loader | `LoadMgr::PollFrontLoader` |
| system/utl/Song | `Song::SyncState` |
| system/world/CameraManager | `CameraManager::Poll` |
| lazer/meta_ham/MetaPerformer | `MetaPerformer::CheckRecommendedPracticeMove` |

### Tier 2: Small Units (2-5 stubs, Milo engine with RB3 refs)

Good batch targets — complete a unit in one session.

| Unit | Stubs | Notes |
|------|-------|-------|
| system/char/CharBonesMeshes | 2 | Shared Milo |
| system/char/CharLipSync | 2 | Shared Milo |
| system/char/CharClipGroup | 5 | Shared Milo |
| system/char/CharDriver | 8 | Shared Milo |
| system/char/Character | 6 | Core class, RB3 refs |
| system/rndobj/Font3d | 9 | Rendering |
| system/rndobj/Rnd | 9 | Core renderer |
| system/world/SpotlightDrawer | 9 | Rendering |
| system/world/LightPreset | 11 | Rendering |
| system/world/Crowd | 7 | Scene |
| system/synth/Sfx | 9 | Audio |
| system/synth/StandardStream | 7 | Audio |
| system/hamobj/HamDirector | 7 | Game logic |
| system/hamobj/HamCamShot | 7 | Game logic |
| system/hamobj/DanceRemixer | 7 | Game logic |

### Tier 3: Medium Units (10-30 stubs, bulk work)

| Unit | Stubs | Notes |
|------|-------|-------|
| system/rndobj/Utl | 28 | Rendering utilities |
| system/rndobj/Shader | 24 | Shader system |
| system/rndobj/Text | 16 | Text rendering |
| system/rnddx9/Tex | 13 | DX9 textures |
| system/rnddx9/ShaderMgr | 13 | DX9 shader management |
| system/rnddx9/Mesh | 9 | DX9 mesh |
| system/hamobj/HamNavList | 24 | Navigation UI |
| lazer/meta_ham/PlaylistSortMgr | 10 | Meta/UI |
| lazer/meta_ham/HamStorePanel | 9 | Store UI |

### Tier 4: Platform-Heavy / Hard (defer)

Xbox-specific with no RB3 reference. Lower priority.

| Unit | Stubs | Notes |
|------|-------|-------|
| system/os/PlatformMgr_Xbox | 25 | Xbox platform APIs |
| system/moviebink/BinkMovieImpl | 19 | Bink video SDK |
| system/synth_xbox/Mic | 18 | Xbox audio capture |
| system/gesture/LiveCameraInput | 17 | Kinect input |
| system/synth_xbox/Voice | 14 | Xbox voice |
| system/synth_xbox/PitchCorrectedVoice | 8 | Voice processing |
| system/gesture/SkeletonClip | 8 | Kinect skeleton |

## Workflow Per Function

```
1. Decompile (Ghidra)           → /ghidra-decompile <symbol>
2. Check RB3 reference          → lookup_rb3(symbol)
3. Write implementation         → in src/<unit>.cpp
4. Build + diff                 → run_objdiff(symbol, project_dir=".")
5. Iterate until matched        → (or mark AT_LIMIT if unfixable)
6. Remove stub from link_glue   → delete the ALTERNATENAME line
7. Verify link                  → ninja link (optional, batch at end)
8. Report result                → report_result(symbol, ...)
```

## Batch Strategy

- **Start each session** with `batch_check(unit_pattern)` to auto-detect any
  stubs that already match from existing code
- **Process functions by size** within each unit (smallest first)
- **Use parallel subagents** for independent units (see
  `docs/decomp/SUBAGENT_STRATEGY.md`)
- A focused session can do **20-30 trivial stubs** or **5-10 medium functions**

## Finding Functions

```python
# By unit (best approach)
query_functions(unit_pattern="system/rndobj/Shader", status="workable")

# Batch-check a unit (auto-reports 100% matches)
batch_check(unit_pattern="system/rndobj/Utl")
```

All workable functions currently have `verdict_reason='reset: was
COMPLETE+is_stub (no source impl)'`.

## Reference

- [2026-02-28 stub burndown session](../../sessions/2026-02-28-stub-burndown-data-stubs.md) — original investigation that identified the reset stubs and produced the data behind this plan

## Numbers

| Category | Count |
|----------|-------|
| Total workable stubs | 733 |
| Tier 1 (single-stub units) | ~33 |
| Tier 2 (2-5 stubs) | ~15 units, ~90 functions |
| Tier 3 (10-30 stubs) | ~9 units, ~145 functions |
| Tier 4 (platform-heavy) | ~7 units, ~109 functions |
| link_glue templates (skip) | 43 |
| Spread across other units | ~313 |
