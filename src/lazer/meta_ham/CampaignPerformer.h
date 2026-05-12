#pragma once
#include "hamobj/Difficulty.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/Playlist.h"
#include "obj/Data.h"
#include "utl/Symbol.h"

class CampaignPerformer : public MetaPerformer {
public:
    CampaignPerformer(const HamSongMgr &);
    // Hmx::Object
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    // MetaPerformer
    virtual bool IsWinning() const { return false; }
    virtual void SelectSong(Symbol, int);
    virtual void CompleteSong(int, int, int, float, bool);
    virtual void OnLoadSong();
    virtual void OnMovePassed(int, HamMove *, int, float);

    bool InOutroPerform() const;
    bool WonCurrentOutroSong() const;
    void SetDifficulty(Difficulty diff);
    void CheckForOutfitAwards(Difficulty diff, Symbol era);
    void CheckForMasteryGoal(Difficulty diff, Symbol era);
    int GetStarsRequiredForOutfits(Symbol era) const;
    int GetSongStarsEarned(Symbol era, Symbol song) const;
    int GetStarsRequiredForMastery(Symbol era) const;
    int GetMovesRequiredForMastery(Symbol era) const;
    Symbol GetCompletionAccomplishment(Symbol era) const;
    bool IsEraNew() const;
    bool IsCampaignNew() const;
    bool IsCampaignIntroComplete() const;
    void SetCampaignIntroComplete(bool complete);
    bool IsCampaignMindControlComplete() const;
    void SetCampaignMindControlComplete(bool complete);
    bool IsCampaignComplete() const;
    void SetCampaignComplete();
    bool IsEraMastered(Symbol era) const;
    bool IsDanceCrazeSongAvailable(Symbol era) const;
    bool IsEraComplete(Symbol era) const;
    bool HasEraOutfits(Symbol era) const;
    Symbol GetDanceCrazeSong() const;
    bool IsAttemptingDanceCrazeSong() const;
    Symbol GetEraSongUnlockedToken() const;
    Symbol GetEraCompleteToken() const;
    Symbol GetWinInstructionsToken() const;
    Symbol GetCharacterForSong() const;
    Symbol GetChallengeCharacter() const;
    Symbol GetEraIntroMovieToken() const;
    bool GetEraIntroMoviePlayed() const;
    void SetEraIntroMoviePlayed(bool complete);
    bool IsDanceCrazeMove(Symbol era, Symbol song, HamMove *move);
    bool IsDanceCrazeMoveMastered(Symbol era, Symbol song, HamMove *move);
    int GetNumEraSongs();
    Symbol GetEraSong(int index);
    Symbol GetEraIntroSong();
    int GetSongIndex(Symbol song);
    int GetNumSongCrazeMoves(Symbol song);
    bool HasSongBeenAttempted(Symbol song);
    bool CanSelectEraSong(Symbol song);
    bool IsEraMoveMastered(Symbol song, int index);
    void BookmarkCurrentProgress();
    void ClearAllCampaignProgress();
    void UnlockAllMoves(Symbol era, Symbol song, int stars);
    void ResetAllCampaignProgress();
    void ClearSongProgress(Symbol era, Symbol song);
    Symbol GetFirstEra() const;
    Symbol GetLastEra() const;
    void SetOutroPlaylist();
    void UpdateEraSong(Difficulty d, Symbol era, Symbol song, int stars);
    int GetEraStarsEarned(Symbol era) const;
    int GetMasteryMoves(Symbol era) const;
    void AwardCrazeAccomplishments();
    void AwardBossAccomplishment();
    void AwardMasterQuestAccomplishments();
    void UpdateStarsEarnedSoFar(int stars);
    int GetSongAttemptedCount();
    void SetupCampaignCharacters(Symbol crew, Symbol character);
    void SetEra(Symbol era);
    void SetIntroPlaylist();
    bool SetEraToFirstIncomplete();

    Symbol GetTanBattleEra() { return "era_tan_battle"; }
    Symbol Era() const { return mEra; }
    Difficulty GetDifficulty() const { return mDifficulty; }
    bool JustUnlockedEraSong() { return mJustUnlockedEraSong; }

protected:
    Playlist mIntroPlaylist; // 0x114
    Playlist mOutroPlaylist; // 0x12c
    Symbol mEra; // 0x144
    Difficulty mDifficulty; // 0x148
    bool mJustFinishedEra; // 0x14c
    bool mJustUnlockedEraSong; // 0x14d
    bool mWasLastMoveMastered; // 0x14e
    Symbol mLastMoveMasteredName; // 0x150
    int mLastEraStars; // 0x154
    int mLastEraMoves; // 0x158
    int mStarsEarnedSoFar; // 0x15c

private:
    void CheckForEraSongUnlock();
};
