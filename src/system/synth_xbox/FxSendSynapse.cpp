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

void FxSendSynapse360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendSynapse360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendSynapse360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

IUnknown *FxSendSynapse360::CreateFx() { return (IUnknown *)new DSP::SynapseAPO(); }

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
    if (mNote2Hz == 0.0f) {
        params.bands[1].gain = mUnisonTrio ? 1.0f : 0.0f;
        params.bands[1].coeff1 = mProximityEffect;
        params.bands[1].freq = mNote1Hz * 0.9904912114143372f;
    } else {
        params.bands[1].coeff1 = 0.0f;
        params.bands[1].freq = mNote2Hz * 0.9960159659385681f;
    }

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

SynapseAPOParams::SynapseAPOParams() {
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
