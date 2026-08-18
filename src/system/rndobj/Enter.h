#pragma once
#include "obj/Object.h"
#include "rndobj\Poll.h"

/** "A simple object with an enter an exit script call" */
class RndEnterable : public RndPollable {
public:
    OBJ_CLASSNAME(Enterable);
    OBJ_SET_TYPE(Enterable);
    virtual DataNode Handle(DataArray *, bool);
    /** An Enterable is driven by Enter()/Exit(), never per-frame polled, so it
     *  reports PollEnabled()==false and RndDir sorts it into mEnters. */
    virtual bool PollEnabled() const { return false; }

    NEW_OBJ(RndEnterable)
    static void Init() { REGISTER_OBJ_FACTORY(RndEnterable) }
};
