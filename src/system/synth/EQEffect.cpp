#include "synth/EQEffect.h"
#include "xdk/xaudio2/xaudio2.h"

EQEffect::EQEffect(IXAudioBatchAllocator *) {
    mBands[0].enabled = false;
    mBands[1].enabled = false;
    mBands[2].enabled = false;
    mBand1Freq = 12000.0f;
    mBand1Gain = 0;
    mBand1Q = 8000.0f;
    mBand2Freq = 1000.0f;
    mBand2Gain = 0;
    mBand2Q = 2000.0f;
    mBand3Freq = 0;
    mBand3Gain = 20000.0f;
    mBand3Q = 0;
    mBand4Freq = 20.0f;
    mBand4Gain = 0;
    mBand4Q = 0;
    mBand5Freq = 0;
    mBands[3].enabled = false;
    mBands[4].enabled = false;
    mBands[0].b0 = 0;
    mBands[0].b1 = 0;
    mBands[0].b2 = 0;
    mBands[0].a1 = 0;
    mBands[0].a2 = 0;
    mBands[0].z1 = 0;
    mBands[1].b0 = 0;
    mBands[1].b1 = 0;
    mBands[1].b2 = 0;
    mBands[1].a1 = 0;
    mBands[1].a2 = 0;
    mBands[1].z1 = 0;
    mBands[2].b0 = 0;
    mBands[2].b1 = 0;
    mBands[2].b2 = 0;
    mBands[2].a1 = 0;
    mBands[2].a2 = 0;
    mBands[2].z1 = 0;
    mBands[3].b0 = 0;
    mBands[3].b1 = 0;
    mBands[3].b2 = 0;
    mBands[3].a1 = 0;
    mBands[3].a2 = 0;
    mBands[3].z1 = 0;
    mBands[4].b0 = 0;
    mBands[4].b1 = 0;
    mBands[4].b2 = 0;
    mBands[4].a1 = 0;
    mBands[4].a2 = 0;
    mBands[4].z1 = 0;
    Reset();
}

void EQEffect::SetParameters(EQEffect::Params const &params) {
    SetParameter(0, params.mBand1Freq);
    SetParameter(1, params.mBand1Gain);
    SetParameter(2, params.mBand1Q);
    SetParameter(3, params.mBand2Freq);
    SetParameter(4, params.mBand2Gain);
    SetParameter(5, params.mBand2Q);
    SetParameter(6, params.mBand3Freq);
    SetParameter(7, params.mBand3Gain);
    SetParameter(8, params.mBand3Q);
    SetParameter(9, params.mBand4Freq);
    SetParameter(10, params.mBand4Gain);
    SetParameter(11, params.mBand4Q);
    SetParameter(12, params.mBand5Freq);
}
