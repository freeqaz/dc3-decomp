#include "synth/DelayEffect.h"
#include "Common_Xbox.h"
#include "math/Decibels.h"
#include "os/Debug.h"
#include "xdk/xaudio2/xaudio2.h"

DelayEffect::DelayEffect(IXAudioBatchAllocator *ix)
    : mDelaySamples(24000), mWritePos(0), mDecay(0.3f), mWetAmount(0.5f) {
    DspAllocate(mBuffer, 0x2ee00, ix);
}

DelayEffect::~DelayEffect() { DspFree(mBuffer); }

void DelayEffect::Reset() { DspClearBuffer(mBuffer, 0x2ee00); }

void DelayEffect::SetParameters(DelayEffect::Params const &params) {
    SetParameter(0, params.mDelaySamples);
    mDecay = DbToRatio(params.mDecayDb);
    mWetAmount = params.mWetPercent / 100.0f;
}

void DelayEffect::Process(float *, int, int) {}

void DelayEffect::SetParameter(int, float) {}
