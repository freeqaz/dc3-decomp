#pragma once
#include "FxSend.h"
#include "obj/Object.h"
#include "synth\FxSendSynapse.h"

class FxSendSynapse360 : public FxSendSynapse, public FxSend360 {
public:
    FxSendSynapse360() : FxSend360(this) {}
    virtual ~FxSendSynapse360() {}
    OBJ_CLASSNAME(FxSendSynapse)
    OBJ_SET_TYPE(FxSendSynapse360)
    virtual void Recreate(std::vector<FxSend *> &);
    virtual void UpdateMix();
    virtual void OnParametersChanged();
    virtual void SyncEffectParams(IXAudio2SubmixVoice *) const;
    /** Not a standard send: UpdateVolumes must not push effect params per voice. */
    virtual bool IsStandard() const { return false; }

    NEW_OBJ(FxSendSynapse360)

protected:
    virtual IUnknown *CreateFx();
};
