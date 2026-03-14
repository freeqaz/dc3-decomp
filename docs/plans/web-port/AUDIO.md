# DC3 Web Port — Audio Subsystem

## Research Summary

Investigation of the DC3 engine's audio pipeline to determine the best approach for web port audio playback. Covers file formats, decoding architecture, encryption, and browser integration options.

---

## Engine Audio Architecture

### File Formats

| Format | Usage | Decoder | Size |
|--------|-------|---------|------|
| `.mogg` | Song audio (gameplay) | VorbisReader (libvorbis + AES-CTR decryption) | ~2-3 MB/song |
| `_prev.bik` | Song preview video+audio | FFmpegAudioReader (native) / stubbed (web) | ~1-2 MB |
| `.wav` | SFX samples | StandardStream | varies |
| `.ogg` | Plain Ogg Vorbis | VorbisReader (no decryption) | varies |

### MOGG Format

MOGG is Harmonix's encrypted multi-channel Ogg Vorbis container. **Not playable by browsers natively.**

Structure:
```
Bytes 0-3:     Version (0x0A-0x10)
Bytes 4-7:     Header size
Bytes 8+:      OggMap (seek table: granule → byte offset pairs)
               Nonce (16 bytes, AES-CTR IV)
               mMagicA, mMagicB (key derivation seeds)
               Key mask (16 bytes, Rijndael-ECB encrypted)
Byte [hdrSize]: AES-CTR encrypted Ogg Vorbis stream
                Pages start as "HMXA" (decrypted → "OggS")
```

Decryption is mandatory and on-the-fly — `VorbisReader::Decrypt()` processes every 4KB read buffer through AES-CTR before feeding to `ogg_sync_pageout()`. The crypto uses `ByteGrinder` (key derivation) + tomcrypt's `ctr_decrypt()`.

### Channel Layout (DC3 Songs)

Analyzed all 62 songs in `songs.dta`:

| Channels | Count | Usage |
|----------|-------|-------|
| 2 (stereo) | 57 | All normal songs — pre-mixed L+R, single "drum" track |
| 8-12 | 5 | Campaign mashups (`hb_camp_*`) — `is_fake=TRUE`, not playable in quickplay |

**Per-stem mixing is not needed for normal songs** — they're pre-mixed stereo. The multi-channel songs are campaign-exclusive fake entries.

### Audio Pipeline

```
MOGG File
    ↓
VorbisReader (decrypt AES-CTR → decode Vorbis → per-channel PCM float arrays)
    ↓
StandardStream::ConsumeData (float → int16_t, distribute to per-channel receivers)
    ├→ StreamReceiver[0]::WriteData(mono PCM)
    ├→ StreamReceiver[1]::WriteData(mono PCM)
    └→ ...
    ↓
StreamReceiverNative ring buffers (64KB mono each)
    ↓
RenderAudio() callback (int16 → float, apply pan/volume, output interleaved stereo)
    ↓
AudioDevice::MixSources() (sum all sources, clamp [-1,1])
    ↓
Audio output device
```

Key classes:
- `VorbisReader` — Ogg/MOGG decoder with encryption support (`src/system/synth/VorbisReader.h`)
- `StandardStream` — channel coordinator, manages StreamReceivers (`src/system/synth/StandardStream.h`)
- `StreamReceiverNative` — per-channel ring buffer + stereo panning (`native/src/platform/StreamReceiver_Native.h`)
- `AudioDevice` — mixer/output via miniaudio (`native/src/audio/AudioDevice.h`)
- `MoggClip` — high-level MOGG playback API (`src/system/synth/MoggClip.h`)
- `HamAudio` — gameplay audio controller, per-channel faders (`src/lazer/game/HamAudio.h`)

### Native Port Audio (Desktop Linux)

- **miniaudio** (vendored, 77K lines) handles audio I/O via ALSA/PulseAudio
- `AudioDevice.cpp` initializes `ma_device` with stereo f32 output, 512-frame period (~10ms)
- `Synth_Stub.cpp` registers `StreamReceiverNative::Create` as the receiver factory
- FFmpeg (`libavformat`/`libavcodec`) decodes `.bik` preview audio — disabled on web

### Current Web Port State

Audio is **completely stubbed** in `web_stubs.cpp`:
- `AudioDevice::Init()` returns `true` but does nothing
- `AddSource()` / `RemoveSource()` are no-ops
- `MixSources()` writes zeros (silence)
- `StreamReceiverNative` compiles and runs, but output goes nowhere
- VorbisReader + libvorbis/libogg are linked and functional in WASM
- FFmpeg is disabled (`if(ENABLE_FFMPEG AND NOT EMSCRIPTEN)`)

---

## Options Evaluated

### Option 1: miniaudio ScriptProcessorNode (no ASYNCIFY)

miniaudio has a built-in Emscripten backend using the deprecated `ScriptProcessorNode` API. When `MA_ENABLE_AUDIO_WORKLETS` is not defined, it falls back to this path automatically.

| Pros | Cons |
|------|------|
| Minimal new code — compile real `AudioDevice.cpp` for web | ScriptProcessorNode is deprecated (still works everywhere, unlikely to be removed) |
| Existing pipeline works as-is | Runs on **main thread** — audio processing competes with rendering |
| No ASYNCIFY needed | Higher latency (~50-100ms) |
| No JS code to write | |

### Option 2: Custom AudioWorkletNode + SharedArrayBuffer (chosen)

Write a thin AudioWorklet processor in JS that pulls PCM from a SharedArrayBuffer ring buffer. WASM pushes mixed audio data from `MixSources()`.

| Pros | Cons |
|------|------|
| Audio runs on **dedicated thread** — zero main-thread overhead | ~100 lines of new JS + C++ glue |
| Modern API (AudioWorklet), not deprecated | Requires COOP/COEP headers (already set in server.py) |
| Low latency (~10-20ms) | SharedArrayBuffer needs careful ring buffer design |
| No ASYNCIFY needed — push model, not blocking | |
| Future-proof | |

### Option 3: Server-side MOGG → OGG + browser `<audio>`/Web Audio

Pre-decrypt MOGGs to plain OGG on server, play via browser native APIs.

| Pros | Cons |
|------|------|
| Zero WASM audio overhead | Must pre-process all 48 songs |
| Browser handles decoding natively | Loses engine timing sync (no sample-accurate position) |
| | No per-channel stem control (irrelevant for stereo, but architecturally limiting) |
| | Seek granularity limited by browser implementation |
| | Must rewrite audio architecture to bridge JS↔WASM for play/stop/seek |

### Option 4: miniaudio AudioWorklet (requires ASYNCIFY)

Enable `MA_ENABLE_AUDIO_WORKLETS` in miniaudio for its built-in modern path.

| Pros | Cons |
|------|------|
| Zero custom code | **ASYNCIFY required** — 20-40% WASM binary bloat (6.4MB → ~8-9MB) |
| miniaudio handles everything | 2-10% runtime performance penalty |
| | `emscripten_sleep()` busy-wait during init is architecturally ugly |

### Option 5: Emscripten SDL_mixer

Use `-sUSE_SDL_MIXER=2` for Emscripten's built-in SDL2_mixer with OGG support.

| Pros | Cons |
|------|------|
| Simple API | Requires significant refactoring to replace StandardStream |
| Low integration effort | No sample-accurate position tracking |
| | Can't integrate with engine's fader/volume system |

---

## Decision: Option 2 (Custom AudioWorklet)

**Rationale**: Audio off the main thread with no ASYNCIFY penalty. The implementation is small (~100 lines JS, ~50 lines C++ adapter) and gives us full control. The existing WASM audio pipeline (VorbisReader → StandardStream → StreamReceiver) remains unchanged — only the final output stage changes.

**Why not Option 1 (ScriptProcessorNode fallback)**: Runs on main thread, competing with rendering. For a rhythm game running at ~1-3 FPS in WASM, audio glitches from main-thread contention would be noticeable. If Option 2 fails, Option 1 is the immediate fallback with near-zero implementation cost.

**Why not Option 3 (server-side conversion)**: Loses integration with the engine's audio timing system. Would require a parallel JS-side audio controller with message passing for play/pause/seek/volume, duplicating what the engine already does well in WASM.

**Why not Option 4 (ASYNCIFY)**: 20-40% binary bloat for a single `emscripten_sleep()` call during audio init is not acceptable.

---

## Implementation Plan

### Architecture

```
┌─────────────────────────────────────────────────────┐
│  WASM Main Thread                                   │
│                                                     │
│  VorbisReader → StandardStream → StreamReceiverNative
│                                      │              │
│                            AudioDevice::MixSources()│
│                                      │              │
│                              ┌───────▼────────┐     │
│                              │ SharedArrayBuffer│     │
│                              │  Ring Buffer    │     │
│                              │  (PCM f32 stereo)│    │
│                              └───────┬────────┘     │
└──────────────────────────────────────┼──────────────┘
                                       │
┌──────────────────────────────────────┼──────────────┐
│  AudioWorklet Thread                 │              │
│                              ┌───────▼────────┐     │
│                              │ SharedArrayBuffer│     │
│                              │  (same memory)  │     │
│                              └───────┬────────┘     │
│                                      │              │
│                         AudioWorkletProcessor       │
│                              process()              │
│                                      │              │
│                              Speaker Output         │
└─────────────────────────────────────────────────────┘
```

### Step 1: AudioWorklet JS Processor

Create `native/web/audio-worklet.js`:
- `class DC3AudioProcessor extends AudioWorkletProcessor`
- Receives SharedArrayBuffer via port message during init
- `process()` reads from ring buffer, writes to output channels
- Lock-free: uses `Atomics.load/store` on read/write cursors

### Step 2: AudioDevice_Web.cpp

Replace the `web_stubs.cpp` AudioDevice stub:
- `Init()`: Create `AudioContext` + register worklet via `EM_ASM`
- Allocate SharedArrayBuffer for ring buffer (~64KB stereo float)
- `MixSources()`: Mix all registered AudioSources into local buffer, then copy to SharedArrayBuffer ring
- `AddSource()`/`RemoveSource()`: Thread-safe source management (same as desktop impl)
- Cursor management via `Atomics` for lock-free producer/consumer

### Step 3: CMakeLists.txt Changes

- Add `src/audio/AudioDevice_Web.cpp` to web sources (or `#ifdef __EMSCRIPTEN__` in AudioDevice.cpp)
- Remove AudioDevice stubs from `web_stubs.cpp`
- Serve `audio-worklet.js` from the dev server

### Step 4: Browser Audio Activation

Browsers require user gesture to start `AudioContext`. Add click/keypress handler in `index.html` to call `audioContext.resume()` on first interaction.

### Step 5: Testing

- Verify shell music / menu SFX plays
- Navigate to song select, confirm preview audio (requires `.bik` handling or skip)
- Select song, verify MOGG decryption + playback during gameplay
- Check for glitches, underruns, latency

### Fallback

If AudioWorklet + SharedArrayBuffer proves problematic (browser compat, timing issues):
- **Immediate fallback**: Option 1 — compile desktop `AudioDevice.cpp` with miniaudio's ScriptProcessorNode path. Single-line change: include `AudioDevice.cpp` in web sources, remove stub.

---

## Prerequisites Already in Place

- **COOP/COEP headers**: `server.py` already sends `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` (required for SharedArrayBuffer)
- **Vorbis/OGG libs**: Linked in web build, functional in WASM
- **StreamReceiverNative**: Compiles for web, ring buffer logic works
- **Synth_Stub.cpp**: Registers receiver factory, calls `AudioDevice::Init(44100)`
- **MOGG decryption**: ByteGrinder + tomcrypt AES-CTR compiled for WASM

## Decisions

- **Preview audio**: Deferred to Phase 7b. The server already transcodes Bink videos via FFmpeg (`scripts/web/transcode_bink.py`); extend that pipeline to also extract audio to OGG for song previews.
- **Shell music**: Deferred to Phase 7c. Serve `sfx/samples/shell/` files from assets once the audio system is working.
- **Sample rate**: Force `{sampleRate: 44100}` on `AudioContext` constructor for MVP. Phase 7d: add ring buffer resampling for browsers that don't honor the requested rate.
- **User gesture**: First keypress on canvas triggers `audioContext.resume()`. The game requires keyboard input to navigate, so this happens naturally before any audio is needed.
- **Audio latency**: For the web demo (no scoring), ~50ms is acceptable.

---

## Phasing

| Phase | Scope | Status |
|-------|-------|--------|
| **7a** | Core audio output: AudioWorklet + SharedArrayBuffer ring buffer, MOGG playback | **Done** |
| **7b** | Video playback: WebMovieImpl via browser `<video>` + pre-transcoded .webm | **Done** |
| **7c** | Shell/menu music: serve sfx/samples files, background music playback | Later |
| **7d** | Sample rate resampling: ring buffer consumer handles 44.1→48kHz mismatch | Later |
