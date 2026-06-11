#include "FxSendMeterEffect.h"
#include "FxSend.h"
#include "macros.h"
#include "os/Debug.h"
#include "synth/FxSend.h"
#include "synth_xbox/MeterEffect.h"
#include "xdk/unknwn.h"

FxSendMeterEffect360::FxSendMeterEffect360() : FxSend360(this), mParams(0) {}

FxSendMeterEffect360::~FxSendMeterEffect360() { RELEASE(mParams); }

void FxSendMeterEffect360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendMeterEffect360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendMeterEffect360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

void FxSendMeterEffect360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    MeterEffectParams p;
    if (mParams) {
        p.unk0 = mParams->unk0;
    }
    voice->SetEffectParameters(0, &p, sizeof(p), 0);
}

void FxSendMeterEffect360::InitParams(IXAudio2SubmixVoice *, int) {}

IUnknown *FxSendMeterEffect360::CreateFx() {
    return static_cast<CXAPOBase *>(new MeterEffect());
}
