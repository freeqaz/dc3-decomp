// G-15: desktop movie sound.
//
// The image plays a movie's Bink sound track through Bink's XAudio2 mixer and
// paces the picture against it (BinkMovieImpl: MovieOpen's BinkSetSoundTrack
// for a localized track, FinishOpen's BinkSetSoundOnOff(!noSound),
// SetVolume's BinkSetVolume, DoFrame/Poll's keep-playing flag).  These tests
// drive FFmpegMovieImpl with its sound source DETACHED from the audio device
// and pull it here, standing in for the device callback, so they run headless
// and compare samples instead of listening.
//
// Sabotage record (each revert re-run, test watched FAIL, fix restored):
//   * BeginFromFile without OpenAudio (the old impl: no sound at all)
//       -> PreviewSoundIsTheFilesTrack, LocalizedMovieSelectsItsTrack,
//          PictureFollowsTheSoundClock, VolumeIsTheFaderLevel FAIL
//   * Poll timing the picture on mPlayTimer again instead of the sound clock
//       -> PictureFollowsTheSoundClock FAIL
//   * mLoop = noSound (the old argument mapping)
//       -> LoopIsTheSecondFlag FAIL
//   * OpenAudio always picking the first audio stream
//       -> LocalizedMovieSelectsItsTrack FAIL

#include <gtest/gtest.h>

#include "test_helpers.h"
#include "os/File.h"
#include "platform/FFmpegMovieImpl.h"

// Compiled only against an engine whose FFmpegMovieImpl plays the sound track
// (MILO_MOVIE_AUDIO_SOURCE; engine branch np-media).
#ifdef MILO_MOVIE_AUDIO_SOURCE

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

const int kRate = 44100;
const int kChunk = 512; // the device's period

class DetachedSound {
public:
    DetachedSound() { FFmpegMovieImpl::SetDetachedAudioRateForTest(kRate); }
    ~DetachedSound() { FFmpegMovieImpl::SetDetachedAudioRateForTest(0); }
};

// Copy a game file (out of the ark) to a temp file, so the reference decode
// below reads the same bytes through FFmpeg's own file protocol.
std::string CopyGameFile(const char *path) {
    File *f = NewFile(path, 2);
    if (!f)
        return std::string();
    std::vector<char> buf(f->Size());
    int got = f->Read(buf.data(), (int)buf.size());
    delete f;
    if (got != (int)buf.size())
        return std::string();
    const char *tmp = getenv("TMPDIR");
    std::string tmpl = std::string((tmp && *tmp) ? tmp : "/tmp") + "/dc3_movie_ref_XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');
    int fd = mkstemp(name.data());
    if (fd < 0)
        return std::string();
    bool ok = write(fd, buf.data(), buf.size()) == (ssize_t)buf.size();
    close(fd);
    if (!ok) {
        unlink(name.data());
        return std::string();
    }
    return std::string(name.data());
}

// Reference: the first `frames` stereo frames of the file's `nth` audio
// stream, decoded independently of FFmpegMovieImpl (no resampler; only used
// for tracks already at kRate).
std::vector<float> ReferenceTrack(const char *gamePath, int nth, int frames,
                                  int *sampleRate = nullptr) {
    std::vector<float> out;
    std::string path = CopyGameFile(gamePath);
    if (path.empty())
        return out;
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        unlink(path.c_str());
        return out;
    }
    avformat_find_stream_info(fmt, nullptr);
    int idx = -1, seen = 0;
    for (unsigned i = 0; i < fmt->nb_streams; i++) {
        if (fmt->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (seen++ == nth)
            idx = (int)i;
    }
    if (idx >= 0) {
        const AVCodec *codec = avcodec_find_decoder(fmt->streams[idx]->codecpar->codec_id);
        AVCodecContext *ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(ctx, fmt->streams[idx]->codecpar);
        avcodec_open2(ctx, codec, nullptr);
        if (sampleRate)
            *sampleRate = ctx->sample_rate;
        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        while ((int)out.size() < frames * 2 && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) >= 0) {
                while (avcodec_receive_frame(ctx, fr) == 0) {
                    int ch = fr->ch_layout.nb_channels;
                    for (int s = 0; s < fr->nb_samples; s++) {
                        for (int c = 0; c < 2; c++) {
                            int cc = c < ch ? c : 0;
                            float v = 0;
                            if (fr->format == AV_SAMPLE_FMT_FLTP)
                                v = ((const float *)fr->extended_data[cc])[s];
                            else if (fr->format == AV_SAMPLE_FMT_FLT)
                                v = ((const float *)fr->data[0])[s * ch + cc];
                            out.push_back(v);
                        }
                    }
                    av_frame_unref(fr);
                }
            }
            av_packet_unref(pkt);
        }
        av_frame_free(&fr);
        av_packet_free(&pkt);
        avcodec_free_context(&ctx);
    }
    avformat_close_input(&fmt);
    unlink(path.c_str());
    if ((int)out.size() > frames * 2)
        out.resize(frames * 2);
    return out;
}

// Pull `frames` from the movie's sound source in device-sized chunks, polling
// the movie between chunks the way the game loop does.
std::vector<float> Pull(FFmpegMovieImpl &movie, int frames) {
    std::vector<float> out;
    std::vector<float> chunk(kChunk * 2);
    MovieAudioSource *src = movie.AudioOut();
    while ((int)out.size() < frames * 2) {
        movie.Poll();
        src->RenderAudio(chunk.data(), kChunk);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    out.resize(frames * 2);
    return out;
}

double Rms(const std::vector<float> &v) {
    double s = 0;
    for (float x : v)
        s += (double)x * x;
    return v.empty() ? 0 : std::sqrt(s / v.size());
}

double MaxAbsDiff(const std::vector<float> &a, const std::vector<float> &b, float scale) {
    double m = 0;
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++)
        m = std::max(m, (double)std::fabs(a[i] - b[i] * scale));
    return m;
}

} // namespace

class MovieAudio : public EngineTestFixture {};

// SongPreview hands the preview .bik to a TexMovie: its sound IS the preview
// (the image's PrepareSong returns without opening a song stream).  TexMovie
// passes noSound = 0, loop = mLoop, track 1.
TEST_F(MovieAudio, PreviewSoundIsTheFilesTrack) {
    DetachedSound detached;
    const int kFrames = kRate * 2;
    std::vector<float> ref = ReferenceTrack("songs/ymca/ymca_prev.bik", 0, kFrames);
    ASSERT_EQ((int)ref.size(), kFrames * 2) << "reference decode of ymca_prev.bik failed";
    ASSERT_GT(Rms(ref), 0.01) << "vacuous: the reference is silent";

    FFmpegMovieImpl movie;
    ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, false, true, true,
                                    false, 1, nullptr, kLoadFront));
    ASSERT_NE(movie.AudioOut(), nullptr) << "the preview opened with no sound";
    EXPECT_EQ(movie.AudioTrack(), 0);
    std::vector<float> got = Pull(movie, kFrames);
    // Same rate, unity gain: the device receives the track's samples exactly.
    EXPECT_LT(MaxAbsDiff(got, ref, 1.0f), 1e-4);
    EXPECT_GT(Rms(got), 0.01);
    EXPECT_EQ(movie.AudioOut()->FramesDroppedLate(), 0);
    EXPECT_EQ(movie.AudioOut()->SilenceFramesRendered(), 0) << "the sound track underran";
}

// MoviePanel passes noSound = !mAudio: a panel with (audio FALSE) is silent.
TEST_F(MovieAudio, NoSoundFlagOpensNoTrack) {
    DetachedSound detached;
    FFmpegMovieImpl movie;
    ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, true, false, false,
                                    false, 0, nullptr, kLoadFront));
    EXPECT_EQ(movie.AudioOut(), nullptr);
    EXPECT_TRUE(movie.Poll()); // the picture still plays
}

// The second flag is Bink's keep-playing flag (unk26); the first is noSound.
TEST_F(MovieAudio, LoopIsTheSecondFlag) {
    {
        FFmpegMovieImpl movie; // TexMovie: (noSound 0, loop 1)
        ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, false, true, true,
                                        false, 1, nullptr, kLoadFront));
        EXPECT_TRUE(movie.Looping());
        movie.SetVirtualTime(31000.0f); // past the 30 s preview
        EXPECT_TRUE(movie.Poll()) << "a looping preview ended at its last frame";
        EXPECT_LT(movie.GetFrame(), 5);
    }
    {
        FFmpegMovieImpl movie; // MoviePanel with (audio TRUE) (loop FALSE)
        ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, false, false,
                                        false, true, 0, nullptr, kLoadFront));
        EXPECT_FALSE(movie.Looping());
        movie.SetVirtualTime(31000.0f);
        EXPECT_FALSE(movie.Poll()) << "a non-looping movie kept playing past its end";
    }
}

// attract / era intros are (localize TRUE): MoviePanel passes
// Movie::LocalizationTrack() and Bink plays track - 1 of the file's 15.
TEST_F(MovieAudio, LocalizedMovieSelectsItsTrack) {
    DetachedSound detached;
    const int kFrames = kRate / 2;
    int refRate = 0;
    std::vector<float> ref = ReferenceTrack("videos/intro.bik", 3, kFrames, &refRate);
    ASSERT_EQ(refRate, kRate) << "intro.bik track 3 is expected at 44.1 kHz";
    ASSERT_EQ((int)ref.size(), kFrames * 2);
    ASSERT_GT(Rms(ref), 1e-4) << "vacuous: track 3 opens silent";

    FFmpegMovieImpl movie;
    ASSERT_TRUE(movie.BeginFromFile("videos/intro.bik", 0.0f, false, false, false, true, 4,
                                    nullptr, kLoadFront));
    ASSERT_NE(movie.AudioOut(), nullptr);
    EXPECT_EQ(movie.AudioTrack(), 3);
    EXPECT_EQ(movie.AudioStreamIndex(), 4); // stream 0 is the picture
    std::vector<float> got = Pull(movie, kFrames);
    EXPECT_LT(MaxAbsDiff(got, ref, 1.0f), 1e-4) << "not track 3's samples";

    // Track 0 (no localization) is the file's first sound track.
    FFmpegMovieImpl plain;
    ASSERT_TRUE(plain.BeginFromFile("videos/intro.bik", 0.0f, false, false, false, true, 0,
                                    nullptr, kLoadFront));
    EXPECT_EQ(plain.AudioTrack(), 0);
    EXPECT_EQ(plain.AudioStreamIndex(), 1);

    // A track the file does not have plays no sound (the image asserts).
    FFmpegMovieImpl bad;
    ASSERT_TRUE(bad.BeginFromFile("videos/intro.bik", 0.0f, false, false, false, true, 40,
                                  nullptr, kLoadFront));
    EXPECT_EQ(bad.AudioOut(), nullptr);
}

// Bink paces the picture against its sound: frame N shows when the device
// has played N frames' worth of the track, and not before -- however much
// wall time passes.
TEST_F(MovieAudio, PictureFollowsTheSoundClock) {
    DetachedSound detached;
    FFmpegMovieImpl movie;
    ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, false, true, true,
                                    false, 1, nullptr, kLoadFront));
    ASSERT_NE(movie.AudioOut(), nullptr);
    float msPerFrame = movie.MsPerFrame();
    ASSERT_GT(msPerFrame, 1.0f);

    EXPECT_TRUE(movie.Poll());
    EXPECT_EQ(movie.GetFrame(), 1); // frame 0 shown at clock 0
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(movie.Poll());
    EXPECT_EQ(movie.GetFrame(), 1) << "the picture advanced on wall time with no sound played";

    std::vector<float> chunk(kChunk * 2);
    for (int played = 0; played < kRate * 3;) {
        movie.AudioOut()->RenderAudio(chunk.data(), kChunk);
        played += kChunk;
        movie.Poll();
        double clockMs = played * 1000.0 / kRate;
        int want = (int)(clockMs / msPerFrame) + 1;
        ASSERT_EQ(movie.GetFrame(), want) << "at " << clockMs << " ms of sound";
    }

    // The picture decoded through the read-ahead queue is the dancer, not a
    // blank frame: some pixels carry alpha.
    {
        const uint8_t *px = movie.GetRGBABuffer();
        int n = movie.GetDecodedWidth() * movie.GetDecodedHeight();
        int opaque = 0;
        for (int i = 0; i < n; i++)
            opaque += px[i * 4 + 3] > 128;
        EXPECT_GT(opaque, n / 50) << "decoded frame " << movie.GetFrame() << " is empty";
    }

    // Paused: the sound clock stops, and so does the picture.
    movie.SetPaused(true);
    int frame = movie.GetFrame();
    int64_t played = movie.AudioOut()->PlayedFrames();
    movie.AudioOut()->RenderAudio(chunk.data(), kChunk);
    EXPECT_EQ(movie.AudioOut()->PlayedFrames(), played);
    for (float s : chunk)
        ASSERT_EQ(s, 0.0f);
    movie.SetPaused(false);
    movie.Poll();
    EXPECT_EQ(movie.GetFrame(), frame);
}

// Movie::Poll pushes the fader group's level into SetVolume(db); Bink scales
// by DbToRatio(db), never above unity.
TEST_F(MovieAudio, VolumeIsTheFaderLevel) {
    DetachedSound detached;
    const int kFrames = kRate / 2;
    std::vector<float> ref = ReferenceTrack("songs/ymca/ymca_prev.bik", 0, kFrames);
    ASSERT_EQ((int)ref.size(), kFrames * 2);

    FFmpegMovieImpl movie;
    movie.SetVolume(-6.0f); // before the open: kept, as BinkMovieImpl::mVolume
    ASSERT_TRUE(movie.BeginFromFile("songs/ymca/ymca_prev.bik", 0.0f, false, true, true,
                                    false, 1, nullptr, kLoadFront));
    ASSERT_NE(movie.AudioOut(), nullptr);
    float g = std::pow(10.0f, -6.0f / 20.0f);
    std::vector<float> got = Pull(movie, kFrames);
    EXPECT_LT(MaxAbsDiff(got, ref, g), 1e-4);

    movie.SetVolume(6.0f);
    EXPECT_FLOAT_EQ(movie.AudioOut()->Gain(), 1.0f);
    movie.SetVolume(-96.0f);
    EXPECT_FLOAT_EQ(movie.AudioOut()->Gain(), 0.0f);
}

// The ring itself: late audio is dropped rather than played late, a gap
// plays as silence, and a paused source holds its clock.
TEST(MovieAudioSourceRing, LateDroppedGapSilentPauseHolds) {
    MovieAudioSource src(1000, 1000);
    std::vector<float> data(200 * 2);
    for (int i = 0; i < 200; i++) {
        data[i * 2] = (float)(i + 1);
        data[i * 2 + 1] = -(float)(i + 1);
    }
    std::vector<float> out(100 * 2);

    // Device at 0, audio queued for 50..249: 50 frames of silence first.
    ASSERT_EQ(src.Write(50, data.data(), 200), 200);
    src.RenderAudio(out.data(), 100);
    for (int i = 0; i < 50; i++)
        ASSERT_EQ(out[i * 2], 0.0f) << i;
    for (int i = 50; i < 100; i++)
        ASSERT_EQ(out[i * 2], (float)(i - 50 + 1)) << i;
    EXPECT_EQ(src.PlayedFrames(), 100);

    // Paused: silence, clock held.
    src.SetPaused(true);
    src.RenderAudio(out.data(), 100);
    EXPECT_EQ(src.PlayedFrames(), 100);
    EXPECT_EQ(out[0], 0.0f);
    src.SetPaused(false);

    // Producer fell behind: queued 150..249 still plays at 100.. in order.
    src.RenderAudio(out.data(), 100);
    EXPECT_EQ(out[0], 51.0f);

    // A write for a time already played is dropped, never played late.
    src.TruncateFrom(src.PlayedFrames());
    ASSERT_EQ(src.Write(150, data.data(), 100), 100); // positions 150..249, device at 200
    src.RenderAudio(out.data(), 100);
    EXPECT_EQ(out[0], 51.0f); // the frame queued for position 200
    EXPECT_EQ(src.FramesDroppedLate(), 50);
}

#endif // MILO_MOVIE_AUDIO_SOURCE
