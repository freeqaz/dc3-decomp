# Native StreamReceiver EOF Detection Bug

Date: 2026-03-25

## Symptom

Title ambience audio (`shell_dciambience.mogg`) persists into gameplay on the native/web port. The sound starts on the title screen and never stops, playing silently underneath the gameplay song forever.

## Investigation

### How the sound starts

`title.milo` references `common_bank.milo` as a sub-dir. When the title panel loads, `RndDir::HarvestPollables()` (which uses `ObjDirItr<RndPollable>(this, true)` — recursive) finds `titlescreen_amb.flow` inside common_bank and adds it to the title PanelDir's `mEnters` list.

When the title panel enters: `UIPanel::Enter()` → `PanelDir::Enter()` → `RndDir::Enter()` → `Flow::Enter()` on `titlescreen_amb.flow`. The flow's `mStartMode > 0` causes it to auto-execute. Inside, a `FlowSound` with `mImmediateRelease=true` calls `Sound::Play()` and returns immediately (fire-and-forget).

### How the sound stops on Xbox

The MoggClip (`shell_dciambience.mogg`) is non-looping (`mLoop=false`). The normal lifecycle:

1. `MoggClip::Play()` creates a `StandardStream` backed by `StreamReceiver` channels
2. `MoggClip::SynthPoll()` calls `mStream->PollStream()` each frame
3. `StandardStream::PollStream()` calls `StreamReceiver::Poll()` on each channel
4. Xbox `StreamReceiver::Poll()` manages a ring buffer. When `mEndData` is true (source exhausted) and a hardware buffer send completes, it increments `mDoneBufferCounter`
5. `StandardStream::PollStream()` checks `mChannels[0]->mDoneBufferCounter > mChannels[0]->mNumBuffers + 2` → transitions to `kFinished`
6. `MoggClip::SynthPoll()` sees `mStream->IsFinished()` → calls `Stop(0)` → stream destroyed

The sound plays once and ends naturally. No explicit stop needed.

### Why it never stops on native

`StreamReceiver::Poll()` has a `#ifdef HX_NATIVE` branch that skips the entire ring buffer management code. The native branch was only 4 lines:

```cpp
#ifdef HX_NATIVE
    if (mSending && SendDoneImpl()) {
        mSending = false;
        mBuffersSent++;
    }
#else
    // ... 60 lines of ring buffer logic including mDoneBufferCounter++ ...
#endif
```

`mDoneBufferCounter` was **never incremented** on native. `StandardStream` never reached `kFinished`. `MoggClip::SynthPoll()` never called `Stop()`. Every non-looping MoggClip stream ran forever (outputting silence after the data was consumed, but the stream object stayed alive).

## Root cause

`StreamReceiver::Poll()` native branch missing `mDoneBufferCounter` tracking.

This is a systemic bug affecting all non-looping MoggClip streams, not just title ambience. Any one-shot sound effect using MoggClip would leak a stream object on native.

## Fix

Added a virtual `IsOutputDrained()` method to `StreamReceiver` (native-only, `#ifdef HX_NATIVE`). The base class returns `true` (immediate completion). `StreamReceiverNative` overrides it to check `mPlayCursor >= mWriteCursor` — true only when the audio thread has consumed all buffered PCM.

The native branch of `StreamReceiver::Poll()` now increments `mDoneBufferCounter` when `mEndData && IsOutputDrained()`. This gives `StandardStream` the signal to transition to `kFinished`, which lets `MoggClip::SynthPoll()` call `Stop()`.

### Files changed

- `src/system/synth/StreamReceiver.h` — added `virtual bool IsOutputDrained() const`
- `src/system/synth/StreamReceiver.cpp` — native Poll() branch increments `mDoneBufferCounter`
- `native/src/platform/StreamReceiver_Native.h` — override `IsOutputDrained()` with actual playback check

## Status

**Fixed.** Two separate issues were found and addressed:

### Issue 1: `mDoneBufferCounter` never incremented (systemic)

The `StreamReceiver::Poll()` native branch skipped `mDoneBufferCounter`, so non-looping MoggClip streams never detected EOF. Fixed with `IsOutputDrained()` virtual method. This is a real bug fix for all non-looping MoggClip streams, but it was not the cause of the title ambience persisting.

### Issue 2: MoggClip is looping (title ambience specific)

Web diagnostic logging confirmed `mLoop=1` in the binary asset:
```
DC3 MOGG play file='sfx/samples/shell/shell_dciambience.mogg' loop=1 loopStart=0 loopEnd=-1
```

A looping stream never reaches EOF, so the `mDoneBufferCounter` fix has no effect. The sound loops forever unless explicitly stopped.

On Xbox, this is masked — the gameplay audio drowns it out, or the audio subsystem handles it differently. On native/web, the sound is audible alongside gameplay.

Fixed with an explicit `Sound::Stop()` call in `GamePanel::Enter()`, alongside the existing `TheMetaMusic->Kill()` hack. The sound is found via `TheSynth->Find<Sound>("shell_dciambience.snd", false)`.

### Issue 3: Stream-finished source removal freezes songMs (song-end hang)

When `IsFinished()` returned true, `AudioDevice::MixSources()` immediately removed the source. This stopped `RenderAudio()` calls, freezing `mPlayCursor` and `mTotalBytesPlayed`. Since `StandardStream::GetRawTime()` depends on `GetBytesPlayed()`, the frozen cursor caused `songMs` to stall. `MidiParser::Poll()` never reached the end-of-song MIDI event, so the DTA script `{$game_panel win}` never fired → gameplay hang.

On Xbox, hardware ring buffers keep advancing the play cursor through zero-filled buffers after `EndData()`, giving songMs enough time to reach the end event.

Fixed with two changes:
1. `RenderAudio()` — when `mEndData` and buffer empty, advance `mPlayCursor` through silence (replicates Xbox zero-fill behavior)
2. `IsFinished()` — gate on `mDoneBufferCounter > mNumBuffers + 2` to keep the source in the mixer long enough for songMs to reach the end event

### Files changed (final)

- `src/system/synth/StreamReceiver.h` — added `virtual bool IsOutputDrained() const` (Issue 1)
- `src/system/synth/StreamReceiver.cpp` — native Poll() branch increments `mDoneBufferCounter` (Issue 1)
- `native/src/platform/StreamReceiver_Native.h` — override `IsOutputDrained()`, gate `IsFinished()` on counter (Issues 1, 3)
- `native/src/platform/StreamReceiver_Native.cpp` — advance play cursor through silence after data exhaustion (Issue 3)
- `src/lazer/game/GamePanel.cpp` — explicit stop for `shell_dciambience.snd` on gameplay enter (Issue 2)

## Red herrings investigated

- **DTA scripts not executing on native** — they do execute, but `{meta music_stop}` only stops MetaMusic, not `shell_dciambience`
- **FlowSound fire-and-forget escaping Flow lifecycle** — true (`mImmediateRelease` means the sound is never tracked in `mRunningNodes`), and relevant because the sound IS looping (`mLoop=true` in asset), so it never ends naturally
- **Assumption that `mLoop=false`** — the constructor default is false, but the binary asset sets `mLoop=true` with `loopStart=0 loopEnd=-1` (infinite loop)
- **common_bank never receiving Enter()/Exit()** — common_bank is a plain `ObjectDir` (not `RndDir`), but `title.milo` references it as a sub-dir, so its flows ARE harvested into the title PanelDir's pollable lists

## Key code paths

| Step | File | Key line |
|---|---|---|
| common_bank loaded as sub-dir of title.milo | binary asset | `strings title.milo_xbox` → `../../sfx/common_bank.milo` |
| Flows harvested recursively | `rndobj/Dir.cpp` | `HarvestPollables()` uses `ObjDirItr<RndPollable>(this, true)` |
| Flow auto-starts on Enter | `flow/Flow.cpp:457` | `Flow::Enter()` checks `mStartMode > 0` |
| FlowSound fire-and-forget | `flow/FlowSound.cpp:92` | `mImmediateRelease` → `Play()` then return false |
| MoggClip EOF check | `synth/MoggClip.cpp:148` | `mStream->IsFinished()` → `Stop(0)` |
| StandardStream finish check | `synth/StandardStream.cpp:438` | `mDoneBufferCounter > mNumBuffers + 2` → `kFinished` |
| **Bug**: native Poll skips counter | `synth/StreamReceiver.cpp:87` | `#ifdef HX_NATIVE` branch had no `mDoneBufferCounter++` |
