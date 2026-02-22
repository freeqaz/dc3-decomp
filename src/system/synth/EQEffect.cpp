#include "synth/EQEffect.h"
#include "xdk/xaudio2/xaudio2.h"

EQEffect::EQEffect(IXAudioBatchAllocator *) {
    unk38 = false;
    unk54 = false;
    unk74 = false;
    mBand1Freq = 12000.0f;
    unk90 = false;
    mBand1Gain = 0;
    unka8 = false;
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
    mBand5Freq = 25.0f;
    unk3c = 0;
    unk40 = 0;
    unk44 = 0;
    unk48 = 0;
    unk4c = 0;
    unk50 = 0;
    unk58 = 0;
    unk5c = 0;
    unk60 = 0;
    unk64 = 0;
    unk68 = 0;
    unk6c = 0;
    unk70 = 0;
    unk78 = 0;
    unk7c = 0;
    unk80 = 0;
    unk84 = 0;
    unk88 = 0;
    unk8c = 0;
    unk94 = 0;
    unk98 = 0;
    unk9c = 0;
    unka0 = 0;
    unka4 = 0;
    unkac = 0;
    unkb0 = 0;
    unkb4 = 0;
    unkb8 = 0;
    unkbc = 0;
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
