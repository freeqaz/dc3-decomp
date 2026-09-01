#include "FxSendMeterEffect.h"
#include "FxSend.h"
#include "macros.h"
#include "os\Debug.h"
#include "synth\FxSend.h"
#include "synth_xbox\MeterEffect.h"
#include "xdk\unknwn.h"

FxSendMeterEffect360::FxSendMeterEffect360() : FxSend360(this), mParams(0) {}

FxSendMeterEffect360::~FxSendMeterEffect360() { RELEASE(mParams); }

void FxSendMeterEffect360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendMeterEffect360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendMeterEffect360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

void FxSendMeterEffect360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    MeterEffectParams p;
    if (mParams) {
        p.mLevelData = mParams->mLevelData;
    }
    voice->SetEffectParameters(0, &p, sizeof(p), 0);
}

void FxSendMeterEffect360::InitParams(IXAudio2SubmixVoice *voice, int numChannels) {
    std::vector<LevelData> &channels = mChannels;
    channels.clear();
    switch (numChannels) {
    case 1:
        channels.push_back("center");
        break;
    case 2: {
        LevelData left("left");
        LevelData right("right");
        channels.push_back(left);
        channels.push_back(right);
        break;
    }
    default:
        MILO_NOTIFY("InitParams only supports up to 2 channels");
        break;
    }
    RELEASE(mParams);
    // No parentheses: the target has no null-check-then-zero-store after the
    // allocation, which is what `new MeterEffectParams()` value-initialisation
    // emits for a POD.
    MeterEffectParams *created = new MeterEffectParams;
    LevelData *levels = &channels[0];
    mParams = created;
    created->mLevelData = levels;
    // &mParams, not mParams -- reproduced from the target, which passes the
    // ADDRESS OF THE MEMBER POINTER (addi rN, this, 0x40) as pParameters. Since
    // sizeof(MeterEffectParams) is 4, the effect receives the heap pointer
    // itself as its LevelData*, one indirection short. SyncEffectParams
    // (100% matched) passes a local by address correctly, so this is a shipped
    // defect, not a decomp artifact.
    voice->SetEffectParameters(0, &mParams, sizeof(MeterEffectParams), 0);
}

IUnknown *FxSendMeterEffect360::CreateFx() {
    return static_cast<CXAPOBase *>(new MeterEffect());
}
