#include "meta_ham/HamStarsDisplay.h"
#include "hamobj/Difficulty.h"
#include "hamobj/StarsDisplay.h"
#include "meta/SongMgr.h"
#include "meta_ham/CampaignPerformer.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/Utl.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

BEGIN_HANDLERS(HamStarsDisplay)
    HANDLE_ACTION(set_song, SetSongImpl(_msg->Int(2), kNumDifficulties, kStarDisplay_0))
    HANDLE_ACTION(
        set_song_with_diff,
        SetSongImpl(_msg->Int(2), (Difficulty)_msg->Int(3), kStarDisplay_1)
    )
    HANDLE_ACTION(
        set_song_with_diff_always,
        SetSongImpl(_msg->Int(2), (Difficulty)_msg->Int(3), kStarDisplay_2)
    )
    HANDLE_ACTION(
        set_song_last_played, SetSongImpl(_msg->Int(2), kNumDifficulties, kStarDisplay_3)
    )
    HANDLE_SUPERCLASS(StarsDisplay)
END_HANDLERS

void HamStarsDisplay::SetSongChallenge(Difficulty diff) {
    MILO_ASSERT(diff != kNumDifficulties, 0x2f);
    mDiffLabel->SetShowing(true);
    char const *s = MakeString("%s_short", DifficultyToSym(diff));
    mDiffLabel->SetTextToken(s);
    mStarsLabel->SetShowing(false);
    mNoFlashcardsLabel->SetShowing(false);
}

void HamStarsDisplay::SetSong(int i) { SetSongImpl(i, kNumDifficulties, kStarDisplay_0); }

void HamStarsDisplay::SetSongCampaign(int i) {
    SetSongImpl(i, kNumDifficulties, kStarDisplay_4);
}

void HamStarsDisplay::SetSongWithDifficulty(int i, Difficulty d, bool b) {
    SetSongImpl(i, d, b ? kStarDisplay_2 : kStarDisplay_1);
}

void HamStarsDisplay::SetSongImpl(int songID, Difficulty diff, StarDisplayMode mode) {
    bool u11 = false;
    bool c10 = 0;
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        if (profile->GetSongStatusMgr()->HasSongStatus(songID)) {
            const SongStatus &status = profile->GetSongStatusMgr()->GetSongStatus(songID);
            switch (mode) {
            case 0: {
                MILO_ASSERT((unsigned int)diff == kNumDifficulties, 0x48);
                const SongStatusData &data = status.GetBestSongStatusData();
                if (data.mScore > 0) {
                    u11 = true;
                    for (int i = 0; i < 4; i++)
                        ;
                    c10 = data.mFiveStarNoFlashcards;
                }
                break;
            }
            case 1:
            case 2: {
                MILO_ASSERT(diff < kNumDifficulties, 0x5C);
                if (status.mStatusData[diff].mScore > 0) {
                    u11 = true;
                    c10 = status.mStatusData[diff].mFiveStarNoFlashcards;
                }
                break;
            }
            case 3: {
                MILO_ASSERT(diff == kNumDifficulties, 0x67);
                if (status.mLastPlayedDifficulty != kNumDifficulties) {
                    u11 = true;
                    c10 = status.mLastNoFlashcards;
                }
                break;
            }
            case 4: {
                MILO_ASSERT(diff == kNumDifficulties, 0x73);
                CampaignPerformer *pPerformer =
                    dynamic_cast<CampaignPerformer *>(MetaPerformer::Current());
                MILO_ASSERT(pPerformer, 0x76);
                Symbol era = pPerformer->Era();
                Symbol shortname = TheSongMgr.GetShortNameFromSongID(songID);
                HamProfile *pProfile = TheProfileMgr.GetActiveProfile(true);
                MILO_ASSERT(pProfile, 0x7B);
                u11 = pProfile->GetCampaignProgress(pPerformer->GetDifficulty())
                          .GetSongStarsEarned(era, shortname)
                    > 0;
            }
            }
        }
    }
    if (u11) {
        mDiffLabel->SetShowing(true);
        mDiffLabel->SetTextToken(MakeString("%s_short", DifficultyToSym(diff)));
        mStarsLabel->SetShowing(true);
        mStarsLabel->SetTextToken(GetStarsToken(songID));
        if (c10) {
            mNoFlashcardsLabel->SetShowing(true);
        } else {
            mNoFlashcardsLabel->SetShowing(false);
        }
    } else {
        if (mShowUnplayedSong) {
            if (mode == 2) {
                mDiffLabel->SetShowing(true);
                mDiffLabel->SetTextToken(MakeString("%s_short", DifficultyToSym(diff)));
            } else {
                mDiffLabel->SetShowing(false);
            }
            mStarsLabel->SetShowing(true);
            mStarsLabel->SetTextToken(GetStarsToken(0));
        } else {
            mDiffLabel->SetShowing(false);
            mStarsLabel->SetShowing(false);
        }
        mNoFlashcardsLabel->SetShowing(false);
    }
}
