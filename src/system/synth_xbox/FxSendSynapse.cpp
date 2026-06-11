#include "synth_xbox/FxSendSynapse.h"
#include "synth_xbox/FxSendSynapse360.h"

void FxSendSynapse360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendSynapse360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendSynapse360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

IUnknown *FxSendSynapse360::CreateFx() { return nullptr; }

void FxSendSynapse360::SyncEffectParams(IXAudio2SubmixVoice *) const {}

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
