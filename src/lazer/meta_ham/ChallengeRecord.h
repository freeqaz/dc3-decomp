#pragma once
#include "net_ham/ChallengeSystemJobs.h"

class ChallengeRecord {
public:
    ChallengeRecord(ChallengeRow);
    virtual ~ChallengeRecord() {}
    ChallengeRecord(const ChallengeRecord &);

    ChallengeRecord &operator=(const ChallengeRecord &other);

    ChallengeRow &GetChallengeRow() { return mRow; }
    Symbol GetUnk40() { return mSongShortName; }
    Symbol GetUnk44() { return mSongTitle; }
    Symbol GetUnk48() { return mChallengerGamertag; }
    Symbol GetUnk4c() { return mMissionInfo; }
    int GetUnk50() { return mSongContentLockState; }

private:
    ChallengeRow mRow; // 0x4
    Symbol mSongShortName; // 0x40
    Symbol mSongTitle; // 0x44
    Symbol mChallengerGamertag; // 0x48
    Symbol mMissionInfo; // 0x4c
    int mSongContentLockState; // 0x50
};
