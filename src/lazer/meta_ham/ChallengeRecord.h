#pragma once
#include "net_ham/ChallengeSystemJobs.h"

class ChallengeRecord {
public:
    ChallengeRecord(ChallengeRow);
    ~ChallengeRecord() {}
    ChallengeRecord(const ChallengeRecord &);

    ChallengeRecord &operator=(const ChallengeRecord &other);

    ChallengeRow &GetChallengeRow() { return mRow; }
    Symbol GetSongShortName() { return mSongShortName; }
    Symbol GetSongTitle() { return mSongTitle; }
    Symbol GetChallengerGamertag() { return mChallengerGamertag; }
    Symbol GetMissionInfo() { return mMissionInfo; }
    int GetSongContentLockState() { return mSongContentLockState; }

private:
    ChallengeRow mRow; // 0x0
    Symbol mSongShortName; // 0x3c
    Symbol mSongTitle; // 0x40
    Symbol mChallengerGamertag; // 0x44
    Symbol mMissionInfo; // 0x48
    int mSongContentLockState; // 0x4c
};
