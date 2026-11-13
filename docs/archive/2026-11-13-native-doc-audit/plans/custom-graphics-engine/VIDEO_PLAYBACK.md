# Native Port: Video Playback (Bink → FFmpeg)

## Current State

DC3 uses RAD Game Tools' **Bink SDK** for all video playback. The Bink library
ships as precompiled Xbox 360 objects in `lib/binkxenon/` — we have no source
code for them and can't compile them for other platforms.

## Where Bink Is Used

### 1. Video Playback (`src/system/moviebink/`)

`BinkMovieImpl` is the concrete `MovieImpl` for FMV/cutscene playback.

**Files**:
- `src/system/moviebink/BinkMovieImpl.cpp` — open/close/decode/draw video
- `src/system/moviebink/BinkMovieImpl_Xbox.cpp` — Xbox-specific texture upload
- `src/system/moviebink/BinkMovieSys.cpp` — factory, creates `BinkMovieImpl`
- `src/system/moviebink/BinkMovieSys_Xbox.cpp` — XAudio2 sound system binding

**Callers** (what triggers video playback):
- `src/system/meta/MoviePanel.cpp` — FMV panel (intro videos, cutscenes)
- `src/system/movie/Splash.cpp` — splash screen videos
- `src/lazer/meta_ham/Campaign.cpp` — campaign intro/outro videos
- `src/lazer/meta_ham/CampaignEra.cpp` — era transition videos
- `src/lazer/meta_ham/CampaignPerformer.cpp` — performer unlock videos
- `src/lazer/meta_ham/MetaPanel.cpp` — menu background videos

**Bink SDK calls**: `BinkOpen`, `BinkClose`, `BinkDoFrame`, `BinkNextFrame`,
`BinkCopyToBuffer`, `BinkSetSoundSystem`, `BinkOpenXAudio2`, `BinkGoto`,
`BinkGetError`

### 2. Audio-Only Decoding (`src/system/synth/BinkReader.cpp`)

`BinkReader` extracts audio tracks from `.bik` files for song preview playback.
Bink is used here as an audio container format — video is explicitly disabled
(`BinkSetVideoOnOff(mBink, 0)`).

**Callers**:
- `src/system/meta/SongPreview.cpp` — song preview in song select
- `src/system/meta/StorePreviewMgr.cpp` — store item previews

**Bink SDK calls**: `BinkInit`, `BinkOpen`, `BinkClose`, `BinkOpenTrack`,
`BinkCloseTrack`, `BinkGetTrackData`, `BinkSetSoundTrack`,
`BinkSetVideoOnOff`, `BinkNextFrame`, `BinkGoto`, `BinkGetError`

**Audio format**: 16-bit PCM, 44100 Hz, mono tracks. Buffer size 0xB400 bytes.

## Replacement Strategy: FFmpeg/libavcodec

FFmpeg's `libavcodec` includes a Bink video decoder (`bink`) and Bink audio
decoder (`binkaudio_dct`, `binkaudio_rdft`) — both fully open source. No need
to re-encode existing `.bik` files.

### Architecture

```
┌─────────────────────────────────────────────┐
│         MovieImpl interface (unchanged)      │
│  Begin/End/Poll/Draw/SetFile/SetPaused      │
└─────────────┬───────────────────────────────┘
              │
┌─────────────▼───────────────────────────────┐
│  FFmpegMovieImpl (new, replaces BinkMovieImpl)│
│                                               │
│  avformat_open_input()     ← open .bik file  │
│  avcodec_find_decoder()    ← CODEC_ID_BINKVIDEO│
│  av_read_frame()           ← demux packets   │
│  avcodec_send/receive()    ← decode frames   │
│  sws_scale()               ← YUV→RGB convert │
│  → upload to RndTex        ← display frame   │
└───────────────────────────────────────────────┘

┌───────────────────────────────────────────────┐
│  FFmpegAudioReader (new, replaces BinkReader) │
│                                               │
│  Same libavformat/libavcodec pipeline         │
│  avcodec_find_decoder()    ← CODEC_ID_BINKAUDIO│
│  → PCM samples to StandardStream              │
└───────────────────────────────────────────────┘
```

### Implementation — COMPLETE

Both FFmpeg replacements are implemented, tested, and wired into the engine.

#### CMake Integration

FFmpeg is gated behind the `ENABLE_FFMPEG` CMake option (default ON). Disabling
with `cmake -DENABLE_FFMPEG=OFF ..` removes all FFmpeg code and dependencies.

```cmake
option(ENABLE_FFMPEG "Enable FFmpeg for Bink video/audio decoding" ON)
# Defines HX_FFMPEG=1 when enabled
```

#### FFmpegMovieImpl (video playback)

**File**: `native/src/platform/FFmpegMovieImpl.h/.cpp`

Implements the full `MovieImpl` interface:
- `BeginFromFile` → `avformat_open_input` + `avcodec_open2` + `sws_getContext`
- `Poll` → timer-based frame advancement, `av_read_frame` + decode + `sws_scale` (YUV→RGBA)
- `Draw` → marks RGBA frame buffer as consumed (ready for RndTex upload)
- `SetPaused` / `End` / loop support via `av_seek_frame`

**Factory wiring** (`src/system/moviebink/BinkMovieSys.cpp`):
```cpp
MovieImpl* BinkMovieSys::CreateMovieImpl() {
#ifdef HX_FFMPEG
    return new FFmpegMovieImpl();
#else
    return new BinkMovieImpl();
#endif
}
```

Bink SDK calls in `BinkMovieSys::Init()` are also guarded behind `#ifndef HX_FFMPEG`.

#### FFmpegAudioReader (audio-only .bik decoding)

**File**: `native/src/platform/FFmpegAudioReader.h/.cpp`

Implements the `StreamReader` interface (same as `BinkReader`):
- Opens .bik via `avformat_open_input`, finds all audio streams
- Converts float→int16 PCM (matching BinkReader's 16-bit format)
- Feeds `StandardStream::ConsumeData()` with per-track PCM buffers (0xB400 bytes)
- Seek via `av_seek_frame` + `avcodec_flush_buffers`

**Factory wiring** (`native/src/platform/Synth_Stub.cpp`):
```cpp
class NativeSynth : public Synth {
    StreamReader *NewStreamDecoder(File *file, StandardStream *stream, Symbol type) override {
#ifdef HX_FFMPEG
        if (type == "bink") return new FFmpegAudioReader(file, stream);
#endif
        return Synth::NewStreamDecoder(file, stream, type);
    }
};
```

### Testing — ALL PASSING

8 tests across 3 test files, all passing against real `.bik` fixtures extracted
from the game archive.

**Fixture extraction**: `test_extract_bik.cpp` boots the engine, enumerates the
archive (142 .bik files found), and extracts fixtures to `/tmp/claude-1000/bik_fixtures/`.

**Run tests:**
```bash
cd native/build
MILO_TEST_BIK=/tmp/claude-1000/bik_fixtures/satisfaction_prev.bik \
  ./milo-tests --gtest_filter='BinkFFmpeg.*:FFmpegIntegration.*:FFmpegMovieImplError.*'
```

| Test | File | Status |
|------|------|--------|
| `BinkFFmpeg.OpenBikFile` | `test_bink_ffmpeg.cpp` | PASS — video 256x256, audio 44100Hz stereo |
| `BinkFFmpeg.DecodeFirstVideoFrame` | `test_bink_ffmpeg.cpp` | PASS — full YUV→RGBA decode |
| `BinkFFmpeg.DecodeAudioTrack` | `test_bink_ffmpeg.cpp` | PASS — 192K+ samples decoded |
| `BinkFFmpeg.SeekAndDecode` | `test_bink_ffmpeg.cpp` | PASS — seek + decode verified |
| `FFmpegIntegration.AudioReaderOpenAndDecode` | `test_bink_integration.cpp` | PASS — end-to-end PCM with non-zero check |
| `FFmpegIntegration.MovieImplPollAndDraw` | `test_bink_integration.cpp` | PASS — open/poll/draw/pause/end lifecycle |
| `FFmpegIntegration.MovieImplLoop` | `test_bink_integration.cpp` | PASS — loop via av_seek_frame |
| `FFmpegMovieImplError.BadFile` | `test_bink_integration.cpp` | PASS — error path for nonexistent files |

All tests gated by `MILO_TEST_BIK` env var — skip gracefully without fixtures.

### Bink Library Objects (decomp status)

The `lib/binkxenon/` objects contain ~62 functions across 10 TUs. These are
third-party precompiled objects with no source — they exist in the decomp DB
as AT_LIMIT/unimplemented stubs. They don't need to be decomped since:
1. We have no compilation unit to put source into
2. The native port replaces them entirely with FFmpeg
3. The Xbox 360 build links against the original precompiled objects

TUs: `binkread`, `binkasyncthread`, `binkxa2`, `binkasyncRR`, `binkacd`,
`popmal`, `radmem`, `rrstring`, `xenon_rrAtomics`, `xenon_rrcpu`
