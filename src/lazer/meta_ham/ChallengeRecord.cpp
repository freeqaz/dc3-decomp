#include "meta_ham/ChallengeRecord.h"
#include "meta_ham/Challenges.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/ProfileMgr.h"

ChallengeRecord::ChallengeRecord(ChallengeRow row) {
    mRow = row;
    mSongShortName = TheHamSongMgr.GetShortNameFromSongID(mRow.mSongID, false);
    if (mSongShortName.Null()) {
        if (TheChallenges->IsExportedSongDC1(mRow.mSongID)) {
            mSongContentLockState = 2;
        } else if (TheChallenges->IsExportedSongDC2(mRow.mSongID)) {
            mSongContentLockState = 3;
        } else {
            mSongContentLockState = 4;
        }
    } else if (TheProfileMgr.IsContentUnlocked(mSongShortName)) {
        mSongContentLockState = 0;
    } else {
        mSongContentLockState = 1;
    }
    mSongTitle = mRow.mSongTitle.c_str();
    mChallengerGamertag = Symbol(mRow.mGamertag.c_str());
    mMissionInfo = Symbol(mRow.mNotes.c_str());
}
