#pragma once
#include "synth/SampleInst.h"
#include "synth_xbox/SynthSample.h"

class SampleInst360 : public SampleInst {
public:
    SampleInst360(SynthSample360 *, bool, int, int);
    virtual ~SampleInst360();

    // SampleInst pure virtuals
    virtual bool IsPlaying() const;
    virtual void SetFXCore(FXCore);
    virtual void Pause(bool);
    virtual void SetADSR(const ADSRImpl &);

    POOL_OVERLOAD(SampleInst360, 0x16)

protected:
    virtual void StartImpl();
    virtual void StopImpl(bool);
    virtual void SetVolumeImpl(float);
    virtual void SetPanImpl(float);
    virtual void SetSpeedImpl(float);

private:
    // Xbox 360 specific members
    // Size is 0xB0 - base SampleInst is ~0xA8
    int unk_a8; // 0xa8
    int unk_ac; // 0xac
};
