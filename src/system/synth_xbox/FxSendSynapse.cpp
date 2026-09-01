#include "synth_xbox\FxSendSynapse.h"
#include "synth_xbox\FxSendSynapse360.h"

// SynapseAPO.h cannot be included here: it ships a hand-rolled global
// IXAPOParameters that ODR-collides with the real xapo.h one pulled in via the
// FxSend/XAUDIO2 chain. Forward-declare just enough of DSP::SynapseAPO (object
// size 0x1c8, default ctor) to allocate + construct one — matching the target's
// inline `operator new(0x1c8)` + ??0SynapseAPO@DSP@@QAA@XZ.
namespace DSP {
class SynapseAPO {
public:
    SynapseAPO();

private:
    char mOpaque[0x1c8];
};
}  // namespace DSP

IUnknown *FxSendSynapse360::CreateFx() { return (IUnknown *)new DSP::SynapseAPO(); }

// NOT YET 100% (96.4%, 32 rows of 89), and the residual is a *build-model*
// finding rather than a source one.  Every remaining row follows from register
// allocation: the target spends two callee-saved registers (r30 = voice,
// r31 = this) and a 0xd0 frame, while we keep `this` in the VOLATILE r8 across
// the `bl ??0SynapseAPOParams` and need only r31 and a 0xc0 frame.  That is
// only legal if the compiler knows the callee's clobber set -- MSVC's whole-TU
// register-usage propagation -- and it does, because the ctor is defined in
// this file.  Measured, one variable at a time:
//
//   ctor defined in this TU, at the bottom (as shipped here)   96.4%
//   ctor defined in this TU, between CreateFx and here          96.4%  (inert:
//       the analysis is whole-TU, not emission-order dependent)
//   ctor moved to SynapseAPO.cpp (a different TU)               98.8%
//
// So the original compiled SyncEffectParams without the ctor body in scope,
// i.e. in a separate object -- exactly how rb3-xenon splits the same engine
// code (synth_xbox/FxSendSynapse.cpp holds only the Params ctor;
// synth_xbox/FxSendSynapse360.cpp holds these methods).  Acting on that here
// needs config/373307D9/splits.txt to model two objects whose .text
// interleaves (CreateFx, ctor, SyncEffectParams by address), including a
// correct .rdata/.pdata division, so it is left for a lane that can validate
// the split.  Moving the ctor alone is a net loss: it costs the 80 bytes
// ??0SynapseAPOParams currently matches in this unit and SyncEffectParams
// still does not reach 100%.
void FxSendSynapse360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    DSP::SynapseAPOParams params;

    // Band 0: the primary target note, always at full gain.
    params.bands[0].enabled = 1;
    params.bands[0].freq = mNote1Hz;
    params.bands[0].gain = 1.0f;
    params.bands[0].coeff0 = mAmount;
    params.bands[0].coeff1 = mProximityEffect;
    params.bands[0].coeff2 = mProximityFocus;

    // Band 1: target note 2 (or a detuned copy of note 1 when note 2 is unset).
    params.bands[1].enabled = 1;
    params.bands[1].coeff0 = mAmount;
    params.bands[1].coeff2 = mProximityFocus;
    params.lowCutoffFreq = mAttackSmoothing;
    params.highCutoffFreq = mReleaseSmoothing;
    float band1Gain = 1.0f;
    if (mNote2Hz == 0.0f) {
        if (!mUnisonTrio)
            band1Gain = 0.0f;
        params.bands[1].coeff1 = mProximityEffect;
        params.bands[1].freq = mNote1Hz * 0.9904912114143372f;
    } else {
        params.bands[1].coeff1 = 0.0f;
        params.bands[1].freq = mNote2Hz * 0.9960159659385681f;
    }
    params.bands[1].gain = band1Gain;

    // Band 2: target note 3 (or a detuned copy of note 1 when note 3 is unset).
    params.bands[2].enabled = 1;
    params.bands[2].coeff0 = mAmount;
    params.bands[2].coeff2 = mProximityFocus;
    float band2Gain = 1.0f;
    if (mNote3Hz == 0.0f) {
        if (!mUnisonTrio)
            band2Gain = 0.0f;
        params.bands[2].coeff1 = mProximityEffect;
        params.bands[2].freq = mNote1Hz * 1.009600043296814f;
    } else {
        params.bands[2].coeff1 = 0.0f;
        params.bands[2].freq = mNote3Hz * 1.003999948501587f;
    }
    params.bands[2].gain = band2Gain;

    voice->SetEffectParameters(0, &params, sizeof(DSP::SynapseAPOParams), 0);
}

namespace DSP {

SynapseAPOParams::SynapseAPOParams() throw() {
    for (int i = 0; i < 3; i++) {
        bands[i].freq = 220.0f;
        bands[i].gain = 0.0f;
        bands[i].enabled = 0;
        bands[i].q = 0.0f;
    }
    lowCutoffFreq = 20.0f;
    highCutoffFreq = 40.0f;
}

}  // namespace DSP
