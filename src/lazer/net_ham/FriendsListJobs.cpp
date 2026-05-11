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
    mPadNum = profile->GetPadNum();
    mFriendsListJobState = kFriendsListState_0;
}

void UpdateFriendsListJob::EnumerateFriends() {
    mFriendsListJobState = kEnumeratingFriends;
    ThePlatformMgr.EnumerateFriends(mPadNum, mFriendsList, this);
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
    for (int i = 0; i < mFriendsList.size(); i++) {
        const char *name = mFriendsList[i]->GetName();
        for (int j = 0; j < strlen(name); j += 4) {
            int buffer = 0;
            int num;
            if (strlen(name) - j >= 4) {
                num = 4;
            } else {
                num = strlen(name) - j;
            }
            memcpy(&buffer, name + j, num);
            mEnumerationToken ^= buffer;
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
        String friendInfo;
        String friendName;
        int friendSize = mFriendsList.size();
        static Symbol friends("friends");
        char namebuf[8];
        char buf[24];
        for (int i = 0; i < friendSize - 1; i++) {
            friendName = mFriendsList[i]->GetName();
            XUID xuid = mFriendsList[i]->mXUID;
            friendInfo += MakeString("%llu,", xuid);
            Hx_snprintf(namebuf, 8, "name%03d", i);
            dataP.AddPair(namebuf, friendName);
            Hx_snprintf(namebuf, 8, "guid%03d", i);
            Hx_snprintf(buf, 24, "%lld", xuid);
            dataP.AddPair(namebuf, buf);
        }
        if (mFriendsList.size() > 0) {
            friendName = mFriendsList[friendSize - 1]->GetName();
            XUID xuid = mFriendsList[friendSize - 1]->mXUID;
            friendInfo += MakeString("%llu", xuid);
        }
        dataP.AddPair(friends, friendInfo);
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
