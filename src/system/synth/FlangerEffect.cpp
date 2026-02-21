#include "synth/FlangerEffect.h"
#include "Common_Xbox.h"
#include "math/Rot.h"
#include "xdk/xaudio2/xaudio2.h"

FlangerEffect::FlangerEffect(IXAudioBatchAllocator *ix)
    : mWritePos(0), mDelaySamples(100), mDepthFrac(0), unk1c(0), mFeedbackFrac(0.5f), unk24(0), mRateRadians(0), unk2c(0),
      mWetFrac(0.1f) {
    for (int i = 0; i < 2; i++) {
        DspAllocate(mDelayBuffers[i], 0x2580, ix);
        DspAllocate(mDelayBuffers[i + 2], 0x2580, ix);
    }
}

FlangerEffect::~FlangerEffect() {
    for (int i = 0; i < 2; i++) {
        DspFree(mDelayBuffers[i]);
        DspFree(mDelayBuffers[i + 2]);
    }
}

void FlangerEffect::Reset() {
    mWritePos = 0;
    unk1c = 0;
    unk24 = 0;
    unk2c = 0;
    for (int i = 0; i < 2; i++) {
        DspClearBuffer(mDelayBuffers[i], 0x2580);
        DspClearBuffer(mDelayBuffers[i + 2], 0x2580);
    }
}

void FlangerEffect::SetParameters(FlangerEffect::Params const &params) {
    float sampleRate = 48000.0f;
    mDelaySamples = (int)(params.mDelayMs * 48.0f);
    mRateRadians = (params.mRate / sampleRate) * 6.2831853f;
    mDepthFrac = params.mDepth / 100.0f;
    mFeedbackFrac = params.mFeedback / 100.0f;
    mWetFrac = params.mWet / 100.0f;
}
