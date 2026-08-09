#pragma once
#include "meta_ham\HamProfile.h"
#include "meta_ham\SongStatusMgr.h"
#include "net_ham\RCJobDingo.h"
#include "obj\Object.h"

class RecordScoreData {
public:
    RecordScoreData() : mProfile(0), mStatus(0), mChallengeScore(0), mChainChallengeScore(0) {}
    virtual ~RecordScoreData() {}

    HamProfile *mProfile; // 0x4
    SongStatusData *mStatus; // 0x8
    int mChallengeScore; // 0xc - c score
    int mChainChallengeScore; // 0x10 - cc score
};

class RecordScoreJob : public RCJob {
public:
    RecordScoreJob(
        Hmx::Object *callback, RecordScoreData &data, int songID, bool provideInstarank
    );
};
