#pragma once
#include "FxSend.h"
#include "obj/Object.h"
#include "synth\FxSendPitchShift.h"

class FxSendPitchShift360 : public FxSendPitchShift, public FxSend360 {
public:
    FxSendPitchShift360() : FxSend360(this) {}
    // No user-declared destructor: the target's (like FxSendWah360's) is the
    // implicit one, which MSVC emits without the two vtable-pointer resets.
    OBJ_CLASSNAME(FxSendPitchShift)
    OBJ_SET_TYPE(FxSendPitchShift360)
    // Defined in the class body: the target emits all three as COMDATs and the
    // linker takes them out of Synth.obj, not out of this class's own .cpp.
    virtual void Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }
    virtual void UpdateMix() { FxSend360::UpdateVolumes(); }
    virtual void OnParametersChanged() { FxSend360::SyncEffectParams(); }
    virtual void SyncEffectParams(IXAudio2SubmixVoice *) const;
    /** Not a standard send: UpdateVolumes must not push effect params per voice. */
    virtual bool IsStandard() const { return false; }

    NEW_OBJ(FxSendPitchShift360)

protected:
    virtual IUnknown *CreateFx();
};
