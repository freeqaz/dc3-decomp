#pragma once
#include "Accomplishment.h"
#include "meta/FixedSizeSaveable.h"
#include "meta/FixedSizeSaveableStream.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include "xdk/xapilibi/xbase.h"
#include <list>
#include <utility>
#include <map>
#include <set>
#include <vector>

class HamProfile;

enum GamerAwardType {
    type0,
    type1
};

class GamerAwardStatus : public FixedSizeSaveable {
public:
    GamerAwardStatus();
    GamerAwardStatus(int, GamerAwardType);
    virtual ~GamerAwardStatus();
    virtual void SaveFixed(FixedSizeSaveableStream &) const;
    virtual void LoadFixed(FixedSizeSaveableStream &, int);

    static int SaveSize(int);

    int mRewardId;
    GamerAwardType mType; // 0xc
    bool mIsPending;
    XUSER_AVATARASSET mAsset; // 0x14
    XOVERLAPPED mOverlapped; // 0x1c
};

class AccomplishmentProgress : public Hmx::Object, public FixedSizeSaveable {
public:
    // Hmx::Object
    virtual ~AccomplishmentProgress();
    virtual DataNode Handle(DataArray *, bool);
    virtual void SaveFixed(FixedSizeSaveableStream &) const;
    virtual void LoadFixed(FixedSizeSaveableStream &, int);

    static int SaveSize(int);

    AccomplishmentProgress(HamProfile *);
    int GetNiceMoveCount() const;
    void IncrementDanceBattleCount();
    void ClearAllPerfectMoves();
    void ClearPerfectStreak();
    bool HasNewAwards() const;
    void NotifyPlayerOfAccomplishment(Symbol, const char *);
    void SetTotalSongsPlayed(int);
    void SetTotalCampaignSongsPlayed(int);
    void MovePassed(Symbol, int);
    Symbol GetFirstNewAward() const;
    Symbol GetFirstNewAwardReason() const;
    void Poll();
    bool IsAccomplished(Symbol) const;
    void ClearFirstNewAward();
    int GetNumCompletedInCategory(Symbol) const;
    int GetNumCompletedInGroup(Symbol) const;
    int GetCharacterUseCount(Symbol) const;
    int GetCount(Symbol) const;
    bool AddAward(Symbol, Symbol);
    bool AddAccomplishment(Symbol);
    void Clear();
    void IncrementCharacterUseCount(Symbol);
    void IncrementCount(Symbol, int);
    int GetTotalSongsPlayed() const;
    int GetTotalCampaignSongsPlayed() const;
    int GetNumCompleted() const;
    int GetFlawlessMoveCount() const;
    bool HasAward(Symbol s) const { return mUnlockedAwards.find(s) != mUnlockedAwards.end(); }
    int NumDays() const { return mTotalDaysActive; }
    void SetNumDays(int i) { mTotalDaysActive = i; }
    int NumWeekends() const { return mWeekendCount; }
    int GetChallengeProgress() const { return mChallengeProgress; }
    void SetChallengeProgress(int i) { mChallengeProgress = i; }
    int GetWeeklyPlayCount() const { return mWeeklyPlayCount; }
    void SetWeekends(int i) { mWeekendCount = i; }
    void SetWeeklyPlayCount(int i) { mWeeklyPlayCount = i; }
    const std::list<std::pair<Symbol, Symbol> >& GetNewAwards() const { return mNewAwards; }

private:
    void GiveGamerpic(Accomplishment *);
    void GiveAvatarAsset(Accomplishment *);

    std::map<Symbol, int> unk34;
    HamProfile *mParentProfile; // 0x4c
    std::list<GamerAwardStatus *> mPendingAwards; // 0x50
    std::set<Symbol> mCompletedAchievements; // 0x58
    std::set<Symbol> mHardcoreAchievements; // 0x70
    std::vector<Symbol> mCharacterAchievementList; // 0x88
    std::set<Symbol> mUnlockedAwards; // 0x94
    // award, reason
    std::list<std::pair<Symbol, Symbol> > mNewAwards; // 0xac
    int mTotalSongsPlayed; // 0xb4
    int mTotalCampaignSongsPlayed; // 0xb8
    std::map<Symbol, int> mAchievementCounts; // 0xbc
    int mDanceBattleCount; // 0xd4
    int mFreestylePhotoCount; // 0xd8
    bool mPerfectMovesCleared; // 0xdc - completely flawless?
    int mGamerscoreAccumulator; // 0xe0
    int mSessionGamerScore; // 0xe4
    int mPendingGamerScore; // 0xe8
    // symbol = char, int = use count
    std::map<Symbol, int> mCharacterUseCounts; // 0xec
    int mFlawlessMoveCount; // 0x104
    int mNiceMoveCount; // 0x108
    Symbol unk10c;
    int unk110;
    int mTotalDaysActive; // 0x114
    int mChallengeProgress; // 0x118
    int mWeekendCount; // 0x11c
    int mWeeklyPlayCount; // 0x120
};
