# Plan: Gameplay State Machine Integration Tests

**Date**: 2026-03-17
**Status**: PLANNED
**Prerequisite**: Song animation advancement fix (this session), headless test infra (test_headless_boot.cpp)

## Problem

We have no automated way to verify the gameplay animation pipeline works end-to-end.
Bugs like "characters freeze after 1-3 seconds" or "song.anim frame stuck at 0" go
undetected until manual testing on web/native. The existing headless boot tests only
check that the engine doesn't crash — they don't verify animation state.

## Existing Infrastructure

- **Headless subprocess tests**: `native/tests/test_headless_boot.cpp` — launches
  `dc3-native` as a subprocess, captures stdout/stderr, parses for assertions
- **Input replay scripts**: `scripts/dc3-input-flows/ymca.txt` — scripted input
  to navigate menus and reach `game_screen`
- **Env var controls**: `MILO_HEADLESS=1`, `MILO_MAX_FRAMES=N`, `MILO_INPUT_SCRIPT=path`
- **Test framework**: Google Test, built as `milo-tests` target

## Design

### Step 1: Structured telemetry emitter

Add a periodic state dump in `HamDirector::Poll()`, gated behind
`MILO_GAMEPLAY_TELEMETRY=1` env var. Emits one line every N frames (e.g. 10)
to stderr in a parseable format:

```
DC3_GAMEPLAY: frame=1200 beat=4.20 songAnimFrame=126.0 clip=Slither_L state=playing pollEnabled=1 selectCamera=1
```

Fields:
| Field | Source | What it tells us |
|-------|--------|------------------|
| `frame` | Global frame counter | Time reference |
| `beat` | `TheTaskMgr.Beat()` | Whether beat system is driving timing |
| `songAnimFrame` | `songAnim->GetFrame()` | Whether song.anim advances |
| `clip` | Current clip name from ClipPlayer | Whether clip selection works |
| `state` | `GamePanel::mState` | Intro vs playing |
| `pollEnabled` | `mPollEnabled` | Whether HamDirector is active |
| `selectCamera` | Whether OnSelectCamera has fired | Whether DTA TypeDef chain works |

Implementation location: `HamDirector::Poll()`, after the songAnim check, inside
`#ifdef HX_NATIVE` guard. Only emits when `MILO_GAMEPLAY_TELEMETRY` env var is set.

### Step 2: Telemetry parser helper

Add a helper in `native/tests/test_helpers.cpp` (or new file) that parses the
`DC3_GAMEPLAY:` lines from subprocess output into a vector of structs:

```cpp
struct GameplayFrame {
    int frame;
    float beat;
    float songAnimFrame;
    std::string clip;
    std::string state;
    bool pollEnabled;
    bool selectCamera;
};

std::vector<GameplayFrame> ParseGameplayTelemetry(const std::string &output);
```

### Step 3: Test file `native/tests/test_gameplay_animation.cpp`

Uses `RunHeadless()` with the YMCA input script and `MILO_GAMEPLAY_TELEMETRY=1`.
Parses telemetry and asserts state transitions.

#### Test cases

**`SongAnimAdvancesDuringIntro`**
- Verify `songAnimFrame > 0` within first 50 telemetry samples
- Validates: wall-clock fallback works when `beat == 0`

**`BeatStartsAfterIntro`**
- Find first sample where `beat > 0`
- Verify it happens (intro eventually completes)
- Verify `songAnimFrame` continues advancing after beat starts

**`ClipChangesOverTime`**
- Collect unique `clip` names across all samples
- Assert at least 2 different clips seen (characters don't freeze on one clip)

**`SongAnimMonotonicallyIncreases`**
- Verify `songAnimFrame` never decreases (except for loop wrap)
- Catches bugs where frame resets to 0 or regresses

**`SelectCameraFires`**
- Verify at least one sample has `selectCamera=1`
- Validates: WorldDir TypeDef "world" is set, DTA handler chain works

**`CharactersAnimateFor10Seconds`**
- Run for ~3000 frames with YMCA song
- Verify `clip` changes at least 3 times over the run
- End-to-end: characters dance through multiple moves

#### Test configuration

```cpp
class GameplayAnimationTest : public ::testing::Test {
protected:
    RunResult result;
    std::vector<GameplayFrame> frames;

    void SetUp() override {
        std::string script = GetScriptDir() + "/ymca.txt";
        result = RunHeadless(3000, script.c_str(), 120,
                             {{"MILO_GAMEPLAY_TELEMETRY", "1"}});
        frames = ParseGameplayTelemetry(result.output);
    }
};
```

All tests in the fixture share a single engine run (expensive to boot). The
`SetUp` runs once per test but Google Test caches the fixture — if we want
to share a single run across all tests, use `SetUpTestSuite` with a static
result.

### Step 4: CI integration

- Add to CMakeLists.txt as part of `milo-tests`
- Gate behind `MILO_GAMEPLAY_TESTS=1` env var (requires game assets)
- Typical run: `MILO_GAMEPLAY_TESTS=1 ctest --test-dir native/build -R GameplayAnimation`

## Implementation Order

1. **Telemetry emitter** (~20 lines in HamDirector.cpp)
   - Add env var check, periodic fprintf with structured fields
   - Test manually: `MILO_GAMEPLAY_TELEMETRY=1 MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt native/build/dc3-native 2>&1 | grep DC3_GAMEPLAY`

2. **Parser helper** (~40 lines)
   - Simple `sscanf`/`std::regex` parsing of `DC3_GAMEPLAY:` lines

3. **Test file** (~150 lines)
   - 6 test cases as described above
   - Single fixture sharing one engine run

4. **Web variant** (future)
   - Same telemetry format, captured via CDP log scraping
   - Reuse same parser, different subprocess launcher

## What This Catches

| Bug | Test that catches it |
|-----|---------------------|
| song.anim frame stuck at 0 | `SongAnimAdvancesDuringIntro` |
| Beat never starts | `BeatStartsAfterIntro` |
| Characters freeze on first clip | `ClipChangesOverTime` |
| OnSelectCamera never fires (TypeDef missing) | `SelectCameraFires` |
| Frame regression/reset | `SongAnimMonotonicallyIncreases` |
| Wall-clock fallback removed | `SongAnimAdvancesDuringIntro` |
| Characters stop dancing mid-song | `CharactersAnimateFor10Seconds` |

## Files to Create/Modify

| File | Action |
|------|--------|
| `src/system/hamobj/HamDirector.cpp` | Add telemetry emitter in Poll() |
| `native/tests/test_gameplay_animation.cpp` | New test file |
| `native/tests/test_helpers.h` | Add `ParseGameplayTelemetry()` |
| `native/tests/test_helpers.cpp` | Implement parser |
| `native/CMakeLists.txt` | Add test file to milo-tests |
