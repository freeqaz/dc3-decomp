#include "game/SongSequence.h"
#include "flow/PropertyEventProvider.h"
#include "game/Game.h"
#include "game/GameMode.h"
#include "game/GamePanel.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMaster.h"
#include "hamobj/HamPlayerData.h"
#include "macros.h"
#include "math/Easing.h"
#include "meta_ham/CampaignPerformer.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "midi/MidiParserMgr.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Group.h"
#include "rndobj/PropAnim.h"
#include "ui/UILabel.h"
#include "ui/UIPanel.h"
#include "utl/Symbol.h"
#include "utl/TimeConversion.h"

SongSequence TheSongSequence;

SongSequence::SongSequence() {}
SongSequence::~SongSequence() {}

BEGIN_HANDLERS(SongSequence)
    HANDLE_ACTION(clear, Clear())
    HANDLE_ACTION(add, Add(_msg))
    HANDLE_EXPR(do_next, DoNext(false, _msg->Int(2)))
    HANDLE_EXPR(done, Done())
    HANDLE_ACTION(on_rhythm_battle_combo_full, DoNext(false, false))
    HANDLE_ACTION(load_next_song_audio, LoadNextSongAudio())
    HANDLE_EXPR(get_intro_cam_shot, GetIntroCamShot())
    HANDLE_EXPR(get_outro_cam_shot, GetOutroCamShot())
    HANDLE_EXPR(
        loop_start,
        mCurrentIndex > mEntries.size() ? 0
                                        : BeatToMs(mEntries[mCurrentIndex].mEventStartMeasure * 4.0f)
    )
    HANDLE_EXPR(empty, mEntries.size() == 0)
    HANDLE_EXPR(current_index, mCurrentIndex)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void SongSequence::Init() {
    mFileCache = nullptr;
    SetName("songseq", ObjectDir::Main());
    Clear();
}

bool SongSequence::Done() const {
    int numEntries = mEntries.size();
    return numEntries == 0 || mCurrentIndex >= numEntries;
}

void SongSequence::Add(const DataArray *a) {
    static Symbol babygotback("babygotback");
    static Symbol perform("perform");
    static Symbol holla_back_config_default("holla_back_config_default");
    if (a) {
        Entry entry;
        int aSize = a->Size();
        entry.mSongLongName = aSize > 2 ? a->Sym(2) : babygotback;
        entry.mSongShortName = aSize > 3 ? a->Sym(3) : babygotback;
        entry.mGameplayMode = aSize > 4 ? a->Sym(4) : perform;
        entry.mIntroLoopMeasure = aSize > 5 ? a->Float(5) : -1;
        entry.mOutroLoopMeasure = aSize > 6 ? a->Float(6) : -1;
        entry.mModeConfig = aSize > 7 ? a->Sym(7) : holla_back_config_default;
        entry.mEventStartMeasure = aSize > 8 ? a->Float(8) : -1;
        entry.mEventEndMeasure = aSize > 9 ? a->Float(9) : -1;
        entry.mIsIntro = aSize > 10 ? a->Int(10) : false;
        entry.mIsOutro = aSize > 11 ? a->Int(11) : entry.mIsIntro;
        entry.mIntroCamShot = aSize > 12 ? a->Sym(12) : "";
        entry.mOutroCamShot = aSize > 13 ? a->Sym(13) : "";
        entry.mCrew1Symbol = aSize > 14 ? a->Sym(14) : "";
        entry.mCrew2Symbol = aSize > 15 ? a->Sym(15) : "";
        entry.mTotalScore = entry.mStarCount = 0;
        mEntries.push_back(entry);
    }
}

void SongSequence::Clear() {
    mCurrentIndex = -1;
    mEntries.clear();
    mPrevSongPosition = 0;
    mNextSongLoadPosition = 0;
    if (mFileCache) {
        RELEASE(mFileCache);
    }
}

Symbol SongSequence::GetIntroCamShot() const {
    if (Done()) {
        return "";
    } else {
        return mEntries[mCurrentIndex].mIntroCamShot;
    }
}

Symbol SongSequence::GetOutroCamShot() const {
    if (Done()) {
        return "";
    } else {
        return mEntries[mCurrentIndex].mOutroCamShot;
    }
}

void SongSequence::LoadNextSongAudio() {
    if (!Done() && mCurrentIndex <= mEntries.size() - 2) {
        Hmx::Object *game = ObjectDir::Main()->Find<Hmx::Object>("game");
        if (game) {
            game->Handle(Message("set_realtime", true), true);
        }
        Entry &curEntry = mEntries[mCurrentIndex + 1];
        TheGame->LoadNewSongAudio(curEntry.mSongLongName);
        bool b3 = false;
        if (*curEntry.mCrew1Symbol.Str() != '\0') {
            b3 = true;
            HamPlayerData *hpd = TheGameData->Player(0);
            hpd->SetOutfit("");
            hpd->SetCrew(curEntry.mCrew1Symbol);
            if (curEntry.mGameplayMode == Symbol("mind_control")) {
                hpd->SetOutfit("lima06");
            }
        }
        if (*curEntry.mCrew2Symbol.Str() != '\0') {
            b3 = true;
            HamPlayerData *hpd = TheGameData->Player(1);
            hpd->SetOutfit("");
            hpd->SetCrew(curEntry.mCrew2Symbol);
            if (curEntry.mGameplayMode == Symbol("mind_control")) {
                hpd->SetOutfit("rasa06");
            }
        }
        if (b3) {
            TheHamDirector->LoadCrew(
                TheGameData->Player(0)->Crew(), TheGameData->Player(1)->Crew()
            );
        }
    }
}

bool SongSequence::DoNext(bool b1, bool b2) {
    static Symbol midi_player("midi_player");
    static Symbol holla_back_config("holla_back_config");
    static Symbol active("active");
    static Symbol gameplay_mode("gameplay_mode");
    static Symbol perform("perform");
    static Symbol in_campaign_era_intro("in_campaign_era_intro");
    static Symbol holla_back("holla_back");
    static Symbol mind_control("mind_control");
    mVenueEntered = false;
    int numEntries = mEntries.size();
    if (numEntries == 0)
        return true;
    bool isLoaded = TheGame->IsLoaded();
    if (!b1 && !isLoaded) {
        return numEntries <= mCurrentIndex;
    }
    if (!b1) {
        float ui = TheTaskMgr.UISeconds();
        float old = mPrevSongPosition;
        mPrevSongPosition = ui;
        if (mPrevSongPosition - old < 0.5f) {
            return numEntries <= mCurrentIndex;
        }
    }
    if (!b2 && mCurrentIndex >= 0) {
        if (TheHamProvider->Property(in_campaign_era_intro)->Int()) {
            static Symbol num_stars("num_stars");
            const DataNode *prop = TheGamePanel->Property(num_stars, false);
            int stars;
            if (prop) {
                stars = prop->Float();
            } else {
                stars = 0;
            }
            mEntries[mCurrentIndex].mStarCount = stars;
            PropertyEventProvider *p0 = TheGameData->Player(0)->Provider();
            PropertyEventProvider *p1 = TheGameData->Player(1)->Provider();
            mEntries[mCurrentIndex].mTotalScore =
                p0->Property("score")->Int() + p1->Property("score")->Int();
            CampaignPerformer *campaignPerf =
                static_cast<CampaignPerformer *>(MetaPerformer::Current());
            campaignPerf->UpdateEraSong(
                campaignPerf->GetDifficulty(),
                campaignPerf->Era(),
                mEntries[0].mSongLongName,
                mEntries[0].mStarCount
            );
            campaignPerf->TriggerSongCompletion(mEntries[0].mTotalScore, mEntries[0].mStarCount);
        }
    }
    if (!b2 && mEntries[mCurrentIndex].mGameplayMode == mind_control) {
        CampaignPerformer *campaignPerf =
            static_cast<CampaignPerformer *>(MetaPerformer::Current());
        campaignPerf->SetCampaignMindControlComplete(true);
    }
    if (++mCurrentIndex < (int)mEntries.size() && !b2) {
        Entry &nextEntry = mEntries[mCurrentIndex];
        bool loadCrew = false;
        if (*nextEntry.mCrew1Symbol.Str() != '\0') {
            loadCrew = true;
            HamPlayerData *hpd = TheGameData->Player(0);
            hpd->SetOutfit("");
            hpd->SetCrew(nextEntry.mCrew1Symbol);
            if (nextEntry.mGameplayMode == Symbol("mind_control")) {
                hpd->SetOutfit("lima06");
            }
        }
        if (*nextEntry.mCrew2Symbol.Str() != '\0') {
            loadCrew = true;
            HamPlayerData *hpd = TheGameData->Player(1);
            hpd->SetOutfit("");
            hpd->SetCrew(nextEntry.mCrew2Symbol);
            if (nextEntry.mGameplayMode == Symbol("mind_control")) {
                hpd->SetOutfit("rasa06");
            }
        }
        if (loadCrew && isLoaded) {
            TheHamDirector->LoadCrew(
                TheGameData->Player(0)->Crew(), TheGameData->Player(1)->Crew()
            );
        }
        static Symbol hud_panel("hud_panel");
        static Symbol clear_flash_cards("clear_flash_cards");
        static Symbol clear_all_flashcard_campaign_status(
            "clear_all_flashcard_campaign_status"
        );
        TheMidiParserMgr->GetParser(midi_player)->SetProperty(active, 0);
        TheHamProvider->SetProperty(holla_back_config, nextEntry.mModeConfig);
        if (isLoaded) {
            ObjectDir *hudPanel = DataVariable(hud_panel).Obj<ObjectDir>();
            if (hudPanel) {
                hudPanel->Handle(Message(clear_flash_cards, 0), true);
                hudPanel->Handle(Message(clear_flash_cards, 1), true);
                hudPanel->Handle(Message(clear_all_flashcard_campaign_status), true);
            }
        }
        if (nextEntry.mGameplayMode == "holla_back") {
            TheHamDirector->StartStopVisualizer(true, 0);
        }
        TheGameMode->SetGameplayMode(nextEntry.mGameplayMode, nextEntry.mGameplayMode == perform);
        TheGame->LoadNewSong(nextEntry.mSongLongName, nextEntry.mSongShortName);
        mCurrentPlaybackPosition = TheTaskMgr.UISeconds();
        static Symbol deinit("deinit");
        UIPanel *gamePanel = ObjectDir::Main()->Find<UIPanel>("game_panel");
        gamePanel->Handle(Message(deinit), true);
        if (nextEntry.mGameplayMode == "holla_back") {
            TheHamProvider->SetProperty("hide_venue", true);
        }
        return false;
    } else {
        MILO_LOG("SongSequence::DoNext: terminating. forced=%s\n", b2 ? "T" : "F");
        static Symbol holla_back("holla_back");
        Symbol mode = TheGameMode->Property(gameplay_mode)->Sym();
        if (mode == holla_back) {
            RndGroup *grp = TheHamDirector->GetVenueWorld()->Find<RndGroup>("bid.grp");
            if (grp) {
                grp->SetShowing(false);
            }
            RndPropAnim *anim = TheHamDirector->GetVenueWorld()->Find<RndPropAnim>(
                "set_performance.anim", true
            );
            if (anim) {
                anim->Animate(0, false, 0);
            }
        }
        TheGameMode->SetGameplayMode(perform, true);
        Clear();
        mCurrentIndex = -1;
        return true;
    }
}

void SongSequence::OnSongLoaded() {
    if (!Done()) {
        static Symbol reset("reset");
        static Symbol init("init");
        static Symbol set_type("set_type");
        static Symbol start_song_now("start_song_now");
        static Symbol show_hud("show_hud");
        static Symbol hide_hud("hide_hud");
        static Symbol start_score_move_index("start_score_move_index");
        static Symbol gameplay_mode("gameplay_mode");
        static Symbol freestyle_enabled("freestyle_enabled");
        static Symbol holla_back("holla_back");
        static Symbol mind_control("mind_control");
        MILO_LOG("Time to advance song sequence = %.3f\n", TheTaskMgr.UISeconds() - mCurrentPlaybackPosition);
        const Entry &curEntry = mEntries[mCurrentIndex];
        UIPanel *gamePanel = ObjectDir::Main()->Find<UIPanel>("game_panel");
        int hasIntro = (unsigned char)(TheGame->HasIntro());
        Symbol gameMode = TheGameMode->Property(gameplay_mode)->Sym();
        bool inHollaback = gameMode == holla_back;
        bool inMindControl = gameMode == mind_control;
        TheHamDirector->SetProperty("disabled", false);
        gamePanel->Handle(Message(set_type, curEntry.mGameplayMode), true);
        gamePanel->Handle(Message(init), true);
        for (int i = 0; 2 > i; i++) {
            HamPlayerData *hpd = TheGameData->Player(i);
            if (!(!(hpd->IsPlaying()))) {
                hpd->Provider()->SetProperty(start_score_move_index, -1);
            } else {
                hpd->Provider()->SetProperty(start_score_move_index, 1000);
            }
        }
        if (mCurrentIndex != 0 || !inHollaback) {
            auto resetMsg = Message(reset);
            gamePanel->Handle(resetMsg, true);
        }
        if (!inMindControl) {
            TheHamProvider->SetProperty("game_stage", Symbol("intro"));
        }
        if (!hasIntro) {
            gamePanel->Handle(Message(start_song_now), true);
            TheMaster->GetAudio()->ClearLoop();
        }
        TheHamDirector->SetProperty(freestyle_enabled, false);
        if (curEntry.mIntroLoopMeasure >= 0 && curEntry.mOutroLoopMeasure >= 0) {
            float introBeat = curEntry.mIntroLoopMeasure * 4.0f;
            float outroBeat = curEntry.mOutroLoopMeasure * 4.0f;
            float introMs = BeatToMs(introBeat);
            float outroMs = BeatToMs(outroBeat);
            TheMaster->GetAudio()->SetLoop(introMs, outroMs);
        }
        if (0 <= curEntry.mEventStartMeasure && curEntry.mEventEndMeasure >= 1) {
            TheMaster->GetAudio()->SetLoop(curEntry.mEventStartMeasure * 4.0f, curEntry.mEventEndMeasure * 4.0f);
            TheGame->Jump(BeatToMs(curEntry.mEventStartMeasure * 4.0f), true);
        }
        if (curEntry.mIsIntro) {
            ObjectDir *hudPanel = DataVariable("hud_panel").Obj<ObjectDir>();
            hudPanel->Find<UILabel>("song_name.lbl")->SetPrelocalizedString(String(""));
            hudPanel->Find<UILabel>("song_artist.lbl")->SetPrelocalizedString(String(""));
        }
        if (mFileCache) {
            mFileCache->Clear();
            if (mCurrentIndex < mEntries.size() - 1) {
                char buffer[256];
                mFileCache->StartSet(0);
                Symbol s0 = mEntries[mCurrentIndex + 1].mSongShortName;
                int s0len = strlen(s0.Str());
                strcpy(buffer, TheHamSongMgr.SongPath(s0, 0));
                buffer[strlen(buffer) - s0len] = 0;
                const char *milo = MakeString("%s%s.milo", buffer, s0.Str());
                const char *moves = MakeString("%s%s.milo", buffer, "moves");
                const char *clips = MakeString("%s%s.milo", buffer, "clips");
                const char *mogg = MakeString("%s%s.mogg", buffer, s0.Str());
                mFileCache->Add(milo, 1, milo);
                mFileCache->Add(moves, 1, moves);
                mFileCache->Add(clips, 1, clips);
                mFileCache->Add(mogg, 1, mogg);
                mFileCache->EndSet();
            }
        }
        mNextSongLoadPosition = 0;
        if (!curEntry.mIsOutro) {
            TheHamDirector->StartStopVisualizer(false, 1);
            RndGroup *grp = TheHamDirector->GetVenueWorld()->Find<RndGroup>("bid.grp");
            if (grp) {
                grp->SetShowing(false);
            }
        } else {
            TheHamDirector->StartStopVisualizer(false, 2);
        }
        if (inHollaback) {
            RndGroup *grp = TheHamDirector->GetVenueWorld()->Find<RndGroup>("bid.grp");
            if (grp) {
                grp->SetShowing(true);
            }
            RndPropAnim *anim =
                TheHamDirector->GetVenueWorld()->Find<RndPropAnim>("set_bid.anim");
            if (anim) {
                anim->Animate(0, false, 0, nullptr, kEaseLinear, 0, false);
            }
        }
        if (hasIntro) {
            TheGame->Handle(Message("set_realtime", hasIntro), true);
        }
        if (!inHollaback) {
            if (inMindControl)
                goto next;
            TheHamDirector->PlayIntroShot();
            static Message songseq_intro("songseq_intro");
            TheHamProvider->Export(songseq_intro, true);
        }
        if (!inMindControl)
            return;
    next:
        TheGame->Handle(Message("set_realtime", false), true);
        TheHamProvider->SetProperty("game_stage", Symbol("playing"));
        mVenueEntered = true;
        TheGame->Jump(0, true);
        TheHamDirector->Handle(Message("set_suppress_next_shot", 0x78), true);
    }
}
