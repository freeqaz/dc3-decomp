#include "meta_ham/LockedContentPanel.h"
#include "Accomplishment.h"
#include "HamPanel.h"
#include "HamSongMetadata.h"
#include "HamSongMgr.h"
#include "flow/Flow.h"
#include "game/GameMode.h"
#include "hamobj/Difficulty.h"
#include "hamobj/HamLabel.h"
#include "hamobj/MoveDir.h"
#include "macros.h"
#include "meta_ham/AccomplishmentCountConditional.h"
#include "meta_ham/AccomplishmentManager.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/Campaign.h"
#include "meta_ham/HamStarsDisplay.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/Timer.h"
#include "rndobj/Draw.h"
#include "synth/Sound.h"
#include "ui/PanelDir.h"
#include "ui/UIPanel.h"
#include "utl/MakeString.h"
#include "utl/Symbol.h"

LockedContentPanel *TheLockedContentPanel;

LockedContentPanel::LockedContentPanel() : mSound(), mTimer(new Timer()), mIsTeaserTextShowing(false) {
    MILO_ASSERT(TheLockedContentPanel == NULL, 0x27);
    TheLockedContentPanel = this;
}

LockedContentPanel::~LockedContentPanel() {
    MILO_ASSERT(TheLockedContentPanel != NULL, 0x2d);
    TheLockedContentPanel = nullptr;
    RELEASE(mTimer);
}

void LockedContentPanel::Exit() {
    if (mSound && mIsTeaserTextShowing) {
        mSound->Stop(nullptr, false);
    }
    mSound = nullptr;
    mTimer->Reset();
    mIsTeaserTextShowing = false;
    UIPanel::Exit();
}

void LockedContentPanel::Enter() {
    mTimer->Start();
    HamPanel::Enter();
}

void LockedContentPanel::Poll() {
    if (mSound && !mIsTeaserTextShowing && mTimer->SplitMs() >= 750.0f) {
        mSound->Play(0, 0, 0, nullptr, 0);
        mIsTeaserTextShowing = true;
        mTimer->Reset();
    }
    HamPanel::Poll();
}

void LockedContentPanel::SetVoiceOver(Sound *s, bool b) {
    mSound = s;
    mIsTeaserTextShowing = b;
}

void LockedContentPanel::FinishLoad() {
    UIPanel::FinishLoad();
    for (int i = 0; i < 8; i++) {
        mLabels[i] = (HamLabel *)mDir->Find<AppLabel>(MakeString("song_name0%d.lbl", i + 1));
        HamStarsDisplay *stars = mDir->Find<HamStarsDisplay>(MakeString("stars0%d.std", i + 1));
        mLabels[i + 8] = (HamLabel *)stars;
        stars->SetShowUnplayedSong(true);
    }
}

void LockedContentPanel::TriggerTeaserText() {
    ObjectDir *pDir = DataDir();
    MILO_ASSERT(pDir, 0x69);
    Flow *pFlow = pDir->Find<Flow>("teaser_text.flow", true);
    MILO_ASSERT(pFlow, 0x6b);
    pFlow->Activate();
}

void LockedContentPanel::SetUpCampaignSong(Symbol song) {
    AppLabel *pContentName = DataDir()->Find<AppLabel>("content_name.lbl");
    MILO_ASSERT(pContentName, 0x73);
    HamLabel *pTeaser = DataDir()->Find<HamLabel>("teaser.lbl");
    MILO_ASSERT(pTeaser, 0x75);
    HamLabel *pInstructions = DataDir()->Find<HamLabel>("instructions.lbl");
    MILO_ASSERT(pInstructions, 0x77);
    HamLabel *pPracticeScore = DataDir()->Find<HamLabel>("practice_score.lbl");
    MILO_ASSERT(pPracticeScore, 0x79);
    pTeaser->SetShowing(false);
    TriggerTeaserText();
    mSound = nullptr;
    if (TheCampaign->UpdateEraSongUnlockInstructions(song, pInstructions)) {
        static Symbol era_craze_title("era_craze_title");
        pContentName->SetTextToken(era_craze_title);
    } else {
        const HamSongMetadata *pMetaData =
            TheHamSongMgr.Data(TheHamSongMgr.GetSongIDFromShortName(song));
        pContentName->SetTextToken(pMetaData->Title());
    }
    pPracticeScore->SetTextToken(gNullStr);
}

void LockedContentPanel::SetUpCampaignMasterQuestHeader(Symbol song) {
    String str;
    Symbol s = TheAccomplishmentMgr->GetAssetSource(song);
    Accomplishment *pAccomplishment = TheAccomplishmentMgr->GetAccomplishment(s);
    AppLabel *pContentName = DataDir()->Find<AppLabel>("content_name.lbl");
    HamLabel *pTeaser = DataDir()->Find<HamLabel>("teaser.lbl");
    HamLabel *pInstructions = DataDir()->Find<HamLabel>("instructions.lbl");
    HamLabel *pProgress = DataDir()->Find<HamLabel>("progress.lbl");
    HamLabel *pPracticeScore = DataDir()->Find<HamLabel>("practice_score.lbl");
    pContentName->SetTextToken(gNullStr);
    pTeaser->SetTextToken(MakeString("campaign_mq_locked_%s", song.Str()));
    pInstructions->SetTextToken(gNullStr);
    pProgress->SetTextToken(gNullStr);
    pPracticeScore->SetTextToken(gNullStr);
    TriggerTeaserText();
    mSound = nullptr;
    if (!pAccomplishment) {
        MILO_NOTIFY("Could not find accomplishment for %s", song);
    } else {
        if (pAccomplishment->GetType() == kAccomplishmentTypeLessonSongListConditional
            || pAccomplishment->GetType() == kAccomplishmentTypeTourConditional) {
            AccomplishmentCountConditional *pAccomplishmentCountConditional =
                dynamic_cast<AccomplishmentCountConditional *>(pAccomplishment);
            Flow *pFlow = DataDir()->Find<Flow>("one_shot.flow");
            pFlow->Activate();
            pInstructions->SetTextToken(
                MakeString("%s%s%s", "award_", song, "_instruction")
            );
        } else {
            MILO_ASSERT(false, 0xbe);
        }
    }
}

void LockedContentPanel::SetUp(Symbol song) {
    String str;
    Symbol s = TheAccomplishmentMgr->GetAssetSource(song);
    Accomplishment *pAccomplishment = TheAccomplishmentMgr->GetAccomplishment(s);
    AppLabel *pContentName = DataDir()->Find<AppLabel>("content_name.lbl");
    HamLabel *pTeaser = DataDir()->Find<HamLabel>("teaser.lbl");
    HamLabel *pInstructions = DataDir()->Find<HamLabel>("instructions.lbl");
    HamLabel *pProgress = DataDir()->Find<HamLabel>("progress.lbl");
    bool songID = TheHamSongMgr.GetSongIDFromShortName(song, false) != 0;
    if (songID != 0) {
        pContentName->SetSongName(song, -1, false);
    } else {
        pContentName->SetTextToken(song);
    }
    auto teaserAwardToken = MakeString("%s%s", "teaser_award_", song);
    pTeaser->SetTextToken(teaserAwardToken);
    TriggerTeaserText();
    mSound = nullptr;
    if (!pAccomplishment) {
        MILO_NOTIFY("Could not find accomplishment for %s", song);
    } else {
        if (pAccomplishment->GetType() == kAccomplishmentTypeLessonSongListConditional
            || pAccomplishment->GetType() == kAccomplishmentTypeTourConditional) {
            AccomplishmentCountConditional *pAccomplishmentCountConditional =
                dynamic_cast<AccomplishmentCountConditional *>(pAccomplishment);
            Flow *pFlow = DataDir()->Find<Flow>("one_shot.flow");
            pFlow->Activate();
            pInstructions->SetTextToken(
                MakeString("%s%s%s", "award_", song, "_instruction")
            );
        } else {
            MILO_ASSERT(false, 0xed);
        }
    }
}

void LockedContentPanel::SetUpNoFlashcards(Symbol song, Difficulty diff) {
    AppLabel *pContentName = DataDir()->Find<AppLabel>("content_name.lbl");
    HamLabel *pTeaser = DataDir()->Find<HamLabel>("teaser.lbl");
    HamLabel *pInstructions = DataDir()->Find<HamLabel>("instructions.lbl");
    HamLabel *pProgress = DataDir()->Find<HamLabel>("progress.lbl");
    pContentName->SetTextToken(gNullStr);
    pTeaser->SetTextToken(gNullStr);
    Flow *pFlow = DataDir()->Find<Flow>("no_teaser_text.flow");
    pFlow->Activate();
    static Symbol award_noflashcard_instruction("award_noflashcard_instruction");
    pInstructions->SetTokenFmt(award_noflashcard_instruction, 5);
    Flow *pFlowSingle = DataDir()->Find<Flow>("song_list_single.flow");
    pFlowSingle->Activate();
    int songID = TheHamSongMgr.GetSongIDFromShortName(song);
    mLabels[0]->SetShowing(true);
    mLabels[8]->SetShowing(true);
    pContentName->SetSongName(song, -1, false);
    ((HamStarsDisplay *)mLabels[8])->SetSongWithDifficulty(songID, diff, true);
    HamLabel **pPtr = &mLabels[8];
    int loopCount = 7;
    do {
        pPtr[-7]->SetShowing(false);
        pPtr++;
        (*pPtr)->SetShowing(false);
        loopCount -= 1;
    } while (loopCount != 0);
}

void LockedContentPanel::SetUpDifficultyLocked(Symbol s1, Symbol s2) {
    Difficulty diff = SymToDifficulty(s2);
    MILO_ASSERT(diff > kDifficultyEasy, 0x111);
    AppLabel *pContentName = DataDir()->Find<AppLabel>("content_name.lbl");
    HamLabel *pTeaser = DataDir()->Find<HamLabel>("teaser.lbl");
    HamLabel *pInstructions = DataDir()->Find<HamLabel>("instructions.lbl");
    HamLabel *pProgress = DataDir()->Find<HamLabel>("progress.lbl");
    HamLabel *pPracticeScore = DataDir()->Find<HamLabel>("practice_score.lbl");
    MILO_ASSERT(pPracticeScore, 0x118);
    pPracticeScore->SetTextToken(gNullStr);
    pContentName->SetTextToken(gNullStr);
    pTeaser->SetTextToken(gNullStr);
    Flow *pFlow = DataDir()->Find<Flow>("no_teaser_text.flow");
    pFlow->Activate();
    static Symbol award_medium_instruction("award_medium_instruction");
    static Symbol award_expert_instruction("award_expert_instruction");
    static Symbol award_medium_playlist_instruction("award_medium_playlist_instruction");
    static Symbol award_hard_playlist_instruction("award_hard_playlist_instruction");
    static Symbol award_mediummedley_instruction("award_mediummedley_instruction");
    static Symbol award_hardmedley_instruction("award_hardmedley_instruction");
    if (TheGameMode->InMode("playlist_perform", true)) {
        if (diff == kDifficultyMedium) {
            pInstructions->SetTokenFmt(award_medium_playlist_instruction, 3);
        } else {
            pInstructions->SetTokenFmt(award_hard_playlist_instruction, 3);
        }

        for (int i = 0; i <= 7; i++) {
            mLabels[i]->SetShowing(false);
            mLabels[i + 8]->SetShowing(false);
        }
    } else {
        if (diff == kDifficultyMedium) {
            pInstructions->SetTokenFmt(award_medium_instruction, 3);
        } else {
            pInstructions->SetTokenFmt(award_expert_instruction, 3);
        }
        Flow *pPracticeFlow = DataDir()->Find<Flow>("song_list_perform_practice.flow");
        pPracticeFlow->Activate();
        int songID = TheHamSongMgr.GetSongIDFromShortName(s1);
        mLabels[0]->SetShowing(true);
        mLabels[8]->SetShowing(true);
        ((AppLabel *)mLabels[0])->SetSongName(s1, -1, false);
        ((HamStarsDisplay *)mLabels[8])->SetSongWithDifficulty(songID, (Difficulty)(diff - 1), true);
        for (int i = 1; i <= 7; i++) {
            mLabels[i]->SetShowing(false);
            mLabels[i + 8]->SetShowing(false);
        }
    }
}

BEGIN_HANDLERS(LockedContentPanel)
    HANDLE_ACTION(set_up, SetUp(_msg->ForceSym(2)))
    HANDLE_ACTION(set_up_campaign_song, SetUpCampaignSong(_msg->ForceSym(2)))
    HANDLE_ACTION(
        set_up_no_flashcards,
        SetUpNoFlashcards(_msg->ForceSym(2), (Difficulty)_msg->Int(3))
    )
    HANDLE_ACTION(
        set_up_difficulty_locked, SetUpDifficultyLocked(_msg->ForceSym(2), _msg->Sym(3))
    )
    HANDLE_ACTION(
        set_up_campaign_master_quest_header,
        SetUpCampaignMasterQuestHeader(_msg->ForceSym(2))
    )
    HANDLE_ACTION(set_voice_over, SetVoiceOver(_msg->Obj<Sound>(2), false))
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS
