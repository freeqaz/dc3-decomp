#include "synth_xbox\FxSendPitchShift360.h"
#include "synth_xbox\PitchShiftEffect.h"

void FxSendPitchShift360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendPitchShift360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendPitchShift360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

void FxSendPitchShift360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    PitchShiftEffectParams p;
    p.unk0 = mRatio;
    voice->SetEffectParameters(0, &p, sizeof(p), 0);
}

IUnknown *FxSendPitchShift360::CreateFx() {
    return static_cast<CXAPOBase *>(new PitchShiftEffect());
}
