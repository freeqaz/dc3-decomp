# Host-Driven Beat Counter Design — Bypassing Broken Audio Pipeline

*Date: 2026-03-30*

## Problem Statement

DC3 running in Xenia reaches `game_screen`, but character animation is frozen. The root cause: XMA audio decoding is broken, so `HamAudio::GetTime()` returns 0. The entire timeline chain is:

```
HamAudio::GetTime() → LiveInput::CurrentMs() → Game::Poll() → TheTaskMgr.SetSecondsAndBeat() → HamDirector reads TheTaskMgr.Beat() → BeatToSeconds(beat) * 30.0f → songAnim->SetFrame()
```

With audio returning 0, `songMs` stays at 0, `beat` stays at 0, and animation never advances.

## 1. TheTaskMgr Memory Layout

**Global address**: `0x82F64A58` (from linker map: `?TheTaskMgr@@3VTaskMgr@@A`)

### TaskMgr Object Layout (total ~0x90 bytes)

| Offset | Size | Type | Field |
|--------|------|------|-------|
| `+0x00` | `0x2C` | `Hmx::Object` | Base class (vtable, refs, name, dir, etc.) |
| `+0x2C` | `0x04` | `TaskTimeline*` | `mTimelines` — pointer to heap-allocated array[4] |
| `+0x30` | `0x18` | `SongPos` | `mSongPos` |
| `+0x48` | `0x01` | `bool` | `mAutoSecondsBeats` |
| `+0x4C` | `0x04` | `int` | `unk4c` |
| `+0x50` | `0x30` | `Timer` | `mTime` (wall clock) |
| `+0x80` | `0x04` | `float` | `mAVOffset` |
| `+0x84` | `0x0C` | `vector<ObjPtr<Task>>` | `unk84` (delete queue) |

### TaskTimeline Layout (each 0x1C bytes, allocated as array[4])

| Offset | Size | Type | Field |
|--------|------|------|-------|
| `+0x00` | `0x08` | `std::list<TaskInfo>` | `mTasks` |
| `+0x08` | `0x08` | `std::list<TaskInfo>` | `mAddedTasks` |
| `+0x10` | `0x04` | `float` | `mTime` — **THE value** |
| `+0x14` | `0x04` | `float` | `mLastTime` — previous frame's value |
| `+0x18` | `0x04` | `Task*` | `mPollingTask` |

### Timeline Index to Purpose

| Index | Enum | What it stores |
|-------|------|---------------|
| 0 | `kTaskSeconds` | Song time in seconds |
| 1 | `kTaskBeats` | Song time in beats |
| 2 | `kTaskUISeconds` | UI time in seconds |
| 3 | `kTaskTutorialSeconds` | Tutorial time |

### How to compute member addresses

```
TheTaskMgr = 0x82F64A58
mTimelines_ptr = load_and_swap<uint32_t>(TheTaskMgr + 0x2C)  // pointer to heap array

// Timeline[0] = kTaskSeconds
seconds_time     = mTimelines_ptr + 0x10   // float
seconds_lasttime = mTimelines_ptr + 0x14   // float

// Timeline[1] = kTaskBeats  (offset by 0x1C)
beats_time       = mTimelines_ptr + 0x1C + 0x10  = mTimelines_ptr + 0x2C
beats_lasttime   = mTimelines_ptr + 0x1C + 0x14  = mTimelines_ptr + 0x30

// mAutoSecondsBeats
auto_flag        = TheTaskMgr + 0x48       // bool, must be set to false
```

## 2. What SetSecondsAndBeat Writes

From `Task.cpp:53-57`:

```cpp
void TaskMgr::SetSecondsAndBeat(float seconds, float beat, bool resetLast) {
    mAutoSecondsBeats = false;           // +0x48 = false
    mTimelines[0].SetTime(seconds, resetLast);  // kTaskSeconds
    mTimelines[1].SetTime(beat, resetLast);      // kTaskBeats
}
```

And `SetTime` (from Task.h:108-111):
```cpp
void SetTime(float f, bool b) {
    mLastTime = b ? f : mTime;   // if b=false, mLastTime = old mTime
    mTime = f;
}
```

**To write from host, you must**:
1. Set `mAutoSecondsBeats` to `false` (0) at `TheTaskMgr + 0x48`
2. Read current `mTime` from `mTimelines[0] + 0x10`, store it at `mTimelines[0] + 0x14` (mLastTime)
3. Write new seconds to `mTimelines[0] + 0x10`
4. Read current `mTime` from `mTimelines[1] + 0x10`, store it at `mTimelines[1] + 0x14`
5. Write new beat to `mTimelines[1] + 0x10`

## 3. Beat Advancement Formula

### The Real Pipeline

The real game converts ms → tick → beat using the song's TempoMap and BeatMap:

```cpp
float MsToBeat(float ms) {
    return TheBeatMap->Beat(TheTempoMap->TimeToTick(ms));
}
```

At 120 BPM with standard MIDI resolution (480 ticks/beat):
- `mTempo = (60000/120) / 480 = 1.04167 ms/tick`
- `TimeToTick(ms) = ms / 1.04167`
- 1 second = 960 ticks = 2 beats
- **1 beat = 500 ms**

### Simplified Host-Side Formula

Without access to the actual TempoMap/BeatMap objects in guest memory (they are complex vtable-based objects requiring guest function calls to invoke), a linear approximation:

```
frame_interval_ms = 33.333   (Xenia runs at ~30 fps)
bpm = 120                     (typical DC3 song BPM — varies per song)
beats_per_ms = bpm / 60000.0
beat_delta = frame_interval_ms * beats_per_ms  = 33.333 * 0.002 = 0.06667 beats/frame
seconds_delta = frame_interval_ms / 1000.0     = 0.03333 seconds/frame
```

**Per NUI frame**:
```
songMs += 33.333
seconds = songMs / 1000.0
beat = songMs * (bpm / 60000.0)
```

### More Accurate: Use Guest TheTempoMap

If we want tempo-accurate beats (songs change BPM mid-track), we can call `MsToBeat()` via guest function invocation:

```
MsToBeat guest address: search for wrapper, or use:
  TheTempoMap = 0x82F18D44 (pointer)
  TheBeatMap  = 0x82F18C8C (pointer)

Better: call TaskMgr::SetSeconds(seconds, false) at 0x825A77E8
  This does the full TempoMap→BeatMap conversion internally:
    mTimelines[0].SetTime(f, b);
    mTimelines[1].SetTime(TheBeatMap->Beat(TheTempoMap->TimeToTick(f * 1000.0f)), b);
```

## 4. Subsystems That Depend on songMs/Beat

### Critical (will break without advancing time)

| Subsystem | How it reads time | Impact |
|-----------|------------------|--------|
| **HamDirector::OnSelectCamera** | `TheTaskMgr.Beat()` → `BeatToSeconds(beat) * 30.0f` | Drives songAnim frame = character animation |
| **HamDirector::Poll** | `TheTaskMgr.Beat()`, `TheTaskMgr.Seconds(kRealTime)`, `DeltaSeconds()` | Drives clip playback, backup dancers |
| **HamMaster::Poll** | Receives `songMs` as arg, calls `CalcSongPos()`, `CheckBeat()` | Fires beat/downbeat/halfbeat messages |
| **MoveDir::Poll** | `TheTaskMgr.CurrentMeasure()` to index moves | Move progression |
| **MoveDir::MoveBeat/MoveIdx** | `TheTaskMgr.CurrentBeat()`, `CurrentMeasure()` | Move scoring index |
| **Game::Poll** | Calls `SetSecondsAndBeat`, `SetSongPos` | Central timeline driver |

### Important (needs time but won't crash without it)

| Subsystem | Dependency |
|-----------|-----------|
| **MidiParserMgr::Poll** | Polled by Game::Poll when `songMs >= 0`; dispatches MIDI events |
| **SongPos on TheTaskMgr** | Written by Game::Poll via `CalcSongPos` — `mSongPos` at +0x30 |
| **TheHamProvider** | Receives "beat", "downbeat", "halfbeat", "quarterbeat" messages from HamMaster::CheckBeat |
| **PropertyEventProvider ("audio_channels")** | HamMaster::CheckLevels reads audio RMS — will get zeros with no audio (harmless) |

### Safe (won't crash if beat jumps)

| Subsystem | Notes |
|-----------|-------|
| **Shuttle** | Only active during seek operations, bypassed normally |
| **FlashCard/HUD** | Driven by MoveDir state which reads TheTaskMgr |
| **Game::SetHamMove** | Uses `CurrentBeat()` and `CurrentMeasure()` — read-only |

### Guard: Beat Jump Detection

`Game::Poll` has a safeguard at line 408:
```cpp
if (fabs(beat - sLastBeat) > 4.0f) {
    TheTaskMgr.ResetBeatTaskTime(beat);
}
```

This resets task start times when beat jumps more than 4.0. A smooth host-driven advancement (0.067 beats/frame) will NOT trigger this. But the first frame transition from 0 to a nonzero beat WILL trigger it if the delta > 4.0. This is harmless — it just readjusts scheduled task times.

## 5. Resetting LiveInput::mTimer vs. Driving TheTaskMgr

### Option A: Reset LiveInput::mTimer (Simpler — RECOMMENDED)

The `mRealTime = true` path in `PostWaitStart` already exists in the `#ifdef HX_NATIVE` code. When audio fails:

```cpp
// PostWaitStart (already implemented):
mPaused = false;
mRealTime = true;
mGameInput->SetTimeOffset();
```

Then `Game::Poll` calls:
```cpp
float songMs = mGameInput->CurrentMs(mRealTime);  // mRealTime=true
```

Which hits `LiveInput::CurrentMs(true)`:
```cpp
if (b1) {  // wall-clock path
    toAdd = mTimer.Ms() + mTimeOffset;
}
return TheGamePanel->DeJitter(GetSongToTaskMgrMs() + toAdd);
```

And then:
```cpp
TheTaskMgr.SetSecondsAndBeat(songMs * 0.001f, beat, false);
// where beat = MsToBeat(songMs + drift)
```

**The problem**: `MsToBeat()` requires `TheTempoMap` and `TheBeatMap` to be loaded. If `HamSongData::SetMaps()` was called (which happens in `Game::Reset()`), these globals are set. The question is whether the TempoMap object is valid when audio fails.

**Answer**: Yes! `HamSongData::Load()` loads the MIDI file and creates the TempoMap and BeatMap independently of audio. The MIDI file loads from the `.mid` file (not the `.mogg`). So `TheTempoMap` and `TheBeatMap` ARE valid even when XMA audio fails.

**LiveInput::mTimer** layout (LiveInput.h):
| Offset | Type | Field |
|--------|------|-------|
| `+0x00` | vtable | GameInput vtable |
| `+0x04` | `HamAudio&` | `mAudio` (reference) |
| `+0x08` | `float` | `mTimeOffset` |
| `+0x10` | `Timer` | `mTimer` (0x30 bytes) |
| `+0x44` | `int` | `unk44` |

The Timer uses `__mftb()` (PPC timebase register) for cycle counting. On Xbox 360, the timebase runs at ~49.875 MHz. The `Timer::sLowCycles2Ms` and `sHighCycles2Ms` static variables convert cycles to milliseconds. If the timer is started (mRunning > 0), `Timer::Ms()` returns elapsed time in ms.

**What the host needs to do**: Nothing special if `mRealTime=true` and `SetTimeOffset()` was called. The wall-clock timer naturally advances. The only failure mode is if `Game::HandleWait()` doesn't transition to `PostWaitStart()` because audio never becomes "ready".

### Option B: Direct TheTaskMgr Memory Write (Bypass everything)

Write `mTimelines[0].mTime` (seconds), `mTimelines[1].mTime` (beats), and their `mLastTime` values directly via `TranslateVirtual`. Also write `mSongPos` and call `HamMaster::Poll(songMs)` via guest invocation.

**Pros**: Completely decoupled from audio system
**Cons**: Misses SongPos updates, MidiParserMgr polling, CheckBeat messages, task timeline polling

### Recommendation: Option A with a Guest Function Call Failsafe

The existing `#ifdef HX_NATIVE` path in `Game::PostWaitStart()` and `Game::HandleWait()` already handles audio failure correctly. The real question is: **does this code path actually execute in Xenia?**

Looking at `HandleWait()`:
```cpp
if (audio->Fail()) {
#ifdef HX_NATIVE
    // Fall through to dispatch
#else
    return true;  // original: bail out, stay waiting forever
#endif
}
```

This `#ifdef HX_NATIVE` code only exists in the **native** build. In the **original debug XEX** running in Xenia, it hits `return true` — stuck forever waiting for audio that will never be ready.

**Therefore, the host must intervene.**

## 6. Concrete Implementation Plan

### Approach: Guest Function Override on `Game::HandleWait` + Direct Memory Write

Since we're running the **original debug XEX** (not native), the `#ifdef HX_NATIVE` fallbacks don't exist. We need to patch from the host side.

#### Phase 1: Force Audio-Failed Wait State Transition

Register a guest function override on `Game::PostWaitStart` (`0x82865128`) that also works when audio has failed.

OR simpler: bytepatch `Game::HandleWait` at the `audio->Fail()` branch to fall through instead of returning.

```
Game::HandleWait = somewhere in 0x828XXXXX range
  — find the "if (audio->Fail()) return true;" branch
  — NOP the conditional branch (change `beqlr` to `nop`)
```

This makes the original game behave like the native port's HX_NATIVE path.

#### Phase 2: Force mRealTime = true When Audio Fails

After `PostWaitStart` executes (which calls `mAudio->Play()` — will fail silently since audio is broken), the game sets `mRealTime = false`. We need it to be `true`.

**Option 1**: Guest function override on `PostWaitStart` that:
1. Calls the original function
2. Reads `Game + 0x50` → HamMaster → `+0x34` → HamAudio → check Fail()
3. If failed, writes `true` to `Game + 0x60` (mRealTime)
4. Calls `GameInput::SetTimeOffset()` via r3 = `Game + 0x54` (mGameInput)

**Option 2**: Periodic NUI-handler memory write:
```cpp
// In the NUI handler, every frame:
uint32_t game_addr = load_and_swap<uint32_t>(0x83116EC8);  // TheGame
if (game_addr) {
    uint8_t* game = memory->TranslateVirtual<uint8_t*>(game_addr);
    bool paused = load_and_swap<uint8_t>(game + 0x5E);
    bool realtime = load_and_swap<uint8_t>(game + 0x60);

    // If game is not paused and not realtime, force realtime mode
    // This makes LiveInput::CurrentMs use wall-clock instead of audio
    if (!paused && !realtime) {
        // Check if audio has failed
        uint32_t master_addr = load_and_swap<uint32_t>(game + 0x50);
        uint32_t audio_addr = load_and_swap<uint32_t>(
            memory->TranslateVirtual<uint8_t*>(master_addr) + 0x34);
        // HamAudio::Fail() checks mSongStream && mSongStream->Fail()
        // Rather than walking the vtable, just force mRealTime=true always
        store_and_swap<uint8_t>(game + 0x60, 1);  // mRealTime = true
    }
}
```

#### Phase 3: Ensure SetTimeOffset Gets Called

`LiveInput::SetTimeOffset()` must be called once after `mRealTime` becomes true. It computes:
```cpp
mTimeOffset = (TheTaskMgr.Seconds(kRealTime) * 1000.0f) - mTimer.SplitMs()
              - TheProfileMgr.GetSongToTaskMgrMs(kGame);
```

This calibrates the wall-clock timer to match TaskMgr's current time.

**Simplest approach**: Call `SetTimeOffset` via guest function invocation once:
```cpp
// Get LiveInput* from Game + 0x54
uint32_t input_addr = load_and_swap<uint32_t>(game + 0x54);
// SetTimeOffset is a virtual call - resolve from vtable
// GameInput vtable: slot 3 = SetTimeOffset (0-indexed: dtor=0, CurrentMs=1, GetSongToTaskMgrMs=2, SetPaused=3, SetTimeOffset=4)
// OR call LiveInput::SetTimeOffset directly if we find its address
```

From the map file, `LiveInput::SetTimeOffset` is not directly listed (it's part of the game library), but we can find it via the vtable.

**Alternative**: Write `mTimeOffset` directly:
```cpp
// LiveInput at Game + 0x54
uint32_t input_addr = load_and_swap<uint32_t>(game + 0x54);
uint8_t* input = memory->TranslateVirtual<uint8_t*>(input_addr);
// mTimeOffset is at +0x08
// Set to 0 — simplest. The wall-clock timer starts from ~0 at game boot,
// and TaskMgr seconds starts from ~0 at game start. Close enough.
store_and_swap<float>(input + 0x08, 0.0f);
```

#### Phase 4: Let Game::Poll Do the Rest

Once `mRealTime = true` and `SetTimeOffset` is calibrated, `Game::Poll` naturally does:

```cpp
float songMs = mGameInput->CurrentMs(true);  // wall-clock path
float beat = MsToBeat(songMs + drift);        // TempoMap conversion
TheTaskMgr.SetSecondsAndBeat(songMs * 0.001f, beat, false);
mSongPos = mSongDB->CalcSongPos(TheMaster, ms);
TheTaskMgr.SetSongPos(mSongPos);
mMaster->Poll(songMs);  // fires beat events, CheckBeat, etc.
```

**Everything downstream works automatically**: HamDirector reads `TheTaskMgr.Beat()`, converts to frame, drives `songAnim->SetFrame()`, and character animation plays.

### Pseudocode for NUI Handler

```cpp
// dc3_hack_pack.cc — in the NUI frame handler
static bool sAudioFailHandled = false;
static int sFrameCount = 0;

void DC3_NuiFrameHandler() {
    sFrameCount++;

    // Wait for game to be fully set up (HandleWait transitions complete)
    if (sFrameCount < 300) return;  // ~10 seconds at 30fps

    uint32_t game_ptr = load_and_swap<uint32_t>(
        memory->TranslateVirtual<uint8_t*>(0x83116EC8));  // TheGame
    if (!game_ptr) return;

    uint8_t* game = memory->TranslateVirtual<uint8_t*>(game_ptr);
    bool paused = game[0x5E];  // mPaused (single byte, no endian swap needed for bool)

    if (paused) return;  // Game hasn't started yet

    if (!sAudioFailHandled) {
        // Force mRealTime = true
        game[0x60] = 1;

        // Zero out mTimeOffset on the LiveInput
        uint32_t input_ptr = load_and_swap<uint32_t>(game + 0x54);
        if (input_ptr) {
            uint8_t* input = memory->TranslateVirtual<uint8_t*>(input_ptr);
            store_and_swap<float>(input + 0x08, 0.0f);  // mTimeOffset

            // Also restart the timer — set mStart to current __mftb value
            // Timer at input + 0x10:
            //   +0x00 (mStart) = uint32
            //   +0x08 (mCycles) = uint64 — set to 0
            //   +0x24 (mRunning) = int — ensure > 0
            store_and_swap<uint64_t>(input + 0x10 + 0x08, 0);  // mCycles = 0
            // mStart will be set by next Timer::Split() call
            int running = load_and_swap<int32_t>(input + 0x10 + 0x24);
            if (running <= 0) {
                store_and_swap<int32_t>(input + 0x10 + 0x24, 1);  // mRunning = 1
            }
        }

        sAudioFailHandled = true;
    }
}
```

### Alternative: Simpler Bytepatch Approach (RECOMMENDED)

Instead of the NUI handler complexity, bytepatch two locations:

**Patch 1: HandleWait audio fail branch**

Find the `beq` / `bne` instruction in `Game::HandleWait` that tests `audio->Fail()` and branches to `return true`. Change it to fall through. This is a single instruction NOP.

**Patch 2: PostWaitStart — force mRealTime = true**

After the `audio->Play()` call in `PostWaitStart`, the game sets `mRealTime = false`. Bytepatch this to `li r0, 1` + `stb r0, 0x60(game)`.

Or simpler: register a `RegisterGuestFunctionOverride` on `PostWaitStart` (0x82865128) that:
1. Always sets `mRealTime = true` (write to r3+0x60 = 1)
2. Always sets `mPaused = false` (write to r3+0x5E = 0)
3. Calls `SetTimeOffset` via vtable on r3+0x54

This is ~15 lines of host C++ and completely non-invasive.

## 7. Risk Assessment

### Low Risk
- **Beat messages**: `HamMaster::CheckBeat()` fires beat/downbeat/halfbeat messages. With wall-clock time, these fire at correct intervals. No crashes expected.
- **MoveDir::Poll**: Reads `TheTaskMgr.CurrentMeasure()`. Will advance normally. Move scoring may not work perfectly (no real player input), but won't crash.
- **SongPos**: `CalcSongPos` is called in `Game::Poll` and works correctly with wall-clock ms values as long as TempoMap/BeatMap are loaded.

### Medium Risk
- **HamMaster::Poll guards**: `Poll()` checks `IsLoaded() && mAudio->GetSongStream()`. If `GetSongStream()` returns null (because audio failed), the entire Poll body is skipped — no `CalcSongPos`, no `CheckBeat`, no `mMidiParserMgr->Poll()`. This means **beat messages won't fire** and **MIDI events (including "end") won't dispatch**.
  - **Mitigation**: The `#ifdef HX_NATIVE` block at the bottom of `HamMaster::Poll` calls `mMidiParserMgr->Poll()` outside the guard, but this only exists in the native build, not the original XEX. For the original XEX, we may need to override `HamMaster::Poll` to call `CheckBeat()` even without a valid SongStream.
  - **Workaround**: The animation system itself (`HamDirector::Poll/OnSelectCamera`) reads from `TheTaskMgr.Beat()` directly, NOT from `HamMaster::mSongPos`. So character animation will work even without `HamMaster::Poll` running fully.

- **DeJitter**: `GamePanel::DeJitter()` applies jitter smoothing to `songMs`. With wall-clock input this should work fine, but if the jitter buffer hasn't been initialized (never called `ResetJitter`), it could produce garbage. `Game::Reset()` calls `TheGamePanel->ResetJitter()`, so this should be safe.

### High Risk
- **Timer precision**: On Xbox 360, `__mftb()` returns the PPC timebase (~49.875 MHz). In Xenia, this is emulated. If Xenia's timebase emulation is inaccurate or runs at different speed, wall-clock time may drift. At worst, animation plays too fast or too slow.
  - **Mitigation**: Xenia's timebase emulation is well-tested — it's used by many games. Unlikely to be a problem.

- **Song duration**: Without audio, there's no natural "end of song" signal. `HamMaster::SongDurationMs()` reads from MIDI events, which IS available. But the "end" MIDI event dispatch (via MidiParserMgr) requires `HamMaster::Poll` to run its full path. Without it, the game may never detect song completion.
  - **Mitigation**: For animation testing purposes, this is fine — we just want to see characters dance. For gameplay completion testing, we'd need the MidiParserMgr override mentioned above.

### Won't Happen
- **Crash from beat jumps**: The beat-jump guard in `Game::Poll` (`fabs(beat - sLastBeat) > 4.0`) handles jumps gracefully by calling `ResetBeatTaskTime`. No crash risk.
- **TempoMap null**: `Game::Reset()` calls `mMaster->SetMaps()` which sets `TheTempoMap` and `TheBeatMap` globals. These are loaded from the MIDI file independently of audio.

## 8. Can We Call PostWaitStart + SetTimeOffset from Host?

**Yes.** Xenia's `Processor::Execute(thread_state, addr, args, count)` supports calling arbitrary guest functions.

### Key Addresses (from linker map)

| Function | Guest Address |
|----------|--------------|
| `Game::PostWaitStart` | `0x82865128` |
| `TaskMgr::SetSecondsAndBeat` | `0x825A7798` |
| `TaskMgr::SetSeconds` | `0x825A77E8` |
| `HamMaster::Poll` | `0x82523208` |
| `Game::Poll` | `0x82867FB8` |

### Calling Convention

PPC calling convention: `r3` = this pointer, `r4`-`r10` = args, `f1`-`f8` = float args.

```cpp
// Call Game::PostWaitStart()
uint64_t args[] = { game_ptr };  // r3 = this
processor->Execute(thread_state, 0x82865128, args, 1);
```

For `SetTimeOffset()` (virtual call), resolve through vtable:
```cpp
// GameInput vtable layout:
//   slot 0: ~GameInput (dtor)
//   slot 1: CurrentMs
//   slot 2: GetSongToTaskMgrMs
//   slot 3: SetPaused
//   slot 4: SetTimeOffset
//   slot 5: SetPostWaitJumpOffset
uint32_t input_ptr = load_and_swap<uint32_t>(game + 0x54);
uint32_t vtable = load_and_swap<uint32_t>(
    memory->TranslateVirtual<uint8_t*>(input_ptr));
uint32_t set_time_offset_addr = load_and_swap<uint32_t>(
    memory->TranslateVirtual<uint8_t*>(vtable + 4*4));  // slot 4

uint64_t args[] = { input_ptr };
processor->Execute(thread_state, set_time_offset_addr, args, 1);
```

**Caveat**: Guest function invocation must happen from a valid thread state. The NUI handler runs in a host thread — it may or may not have a valid `ThreadState`. If it does (Xenia creates one per hardware thread), this works. If not, you'll need to use `Processor::ExecuteInterrupt()` or queue the call for the main guest thread.

## Summary: Recommended Approach

1. **Bytepatch `HandleWait`** to fall through on `audio->Fail()` (1 instruction NOP)
2. **Override `PostWaitStart`** to set `mRealTime = true` and call `SetTimeOffset` when audio fails
3. **Everything else works automatically**: `Game::Poll` drives `CurrentMs(true)` → wall-clock → `MsToBeat()` → `SetSecondsAndBeat()` → animation plays

Total host-side code: ~30 lines. No periodic NUI handler needed. The game's own main loop handles all time advancement once `mRealTime = true`.
