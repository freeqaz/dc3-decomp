# Audio and Video — Native Reference

How DC3's audio and FMV pipelines run on the native (desktop) and web builds, and which files actually link.

Last verified against the tree: 2026-11-13 (np-docs @ 2d678db7e).

## Which copy links

Most native audio platform files exist twice: a DC3-local copy under `native/src/` and the
engine copy under `../milo-native-engine/src/`. Every engine-linking target (dc3-native,
milo-viewer, render-test, milo-tests, dc3-web, the export tools) drops the DC3 copy via the
`list(REMOVE_ITEM DC3_NATIVE_CORE_SOURCES_ENGINE ...)` block in `native/CMakeLists.txt` and
links the engine's. For audio that means the linked files are:

| Role | Linked file |
|---|---|
| Output device + mixer (miniaudio) | `../milo-native-engine/src/audio/AudioDevice.cpp` (+ vendored `miniaudio.h`) |
| Web output device (AudioWorklet + SharedArrayBuffer) | `../milo-native-engine/src/audio/AudioDevice_Web.cpp` |
| `NativeSynth` / `CreateNativeSynth()` | `../milo-native-engine/src/platform/Synth_Stub.cpp` |
| Per-channel stream receiver | `../milo-native-engine/src/platform/StreamReceiver_Native.cpp` |
| One-shot SFX playback | `../milo-native-engine/src/platform/SampleInst_Native.cpp` |
| FX sends | `../milo-native-engine/src/platform/FxSendNative.cpp` |
| XMA SFX decode (desktop, FFmpeg) | `native/src/platform/XmaSampleDecoder.cpp` (DC3-local, not removed) |
| Web XMA->PCM / Vorbis sidecars | `native/src/platform/XmaPcmSidecar.h` + `native/src/platform/StbVorbisImpl.cpp` (DC3-local) |

The DC3-local copies of `Synth_Stub.cpp`, `StreamReceiver_Native.cpp`, `SampleInst_Native.cpp`
and `FxSendNative.cpp` are currently byte-identical to the engine's; `native/src/audio/AudioDevice.cpp`
is not. Edit the engine copy.

## Song audio pipeline (desktop)

```
.mogg  -> VorbisReader (src/system/synth/VorbisReader.cpp)
            AES-128-CTR decrypt (libtomcrypt), "HMXA" page header -> "OggS"
            ogg_sync / vorbis_synthesis decode
       -> StandardStream::ConsumeData (src/system/synth/StandardStream.cpp)
            distributes PCM to one StreamReceiver per channel
       -> StreamReceiverNative (64 KB ring buffer per channel, kPCMBufSize = 0x10000)
            RenderAudio(): int16 -> float, per-channel volume/pan
       -> AudioDevice::MixSources() (sum of all AudioSources)
       -> miniaudio device (f32 stereo, periodSizeInFrames = 512)
```

- **Factory wiring.** `SynthPreInit` in `src/system/synth/Synth.cpp` calls `CreateNativeSynth()`
  under `#ifdef HX_NATIVE`. `NativeSynth::Init()` registers `StreamReceiverNative::Create` as
  `StreamReceiver::sFactory` and calls `AudioDevice::GetInstance().Init(44100)`.
- **Decoder selection** (`NativeSynth::NewStreamDecoder`): `"bink"` -> `FFmpegAudioReader` (only
  under `HX_FFMPEG`); `"ogg"`/`"mogg"` -> `VorbisReader`, with `expectMap = (type == "mogg")`
  (the .mogg header carries an OggMap and the encryption fields). `NewStreamFile` maps the file
  extension to the codec symbol; with no extension it tries `<path>.mogg`, then `<path>.ogg`.
  `NewBufStream` sniffs `OggS` vs a mogg version byte because `HamAudio` passes a mixer bus name,
  not a codec type.
- **Audio offset.** `NativeSynth::Init()` reads `synth { audio_offset_ms <float> }` from the system
  config into `StandardStream::sAudioOffsetMs`, which `StandardStream` adds to its reported stream time.
- **No decode thread on native.** The Xbox build decodes on a background thread that calls
  `VorbisReader::Decrypt`. On native, `DoFileRead` decrypts inline through the file-local helper
  `NativeDecrypt` in `VorbisReader.cpp`, which also reverses the HMXA anti-tamper only at the start
  of each decrypted block, as the image does.
- **StandardStream states:** `kInit`, `kReady`, `kPlaying`, `kFinished`, and so on; see the enum in
  `src/system/synth/StandardStream.h`. `kBuffering` is entered once channels are created.

### MOGG format

Read in `VorbisReader` (`src/system/synth/VorbisReader.cpp`, header parse around the
`mVersion` asserts):

- `mVersion` (asserted 10..16), then `mHdrSize`, then the OggMap (seek table).
- **Version 0xB:** a 16-byte nonce, then AES-CTR keyed with the built-in `gRB1Key`.
- **Versions 0xC..0x10:** a nonce, `mMagicA`/`mMagicB`, a key mask decrypted by
  `TheSynth->Grinder().HvDecrypt`, and a key index. Then `setupCypher` derives the key via
  `KeyChain` + `Grinder().GrindArray` and starts AES-CTR.
- Other versions log "old mogg version!".
- Audio starts at byte `mHdrSize`.

### Channel layout

Normal songs are pre-mixed stereo. A handful of entries carry 8–12 channels. Measured 2026-11-13
by counting `(pans ...)` entries in `orig-assets/extracted/songs/songs.dta`: 62 entries, 57 with 2
pans, 5 with 8, 10 or 12.

```bash
python3 -c "import re,collections;t=open('orig-assets/extracted/songs/songs.dta',errors='replace').read();print(collections.Counter(len(p.split()) for p in re.findall(r'\(pans\s*\(([^)]*)\)',t)))"
```

## SFX

- **Desktop:** `SampleData::Load` decodes XMA samples to PCM at load time via `DecodeXMAToPCM`
  (`native/src/platform/XmaSampleDecoder.h`, FFmpeg's xma2 codec, `HX_FFMPEG` only).
  `SampleInstNative` plays them.
- **Web (no FFmpeg):** `native/src/platform/XmaPcmSidecar.h` bridges the gap.
  - A native run with FFmpeg writes each decoded XMA sample as a PCM sidecar keyed by a hash of
    the XMA payload, into the directory named by `DC3_SFX_PCM_DIR` (`RB3_SFX_PCM_DIR` is shared
    with rb3).
  - The web build loads the sidecar instead.
  - `scripts/web/gen_sfx_ogg.py` transcodes each `<key>.pcm` to a compact `<key>.ogg` next to it.
    The loader tries `.ogg` first (decoded with the vendored `stb_vorbis.h`, implementation
    compiled once in `StbVorbisImpl.cpp`) and falls back to `.pcm`.
  - Default sidecar dir: `orig-assets/extracted/sfx/gen/xma_pcm`.

## Web output

`../milo-native-engine/src/audio/AudioDevice_Web.cpp` (built only under `__EMSCRIPTEN__`):

- The WASM main thread mixes every `AudioSource` and pushes the result into a SharedArrayBuffer
  ring buffer from `AudioDevice::PumpAudio()`, which runs once per frame.
- An AudioWorklet reads that ring on the audio thread.
- No ASYNCIFY is needed (push model). SharedArrayBuffer requires COOP/COEP headers, which
  `native/web/server.py` sends.
- DC3 sets the JS namespace with `MILO_WEB_AUDIO_NS=dc3` (`native/CMakeLists.txt`).
- An optional off-main-thread mix path exists behind `AudioDevice::OffMainMixEnabled()`; see
  `AudioDevice.h`.

Why this design over the alternatives:

- miniaudio's ScriptProcessorNode fallback runs on the main thread and competes with rendering.
- miniaudio's own AudioWorklet path needs ASYNCIFY, which costs binary size and runtime.
- Pre-decoding to browser `<audio>` loses the engine's sample-accurate stream time.

## Video (FMV) and Bink

DC3 ships Bink (`.bik`) for FMV, splash videos and song-preview audio. The Bink SDK is
precompiled third-party code under `lib/binkxenon/` (for example `xenon_rrAtomics.cpp`,
`radmem.cpp` and `xenon_rrcpu.cpp` units in `config/373307D9/splits.txt`). There is no source for
it, so it is not decompiled. The Xbox build links the original objects, and the native build
replaces Bink entirely.

- **`ENABLE_FFMPEG`** (CMake option in `native/CMakeLists.txt`, default ON): it is honoured only
  when not building with Emscripten (`if(ENABLE_FFMPEG AND NOT EMSCRIPTEN)`) and defines `HX_FFMPEG`.
- **Video:** `BinkMovieSys::CreateMovieImpl()` in `src/system/moviebink/BinkMovieSys.cpp` returns
  `new FFmpegMovieImpl()` under `#ifdef HX_FFMPEG`, else `BinkMovieImpl`. Bink SDK init in that file
  is guarded `#ifndef HX_FFMPEG`. The definition that runs is the engine's
  `../milo-native-engine/src/platform/FFmpegMovieImpl.cpp`. DC3's own copy was deleted when the
  pin moved to `4e738fe` (H-5). The movie-sound work below is on the engine branch `np-media`
  (private clone `/home/free/tmp/claude-1000/np-media-engine`, commit `9b14712`). It is not
  landed yet, and `MovieAudio.*` compiles only against an engine that has it
  (`MILO_MOVIE_AUDIO_SOURCE`).
- **Movie sound (G-15, `np-media`).** The image plays a movie's sound through Bink's own XAudio2
  output (`BinkSetSoundSystem(BinkOpenXAudio2)`, `BinkMovieSys_Xbox.cpp`), so it is the movie's
  clock. `BinkMovieImpl::BeginFromFile(path, volume, noSound, loop, preload, fillWidth, track, …)`:
  the third argument turns sound off (`BinkSetSoundOnOff(!noSound)`), the fourth loops, and a
  non-zero `track` selects Bink sound track `track - 1` (`set_bink_track` overrides it). `TexMovie`
  passes sound on, its own `loop`, track 1 (or its localization track); `MoviePanel` passes
  `!audio`, `loop`, `preload`, `fillWidth` and its language. Natively `FFmpegMovieImpl` does the
  same: the selected track (the n-th audio stream of the file; `intro.bik` carries 15, one per
  language) is decoded, resampled to the device rate with libswresample and fed to a
  `MovieAudioSource` on the engine mixer. That source places samples at timeline positions, so a
  main-thread stall costs a dropout, not a lasting offset, and its played-frame count is the
  movie's clock: the picture shows frame `clock / msPerFrame + 1`. Without an audio device
  (headless, no `MILO_AUDIO`), with no track, or with `noSound`, the clock is wall time less time
  paused. `Movie::Poll`'s fader level reaches the source through `SetVolume(dB)` (clamped to
  `[0, 1]`, as Bink's `[0, 0x8000]`). Before this the sound track was never opened and the
  second flag (`noSound`) was read as `loop`, so song previews never looped.
  A song preview's sound is that track: `SongPreview::PrepareSong` opens no song stream when the
  preview `.bik` exists.
  Measured end to end (headless, `MILO_AUDIO=1 MILO_AUDIO_BACKEND=null DC3_DUMP_AUDIO=…`): the
  intro's English track is in the device output from 1.07 s; 2 s windows from 2 s to 26 s
  cross-correlate with the file's track at offset 0 and coefficient 1.0000 (gain 0.794, -2 dB;
  inferred to be the fader group, not traced); the whole intro closes with `3655168 frames played (3655168 with sound,
  0 silent), 0 dropped late, picture at frame 2484` (all of its frames).
  Tests: `MovieAudio.*`, `MovieAudioSourceRing.*` (`native/tests/test_movie_audio.cpp`).
- **Stream audio:** `FFmpegAudioReader` (`../milo-native-engine/src/platform/FFmpegAudioReader.cpp`)
  replaces `BinkReader` as the `"bink"` stream decoder.
- **Web:** there is no FFmpeg. `native/src/platform/WebMovieImpl.cpp` rewrites a `.bik` path to
  `.webm` and plays it through the browser's decoder. **Open:** it still reads the third argument
  (`noSound`) as `loop` and the fourth as `noSound`, the mapping desktop had before G-15. The offline `.bik` -> `.webm` conversion is
  described in [native/web/VIDEO_ASSETS.md](../../../native/web/VIDEO_ASSETS.md)
  (`scripts/web/transcode_bink.py`, `scripts/web/transcode_bik.sh`).

## Tests

| Tests | File | Needs |
|---|---|---|
| `BinkFFmpeg.*` (OpenBikFile, DecodeFirstVideoFrame, DecodeAudioTrack, SeekAndDecode) | `native/tests/test_bink_ffmpeg.cpp` | `MILO_TEST_BIK` |
| `FFmpegIntegration.*`, `FFmpegMovieImplError.BadFile` | `native/tests/test_bink_integration.cpp` | `MILO_TEST_BIK` |
| `MovieAudio.*` (the track is the file's, sample-exact; `noSound`; loop is the 4th flag; localized track; picture follows the sound clock; fader volume), `MovieAudioSourceRing.*` | `native/tests/test_movie_audio.cpp` | game assets; an engine with `MILO_MOVIE_AUDIO_SOURCE` |
| Audio device / receiver / synth tests | `native/tests/test_audio.cpp` | audio device for some; `MILO_TEST_BIK` for the bik cases |
| Mid-song seek: `VorbisReader::DoRawSeek` re-keys AES-CTR; the counter word must land little-endian on a little-endian host | `native/tests/test_native_audioseek.cpp` | game assets (see the file header) |

Fixtures: `native/tests/test_extract_bik.cpp` extracts `.bik` files from the game archive to
`/tmp/claude-1000/bik_fixtures/`. Tests without `MILO_TEST_BIK` skip.

```bash
MILO_TEST_BIK=/tmp/claude-1000/bik_fixtures/satisfaction_prev.bik \
  native/build/milo-tests --gtest_filter='BinkFFmpeg.*:FFmpegIntegration.*:FFmpegMovieImplError.*'
```

(Run the suite as a whole through `scripts/native_test.sh`; see [../TESTING.md](../TESTING.md).)

## Sources

Lifted from, and superseding the audio/video parts of:
[AUDIO_SYSTEM.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/AUDIO_SYSTEM.md),
[VIDEO_PLAYBACK.md](../../archive/2026-11-13-native-doc-audit/plans/custom-graphics-engine/VIDEO_PLAYBACK.md),
[web-port/AUDIO.md](../../archive/2026-11-13-native-doc-audit/plans/web-port/AUDIO.md),
[DECOMP_GAPS.md](../../archive/2026-11-13-native-doc-audit/native/DECOMP_GAPS.md) (audio pipeline section).
