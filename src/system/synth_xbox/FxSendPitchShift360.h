#pragma once
#include "FxSend.h"
#include "obj/Object.h"
#include "synth/FxSendPitchShift.h"

class FxSendPitchShift360 : public FxSendPitchShift, public FxSend360 {
public:
    FxSendPitchShift360() : FxSend360(this) {}
    virtual ~FxSendPitchShift360() {}
    OBJ_CLASSNAME(FxSendPitchShift)
    OBJ_SET_TYPE(FxSendPitchShift360)
    virtual void Recreate(std::vector<FxSend *> &);
    virtual void UpdateMix();
    virtual void OnParametersChanged();
    virtual void SyncEffectParams(IXAudio2SubmixVoice *) const;

    NEW_OBJ(FxSendPitchShift360)

protected:
    virtual IUnknown *CreateFx();
};
