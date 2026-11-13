#include "Synapse_dsp.h"
#include "Biquad.h"
#include "IPP_basicmath_xbox.h"
#include <cstring>
#include <cmath>

// The target spells this `?Time2IirA@?A0xa7b3dd7d@@YAMMM@Z` -- an anonymous
// namespace at FILE scope, not one nested inside DSP::Synapse (which would
// mangle `?Time2IirA@?A0x...@Synapse@DSP@@YAMMM@Z`).  Three call sites named a
// symbol the image does not contain: Synapse::Synapse, SetAttackSmoothing and
// SetReleaseSmoothing.
namespace {
float Time2IirA(float time, float sampleRate) {
    if (time > 0.0f) {
        return 1.0f - expf(-1.0f / (time * sampleRate));
    }
    return 1.0f;
}
}

namespace DSP {

void LowpassCoefficients(float *const, float, float, float);
void HighpassCoefficients(float *const, float, float, float);

namespace Synapse {

// Minimal class definitions for destructor visibility
class PitchDetector {
public:
#ifdef HX_NATIVE
    PitchDetector(const std::vector<float> &, unsigned int, unsigned int);
#else
    PitchDetector(const stlpmtx_std::vector<float, stlpmtx_std::StlNodeAlloc<float> > &, unsigned int, unsigned int);
#endif
    ~PitchDetector();
    void Detect(unsigned int);
    float mField_0x00;
    float mField_0x04;
    float mField_0x08;
    float mDetectedPitch;    // 0x0C
    float mPitchConfidence;  // 0x10
    float mPitchClarity;     // 0x14
    unsigned char _pad[0x128 - 0x18]; // pad to match sizeof = 0x128
};

class PeakDetector {
public:
#ifdef HX_NATIVE
    PeakDetector(const std::vector<float> &, unsigned int, unsigned int);
#else
    PeakDetector(const stlpmtx_std::vector<float, stlpmtx_std::StlNodeAlloc<float> > &, unsigned int, unsigned int);
#endif
    ~PeakDetector();
    void Detect(unsigned int);
    float mField_0x00;
    float mDetectedPitch;    // 0x04
    // ... more fields
    unsigned char _pad[0x30 - 0x08];
    float mPeak;             // 0x30
};

struct GranularVoice {
    float mField_0x00;
    float mGain;             // 0x04
    float mCorrection;       // 0x08
    bool mEnabled;           // 0x0C
    unsigned char _pad1[3];
    double mTimestamp;        // 0x10
};

class GranularSynth {
public:
#ifdef HX_NATIVE
    GranularSynth(const std::vector<float> &, unsigned int, unsigned int, unsigned int);
#else
    GranularSynth(const stlpmtx_std::vector<float, stlpmtx_std::StlNodeAlloc<float> > &, unsigned int, unsigned int, unsigned int);
#endif
    ~GranularSynth();
    void SetVoiceEnabled(unsigned int idx, bool enabled);
    void Flush();
    void ExtractGranules();
    void Synthesize(unsigned int, float *const *);

    float mField_0x00;
    float mDetectedPitch;    // 0x04
    float mPeak;             // 0x08
    float mPitchConfidence;  // 0x0C
    float mField_0x10;
    unsigned int mDetectionInterval; // 0x14
    unsigned int mSampleCount;       // 0x18
    unsigned char _pad[0x2C - 0x1C];
    GranularVoice *mVoices;          // 0x2C
    unsigned char _pad2[0x44 - 0x30]; // pad to match sizeof = 0x44
};

static const float kBiquadParams[] = { 7902.13f, 0.7071068f, 340.0f };

void Synapse::SetVoiceTargetNote(unsigned int idx, float val) {
    *(float *)((char *)&mVoices[idx] + 4) = val;
}

void Synapse::SetVoiceGain(unsigned int idx, float val) {
    mGranularSynth->mVoices[idx].mGain = val;
}

void Synapse::SetVoiceEnabled(unsigned int idx, bool enabled) {
    mGranularSynth->SetVoiceEnabled(idx, enabled);
}

void Synapse::SetVoiceTransposition(unsigned int idx, float val) {
    mVoices[idx].SetTransposition(val);
}

void Synapse::SetVoiceAmount(unsigned int idx, float val) {
    mVoices[idx].SetAmount(val);
}

void Synapse::SetVoiceProximityEffect(unsigned int idx, float val) {
    mVoices[idx].SetProximityEffect(val);
}

void Synapse::SetVoiceProximityFocus(unsigned int idx, float val) {
    mVoices[idx].SetProximityFocus(val);
}

void GranularSynth::SetVoiceEnabled(unsigned int idx, bool enabled) {
    if (enabled != 0 && mVoices[idx].mEnabled == 0) {
        double timestamp = mVoices[idx].mTimestamp;
        unsigned int thresh = mDetectionInterval * 3;
        unsigned int samp = mSampleCount;
        if ((float)samp - timestamp > (double)thresh) {
            mVoices[idx].mTimestamp = (double)samp;
        }
    }
    mVoices[idx].mEnabled = enabled;
}

// Lane history (34 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
// > The one charged row in SetAttackSmoothing AND the one in SetReleaseSmoothing are the
// > SAME mechanism, mirrored -- instruction 26, `add r3, r29, r11` in the image but `add
// > r3, r11, r29` in ours. (w26-f9)
void Synapse::SetAttackSmoothing(float val) {
    float coeff = Time2IirA(val * 0.001f / (float)mDetectionInterval, mTargetPitch);
    unsigned int count = 0;
    void *vp = (char *)this + 0x5C;
    int voiceCount = (int)((int)(*(void **)((char *)vp + 0x4)) - (int)(*(void **)vp)) / 56;
    if (voiceCount != 0) {
        int offset = 0;
        do {
            ((PitchCorrectedVoice *)((char *)(*(void **)vp) + offset))->SetAttackSmoothing(coeff);
            count++;
            offset += 0x38;
        } while (count < (unsigned int)((int)((int)(*(void **)((char *)vp + 0x4)) - (int)(*(void **)vp)) / 56));
    }
}

void Synapse::SetReleaseSmoothing(float val) {
    float coeff = Time2IirA(val * 0.001f / (float)mDetectionInterval, mTargetPitch);
    unsigned int count = 0;
    void *vp = (char *)this + 0x5C;
    int voiceCount = (int)((int)(*(void **)((char *)vp + 0x4)) - (int)(*(void **)vp)) / 56;
    if (voiceCount != 0) {
        int offset = 0;
        do {
            ((TrueColor::ExposureRecipe *)((char *)(*(void **)vp) + offset))->SetMinIntegrationTime(coeff);
            count++;
            offset += 0x38;
        } while (count < (unsigned int)((int)((int)(*(void **)((char *)vp + 0x4)) - (int)(*(void **)vp)) / 56));
    }
}

// Lane history (7 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
static inline float AsRvalue(float f) { return f; }
static inline unsigned int RoundToSamples(const float &sampleRate, float seconds) {
    float prod = sampleRate * seconds;
    return (unsigned int)(prod + (prod >= 0.0f ? 0.5f : -0.5f));
}

Synapse::Synapse(float sampleRate) : mDetectionInterval(64), mTargetPitch(sampleRate) {
    // Lane history (10 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
    // > The 0.4 s ring-buffer length is a LOCAL, rounded down to a multiple of four (the
    // > downsampled buffer is a quarter of it).
    unsigned int inputLen = RoundToSamples(AsRvalue(mTargetPitch), 0.4f) & ~3u;
    mDefaultPitch = RoundToSamples(AsRvalue(mTargetPitch), 0.0015384615f);
    mField_0x20 = RoundToSamples(AsRvalue(mTargetPitch), 0.016666668f);

    // The fill values are unnamed temporaries; the target reuses ONE stack
    // slot for all of them (and for the ChannelBuffer prototype / coeffs),
    // which named locals cannot do.
    mInputBuffer.resize(inputLen, 0.0f);
    mDownsampledBuffer.resize((unsigned int)mInputBuffer.size() >> 2, 0.0f);

    mBufferIndex = 0;
    mGain = 1.0f;

    // PitchDetector
    PitchDetector *pd = new PitchDetector(mDownsampledBuffer, (mDefaultPitch + 3) >> 2, mField_0x20 >> 2);
    mPitchDetector.reset(pd);

    mPitchConfidence = 0.0f;
    mPitchClarity = 0.0f;
    mPitchThreshold = 0.35f;
    mDetectedPitch = (float)mDefaultPitch;

    // PeakDetector
    PeakDetector *peak = new PeakDetector(mInputBuffer, mDefaultPitch, mField_0x20);
    mPeakDetector.reset(peak);

    // Voices.  The prototype is an unnamed temporary: the target feeds the
    // ctor's RETURN value straight into resize and destroys it immediately
    // after, which a named local cannot express.
    mVoices.resize(3, PitchCorrectedVoice());

    // Channel buffers
#ifdef HX_NATIVE
    typedef std::vector<float> ChannelBuffer;
#else
    typedef stlpmtx_std::vector<float, stlpmtx_std::StlNodeAlloc<float> > ChannelBuffer;
#endif
    mChannelBuffers.resize((int)mVoices.size(), ChannelBuffer());

    // Output buffers
    mOutputBuffers.resize((int)mVoices.size(), (float *)0);

    // Lane history (7 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
    // > Resize each channel buffer to 0x2000 floats and set output buffer pointers.
    for (unsigned int i = 0; i < (unsigned int)((int)mChannelBuffers.size()); i++) {
        mChannelBuffers[i].resize((size_t)0x2000, 0.0f);
        mOutputBuffers[i] = mChannelBuffers[i].begin();
    }

    // GranularSynth
    GranularSynth *gs = new GranularSynth(mInputBuffer, (int)mVoices.size(), mDefaultPitch, mField_0x20);
    mGranularSynth.reset(gs);

    // Lane history (11 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
    for (unsigned int j = 0; j < (unsigned int)((int)mVoices.size()); j++) {
        mGranularSynth->mVoices[j].mField_0x00 = 0.0f;
    }

    // Biquad filters.  The coefficient array shares the target's stack block
    // with the ChannelBuffer prototype above (both live at r31+0x60), so it
    // has to be lexically scoped -- a function-scope array gets its own slot.
    {
        float coeffs[5];
        LowpassCoefficients(coeffs, mTargetPitch, kBiquadParams[0], kBiquadParams[1]);
        Biquad *lpf = new Biquad(coeffs);
        mScratchBuffer1.reset(lpf);

        HighpassCoefficients(coeffs, mTargetPitch * 0.25f, kBiquadParams[2], kBiquadParams[1]);
        Biquad *hpf = new Biquad(coeffs);
        mScratchBuffer2.reset(hpf);
    }

    mIirSmooth = 0.0f;
    // Lane history (7 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
    // > BUG FIX (w16-e): the time constant is 8.16 ms -- the image loads __real@3c05b186
    // > (0.0081600007f) at 82E4763C `lfs f1, ...` for the Time2IirA call at 82E47640.
    mIirCoeff = Time2IirA(8.16f * 0.001f, mTargetPitch * 0.25f);

    SetAttackSmoothing(30.0f);
    SetReleaseSmoothing(80.0f);
}

Synapse::~Synapse() {}

// Lane history (37 lines moved): docs/decomp/notes/system/synth_xbox/Synapse_dsp.md
void Synapse::ProcessInPlace(unsigned int arg1, float *arg2) {
    float temp_f30 = 0.0f;
    float temp_f31 = 4.0f;

    if (arg1 != 0) {
        float *var_r24 = arg2;
        unsigned int var_r22 = arg1;

        do {
            float *inputStart = mInputBuffer.begin();
            inputStart[mBufferIndex] = *var_r24;

            unsigned int temp_r10 = mBufferIndex;

            if (!(temp_r10 & 3)) {
                float *temp_r11 = mInputBuffer.begin();

                float var_f0;
                if (temp_r10 == 0) {
                    int temp_r9 = (int)((char *)mInputBuffer.end() - (char *)temp_r11) >> 2;
                    var_f0 = temp_r11[temp_r9 - 1] + temp_r11[temp_r9 - 3] + temp_r11[temp_r9 - 2] + temp_r11[0];
                } else {
                    float *temp_r9_2 = &temp_r11[temp_r10];
                    var_f0 = temp_r11[temp_r10 - 3] + temp_r11[temp_r10 - 2] + temp_r9_2[-1] + temp_r9_2[0];
                }
                *(float *)((temp_r10 & ~3u) + (unsigned int)mDownsampledBuffer.begin()) = var_f0;
            }

            (*(PeakDetector **)((char *)this + 0x40))->Detect(mBufferIndex);
            (*(GranularSynth **)((char *)this + 0x68))->mPeak = (*(PeakDetector **)((char *)this + 0x40))->mPeak;

            unsigned int temp_r11_2 = mBufferIndex;

            if (!((mDetectionInterval - 1) & temp_r11_2)) {
                (*(PitchDetector **)((char *)this + 0x28))->Detect(temp_r11_2 >> 2);
                PitchDetector *pd = *(PitchDetector **)((char *)this + 0x28);
                mPitchConfidence = pd->mPitchConfidence;
                mPitchClarity = pd->mPitchClarity;

                if (mPitchConfidence > mPitchThreshold) {
                    float temp_f0_2 = pd->mDetectedPitch * temp_f31;
                    mDetectedPitch = temp_f0_2;

                    if (temp_f0_2 == temp_f30) {
                        mDetectedPitch = (float)mDefaultPitch;
                    }

                    (*(PeakDetector **)((char *)this + 0x40))->mDetectedPitch = mDetectedPitch;
                    (*(GranularSynth **)((char *)this + 0x68))->mDetectedPitch = mDetectedPitch;
                    (*(GranularSynth **)((char *)this + 0x68))->mPitchConfidence = mPitchConfidence;
                }

                for (unsigned int v = 0; v < (unsigned int)((int)mVoices.size()); v++) {
                    mVoices[v].mFreq0 = mTargetPitch / mDetectedPitch;
                    mVoices[v].mField_0x28 = mPitchConfidence;
                    mVoices[v].mFreqCounter = mPitchClarity;
                    GranularSynth *gs = *(GranularSynth **)((char *)this + 0x68);
                    gs->mVoices[v].mCorrection = mVoices[v].GetCorrection();
                }
            }

            if (!((mDetectionInterval - 1) & mBufferIndex)) {
                (*(GranularSynth **)((char *)this + 0x68))->Flush();
            }

            (*(GranularSynth **)((char *)this + 0x68))->ExtractGranules();

            unsigned int temp_r11_5 = mBufferIndex + 1;
            mBufferIndex = temp_r11_5;

            if (temp_r11_5 >= (unsigned int)((int)((int)mInputBuffer.end() - (int)mInputBuffer.begin()) >> 2)) {
                mBufferIndex = 0;
            }

            var_r22--;
            var_r24++;
        } while (var_r22 != 0);
    }

    (*(GranularSynth **)((char *)this + 0x68))->Synthesize(arg1, (float *const *)mOutputBuffers.begin());

    if (arg1 != 0) {
        memset(arg2, 0, arg1 * 4);
    }

    for (unsigned int v = 0; v < (unsigned int)((int)mVoices.size()); v++) {
        IPP::Add_InPlace(arg1, mOutputBuffers[v], arg2);
    }

    mGain = 1.0f;
    unsigned int final_count = (int)mVoices.size();
    IPP::MulConstant_InPlace(arg1, arg2, 1.0f / (float)final_count);
}

} // namespace Synapse
} // namespace DSP
