#pragma once
#include "hamobj\RhythmBattlePlayer.h"
#include "meta_ham\HamProfile.h"
#include "net_ham\RCJobDingo.h"
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\Friend.h"
#include "stl\_vector.h"

// w8-g: the message name has no `_msg` suffix in the image.  The string COMDAT
// PlatformMgrOpCompleteMsg::Type() references is `??_C@_0BJ@...` -- 0x19 = 25
// bytes with the NUL, i.e. 24 characters, which is exactly
// "platform_mgr_op_complete"; ours was `??_C@_0BN@...` (0x1D = 28 characters).
// Proving site: os/PlatformMgr_Xbox, ?Type@PlatformMgrOpCompleteMsg@@SA?AVSymbol@@XZ
// indices 13 and 15 (`lis`/`addi` of the literal).
DECLARE_MESSAGE(PlatformMgrOpCompleteMsg, "platform_mgr_op_complete")
PlatformMgrOpCompleteMsg(bool success) : Message(Type(), success) {}
bool Success() const { return mData->Int(2) != 0; }
END_MESSAGE

enum FriendsListJobState {
    kFriendsListState_0,
    kEnumeratingFriends,
    kUpdatingFriends,
    kFriendsListState_3
};

class UpdateFriendsListJob : public RCJob {
public:
    virtual DataNode Handle(DataArray *, bool);

    UpdateFriendsListJob(Hmx::Object *, HamProfile *);
    void EnumerateFriends();

protected:
    HamProfile *mProfile; // 0xb0
    int mPadNum; // 0xb4
    int mEnumerationToken; // 0xb8
    std::vector<Friend *> mFriendsList; // 0xbc
    FriendsListJobState mFriendsListJobState; // 0xc8

private:
    void GetFriendsListToken();
    DataNode OnMsg(RCJobCompleteMsg const &);
    DataNode OnMsg(PlatformMgrOpCompleteMsg const &);
};
