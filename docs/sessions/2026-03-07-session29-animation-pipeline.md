# Session 29: Animation Pipeline Investigation (2026-03-07)

## Goal
Fix UI animation ticking on the DC3 native port so materials animate (alpha, color) and the choose_mode_screen matches the original game's bright cyan neon UI.

## Starting State
- choose_mode_screen renders with 51 draw calls, correct geometry, text visible
- All UI elements nearly invisible — material alpha values stuck at 0
- Dark purple/gray output vs reference bright cyan neon

## Investigation Path

### 1. Timer Conversion Fix
**Problem**: Native `__mftb()` returns microseconds, but `Timer::Init()` used PPC timebase conversion factors (~50x too slow).

**Fix**: Added `#ifdef HX_NATIVE` in `Timer::Init()`:
```cpp
Timer::sDoubleCycles2Ms = 0.001;      // 1µs = 0.001ms
Timer::sLowCycles2Ms = 0.001f;
Timer::sHighCycles2Ms = 4294967.296f;  // 2^32 * 0.001
```

### 2. StreamReceiverFile::sPlayCursor
**Problem**: Static member declared in header but never defined → linker error.

**Fix**: Added `int StreamReceiverFile::sPlayCursor = 0;` to StreamReceiverFile.cpp.

### 3. EventTrigger::TriggerSelf — Red Herring
The original plan hypothesized that `TriggerSelf()` being stubbed was the root cause. Investigation disproved this:
- **Zero UITrigger/EventTrigger objects** exist in any DC3 UI milo file
- DC3 menus don't use EventTriggers for animation at all
- Animation is driven by PropAnim objects started during panel enter

### 4. dynamic_cast<RndAnimatable*> Failure — Also a Red Herring
Previous session believed dynamic_cast was failing for all objects due to broken vtables.

**Truth**: `nm` analysis showed all vtables and typeinfo are properly emitted (`D` symbol type, not weak). The debug trace was just limited to the first 10 dirs by a `static int sSync < 10` counter — those happened to be character skeleton dirs (147 Trans objects, 0 animatables) which is correct behavior.

Once the counter limit was removed, the actual UI panel dirs appeared:
```
'background' anims=8-12
'letterbox' anims=23
'main_ribbon' anims=32-37
'WorldDir' anims=87-177
'chars_base' anims=81
```

### 5. Milo File Content Analysis (Subagent)
Scanned 1,139 UI `.milo_xbox` files:
- **7,811 PropAnim** objects across ~850 files
- **19 TransAnim** in 16 files
- **2 MeshAnim**, **2 MatAnim** (rare)
- **0 EventTrigger**, **0 UITrigger**

Key panels: choose_mode.milo has 14 PropAnim, background.milo has 8 PropAnim.

### 6. Animation Pipeline Verification
Traced the full chain:
1. `DirLoader::Cleanup()` calls `SyncObjects()` on every loaded dir
2. `RndDir::SyncObjects()` collects animatables via `ObjDirItr<RndAnimatable>`
3. `PanelDir::Enter()` auto-starts animation: `Animate(0, true, 0, k30_fps_ui, sf, ef, ...)`
4. `UIManager::Poll()` sets `TheTaskMgr.SetUISeconds(mTimer.SplitMs() * 0.001f)`
5. `TaskMgr::Poll()` → `TaskTimeline::Poll()` → `AnimTask::Poll(diff)`
6. AnimTask receives increasing time values (0.0 → 0.3 → 0.5 → 0.7s)

**The animation system works end-to-end.** The issue is downstream.

### 7. Stubs Assessment
Checked all 1,267 function stubs in `engine_stubs_generated.cpp`:
- 503 stubs have real implementations in decomp source — weak stubs correctly overridden
- 46 vtable stubs — all properly overridden (confirmed via `nm`)
- Critical unimplemented: `EventTrigger::TriggerSelf()`, `Cleanup()`, `MsgSinks::MergeSinks()`, `UIPanel::SetPaused()`, `RndPropAnim::ForeachKeyframe()` — none are in the rendering critical path

## Files Modified
| File | Change |
|------|--------|
| `src/system/os/Timer.cpp` | `#ifdef HX_NATIVE` timer conversion factors for µs→ms |
| `src/system/synth/StreamReceiverFile.cpp` | Added `sPlayCursor` static definition |
| `src/system/rndobj/Dir.cpp` | Debug traces added/removed (clean now) |
| `src/system/ui/PanelDir.cpp` | Removed printf from auto-animate trace |
| `docs/plans/dc3-native/RENDERING_GAPS.md` | Updated with session 29 findings |
| `docs/native/NATIVE_PORT_STATUS.md` | Added session 26-29 entries |

## Key Insights
1. **Stubs are not the main blocker.** 503/1267 stubs have real implementations that correctly override the weak stubs. The few truly missing functions (TriggerSelf, etc.) are not in the rendering path.
2. **DC3 UI animations are PropAnim-driven**, not EventTrigger-driven. Zero EventTrigger objects exist in any UI milo file.
3. **The animation pipeline is fully operational.** The remaining issue is in the PropAnim → material property → GPU rendering path — either PropAnim::SetFrame isn't updating material values correctly, or the renderer isn't reading the updated values per-frame.
4. **Debug trace counter limits** (`static int s < N`) can give misleading results when the interesting data comes after the Nth call. Always remove limits during investigation.

## Next Steps
1. Trace `RndPropAnim::SetFrame()` to verify it actually writes to material color/alpha
2. Check if `Mesh_Wgpu.cpp` reads `RndMat::GetColor()` per-frame or caches at upload time
3. If material values are being set correctly, the issue is in the shader/uniform upload path
