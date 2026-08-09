#pragma once
#include "net_ham\ChallengeSystemJobs.h"

class ChallengeRecord {
public:
    ChallengeRecord(ChallengeRow);
    virtual ~ChallengeRecord() {}
    ChallengeRecord(const ChallengeRecord &);

    ChallengeRecord &operator=(const ChallengeRecord &other);

    ChallengeRow &GetChallengeRow() { return mRow; }
    Symbol GetSongShortName() { return mSongShortName; }
    Symbol GetSongTitle() { return mSongTitle; }
    Symbol GetChallengerGamertag() { return mChallengerGamertag; }
    Symbol GetMissionInfo() { return mMissionInfo; }
    int GetSongContentLockState() { return mSongContentLockState; }

private:
    ChallengeRow mRow; // 0x4
    Symbol mSongShortName; // 0x40
    Symbol mSongTitle; // 0x44
    Symbol mChallengerGamertag; // 0x48
    Symbol mMissionInfo; // 0x4c
    int mSongContentLockState; // 0x50
};
