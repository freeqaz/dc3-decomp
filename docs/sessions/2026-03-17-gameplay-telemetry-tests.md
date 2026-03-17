# Plan: Gameplay Telemetry Integration Tests

**Date**: 2026-03-17
**Status**: PLANNED
**Prerequisite**: Song animation advancement fix (this session), headless test infra (test_headless_boot.cpp)

## Problem

We have no automated way to verify the gameplay animation pipeline works end-to-end.
Bugs like "characters freeze after 1-3 seconds" or "song.anim frame stuck at 0" go
undetected until manual testing on web/native. The existing headless boot tests only
check that the engine doesn't crash — they don't verify animation state.

These tests should serve as a **north star**: tests that fail today, where each failure
maps directly to an unimplemented engine subsystem. Fixing the engine to pass each test
IS the roadmap.

## Existing Infrastructure

- **Headless subprocess tests**: `native/tests/test_headless_boot.cpp` — launches
  `dc3-native` as a subprocess, captures stdout/stderr, parses for assertions
- **Input replay scripts**: `scripts/dc3-input-flows/ymca.txt` — scripted input
  to navigate menus and reach `game_screen`
- **Env var controls**: `MILO_HEADLESS=1`, `MILO_MAX_FRAMES=N`, `MILO_INPUT_SCRIPT=path`
- **Test framework**: Google Test, built as `milo-tests` target
- **RunHeadless()**: `RunHeadless(maxFrames, inputScriptPath, timeoutSeconds)` — returns
  `RunResult{exitCode, signal, output, timedOut}`. Currently no env var override param.

## Architecture

### Principle: telemetry lives in native, not decomp

`HamDirector::Poll()` is decomp source. Adding `#ifdef HX_NATIVE` telemetry there
pollutes decomp diffs and risks accidentally changing PPC codegen. Instead:

- **Emitter**: `native/src/telemetry/GameplayTelemetry.cpp` — a native-side observer
  that reaches into engine globals (`TheHamDirector`, `TheGamePanel`, `TheTaskMgr`, etc.)
- **Call site**: `App.cpp` main loop (already `#ifdef HX_NATIVE`-heavy), not
  `HamDirector::Poll()`
- **Gating**: `DC3_TEL=1` env var (short, internal-only)

### Principle: key-value format, not positional

Emit key=value pairs, parsed into `std::unordered_map<std::string, std::string>` per
frame. Any subsystem can emit new keys without coordinating with the parser. New tests
check new keys without touching the emitter format.

```
DC3_TEL: frame=1200 beat=4.20 songAnimFrame=126.0
DC3_TEL: frame=1200 clip=Slither_L state=playing pollEnabled=1
DC3_TEL: frame=1200 cameraCount=3 typeDef=world
DC3_TEL: frame=1200 hamProvider=1 navAnimating=0 wardrobeSlot0=outfit_name
```

Multiple lines per sample are fine — parser accumulates key-value pairs keyed by frame.

### Principle: assert on properties, not thresholds

Tests should check **invariants** (monotonicity, eventual change, state transitions),
not magic numbers ("within first 50 samples", "at least 3 times"). Properties survive
timing changes; thresholds break.

```cpp
// BAD: fragile threshold
EXPECT_TRUE(frames[49].songAnimFrame > 0);

// GOOD: property — songAnimFrame eventually advances
auto advancing = std::adjacent_find(frames.begin(), frames.end(),
    [](auto& a, auto& b) { return b.songAnimFrame > a.songAnimFrame; });
EXPECT_NE(advancing, frames.end()) << "songAnimFrame never advanced";
```

### Principle: single run, phase-based assertions

Run the engine once (~3000 frames). Parse the telemetry stream into phases detected
from the data itself:

| Phase | Detection | What's happening |
|-------|-----------|-----------------|
| Boot | `frame < first(state=intro)` | Engine initializes, loads assets |
| Intro | `state=intro` | Song intro plays, pre-beat |
| Gameplay | `state=playing` | Active gameplay, beat-driven |

Each test asserts properties within a phase. All tests share the single run via
`SetUpTestSuite()` with static data — NOT `SetUp()` (which runs per-test and would
mean 6+ separate engine boots).

### Principle: tests gate hack removal

Each Tier 2 test maps to a specific phase of the hamobj hack audit
(`2026-03-17-hamobj-native-hack-audit.md`). The workflow is:

1. Pick a hack audit phase (e.g., A1: `set_type "world"`)
2. Remove the hack (the `#ifdef HX_NATIVE` guard)
3. Run the telemetry tests — the corresponding Tier 2 test should now pass
4. If it doesn't pass, the engine fix is incomplete — debug using the telemetry dump
5. When it passes, remove the `GTEST_SKIP()` and promote the test to Tier 1

This means **you never remove a hack on faith** — you remove it when the test proves
the natural DTA path works.

## Design

### Component 1: RunHeadless env var support

Extend `RunHeadless()` to accept extra environment variables:

```cpp
struct EnvVar { std::string key, value; };

RunResult RunHeadless(int maxFrames,
                      const char *inputScriptPath = nullptr,
                      int timeoutSeconds = 30,
                      std::vector<EnvVar> extraEnv = {});
```

The extra env vars are prepended to the subprocess command string.

### Component 2: Telemetry emitter (`native/src/telemetry/GameplayTelemetry.cpp`)

A standalone native-side observer. Called from `App.cpp` main loop every N frames.

```cpp
namespace GameplayTelemetry {
    void Init();           // Check DC3_TEL env var, set emission interval
    void Sample(int frame); // Emit key=value lines to stderr for this frame
}
```

`Sample()` reaches into engine globals:
- `TheTaskMgr.Beat()`, `TheTaskMgr.Seconds()`
- `TheHamDirector->SongAnim(0)->GetFrame()`
- `TheHamDirector->mPollEnabled`
- `GamePanel` state (via accessor or direct)
- Current clip name from ClipPlayer

**Hack-audit-driven keys** (emit when globals are non-null):
- `typeDef` — venue WorldDir's TypeDef name (empty = `set_type` never fired)
- `hamProvider` — `1` if `TheHamProvider` is non-null, `0` otherwise
- `navAnimating` — `TheHamNavList->IsAnimating()` (for `transition_complete` tracking)
- `wardrobeSlot0` — character outfit name for player 0 (for provider init tracking)
- `moveGraph` — `1` if MoveMgr's move graph is loaded, `0` otherwise
- `mergerDir` — `1` if `mMerger->Dir()` returns non-null WorldDir, `0` otherwise
- `cameraCount` — number of cameras selected via OnSelectCamera

All access is read-only. If a global is null (subsystem not yet initialized), skip that key.

**Output channel**: Write to fd 3 if available, stderr as fallback. Keeps telemetry
separate from engine debug spew and crash messages. The subprocess harness opens
fd 3 as a pipe and reads it alongside stdout/stderr.

### Component 3: Telemetry parser

```cpp
struct TelemetrySample {
    int frame;
    std::unordered_map<std::string, std::string> fields;

    // Typed accessors with defaults
    float getFloat(const std::string &key, float def = 0.0f) const;
    int getInt(const std::string &key, int def = 0) const;
    bool getBool(const std::string &key, bool def = false) const;
    std::string getString(const std::string &key, const std::string &def = "") const;
};

// Parse DC3_TEL: lines, merge by frame number
std::vector<TelemetrySample> ParseTelemetry(const std::string &output);
```

Lives in `native/tests/telemetry_parser.h` / `.cpp` — separate from test_helpers
since it's a self-contained parser with its own tests.

### Component 4: Test file (`native/tests/test_gameplay_telemetry.cpp`)

## Test Taxonomy

Tests are explicitly tiered. Tier 1 tests guard working behavior. Tier 2 tests
fail today — each failure maps to specific engine work AND a specific hack audit
phase. When you fix the engine and the test passes, remove the `GTEST_SKIP()` and
the corresponding `#ifdef HX_NATIVE` hack.

### Tier 1: Smoke (should pass today)

**`EngineReachesGameScreen`**
- Property: at least one sample exists with `state != ""`
- Guards: input script navigates menus successfully

**`SongAnimAdvances`**
- Property: `songAnimFrame` is not constant across all samples (adjacent pair differs)
- Guards: wall-clock fallback works when beat == 0

**`SongAnimMonotonicallyIncreases`**
- Property: `songAnimFrame` never decreases between consecutive samples
- Guards: frame doesn't reset to 0 or regress

**`NoCrashDuringGameplay`**
- Property: `sResult.signal == 0` and `sResult.exitCode == 0`
- Guards: removing hack-audit guards doesn't introduce null derefs

### Tier 2: Capability (expected to fail today)

Each test documents **what engine work** would make it pass AND which hack audit
phase it gates.

**`VenueTypeDefSet`** — *hack audit: Phase A1 (`set_type "world"`) — RECLASSIFIED*
- Telemetry key: `typeDef`
- Property: at least one gameplay-phase sample has `typeDef=world`
- **Note**: Investigation showed this hack is PERMANENT — native doesn't evaluate DTA panel enter scripts (`PanelDir::Enter()` confirms at PanelDir.cpp:428-442). The C++ `SetType("world")` call in VenueEnter is a correct, minimal workaround. This test should be **Tier 1** (expected to pass with the hack in place) — it validates the hack works, not that DTA fires.
- **When this passes**: confirms the existing hack is functioning correctly

**`CameraSelection`** — *hack audit: Phase B2*
- Telemetry key: `cameraCount`
- Property: `cameraCount > 0` in at least one gameplay-phase sample
- Skip reason: `Camera path divergence between mVenue and mMerger->Dir()`
- **Note**: A1 dependency removed — `set_type "world"` hack is permanent and working. This test now gates B2 only (PlayNextShot camera unification). Investigation confirmed `mVenue != mMerger->Dir()` — they are merge source vs merge target. Unification requires CameraManager equivalence post-merge.
- **When this passes**: unify PlayNextShot camera path (remove hack #7) if CameraManagers match

**`HamProviderInitialized`** — *hack audit: Phase A4 — LIKELY PASSES TODAY*
- Telemetry key: `hamProvider`
- Property: `hamProvider=1` before first gameplay-phase sample
- **Note**: Investigation showed `HamInit()` runs at App.cpp:321 (early in constructor), creating TheHamProvider before any subsystem poll. `EnsureHamProvider()` (Ham.cpp:84-99) provides native fallback. Properties are pre-initialized in Ham.cpp:189-217. The 6 null guards are likely dead code. This test should be **Tier 1** (expected to pass today).
- **When this passes**: remove 6 TheHamProvider null guards in HamNavList.cpp (replace with MILO_ASSERT first to confirm)

**`NavTransitionsComplete`** — *hack audit: Phase A2*
- Telemetry key: `navAnimating`
- Property: `navAnimating` transitions from `1` to `0` at least once (animation completes)
- Skip reason: `HamNavList has no OnMsg(UITransitionCompleteMsg) handler`
- **Root cause confirmed**: `UI::Poll()` (UI.cpp:726, 774) DOES send the message. HamNavList doesn't have a handler — on Xbox, DTA `add_sink` wires this up. Fix: add `OnMsg(const UITransitionCompleteMsg&)` to HamNavList (pattern in HamUI.cpp).
- **When this passes**: remove 3 IsAnimating() bypasses in HamNavList.cpp

**`WardrobeLoadsCorrectly`** — *hack audit: Phase A3*
- Telemetry key: `wardrobeSlot0`
- Property: `wardrobeSlot0` is non-empty during gameplay phase
- Skip reason: `Player provider not fully initialized before OnLoadSong`
- **When this passes**: remove HamDirector hack #2 (crew/outfit reconstruction)

**`MoveGraphLoaded`** — *hack audit: Phase B1*
- Telemetry key: `moveGraph`
- Property: `moveGraph=1` before first clip transition
- Skip reason: `Move mergers not loading before MoveMgr access`
- **When this passes**: remove 12 null guards in MoveMgr, OriginalChoreoRemixer, SuperEasyRemixer

**`ClipTransitions`** — *hack audit: Phase B1 (downstream)*
- Telemetry key: `clip`
- Property: unique `clip` values change at least once across all samples
- Skip reason: `ClipPlayer not selecting moves (depends on MoveGraphLoaded)`
- **When this passes**: move data pipeline is fully working

**`BeatStartsDuringGameplay`** — *hack audit: Phase D2 (intro timing)*
- Telemetry key: `beat`
- Property: at least one sample in gameplay phase has `beat > 0`
- Skip reason: `Synth pipeline not yet driving beat`
- **When this passes**: evaluate removing HamDirector hack #3 (wall-clock fallback)

**`BeatDrivenAnimation`** — *hack audit: Phase D2*
- Telemetry key: `beat`, `songAnimFrame`
- Property: after `beat > 0` appears, `songAnimFrame` still advances
  (beat-driven mode works, not just wall-clock fallback)
- Skip reason: `Beat-driven animation path not implemented`
- **When this passes**: remove HamDirector hack #3 (intro frame advancement fallback)

**`MergerDirAvailable`** — *hack audit: Phase B2*
- Telemetry key: `mergerDir`
- Property: `mergerDir=1` during gameplay phase
- Skip reason: `mMerger->Dir() returns null on native`
- **When this passes**: unify PlayNextShot camera path (remove hack #7)

**`FullChoreography`** — *hack audit: all phases complete*
- Telemetry keys: `clip`, `beat`, `cameraCount`
- Property: unique `clip` values >= 3 over a 3000-frame run, beat is driving,
  cameras are selecting
- Skip reason: `Full gameplay pipeline not complete`
- **When this passes**: hamobj DTA flow is converged with Xbox

### Dependency graph

Each node is a test. Each edge is labeled with the engine subsystem that must work
AND the hack audit phase it unblocks.

```
EngineReachesGameScreen [T1]
  └─ SongAnimAdvances [T1]
       ├─ SongAnimMonotonicallyIncreases [T1]
       │
       ├─ VenueTypeDefSet [T1*] ←── Reclassified: hack is permanent, test validates it works
       │
       ├─ HamProviderInitialized [T1*] ←── Reclassified: likely passes today (init is early)
       │    └─ NavTransitionsComplete [T2] ←── Phase A2: add OnMsg handler to HamNavList
       │
       ├─ CameraSelection [T2] ←── Phase B2: camera unify (mVenue vs mMerger->Dir())
       │    └─ MergerDirAvailable [T2] ←── Phase B2: CameraManager equivalence post-merge
       │
       ├─ WardrobeLoadsCorrectly [T2] ←── Phase A3: player provider init
       │
       ├─ MoveGraphLoaded [T2] ←── Phase B1: move merger loading
       │    └─ ClipTransitions [T2] ←── Phase B1: ClipPlayer + CharClipGroup
       │
       ├─ BeatStartsDuringGameplay [T2] ←── Phase D2: Synth/MIDI
       │    └─ BeatDrivenAnimation [T2] ←── Phase D2: beat-driven timing
       │
       └─ FullChoreography [T2] ←── All phases complete

[T1*] = reclassified from T2 based on investigation (expected to pass today)
```

### Fixture design

```cpp
class GameplayTelemetryTest : public ::testing::Test {
protected:
    // Shared across all tests — single engine run
    static RunResult sResult;
    static std::vector<TelemetrySample> sSamples;
    static bool sRanEngine;

    static void SetUpTestSuite() {
        std::string script = GetScriptDir() + "/ymca.txt";
        sResult = RunHeadless(3000, script.c_str(), 120, {{"DC3_TEL", "1"}});
        sSamples = ParseTelemetry(sResult.output);
        sRanEngine = true;
    }

    // Dump telemetry on failure for debugging
    void TearDown() override {
        if (HasFailure()) {
            std::string path = "/tmp/claude-1000/gameplay_tel_" +
                std::string(::testing::UnitTest::GetInstance()
                    ->current_test_info()->name()) + ".log";
            WriteFile(path, sResult.output);
            std::cerr << "Full telemetry dumped to: " << path << "\n";
        }
    }

    // Phase helpers — detect from data, not hardcoded frame numbers
    std::vector<TelemetrySample> samplesInPhase(const std::string &state) {
        std::vector<TelemetrySample> out;
        for (auto &s : sSamples) {
            if (s.getString("state") == state) out.push_back(s);
        }
        return out;
    }
};
```

## Hack Removal Workflow

This is the concrete process for using telemetry tests to drive hack cleanup:

### Step 1: Baseline

Run all telemetry tests with hacks in place. All Tier 1 tests pass, all Tier 2 tests
skip. Record the telemetry output as the baseline — this is what "working with hacks"
looks like.

### Step 2: Pick a phase, remove the hack

Choose a hack audit phase (start with A1). Remove the `#ifdef HX_NATIVE` guard.
Don't fix anything else yet — just remove the guard.

### Step 3: Run telemetry, observe the failure

```bash
DC3_GAMEPLAY_TESTS=1 ctest --test-dir native/build -R GameplayTelemetry
```

Three outcomes:
- **Tier 1 tests still pass + Tier 2 test now passes**: The natural DTA path works.
  Remove the `GTEST_SKIP()`, promote the test to Tier 1. Done.
- **Tier 1 tests still pass + Tier 2 test still fails**: The DTA path doesn't fire
  yet but nothing crashes. Debug using the telemetry dump. Fix the root cause
  (e.g., ensure DTA handler is registered), re-run.
- **Tier 1 tests FAIL (crash/regression)**: The hack was load-bearing — the subsystem
  it guarded isn't ready. Put the hack back, investigate the root cause separately.

### Step 4: Iterate

Each successful hack removal promotes a Tier 2 test to Tier 1 and may unblock
downstream tests in the dependency graph.

### Progress tracking

The hack audit doc tracks overall hack count. The telemetry test tiers track
convergence:

| Milestone | Tier 1 count | Tier 2 remaining | Hacks removed |
|-----------|-------------|-------------------|---------------|
| Baseline | 4 + 2 reclassified = 6 | 9 | 0 |
| Trivial cleanup (logging, sInstance) | 6 | 9 | 9 (trivial) |
| Phase A4 (null guards) + A2 (transition handler) | 7-8 | 7-8 | 18 |
| Phase B complete | 10-11 | 3-4 | 31 |
| Phase C + D complete | 14-15 | 0 | ~55 |

## Implementation Order

1. **RunHeadless env var support** (~15 lines, `test_headless_boot.cpp`)
   - Add `std::vector<EnvVar>` parameter, prepend to command string
   - Non-breaking: default empty vector

2. **Telemetry emitter** (~80 lines, `native/src/telemetry/GameplayTelemetry.cpp`)
   - `Init()`: check `DC3_TEL` env var, set interval (default: every 10 frames)
   - `Sample()`: emit key=value lines to stderr, skip null globals
   - Include all hack-audit-driven keys (typeDef, hamProvider, navAnimating, etc.)
   - Call from `App.cpp` main loop after `TheTaskMgr.Poll()`
   - Manual test: `DC3_TEL=1 MILO_HEADLESS=1 MILO_MAX_FRAMES=3000 MILO_INPUT_SCRIPT=scripts/dc3-input-flows/ymca.txt native/build/dc3-native 2>&1 | grep DC3_TEL`

3. **Telemetry parser** (~60 lines, `native/tests/telemetry_parser.cpp`)
   - Key-value parser, merge by frame
   - Typed accessors with safe defaults
   - Unit test the parser itself with synthetic input

4. **Test file** (~350 lines, `native/tests/test_gameplay_telemetry.cpp`)
   - 4 Tier 1 tests: no skip, should pass
   - 11 Tier 2 tests: `GTEST_SKIP()` with reason string + hack audit cross-ref
   - `SetUpTestSuite` for single shared run
   - `TearDown` dumps telemetry on failure

5. **CMake + CI** (~5 lines)
   - Add test file + parser to `milo-tests` in `native/CMakeLists.txt`
   - Gate behind `DC3_GAMEPLAY_TESTS=1` env var (requires game assets)
   - `DC3_GAMEPLAY_TESTS=1 ctest --test-dir native/build -R GameplayTelemetry`

## Cross-reference: Hack Audit → Telemetry Test

| Hack Audit Phase | Hacks | Gate Test | Telemetry Key | Status |
|------------------|-------|-----------|---------------|--------|
| A1: `set_type "world"` | 1 | `VenueTypeDefSet` | `typeDef` | **Permanent** — hack is correct workaround, test validates it |
| A2: `transition_complete` | 3 | `NavTransitionsComplete` | `navAnimating` | **Actionable** — add OnMsg handler to HamNavList |
| A3: Player provider init | 1 | `WardrobeLoadsCorrectly` | `wardrobeSlot0` | Investigate |
| A4: HamProvider init | 6 | `HamProviderInitialized` | `hamProvider` | **Likely passes today** — init is early |
| B1: Move data guards | 12 | `MoveGraphLoaded` + `ClipTransitions` | `moveGraph`, `clip` | Assert-first validation |
| B2: PlayNextShot unify | 1 | `MergerDirAvailable` + `CameraSelection` | `mergerDir`, `cameraCount` | **May be permanent** — CameraManager check needed |
| D2: Intro timing | 1 | `BeatDrivenAnimation` | `beat` | Architectural decision |
| All complete | ~43 | `FullChoreography` | all keys | End goal |

## What This Catches

| Bug | Test | Tier |
|-----|------|------|
| song.anim frame stuck at 0 | `SongAnimAdvances` | 1 |
| Frame regression/reset | `SongAnimMonotonicallyIncreases` | 1 |
| Input script can't reach game | `EngineReachesGameScreen` | 1 |
| Crash after hack removal | `NoCrashDuringGameplay` | 1 |
| Wall-clock fallback removed | `SongAnimAdvances` | 1 |
| DTA set_type never fires | `VenueTypeDefSet` | 2 |
| OnSelectCamera never fires | `CameraSelection` | 2 |
| HamProvider null at poll time | `HamProviderInitialized` | 2 |
| Nav animations never complete | `NavTransitionsComplete` | 2 |
| Wardrobe loads stale data | `WardrobeLoadsCorrectly` | 2 |
| Move graph not ready | `MoveGraphLoaded` | 2 |
| Characters freeze on first clip | `ClipTransitions` | 2 |
| Beat never starts | `BeatStartsDuringGameplay` | 2 |
| Beat-driven mode broken | `BeatDrivenAnimation` | 2 |
| FileMerger Dir() null on native | `MergerDirAvailable` | 2 |
| Characters stop dancing mid-song | `FullChoreography` | 2 |

## Files to Create/Modify

| File | Action |
|------|--------|
| `native/src/telemetry/GameplayTelemetry.h` | New — emitter interface |
| `native/src/telemetry/GameplayTelemetry.cpp` | New — emitter implementation |
| `native/src/App.cpp` | Modify — call `GameplayTelemetry::Sample()` in main loop |
| `native/tests/telemetry_parser.h` | New — parser interface |
| `native/tests/telemetry_parser.cpp` | New — parser implementation |
| `native/tests/test_gameplay_telemetry.cpp` | New — tiered test suite |
| `native/tests/test_headless_boot.cpp` | Modify — add env var support to RunHeadless |
| `native/CMakeLists.txt` | Modify — add new files to milo-tests |

## Design Review Notes (2026-03-17)

Key decisions from staff review:

1. **Telemetry in native, not decomp** — avoids polluting decomp source with `#ifdef HX_NATIVE`.
   The emitter is a native-side observer that reads engine globals, not a change to
   `HamDirector::Poll()`.

2. **Two-tier taxonomy** — Tier 1 tests are regression guards (pass today). Tier 2 tests
   are the roadmap (fail today, each maps to engine work). `GTEST_SKIP()` with reason
   strings documents what's missing.

3. **Properties over thresholds** — "eventually advances" not "advances by frame 50".
   Survives timing changes.

4. **SetUpTestSuite, not SetUp** — single 3000-frame engine run shared across all tests.
   `SetUp()` runs per-test (GTest does NOT cache fixtures).

5. **Key-value telemetry** — extensible without parser changes. New subsystems emit new
   keys. New tests check new keys. No coordination needed.

6. **Failure diagnostics** — `TearDown` dumps full telemetry to file on assertion failure.
   Makes CI debugging practical.

7. **Tests gate hack removal** — every Tier 2 test maps 1:1 to a hack audit phase. You
   never remove a hack on faith — you remove it when the telemetry proves the natural
   DTA path works. See "Hack Removal Workflow" section.
