// The master-bus level meter (PLAN H-115, engine audio/MasterMeter.h).
//
// The boombox's EQ bars are multiplier_meter's perform_*.anim, keyed off the
// `audio_channels` channel3/channel4 properties. HamMaster::CheckLevels sets
// those every frame from TheSynth->GetLevelData(): the 360's Synth360 meters
// the mastering voice with a MeterEffect XAPO into nine LevelData buses
// (FL FR C LFE SL SR, an unused slot, then the DML/DMR downmix Poll derives),
// and CheckLevels reads the downmix pair's RMS. Native's synth had no buses,
// so CheckLevels returned at `levelData.size() == 0` and every copy of
// multiplier_meter (and the shell background's eq4/pulse anims) sat at frame 0
// in every venue.
//
// These tests drive AudioDevice::MixSources directly (no device is opened in
// the harness) and read the buses back after TheSynth->Poll(), against values
// worked out here from the image's MeterEffect::DoProcess and Synth360::Poll
// (build/373307D9 asm; src/system/synth_xbox/MeterEffect.cpp, Synth.cpp):
//
//  * each 256-frame quantum, meter channel c reads floats [c*256, (c+1)*256)
//    of the 6-channel interleaved mastering buffer -- a block of the buffer,
//    not channel c (reproduced from the target, not corrected); a stereo mix
//    reaches the 5.1 voice as FL/FR only;
//  * sums of squares accumulate until the per-frame parameter set clears them;
//    the peak is the last quantum's; RMS is published as
//    sqrt(sum / frames counted before this quantum) and not at all while that
//    count is 0;
//  * DML = FL + C*g(-3dB) + SL*g(-1.2dB) + SR*g(-6.2dB), DMR the mirror.

#include "test_helpers.h"

#include "audio/AudioDevice.h"
#include "math/Decibels.h"
#include "synth/FxSend.h"
#include "synth/Synth.h"

#include <cmath>
#include <vector>

namespace {

// Constant left/right levels: easy to reason about through the block layout.
class ConstSource : public AudioSource {
public:
    ConstSource(float l, float r) : mL(l), mR(r) {}
    int RenderAudio(float *out, int frames) override {
        for (int i = 0; i < frames; i++) {
            out[i * 2] = mL;
            out[i * 2 + 1] = mR;
        }
        return frames;
    }
    bool IsFinished() const override { return false; }
    float mL, mR;
};

const int kQ = 256;

// The image's per-quantum sum of squares for meter channel c, given a mix of
// constant (l, r): count the FL and FR floats in the block DoProcess reads.
double BlockSumSquares(int c, double l, double r) {
    double sum = 0;
    for (int k = c * kQ; k < (c + 1) * kQ; k++) {
        if (k % 6 == 0) sum += l * l;
        else if (k % 6 == 1) sum += r * r;
    }
    return sum;
}

double BlockPeak(int c, double l, double r) {
    double peak = 0;
    for (int k = c * kQ; k < (c + 1) * kQ; k++) {
        if (k % 6 == 0) peak = std::max(peak, std::fabs(l));
        else if (k % 6 == 1) peak = std::max(peak, std::fabs(r));
    }
    return peak;
}

class MasterMeterTest : public EngineTestFixture {
protected:
    void SetUp() override {
        ASSERT_TRUE(TheSynth);
        // Settle: drain whatever earlier mixing left and clear the sums.
        TheSynth->Poll();
    }
    void TearDown() override {
        AudioDevice::GetInstance().RemoveSource(&mSource);
    }
    void Mix(int frames) {
        std::vector<float> buf(frames * 2);
        AudioDevice::GetInstance().MixSources(buf.data(), frames);
    }
    std::vector<LevelData> &Levels() { return TheSynth->GetLevelData(); }
    ConstSource mSource{0.5f, 0.25f};
};

TEST_F(MasterMeterTest, NativeSynthHasTheImagesNineBuses) {
    // Without the meter CheckLevels stops here, and nothing downstream moves.
    std::vector<LevelData> &l = Levels();
    ASSERT_EQ(l.size(), 9u);
    const char *names[9] = { "  FL", "  FR", "   C", " LFE", "  SL", "  SR", "", " DML", " DMR" };
    for (int i = 0; i < 9; i++) EXPECT_STREQ(l[i].mName.c_str(), names[i]) << i;
    EXPECT_TRUE(AudioDevice::GetInstance().MasterMeter().Enabled());
}

TEST_F(MasterMeterTest, MixedAudioReachesTheBusesAsTheImageMetersIt) {
    ASSERT_EQ(Levels().size(), 9u);
    const double l = 0.5, r = 0.25;
    AudioDevice::GetInstance().AddSource(&mSource);
    const int quanta = 4;
    Mix(quanta * kQ);
    TheSynth->Poll();
    std::vector<LevelData> &lv = Levels();
    double rms[6];
    for (int c = 0; c < 6; c++) {
        // quanta sums over (quanta - 1) counted quanta of frames.
        rms[c] = std::sqrt(quanta * BlockSumSquares(c, l, r) / ((quanta - 1) * (double)kQ));
        EXPECT_NEAR(lv[c].mRMS, rms[c], 1e-4) << "bus " << c;
        EXPECT_NEAR(lv[c].mPeak, BlockPeak(c, l, r), 1e-6) << "bus " << c;
    }
    const double gC = DbToRatio(-3.0f), gS = DbToRatio(-1.2f), gR = DbToRatio(-6.2f);
    EXPECT_NEAR(lv[7].mRMS, rms[0] + rms[2] * gC + rms[4] * gS + rms[5] * gR, 1e-4);
    EXPECT_NEAR(lv[8].mRMS, rms[1] + rms[2] * gC + rms[4] * gR + rms[5] * gS, 1e-4);
    // What CheckLevels makes of it: a level well inside its 40 dB window.
    const float level = (RatioToDb(lv[8].mRMS) + 40.0f) / 40.0f;
    EXPECT_GT(level, 0.5f);
    EXPECT_LT(level, 1.0f);
}

TEST_F(MasterMeterTest, SilenceEmptiesTheBusesOnTheNextFrame) {
    ASSERT_EQ(Levels().size(), 9u);
    AudioDevice::GetInstance().AddSource(&mSource);
    Mix(4 * kQ);
    TheSynth->Poll();
    ASSERT_GT(Levels()[8].mRMS, 0.1f);
    AudioDevice::GetInstance().RemoveSource(&mSource);
    Mix(4 * kQ);
    TheSynth->Poll();
    for (int c = 0; c < 6; c++) {
        EXPECT_EQ(Levels()[c].mRMS, 0.0f) << c;
        EXPECT_EQ(Levels()[c].mPeak, 0.0f) << c;
    }
    EXPECT_EQ(Levels()[7].mRMS, 0.0f);
    EXPECT_EQ(Levels()[8].mRMS, 0.0f);
}

TEST_F(MasterMeterTest, OneQuantumAFramePublishesNoNewRms) {
    // The image skips the RMS store while mFrameCount is 0, and the frame's
    // parameter set zeroes it, so a frame that saw a single quantum leaves the
    // previous RMS in place (the peak is still that quantum's).
    ASSERT_EQ(Levels().size(), 9u);
    AudioDevice::GetInstance().AddSource(&mSource);
    Mix(4 * kQ);
    TheSynth->Poll();
    const float before = Levels()[0].mRMS;
    ASSERT_GT(before, 0.1f);
    mSource.mL = mSource.mR = 0.0f;
    Mix(kQ);
    TheSynth->Poll();
    EXPECT_EQ(Levels()[0].mRMS, before);
    EXPECT_EQ(Levels()[0].mPeak, 0.0f);
}

TEST_F(MasterMeterTest, PartialCallbacksAddUpToWholeQuanta) {
    // A device period is not a multiple of the quantum everywhere: frames
    // carried between callbacks must meter exactly as one call would.
    ASSERT_EQ(Levels().size(), 9u);
    const double l = 0.5, r = 0.25;
    AudioDevice::GetInstance().AddSource(&mSource);
    Mix(300);
    Mix(300);
    Mix(424); // 1024 frames: four quanta
    TheSynth->Poll();
    const double expect = std::sqrt(4 * BlockSumSquares(3, l, r) / (3.0 * kQ));
    EXPECT_NEAR(Levels()[3].mRMS, expect, 1e-4);
}

} // namespace
