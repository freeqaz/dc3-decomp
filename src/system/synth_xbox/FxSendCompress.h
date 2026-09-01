#pragma once
#include "FxSend.h"
#include "obj/Object.h"
#include "synth\FxSendCompress.h"
#include "xdk\xaudio2\xaudio2.h"

class FxSendCompress360 : public FxSendCompress, public FxSend360 {
public:
    virtual ~FxSendCompress360();
    OBJ_CLASSNAME(FxSendCompress)
    OBJ_SET_TYPE(FxSendCompress360)
    // Defined in the class body: the target emits all three as COMDATs and the
    // linker takes them out of Synth.obj, not out of this class's own .cpp.
    virtual void Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }
    virtual void UpdateMix() { FxSend360::UpdateVolumes(); }
    virtual void OnParametersChanged() { FxSend360::SyncEffectParams(); }
    virtual void SyncEffectParams(IXAudio2SubmixVoice *) const;

    NEW_OBJ(FxSendCompress360)

    FxSendCompress360();

protected:
    virtual IUnknown *CreateFx();
};
