# Stub Burndown Plan

Implement ~2,429 unimplemented game/engine functions flagged as `is_stub=1` in the
orchestrator database.

## Background

A full `sync_objdiff.py --all` scan found **3,885 functions** where the decomp
.obj has no code (`base_size=0`). Of these, **~2,435 are game/engine methods**
flagged `is_stub=1`. The rest (STL templates, destructors, etc.) auto-resolve as
game methods get implemented.

These functions previously had false COMPLETE verdicts (the linker resolved them
via ALTERNATENAME stubs) and have been reset so they appear as workable.

**Note:** Many of these are likely **false negatives** — they already have
matching source code but were flagged because `sync_objdiff.py` couldn't detect
the existing implementation. Running `batch_check` on a unit will auto-resolve
these. Early testing found 29 of 33 "reset" stubs in Tier 1 were already
matching.

### ALTERNATENAME Stubs (separate concern)

There are ~246 ALTERNATENAME stubs in `link_glue.cpp` — a separate, non-overlapping
set that prevents link errors. These include 72 `??__E` dynamic initializers,
31 SDK/C stubs, 28 templates, and 115 C++ functions. These are a linking concern,
not an objdiff concern, and are handled separately.

## What NOT to Work On

1. **`link_glue` unit (34 functions)** — Template instantiations that get emitted
   when the TU that *uses* them is compiled. Auto-resolve as parent units are
   implemented.

2. **SDK/external stubs** — `BinkClose`, `D3DTexture_*`, `wmemcpy`, etc. Leave
   as stubs permanently.

3. **`??__E` dynamic initializers** — Auto-resolve when the parent TU's static
   variables are correctly defined.

4. **`lib/binkxenon/*`** — Third-party Bink video library (62+ stubs in
   `binkread` alone). Not worth decompiling.

5. **`system/os/PlatformMgr_Xbox` (88 stubs)** — Xbox platform APIs with no RB3
   reference. Defer indefinitely.

## Querying Stubs

```python
# Find all workable stubs
query_functions(is_stub=True, status="workable")

# Find stubs in a specific unit
query_functions(is_stub=True, unit_pattern="system/rndobj/*")

# Batch-check a unit (auto-resolves false negatives)
batch_check(unit_pattern="system/rndobj/Shader")
```

## Priority Tiers

### Summary

| Tier | Description | Units | Functions |
|------|-------------|-------|-----------|
| 1 | Single-stub units | 147 | 147 |
| 2 | Small units (2-5 stubs) | 186 | 573 |
| 3 | Medium units (6-15 stubs) | 101 | 897 |
| 4 | Large units (16-30 stubs) | 18 | 353 |
| 5 | Very large (31+ stubs) | 10 | 459 |
| **Total** | | **462** | **2,429** |

### Tier 1: Quick Wins — Single-Stub Units (147 units)

Units that become 100% complete with a single function. Highest ROI.
Split by category: 116 Milo engine, 28 DC3 game, 3 library.

Sample (run `query_functions(is_stub=True)` with unit pattern for full list):

| Unit | Function |
|------|----------|
| system/char/CharClip | `CharClip::Transitions::AddNode` |
| system/char/CharCollide | `CharCollide::Highlight` |
| system/char/CharForeTwist | `CharForeTwist::Poll` |
| system/char/ClipCollide | `Transform::LookAt` |
| system/char/FileMerger | `ObjDirPtr<ObjectDir>::ObjDirPtr` |
| system/flow/FlowSound | `FlowSound::OnMarkerEvent` |
| system/flow/FlowSequence | `FlowSequence::Activate` |
| system/flow/FlowSwitchCase | `FlowSwitchCase::IsValidCase` |
| system/rndobj/PropAnim | `RndPropAnim::ForeachKeyframe` |
| system/utl/Loader | `LoadMgr::PollFrontLoader` |
| system/world/CameraManager | `CameraManager::Poll` |
| lazer/meta_ham/MetaPerformer | `MetaPerformer::CheckRecommendedPracticeMove` |

### Tier 2: Small Units (2-5 stubs, 186 units)

Good batch targets — complete a unit in one session.

| Unit | Stubs | Notes |
|------|-------|-------|
| system/char/CharBoneOffset | 2 | Shared Milo |
| system/char/CharLipSyncDriver | 2 | Shared Milo |
| system/char/CharSignalApplier | 2 | Shared Milo |
| system/flow/DrivenPropertyEntry | 2 | Flow system |
| system/flow/FlowManager | 2 | Flow system |
| system/flow/FlowSwitch | 2 | Flow system |
| system/hamobj/FilterQueue | 2 | Game logic |
| system/hamobj/HamAudio | 2 | Audio |
| system/obj/Task | 2 | Core |
| system/rndobj/AnimFilter | 2 | Rendering |
| system/rndobj/Mat | 2 | Rendering |
| system/rndobj/PropAnim | 2 | Rendering |
| system/rndobj/PropKeys | 2 | Rendering |
| system/rndobj/Wind | 2 | Rendering |
| system/synth/MidiInstrument | 2 | Audio |
| system/ui/UILabel | 2 | UI |
| system/utl/Loader | 2 | Core |
| system/world/Dir | 2 | World |
| system/world/FreeCamera | 2 | World |
| system/char/CharClipGroup | 3 | Shared Milo |
| system/hamobj/HamMove | 3 | Game logic |
| system/hamobj/MoveAsyncDetector | 3 | Game logic |
| system/rndobj/Env | 4 | Rendering |
| system/rndobj/MultiMesh | 4 | Rendering |
| system/char/CharBones | 5 | Shared Milo |
| system/char/CharHair | 5 | Shared Milo |

### Tier 3: Medium Units (6-15 stubs, 101 units)

Bulk work, best with parallel subagents.

| Unit | Stubs | Notes |
|------|-------|-------|
| system/rndobj/Font3d | 14 | Rendering |
| system/rndobj/Rnd | 11 | Core renderer |
| system/rnddx9/ShaderMgr | 14 | DX9 shaders |
| system/rnddx9/Tex | 16 | DX9 textures |
| system/world/SpotlightDrawer | 13 | Rendering |
| system/world/SpotlightDrawer_NG | 15 | NG rendering |
| system/world/Crowd | 14 | Scene |
| system/world/LightPreset | 20 | Rendering |
| system/char/CharDriver | 15 | Shared Milo |
| system/char/Character | 17 | Core class, RB3 refs |
| system/char/CharEyes | 20 | Character |
| system/hamobj/HamDirector | 15 | Game logic |
| system/hamobj/HamCamShot | 13 | Game logic |
| system/hamobj/MoveDir | 15 | Game logic |
| system/synth_xbox/FxSend* | 10 ea | Audio FX (multiple) |

### Tier 4: Platform-Heavy / Hard (defer)

Xbox-specific with no RB3 reference. Lower priority.

| Unit | Stubs | Notes |
|------|-------|-------|
| system/os/PlatformMgr_Xbox | 88 | Xbox platform APIs |
| system/moviebink/BinkMovieImpl | 43 | Bink video SDK |
| system/synth_xbox/Synth | 44 | Xbox audio |
| system/synth_xbox/Mic | 31 | Xbox audio capture |
| system/synth_xbox/Voice | 21 | Xbox voice |
| system/os/NetworkSocket_Win | 25 | Networking |
| system/gesture/LiveCameraInput | 25 | Kinect input |
| lib/binkxenon/binkread | 62 | Bink library |
| system/rndobj/Utl | 53 | Rendering utilities |
| system/rndobj/Shader | 34 | Shader system |
| system/flow/FlowSetProperty | 39 | Flow system (large) |

## Workflow Per Function

```
1. Batch-check the unit first   → batch_check(unit_pattern="...")
2. Decompile (Ghidra)           → /ghidra-decompile <symbol>
3. Check RB3 reference          → lookup_rb3(symbol)
4. Write implementation         → in src/<unit>.cpp
5. Build + diff                 → run_objdiff(symbol, project_dir=".")
6. Iterate until matched        → (or mark AT_LIMIT if unfixable)
7. Report result                → report_result(symbol, ...)
```

## Batch Strategy

- **Start each session** with `batch_check(unit_pattern)` to auto-detect any
  stubs that already match from existing code
- **Process functions by size** within each unit (smallest first)
- **Use parallel subagents** for independent units (see
  `docs/tools/WORKFLOW.md`; the archived `SUBAGENT_STRATEGY.md` predates the
  orchestrator tooling)
- A focused session can do **20-30 trivial stubs** or **5-10 medium functions**
- **Expect high false-negative rate**: Early testing found ~88% of "reset" stubs
  already had matching code. `batch_check` resolves these automatically.

## Reference

- [2026-02-28 stub burndown session](../../sessions/2026-02-28-stub-burndown-data-stubs.md) — original investigation
- [2026-02-28 unimplemented functions analysis](../../sessions/2026-02-28-unimplemented-functions-analysis.md) — full scope analysis
