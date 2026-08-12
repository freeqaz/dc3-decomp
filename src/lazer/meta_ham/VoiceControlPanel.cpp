#include "meta_ham\VoiceControlPanel.h"
#include "HamPanel.h"
#include "HamSongMetadata.h"
#include "MultiUserGesturePanel.h"
#include "ProfileMgr.h"
#include "flow\Flow.h"
#include "flow\FlowNode.h"
#include "flow\PropertyEventProvider.h"
#include "game\GameMode.h"
#include "gesture\SpeechMgr.h"
#include "hamobj\Difficulty.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamLabel.h"
#include "hamobj\HamPlayerData.h"
#include "math/Rand.h"
#include "math\Utl.h"
#include "meta\SongPreview.h"
#include "meta_ham\HamSongMetadata.h"
#include "meta_ham\HamSongMgr.h"
#include "meta_ham\HamUI.h"
#include "meta_ham\MetaPerformer.h"
#include "meta_ham\MultiUserGesturePanel.h"
#include "meta_ham\OverlayPanel.h"
#include "meta_ham\SongRecord.h"
#include "meta_ham\SongSortMgr.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\ContentMgr.h"
#include "os\Debug.h"
#include "os\PlatformMgr.h"
#include "os\System.h"
#include "rndobj\Anim.h"
#include "rndobj\Draw.h"
#include "stl\_vector.h"
#include "synth\MetaMusic.h"
#include "ui\UI.h"
#include "ui\UIColor.h"
#include "ui\UIScreen.h"
#include "utl\Locale.h"
#include "utl\Std.h"
#include "utl\Symbol.h"

VoiceControlPanel::VoiceControlPanel()
    : unk44(0), mActive(false), unk4c(3.4028235e+38), unk50(0), mDifficultySelected(false), mModeSelected(false),
      mMusicWasStarted(false), mEnteredGame(false), unk68(0), unk6c(0) {}

VoiceControlPanel::~VoiceControlPanel() {}

BEGIN_HANDLERS(VoiceControlPanel)
    HANDLE_MESSAGE(SpeechRecoMessage)
    HANDLE_MESSAGE(UITransitionCompleteMsg)
    HANDLE_SUPERCLASS(OverlayPanel)
END_HANDLERS

void VoiceControlPanel::Poll() {
    HamPanel::Poll();
    static float sFloats[] = { 0.25f, 2.0f, 4.5f };
    if (mActive && unk44 < sFloats[0]) {
        unk44 = Min(sFloats[0], unk44 + TheTaskMgr.DeltaUISeconds());
    }
    if (!mActive && unk44 > 0) {
        unk44 = Max(0.0f, unk44 - TheTaskMgr.DeltaUISeconds());
    }
    if (mSong == gNullStr && mActive) {
        float set = unk4c + TheTaskMgr.DeltaUISeconds();
        float cmp = sFloats[1];
        unk4c = set;
        if (unk4c > cmp) {
            unk4c = 0;
            Symbol song = TheHamSongMgr.GetRandomSong();
            DisplaySong(song);
        }
    }
    if (mActive) {
        float set = unk50 + TheTaskMgr.DeltaUISeconds();
        float cmp = sFloats[2];
        unk50 = set;
        if (unk50 > cmp) {
            CycleTip();
        }
    }
}

void VoiceControlPanel::Dismiss() {
    mActive = false;
    Symbol lang = HongKongExceptionMet() ? "eng" : SystemLanguage();
    if (lang != "eng" && lang != "jpn") {
        TheSpeechMgr->DisableAndUnloadGrammars();
        DataArray *arr = SystemConfig("kinect")->FindArray("speech");
        TheSpeechMgr->EnableAndLoadGrammars(arr);
    }
    SetRules(true);
    if (mMusicWasStarted) {
        TheMetaMusic->Start();
    }
    SongPreview *songPrev = ObjectDir::Main()->Find<SongPreview>("song_preview");
    songPrev->SetMusicVol(songPrev->PreviewDb());
    MILO_ASSERT(TheHamUI.GetOverlayPanel() == this, 0x98);
    TheHamUI.SetOverlayPanel(nullptr);
    OverlayPanel::Dismiss();
}

void VoiceControlPanel::ContentMounted(const char *, const char *) {
    static Message refreshAlbumArtMsg("refresh_album_art");
    Handle(refreshAlbumArtMsg, true);
}

bool VoiceControlPanel::DifficultyLocked() const {
    if (mSong == gNullStr)
        return false;
    else {
        static Symbol practice("practice");
        if (mGameMode == practice)
            return false;
        else
            return !TheProfileMgr.IsDifficultyUnlocked(
                mSong, DifficultyToSym(mDifficulty)
            );
    }
}

bool VoiceControlPanel::ReadyToStart() const {
    return mSong != gNullStr && mDifficulty != kNumDifficulties && mGameMode != gNullStr
        && !DifficultyLocked();
}

void VoiceControlPanel::WakeUpScreenSaver() const {
    bool b = ThePlatformMgr.ScreenSaver();
    ThePlatformMgr.SetScreenSaver(false);
    ThePlatformMgr.SetScreenSaver(b);
}

void VoiceControlPanel::EnterGame() {
    MILO_FAIL("VoiceControlPanel::EnterGame should never be called! (I hope)\n");
    if (ReadyToStart()) {
        TheGameMode->SetMode(mGameMode, "none");
        MetaPerformer::Current()->SetSong(mSong);
        TheGameData->Player(0)->SetDifficulty(mDifficulty);
        TheGameData->Player(1)->SetDifficulty(mDifficulty);
        static Symbol dance_battle("dance_battle");
        if (mGameMode == dance_battle) {
            MetaPerformer::Current()->ClearCharacters();
            MultiUserGesturePanel *pMultiUserGesturePanel =
                ObjectDir::Main()->Find<MultiUserGesturePanel>("multiuser_panel");
            pMultiUserGesturePanel->UpdateProviderPlayerIndices();
            pMultiUserGesturePanel->UpdateProviders();
            pMultiUserGesturePanel->SetRandomCrew(0);
            pMultiUserGesturePanel->UpdateProviders();
            pMultiUserGesturePanel->SetRandomCrew(1);
        }
        static Symbol practice("practice");
        if (mGameMode == practice) {
            MetaPerformer::Current()->SetSkipPracticeWelcome(true);
        }
        static Symbol byo_bid("byo_bid");
        TheHamProvider->SetProperty(byo_bid, false);
        Flow *pFlow = DataDir()->Find<Flow>("sound_dance.flow");
        pFlow->Activate();
        mEnteredGame = true;
        SetRules(false);
        static Symbol enter_gameplay("enter_gameplay");
        static DataArrayPtr enterGameplayFunc(enter_gameplay);
        enterGameplayFunc->Execute();
        static Message microphoneActivityMsg("microphoneActivityMsg");
        TheHamProvider->Handle(microphoneActivityMsg, false);
    } else {
        Flow *pFlow = DataDir()->Find<Flow>("sound_error.flow");
        pFlow->Activate();
    }
}

void VoiceControlPanel::SetRules(bool b1) {
    static bool s8a0 = false;
    static bool see0 = true;
    if (b1 != s8a0 || see0 != mActive) {
        see0 = mActive;
        bool oldRecognizing = TheSpeechMgr->Recognizing();
        s8a0 = b1;
        TheSpeechMgr->SetRecognizing(false);
        TheSpeechMgr->SetRule("voice_shell", "voice_control", !mActive && b1);
        if (TheSpeechMgr->HasGrammar("play_song_grammar")) {
            TheSpeechMgr->SetRule("play_song_grammar", "play_song", mActive && b1);
        }
        TheSpeechMgr->SetRule("voice_shell", "random_song", mActive && b1);
        TheSpeechMgr->SetRule("voice_shell", "difficulty", mActive && b1);
        TheSpeechMgr->SetRule("voice_shell", "mode", mActive && b1);
        TheSpeechMgr->SetRule("voice_shell", "dance", mActive && b1);
        TheSpeechMgr->SetRule("voice_shell", "back", mActive && b1);
        TheSpeechMgr->SetRecognizing(oldRecognizing);
    }
}

void VoiceControlPanel::DisplaySong(Symbol song) {
    const char *content = TheHamSongMgr.ContentName(song);
    if (content) {
        TheContentMgr.MountContent(content);
    }
    static Message setSongMsg("set_song", 0);
    setSongMsg[0] = song;
    Handle(setSongMsg, true);
}

void VoiceControlPanel::CreatePlaySongGrammar() const {
    void *ruleState;
    void *wordState;

    TheSpeechMgr->SetRecognizing(false);

    if (TheSpeechMgr->HasGrammar("play_song_grammar")) {
        TheSpeechMgr->UnloadGrammar("play_song_grammar");
    }

    TheSpeechMgr->CreateGrammar("play_song_grammar");

    TheSpeechMgr->AddDynamicRule("play_song_grammar", "play_song", &ruleState);

    static Symbol voice_command_song("voice_command_song");
    TheSpeechMgr->AddDynamicRuleWord(
        "play_song_grammar",
        Localize(voice_command_song, nullptr, TheLocale),
        gNullStr,
        &ruleState,
        &wordState
    );

    if (TheUI->FocusPanel() != ObjectDir::Main()->Find<UIPanel>("song_select_panel")) {
        TheSongSortMgr->OnEnter();
    }

    auto songRecordMapEnd = TheSongSortMgr->mSongRecordMap.end();
    for (std::map<Symbol, SongRecord>::iterator it = TheSongSortMgr->mSongRecordMap.begin();
         it != songRecordMapEnd; ++it) {
        SongRecord &record = it->second;
        std::vector<String> prons = record.Metadata()->Pronunciations();
        const std::vector<PronunciationsLoc> &locs = record.Metadata()->PronunciationsLocalized();

        unsigned int locIdx = 0;
        if (locs.size() > 0) {
            do {
                if (locs[locIdx].mLanguage == GetSongTitlePronunciationLanguage()) {
                    prons = locs[locIdx].mPronunciations;
                }
                ++locIdx;
            } while (locIdx < locs.size());
        }

        for (size_t i = 0; i < prons.size(); i++) {
            String pron = prons[i];
            TheSpeechMgr->AddDynamicRuleWord(
                "play_song_grammar", pron.c_str(), record.ShortName().Str(), &wordState, nullptr
            );
        }
    }

    TheSpeechMgr->CommitGrammar("play_song_grammar");
    TheSpeechMgr->SetRecognizing(true);
}

void VoiceControlPanel::CycleTip() {
    unk50 = 0;
    HamLabel *lbl = DataDir()->Find<HamLabel>("instructions.lbl");
    lbl->LStyle(0).mColorOverride = DataDir()->Find<UIColor>("instructions.color");
    std::vector<Symbol> syms;
    if (DifficultyLocked()) {
        lbl->LStyle(0).mColorOverride = DataDir()->Find<UIColor>("red.color");
        syms.push_back("voicecontrol_difficulty_locked");
    } else if (!ReadyToStart()) {
        syms.push_back("voicecontrol_song_instruction");

    } else if (ReadyToStart()) {
        syms.push_back("voicecontrol_dance_instruction");
        if (!mModeSelected) {
            syms.push_back("voicecontrol_mode_instruction");
        }
        if (!mDifficultySelected) {
            syms.push_back("voicecontrol_difficulty_instruction");
        }
        if (!mModeSelected || !mDifficultySelected) {
            syms.push_back("voicecontrol_random_song_instruction");
        }
    }
    for (int i = 0; i < 5; i++) {
        int randIdx = RandomInt(0, syms.size());
        if (lbl->TextToken() != syms[randIdx]) {
            lbl->SetTextToken(syms[randIdx]);
            break;
        }
    }
}

void VoiceControlPanel::PopUp() {
    if (!TheHamUI.GetOverlayPanel()) {
        mActive = true;
        TheHamUI.SetOverlayPanel(this);
        Symbol lang = HongKongExceptionMet() ? "eng" : SystemLanguage();
        if (lang != "eng" && lang != "jpn") {
            TheSpeechMgr->DisableAndUnloadGrammars();
            TheSpeechMgr->Enable(true);
            SystemConfig("kinect")->FindArray("speech")->FindArray("grammars");
            String fullDir = String("grammar/") + TheSpeechMgr->GetSpeechLanguageDir()
                + "/" + "voice_shell_en-US.cfgp";
            TheSpeechMgr->LoadGrammar("voice_shell", fullDir.c_str(), true);
        }
        CreatePlaySongGrammar();
        SetRules(true);
        mMusicWasStarted = TheMetaMusic->IsStarted();
        TheMetaMusic->Stop();
        SongPreview *preview = ObjectDir::Main()->Find<SongPreview>("song_preview");
        preview->SetMusicVol(SongPreview::kSilenceVal);
        mSong = gNullStr;
        mDifficulty = DefaultDifficulty();
        mGameMode = "perform";
        mDifficultySelected = false;
        mModeSelected = false;
        DataDir()->Find<RndDrawable>("song_dimmer.mesh")->SetShowing(true);
        DataDir()->Find<RndAnimatable>("selected_diff.anim")->SetFrame(0, 1);
        DataDir()->Find<RndAnimatable>("selected_mode.anim")->SetFrame(0, 1);
        CycleTip();
        WakeUpScreenSaver();
        mEnteredGame = false;
    }
}

DataNode VoiceControlPanel::OnMsg(const UITransitionCompleteMsg &msg) {
    if (mEnteredGame) {
        Dismiss();
        SetRules(false);
    }
    return DATA_UNHANDLED;
}

DataNode VoiceControlPanel::OnMsg(const SpeechRecoMessage &msg) {
    if (!TheProfileMgr.GetDisableVoiceCommander()) {
        float thresh = TheSpeechMgr->SpeechConfThresh();
        if (msg.Confidence() >= thresh) {
            if (mGameMode == "practice") {
                unk6c++;
            } else {
                unk68++;
            }
            if (mActive) {
                WakeUpScreenSaver();
            }
            DataArray *tags = msg.Tags();
            Symbol rulename = msg.RuleName();
            if (rulename == "voice_control" && !mActive) {
                Symbol arrSym = tags->Sym(0);
                if (arrSym == "voice_control" && !TheHamUI.InTransition()
                    && !TheContentMgr.RefreshInProgress()) {
                    PopUp();
                } else if (arrSym == "xbox") {
                    static Message microphoneActivityMsg("microphone_activity");
                    TheHamProvider->Handle(microphoneActivityMsg, false);
                    static Message voice_commander_help_bump("voice_commander_help_bump");
                    TheHamProvider->Handle(voice_commander_help_bump, false);
                    static Message voice_commander_help("voice_commander_help");
                    TheHamProvider->Handle(voice_commander_help, false);
                }
                return 0;
            } else if (rulename == "back" && mActive) {
                DataDir()->Find<Flow>("sound_back.flow")->Activate();
                Dismiss();
                return 0;
            } else if (rulename == "dance" && mActive) {
                EnterGame();
                return 0;
            } else if (rulename == "play_song" && mActive) {
                Symbol arrSym = tags->Sym(0);
                static Symbol random_song("random_song");
                if (arrSym == random_song) {
                    arrSym = TheHamSongMgr.GetRandomSong();
                }
                int songID = TheHamSongMgr.GetSongIDFromShortName(arrSym, false);
                if (songID != 0) {
                    mSong = tags->Sym(0);
                    TheHamSongMgr.Data(songID);
                    DisplaySong(mSong);
                    DataDir()->Find<RndDrawable>("song_dimmer.mesh")->SetShowing(false);
                    DataDir()->Find<Flow>("sound_selection.flow")->Activate();
                    DataDir()->Find<Flow>("song_selected.flow")->Activate();
                    static Message microphoneActivityMsg("microphone_activity");
                    TheHamProvider->Handle(microphoneActivityMsg, false);
                }
                CycleTip();
                return 0;
            } else if (rulename == "random_song" && mActive) {
                Symbol song = TheHamSongMgr.GetRandomSong();
                int songID = TheHamSongMgr.GetSongIDFromShortName(song, false);
                if (songID != 0) {
                    mSong = song;
                    TheHamSongMgr.Data(songID);
                    DisplaySong(mSong);
                    DataDir()->Find<RndDrawable>("song_dimmer.mesh")->SetShowing(false);
                    DataDir()->Find<Flow>("sound_selection.flow")->Activate();
                    DataDir()->Find<Flow>("song_selected.flow")->Activate();
                    static Message microphoneActivityMsg("microphone_activity");
                    TheHamProvider->Handle(microphoneActivityMsg, false);
                }
                CycleTip();
                return 0;
            } else if (rulename == "mode" && mActive && mSong != gNullStr) {
                Symbol arrSym = tags->Sym(0);
                float frame = 0;
                if (mGameMode == "practice") {
                    frame = 1;
                }
                if (mGameMode == "dance_battle") {
                    frame = 2;
                }
                DataDir()->Find<RndAnimatable>("selected_mode.anim")->SetFrame(frame, 1);
                mModeSelected = true;
                DataDir()->Find<Flow>("sound_selection.flow")->Activate();
                static Message microphoneActivityMsg("microphone_activity");
                TheHamProvider->Handle(microphoneActivityMsg, false);
                CycleTip();
                return 0;
            } else if (rulename == "difficulty" && mActive && mSong != gNullStr) {
                Symbol arrSym = tags->Sym(0);
                if (arrSym == "beginner") {
                    mDifficulty = kDifficultyBeginner;
                } else if (arrSym == "easy") {
                    mDifficulty = kDifficultyEasy;
                } else if (arrSym == "medium") {
                    mDifficulty = kDifficultyMedium;
                } else if (arrSym == "hard") {
                    mDifficulty = kDifficultyExpert;
                } else {
                    MILO_ASSERT(false, 0x14D);
                }
                DataDir()
                    ->Find<RndAnimatable>("selected_diff.anim")
                    ->SetFrame(mDifficulty, 1);
                mDifficultySelected = true;
                DataDir()->Find<Flow>("sound_selection.flow")->Activate();
                static Message microphoneActivityMsg("microphone_activity");
                TheHamProvider->Handle(microphoneActivityMsg, false);
                CycleTip();
                return 0;
            } else {
                return DATA_UNHANDLED;
            }
        }
    }
    return DATA_UNHANDLED;
}
