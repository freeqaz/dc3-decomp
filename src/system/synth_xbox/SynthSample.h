#pragma once
#include "synth\SynthSample.h"

class SampleInst360;

class SynthSample360 : public SynthSample {
public:
    SynthSample360();
    // No user-declared destructor: ham_xbox_r.map folds ??_GSynthSample360 into
    // ??_GSynthSample at 0x82E42CF8, whose body has no vptr store --
    // declaring one made MSVC inline the derived dtor into ??_G.
    OBJ_CLASSNAME(SynthSample);
    OBJ_SET_TYPE(SynthSample360);
    virtual SampleInst *NewInst(bool, int, int);
    virtual float LengthMs() const;

    bool IsXMA() const;
    int GetNumSamples() const;
    int GetNumBytes() const;
    unsigned int GetDataAddr() const;

    NEW_OBJ(SynthSample360)
    static void Register() { REGISTER_OBJ_FACTORY(SynthSample360) }
    static void Init();
};
