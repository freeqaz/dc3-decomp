#pragma once
#include "synth/SynthSample.h"

class SampleInst360;

class SynthSample360 : public SynthSample {
public:
    SynthSample360();
    virtual ~SynthSample360() {}
    OBJ_CLASSNAME(SynthSample360);
    OBJ_SET_TYPE(SynthSample360);
    virtual SampleInst *NewInst(bool, int, int);
    virtual float LengthMs() const;

    bool IsXMA() const;

    NEW_OBJ(SynthSample360)
    static void Register() { REGISTER_OBJ_FACTORY(SynthSample360) }
    static void Init();
};
