#pragma once
#include "hamobj\Difficulty.h"
#include "meta_ham\HamProfile.h"
#include "net_ham\RCJobDingo.h"

class LeaderboardRow {
public:
    String mName; // 0x0 - gamertag
    int mPlayerID; // 0x8
    int mScore; // 0xc
    unsigned int mRank; // 0x10
    int mModeID; // 0x14
    Difficulty mDiffID; // 0x18
    bool mNoFlashcards; // 0x1c
    bool mIsPercentile; // 0x1d
    bool mIsHardcore; // 0x1e
    XUID mXUID; // 0x20
};

class GetLeaderboardByPlayerJob : public RCJob {
public:
    GetLeaderboardByPlayerJob(
        Hmx::Object *callback,
        HamProfile *,
        int songID,
        int typeID,
        int modeID,
        int numRows,
        unsigned int
    );
    void GetRows(std::vector<LeaderboardRow> *);
    unsigned int SongID() const { return mCacheKey; }

private:
    friend class Leaderboards;
    unsigned int mCacheKey; // 0xb0
};

class GetMiniLeaderboardJob : public RCJob {
public:
    GetMiniLeaderboardJob(Hmx::Object *callback, const HamProfile *, int songID);
    void GetRows(std::vector<LeaderboardRow> *);

    int SongID() const { return mSongID; }

private:
    int mSongID; // 0xb0
};
