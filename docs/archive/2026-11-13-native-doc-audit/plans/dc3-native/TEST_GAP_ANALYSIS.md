# Test Gap Analysis — Native Port

**Last Updated**: 2026-03-16
**Purpose**: Identify high-value test gaps for future agents to fill.

## Current Test Coverage

25 test files, ~7,700 lines across `native/tests/`. Strong coverage in:
- **Serialization**: BinStream, ChunkStream, DTA parsing (3 files)
- **Audio**: Device, StreamReceiver, Bink codec, MOGG decode (3 files)
- **Asset loading**: Bulk sweep, archive resolution, dirloader (3 files)
- **Object lifetime**: Dir merge parity, ring corruption (2 files)
- **Character animation**: Bone serialization, pose ground truth (2 files)
- **Integration**: Headless boot, input replay, long-run stability (1 file)

## High-Priority Test Gaps

### 1. DTA Handler Dispatch Verification

**Why**: The biggest native port hack (animation completion) exists because DTA handlers don't fire. We need tests that verify the dispatch chain works.

**Proposed test** (`test_dta_dispatch.cpp`):
```
- Create an Object with a known mTypeDef containing a handler
- Send a message to the object
- Verify the handler executes (e.g., sets a flag or modifies state)
- Test with ContextCheckerInit() registered functions
- Test with AnimTask "on_anim_event" dispatch
```

**Validates**: mTypeDef population, ExecuteScript(), DataRegisterFunc() functions

### 2. System Init Completeness

**Why**: Missing Init calls (ContextCheckerInit, MidiParser::Init, etc.) cause silent failures. Need to verify all expected factories are registered.

**Proposed test** (`test_system_init.cpp`):
```
- After engine init, verify REGISTER_OBJ_FACTORY registered expected types:
  - Hmx::Object::NewObject("MidiParser") != nullptr
  - Hmx::Object::NewObject("WorldCrowd") != nullptr
  - etc.
- Verify DataRegisterFunc functions are callable from DTA scripts:
  - DataArray("random_context").Execute() doesn't fail
```

**Validates**: Init call completeness, factory registration, DTA function availability

### 3. AnimTask Completion Flow

**Why**: The Anim.cpp auto-null hack exists because the normal completion path doesn't work. Need end-to-end test.

**Proposed test** (`test_anim_completion.cpp`):
```
- Create an AnimTask with a known animation length
- Set a listener that records Handle() calls
- Poll() until animation exceeds frame span
- Verify "on_anim_event" with "ended" symbol dispatched to listener
- Verify IsAnimating() returns false after dispatch
```

**Validates**: AnimTask::Poll completion, message dispatch, StopAnimation flow

### 4. Audio Thread Safety (Suspend/Resume)

**Why**: Game.cpp hack 1.3 protects against race condition during song restart. Need regression test.

**Proposed test** (`test_audio_thread_safety.cpp`):
```
- Initialize AudioDevice with real callback thread
- Create MoggClip and start playback
- Call StopAllSounds() WITHOUT Suspend() — verify crash/corruption (ASan)
- Call Suspend() + StopAllSounds() + Resume() — verify clean teardown
```

**Validates**: Threading safety, race condition detection

### 5. Visual Regression Automation

**Why**: render-test generates PNGs but there's no automated baseline comparison. Changes to rendering code have no safety net.

**Proposed enhancement** to `render-test`:
```
- Add --baseline-dir flag
- After rendering, compare output PNG against baseline (pixel diff)
- Fail if diff exceeds threshold (e.g., 1% pixel error)
- Store baselines in repo under native/tests/baselines/
```

**Validates**: Rendering correctness across code changes

### 6. Flow State Machine Traversal

**Why**: UI panel transitions depend on correct state machine behavior. DTA-driven flow is fragile on native.

**Proposed test** (`test_flow_transitions.cpp`):
```
- Load a simple panel hierarchy
- Trigger Enter() → verify state == kOpen
- Trigger Exit() → verify state == kClosed
- Verify OnTransitionComplete fires
- Test with scripted input (button presses advance flow)
```

**Validates**: UIPanel lifecycle, transition callbacks, flow animation

## Medium-Priority Test Gaps

### 7. MoveDir Optional Loading

Test that Game.cpp properly handles null MoveDir without any guards needing to be HX_NATIVE:
```
- Create Game state with mMoveDir = nullptr
- Call FlushMoveRecord(), SwapMoveRecord(), Reset()
- Verify no crash (currently requires #ifdef guards)
```

### 8. Async Asset Loading Resilience

Test the loading state machine when assets arrive late:
```
- Start Game::PollForLoading() with no audio loaded
- Verify timeout bypass works (120 polls)
- Verify gameplay starts correctly after timeout
```

### 9. Profile/Save System Stubs

Test that NativeSaveLoadStub handles all expected messages:
```
- Send save_load_mgr the messages that flow scripts send
- Verify no unhandled message crashes
```

## Test Infrastructure Improvements

### CI Integration

Currently tests run manually via `ctest`. Suggested CI setup:
```yaml
# .github/workflows/native-tests.yml
- Build milo-tests with ASan
- Run ctest --output-on-failure
- Run render-test with baseline comparison
- Run headless boot (500 frames) with MILO_FATAL_FAILS=1
```

### Test Asset Management

Tests requiring real assets (Bink files, .milo files) use env vars and `GTEST_SKIP()` when missing. Consider:
- Minimum asset pack for CI (small .milo, synthetic Bink)
- Docker image with pre-extracted test assets

## Running Tests

```bash
cd native/build
ninja milo-tests
ctest --output-on-failure          # All tests
ctest -R DtaDispatch -V            # Specific test (when implemented)
ctest -R "AudioDevice" -V          # Audio tests
ctest -R "ObjectLifetime" -V       # Lifetime tests

# With real assets
DC3_DATA=/path/to/ark ctest -R AssetLoading
MILO_TEST_BIK=/path/to/file.bik ctest -R AudioDevice
```
