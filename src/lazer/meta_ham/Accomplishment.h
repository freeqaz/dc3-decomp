#pragma once
#include "hamobj\Difficulty.h"
#include "obj\Data.h"
#include "ui\UILabel.h"
#include "utl\Symbol.h"

class HamProfile;

// DC3's own set, read off the shipped image: every GetType() override in
// ham_xbox_r.map is a two-instruction `li r3, N; blr`, and the seven Ns are
// consecutive 0..6.  (The previous list here was RB3's -- it carried
// SongFilter/Lesson/Trainer/Setlist types DC3 has no classes for, and put
// OneShot at 9 and DiscSongConditional at 11 where retail has 3 and 5.)
//   0x82AEAE70 Accomplishment                        li r3, 0
//   0x82E2AB00 AccomplishmentSongListConditional     li r3, 1
//   0x825E9C88 AccomplishmentCountConditional        li r3, 2
//   0x82DC4288 AccomplishmentOneShot                 li r3, 3
//   0x82E77010 AccomplishmentCharacterListConditional li r3, 4
//   0x825E9C80 AccomplishmentDiscSongConditional     li r3, 5
//   0x82918DD8 AccomplishmentCampaignConditional     li r3, 6
enum AccomplishmentType {
    kAccomplishmentTypeUnique = 0,
    kAccomplishmentTypeSongListConditional = 1,
    kAccomplishmentTypeCountConditional = 2,
    kAccomplishmentTypeOneShot = 3,
    kAccomplishmentTypeCharacterListConditional = 4,
    kAccomplishmentTypeDiscSongConditional = 5,
    kAccomplishmentTypeCampaignConditional = 6
};

class Accomplishment {
public:
    Accomplishment(DataArray *, int);
    virtual ~Accomplishment();
    virtual AccomplishmentType GetType() const { return kAccomplishmentTypeUnique; }
    virtual bool ShowBestAfterEarn() const;
    virtual void UpdateIncrementalEntryName(UILabel *, Symbol) {
        MILO_ASSERT(false, 0x4c);
    }
    virtual bool IsFulfilled(HamProfile *) const { return false; }
    virtual bool IsRelevantForSong(Symbol) const { return false; }
    virtual Difficulty GetRequiredDifficulty() const;
    virtual bool InqProgressValues(HamProfile *, int &, int &) { return false; }
    virtual bool InqIncrementalSymbols(HamProfile *, std::vector<Symbol> &) const {
        return false;
    }
    virtual bool IsSymbolEntryFulfilled(HamProfile *, Symbol) const { return false; }
    virtual Symbol GetFirstUnfinishedAccomplishmentEntry(HamProfile *) const {
        return gNullStr;
    }
    virtual bool CanBeLaunched() const;
    virtual bool HasSpecificSongsToLaunch() const { return false; }

    Symbol GetCategory() const;
    bool HasGamerpicReward() const;
    bool HasAvatarAssetReward() const;
    Symbol GetAward() const;
    bool HasAward() const;
    bool IsSecondaryGoal() const;
    bool IsDynamic() const;
    char const *GetIconArt() const;
    Symbol GetName() const;
    const std::vector<Symbol> &GetDynamicPrereqsSongs() const;
    int GetDynamicPrereqsNumSongs() const;
    int GetAvatarAssetReward() const;
    int GetContextID() const;
    int GetGamerpicReward() const;
    bool GiveToAll() const { return mGiveToAll; }

protected:
    Symbol mName; // 0x4
    std::vector<Symbol> mSecretPrereqs; // 0x8
    AccomplishmentType mAccomplishmentType; // 0x14
    Symbol mCategory; // 0x18
    Symbol mAward; // 0x1c
    Symbol mUnitsToken; // 0x20
    Difficulty mDifficulty; // 0x24
    Symbol mPassiveMsgChannel; // 0x28
    int mPassiveMsgPriority; // 0x2c
    bool mRequiresUnisonAbility; // 0x30
    int mPlayerCountMin; // 0x34
    int mPlayerCountMax; // 0x38
    int mNumSongs; // 0x3c
    std::vector<Symbol> mDynamicPrereqsSongs; // 0x40
    int mProgressStep; // 0x4c
    int mGamerpicReward; // 0x50
    int mAvatarAssetReward; // 0x54
    bool mShowBestAfterEarn; // 0x58
    bool mHideProgress; // 0x59
    int mIndex; // 0x5c
    int mContextID; // 0x60
    bool mEarnedNoFail; // 0x64
    bool mLeaderboard; // 0x65
    bool mIsSecondaryGoal; // 0x66
    bool mGiveToAll; // 0x67

private:
    void Configure(DataArray *);
};
