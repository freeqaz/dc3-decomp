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

// SetParameter must NOT be in this TU on PPC — the compiler inlines it
// into SetParameters, breaking the 100% match. On native it's needed for linking.
#ifdef HX_NATIVE
void DelayEffect::SetParameter(int param, float value) {
    if ((unsigned int)param >= 1) {
        if ((unsigned int)param != 1) {
            if ((unsigned int)param >= 3) {
                TheDebug.Fail(MakeString("bad parameter %i", param), 0);
                return;
            }
            mWetAmount = value * 0.01f;
            return;
        }
        mDecay = DbToRatio(value);
        return;
    }

    int delaySamples = (int)(value * 48000.0f);
    mDelaySamples = delaySamples;
    if (delaySamples > 0x176FF) {
        delaySamples = 0x176FF;
    } else if (delaySamples < 1) {
        delaySamples = 1;
    }
    mDelaySamples = delaySamples;
}

void DelayEffect::Process(float *, int, int) {}
#endif
