#pragma once
#include "hamobj\HamPlayerData.h"
#include "obj\Data.h"
#include "obj\Object.h"

enum OvershellSlotState {
};

class OvershellSlot : public Hmx::Object {
public:
    OvershellSlot(HamPlayerData &);
    virtual ~OvershellSlot() {}
    virtual DataNode Handle(DataArray *, bool);

    void SetState(OvershellSlotState);
    void SetPlayerNum(int player) { mPlayerNum = player; }
    void SetPlaying(bool);
    void Poll(const Skeleton *const (&)[6]);

protected:
    const HamPlayerData &mPlayerData; // 0x2c
    OvershellSlotState mState; // 0x30
    int mPlayerNum; // 0x34
    int unk38; // 0x38
    int unk3c; // 0x3c
};

class Overshell : public Hmx::Object {
public:
    Overshell();
    virtual ~Overshell();
    virtual DataNode Handle(DataArray *, bool);

    void Init();
    void ResolveSkeletons();
    void Poll(const Skeleton *const (&)[6]);

protected:
    OvershellSlot *mSlots[2]; // 0x2c
};
