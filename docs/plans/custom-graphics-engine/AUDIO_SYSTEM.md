# Native Port: Phase 3 — Audio System

## Architecture Overview

DC3's audio pipeline has two sides:
- **Decode side** (DONE): `StreamReader` decoders produce PCM samples
- **Output side** (DONE): `StreamReceiver` feeds PCM to hardware

```
                         DECODE SIDE (done)
┌─────────────┐     ┌───────────────────┐     ┌──────────────────┐
│ .bik file   │────>│ FFmpegAudioReader │────>│                  │
└─────────────┘     └───────────────────┘     │  StandardStream  │
┌─────────────┐     ┌───────────────────┐     │  (ConsumeData)   │
│ .ogg/.mogg  │────>│ VorbisReader      │────>│                  │
└─────────────┘     └───────────────────┘     └────────┬─────────┘
                                                       │
                         OUTPUT SIDE (done)              │
                    ┌──────────────────────┐            │
                    │ StreamReceiverNative │<───────────┘
                    │ (ring buffer → PCM)  │   per-channel
                    └──────────┬───────────┘
                               │
                    ┌──────────▼───────────┐
                    │ miniaudio callback   │
                    │ (ma_device → ALSA/   │
                    │  PulseAudio/CoreAudio)│
                    └──────────────────────┘
```

## Library Choice: miniaudio

**miniaudio** — single-header C library, public domain. Supports ALSA, PulseAudio,
JACK, CoreAudio, WASAPI, AAudio. No link dependencies. Drop `miniaudio.h` into
`native/src/` and `#define MINIAUDIO_IMPLEMENTATION` in one .cpp.

Why miniaudio over SDL2/OpenAL:
- Header-only, no new build deps
- Callback-based (matches our ring buffer model perfectly)
- Low-latency by default
- Handles device enumeration, format conversion, resampling

## Implementation Plan

### Sub-phase 3.1: miniaudio Integration + Audio Device — COMPLETE

**Files**: `native/src/audio/AudioDevice.h`, `AudioDevice.cpp`, `miniaudio.h` (vendored)

Singleton `AudioDevice` wraps `ma_device`:
- `Init(sampleRate)` — open default output device (stereo f32, 512-frame period)
- `Terminate()` — close device
- `AddSource(AudioSource*)` / `RemoveSource(AudioSource*)` — thread-safe mixer
- `MixSources()` — audio callback sums all sources, clamps [-1,1]

**Tests** (5 in `test_audio.cpp`): singleton, init/terminate, sine render, source finish,
mixer add/remove. All passing.

### Sub-phase 3.2: StreamReceiverNative — COMPLETE

**File**: `native/src/platform/StreamReceiver_Native.h/.cpp`

Concrete `StreamReceiver` + `AudioSource` dual-inheritance:
- `PlayImpl()` — register with AudioDevice mixer
- `PauseImpl(bool)` — pause/resume
- `StartSendImpl(data, size, idx)` — copy 16-bit PCM into 64KB ring buffer
- `SendDoneImpl()` — returns true (non-blocking)
- `GetPlayCursor()` — returns current playback position
- `RenderAudio()` — converts int16→float, applies volume/pan, advances cursor
- Factory: `StreamReceiverNative::Create(numBuffers, sampleRate, slip, channel)`

Also defines `StreamReceiver::sFactory` and `StreamReceiver::New()` (static dispatch).

**Tests** (5 in `test_audio.cpp`): factory create, send+render, pause silence,
volume scaling, L/R pan. All passing.

### Sub-phase 3.3: Wire Into Synth + StandardStream — COMPLETE

**File**: `native/src/platform/Synth_Stub.cpp`

`NativeSynth` subclass of `Synth`:
- `Init()` — calls `Synth::Init()`, registers `StreamReceiverNative::Create` as
  `StreamReceiver::sFactory`, calls `AudioDevice::Init(44100)`
- `Terminate()` — calls `AudioDevice::Terminate()`, then `Synth::Terminate()`
- `NewStreamDecoder()` → returns `FFmpegAudioReader` for "bink" type (gated by HX_FFMPEG)

`CreateNativeSynth()` exported, called from `SynthPreInit()` via `#ifdef HX_NATIVE` in
`src/system/synth/Synth.cpp`.

**Tests** (2 in `test_audio.cpp`): Decode .bik audio and feed to StreamReceiverNative
(verified non-silent), play .bik audio through real AudioDevice (0.52s at 48kHz). All passing.

### Sub-phase 3.4: SampleInst (One-Shot Playback) — COMPLETE

**Files**: `native/src/platform/SampleInst_Native.h/.cpp`

`SampleInstNative` subclass of `SampleInst` + `AudioSource`:
- `StartImpl()` — reads PCM from SynthSample's SampleData, registers with AudioDevice
- `StopImpl()` — removes from AudioDevice mixer
- `RenderAudio()` — converts int16→float stereo with volume/pan, supports looping

**Wiring**: `SynthSample::NewInst()` overridden via `#ifdef HX_NATIVE` in header, with
implementation in `SampleInst_Native.cpp` that creates `SampleInstNative`.

### Sub-phase 3.5: OGG/Vorbis Streaming — COMPLETE

**Files**: `src/system/synth/VorbisReader.cpp` (native Poll/DoFileRead/Decrypt),
`src/system/synth/StandardStream.cpp` (kBuffering fix),
`src/system/synth/StreamReceiver.cpp` (WriteData/Poll native impl)

VorbisReader uses single-threaded main-loop decode on native (no background DecodeThread).
The constructor/destructor thread management is guarded with `#ifndef HX_NATIVE` to prevent
destructor infinite loop (stubbed DecodeThread never clears `mTerminating`).

Key fixes:
- `StandardStream::InitInfo` was missing `mState = kBuffering` after channel creation (decomp bug)
- `VorbisReader::Poll` early return removed — real OGG decode via `ogg_sync`/`vorbis_synthesis`
- `VorbisReader::DoFileRead` reads async file data, decrypts HMXA→OggS headers
- `HamAudio::Load` appends `.mogg` extension, `NativeSynth` sets `expectMap=true`

### Sub-phase 3.6: Audio Mixing + FX

Basic mixer in `AudioDevice` callback:
- Sum all active `StreamReceiverNative` outputs
- Apply per-channel volume/pan
- Apply master volume from `Synth::mMasterFader`

FX chain (`FxSend` subclasses — reverb, delay, etc.) can be stubbed initially.
The engine's `FxSend` system is mostly Xbox DSP-specific. For MVP, bypass all FX.

## File Summary

| File | Status | Sub-phase |
|------|--------|-----------|
| `native/src/audio/miniaudio.h` | **DONE** — vendored (95K lines) | 3.1 |
| `native/src/audio/AudioDevice.h` | **DONE** — singleton device + mixer | 3.1 |
| `native/src/audio/AudioDevice.cpp` | **DONE** — miniaudio callback impl | 3.1 |
| `native/src/platform/StreamReceiver_Native.h` | **DONE** — dual-inherit SR + AudioSource | 3.2 |
| `native/src/platform/StreamReceiver_Native.cpp` | **DONE** — ring buffer + factory + New() | 3.2 |
| `native/src/platform/Synth_Stub.cpp` | **DONE** — NativeSynth with full wiring | 3.3 |
| `native/src/platform/SampleInst_Native.h` | **DONE** — one-shot playback header | 3.4 |
| `native/src/platform/SampleInst_Native.cpp` | **DONE** — implementation + SynthSample::NewInst() | 3.4 |
| `native/tests/test_audio.cpp` | **DONE** — 12 tests (10 pass + 2 bik) | 3.1-3.4 |
| `native/CMakeLists.txt` | **DONE** — all sources added | 3.1 |
| `src/system/synth/Synth.cpp` | **DONE** — `#ifdef HX_NATIVE` for NativeSynth | 3.3 |
| `src/system/synth/SampleData.h` | **DONE** — added `DataPtr()` for LP64 | 3.4 |
| `src/system/synth/SynthSample.h` | **DONE** — added `GetSampleData()` accessor | 3.4 |
| `src/system/synth/VorbisReader.cpp` | **DONE** — native Poll/DoFileRead/Decrypt, thread guard | 3.5 |
| `src/system/synth/StandardStream.cpp` | **DONE** — `mState = kBuffering` decomp fix | 3.5 |
| `src/system/synth/StreamReceiver.cpp` | **DONE** — WriteData/Poll native implementation | 3.5 |

## Testing Strategy

All tests use the `MILO_TEST_BIK` environment variable (same as existing bink tests).
The `test_extract_bik.cpp` fixture already extracts `.bik` files from the game ark
to `/tmp/claude-1000/bik_fixtures/`. Audio tests can:

1. **Decode-only tests** (no audio device needed):
   - Decode `.bik` audio via FFmpeg, verify PCM format/samples
   - Feed PCM into `StreamReceiverNative`, verify ring buffer state

2. **Device tests** (need audio device, may be silent in CI):
   - Init `AudioDevice`, verify callback fires
   - Play a `.bik` file end-to-end, verify `GetTime()` advances
   - `GTEST_SKIP()` if no audio device available (headless CI)

3. **Integration tests** (need engine init):
   - `Synth::NewStream()` returns a real `StandardStream`
   - `Play()` / `Stop()` / `IsPlaying()` lifecycle

## Risks

- **miniaudio + sandbox**: The Claude sandbox may block audio device access.
  Tests should skip gracefully if `ma_device_init()` fails.
- **Thread safety**: miniaudio callback runs on a separate thread. The ring buffer
  in `StreamReceiver` base class handles this (write cursor vs play cursor).
- **Latency**: For a rhythm game, audio latency matters. miniaudio's default
  buffer size (~10ms) should be fine. Can tune later.
