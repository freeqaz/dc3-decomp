#include "net_ham/FriendsListJobs.h"
#include "meta_ham/PlaylistSortMgr.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/PlatformMgr.h"
#include "utl/DataPointMgr.h"
#include "utl/MakeString.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "xdk/xapilibi/xbox.h"
#include <cstddef>
#include <cstring>

UpdateFriendsListJob::UpdateFriendsListJob(Hmx::Object *callback, HamProfile *profile)
    : RCJob("friends/updatefriends/", callback) {
    MILO_ASSERT(callback == NULL, 0x18);
    mProfile = profile;
    mFriendsCount = profile->GetPadNum();
    mFriendsListJobState = kFriendsListState_0;
}

void UpdateFriendsListJob::EnumerateFriends() {
    mFriendsListJobState = kEnumeratingFriends;
    ThePlatformMgr.EnumerateFriends(mFriendsCount, mFriendsList, this);
}

DataNode UpdateFriendsListJob::OnMsg(RCJobCompleteMsg const &msg) {
    MILO_ASSERT(mFriendsListJobState == kUpdatingFriends, 0x7d);
    if (msg.Success() && mProfile->HasValidSaveData()) {
        mProfile->SetUploadFriendsToken(mEnumerationToken);
    }
    mFriendsListJobState = kFriendsListState_3;
    return 1;
}

void UpdateFriendsListJob::GetFriendsListToken() {
    mEnumerationToken = 0;
    for (unsigned int i = 0; i < (unsigned int)mFriendsList.size(); i++) {
        const char *name = mFriendsList[i]->mName.c_str();
        int nameLen = strlen(name);
        for (int j = 0; j < nameLen; j += 4) {
            int chunk[4];
            chunk[0] = 0;
            int copyLen = nameLen - j;
            if (copyLen >= 4) {
                copyLen = 4;
            }
            memcpy(chunk, name + j, copyLen);
            mEnumerationToken ^= chunk[0];
        }
    }
}

DataNode UpdateFriendsListJob::OnMsg(PlatformMgrOpCompleteMsg const &msg) {
    MILO_ASSERT(mFriendsListJobState == kEnumeratingFriends, 0x28);
    GetFriendsListToken();
    int uploadToken = 0;
    if (mProfile) {
        uploadToken = mProfile->GetUploadFriendsToken();
    }
    if (msg.Success() && mProfile && mProfile->HasValidSaveData()
        && mEnumerationToken != uploadToken) {
        mFriendsListJobState = kUpdatingFriends;
        DataPoint dataP;
        String friendGuids;
        String friendName;
        int numFriends = (int)mFriendsList.size();
        static Symbol friends("friends");
        int loopLimit = numFriends - 1;
        int loopIdx = 0;
        int byteOff = 0;
        char keyBuf[8];
        char guidBuf[0x18];
        if (0 < loopLimit) {
            do {
                friendName = mFriendsList[loopIdx]->mName.c_str();
                XUID xuid = mFriendsList[loopIdx]->mXUID;
                friendGuids += MakeString("%llu,", xuid);
                Hx_snprintf(keyBuf, 8, "name%03d", loopIdx);
                dataP.AddPair(keyBuf, DataNode(friendName));
                Hx_snprintf(keyBuf, 8, "guid%03d", loopIdx);
                Hx_snprintf(guidBuf, 0x18, "%lld", xuid);
                dataP.AddPair(keyBuf, DataNode(guidBuf));
                loopIdx++;
                byteOff += 4;
            } while (loopIdx < loopLimit);
        }
        if (numFriends != 0) {
            friendName = mFriendsList[numFriends - 1]->mName.c_str();
            XUID xuid = mFriendsList[numFriends - 1]->mXUID;
            friendGuids += MakeString("%llu", xuid);
        }
        dataP.AddPair(friends, DataNode(friendGuids));
        SetDataPoint(dataP);
        mCallback = this;
        TheRockCentral.ManageJob(this);
    } else {
        mFriendsListJobState = kFriendsListState_3;
        Cancel(false);
        TheRockCentral.ManageJob(this);
    }
    return 1;
}

BEGIN_HANDLERS(UpdateFriendsListJob)
    HANDLE_MESSAGE(PlatformMgrOpCompleteMsg)
    HANDLE_MESSAGE(RCJobCompleteMsg)
END_HANDLERS
