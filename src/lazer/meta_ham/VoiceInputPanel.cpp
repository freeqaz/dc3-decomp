#include "meta_ham/VoiceInputPanel.h"
#include "gesture/GestureMgr.h"
#include "gesture/SpeechMgr.h"
#include "meta_ham/HamPanel.h"
#include "meta_ham/LetterboxPanel.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/HamUI.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/SongSortMgr.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Overlay.h"
#include "ui/UIPanel.h"
#include "utl/Locale.h"
#include "utl/Symbol.h"
#include <cstring>

VoiceInputPanel *TheVoiceInputPanel;

void VoiceInputPanel::VoiceContext::SetActiveConfig(bool blacklight) {
    if (blacklight) {
        mActiveConfig = mBlacklightOnConfig;
    } else {
        mActiveConfig = mBlacklightOffConfig;
    }
    mConfThreshold = mActiveConfig->mDefaultConfThreshold;
    DataArray *thresholds = mActiveConfig->mAltConfThresholds->FindArray(
        TheSpeechMgr->GetSpeechLanguageDir().c_str(), false
    );
    if (thresholds) {
        mConfThreshold = thresholds->Float(1);
    }
}

VoiceInputPanel::VoiceInputPanel() {
    unk3c = false; // i really gotta declare these here huh
    mActiveVoiceContext = nullptr;
    mDisabledVoiceContext = nullptr;
#ifdef HX_NATIVE
    // Skip LoadVoiceContexts on native — no Kinect/speech config available
#else
    LoadVoiceContexts();
#endif
    TheVoiceInputPanel = this;
}

VoiceInputPanel::~VoiceInputPanel() {
    TheVoiceInputPanel = nullptr;
    int numContexts = mVoiceContexts.size();
    for (int i = 0; i < numContexts; i++) {
        delete mVoiceContexts[i];
    }
}

BEGIN_HANDLERS(VoiceInputPanel)
    HANDLE_ACTION(
        create_song_select_grammar,
        CreateSongSelectGrammar(GetSongTitlePronunciationLanguage())
    )
    HANDLE_ACTION(create_playlist_editor_grammar, CreatePlaylistEditorGrammar())
    HANDLE_ACTION(toggle_blacklight, ToggleBlacklight(_msg->Int(2)))
    HANDLE_ACTION(activate_voice_context, ActivateVoiceContext(_msg->Sym(2)))
    HANDLE_ACTION(disable_currrent_voice_context, DisableCurrentVoiceContext())
    HANDLE_ACTION(restore_currrent_voice_context, RestoreCurrentVoiceContext())
    HANDLE_EXPR(active_voice_context_sym, ActiveVoiceContextSym())
    HANDLE_EXPR(
        get_active_voice_context_name,
        mActiveVoiceContext ? mActiveVoiceContext->mName : gNullStr
    )
    HANDLE_ACTION(confidence_up, OnConfidenceChange(0.05f))
    HANDLE_ACTION(confidence_down, OnConfidenceChange(-0.05f))
    HANDLE_MESSAGE(SpeechRecoMessage)
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS

Symbol VoiceInputPanel::ActiveVoiceContextSym() {
    if (mActiveVoiceContext == nullptr) {
        return gNullStr;
    } else
        return mActiveVoiceContext->mName;
}

void VoiceInputPanel::DisableCurrentVoiceContext() {
    MILO_ASSERT(mDisabledVoiceContext==NULL, 0x1f4);
    if (mActiveVoiceContext) {
        mDisabledVoiceContext = mActiveVoiceContext;
        ActivateVoiceContext(gNullStr);
    }
}

void VoiceInputPanel::RestoreCurrentVoiceContext() {
    if (mDisabledVoiceContext) {
        ActivateVoiceContext(mDisabledVoiceContext->mName);
        mDisabledVoiceContext = nullptr;
    }
}

void VoiceInputPanel::ToggleBlacklight(bool b1) {
    unk3c = b1;
    if (mActiveVoiceContext) {
        ActivateVoiceContext(mActiveVoiceContext->mName);
    }
}

void VoiceInputPanel::LoadVoiceContexts() {
    DataArray *cfg = SystemConfig("kinect", "speech", "voice_contexts");
    int numContexts = cfg->Size();
    for (int i = 1; i < numContexts; i++) {
        VoiceContext *ctx = new VoiceContext();
        DataArray *arr = cfg->Array(i);
        ctx->mName = arr->Sym(0);
        Config *g1 = new Config();
        DataArray *blacklightArr = arr->FindArray("blacklight_off");
        DataArray *grammars = blacklightArr->FindArray("grammars");
        int numGrammars = grammars->Size();
        for (int j = 1; j < numGrammars; j++) {
            g1->mGrammars.push_back(grammars->Sym(j));
        }
        g1->mDefaultConfThreshold =
            blacklightArr->FindFloat("default_confidence_threshold");
        g1->mAltConfThresholds =
            blacklightArr->FindArray("alternate_confidence_thresholds");
        ctx->mBlacklightOffConfig = g1;

        Config *g2 = new Config();
        DataArray *blacklightOnArr = arr->FindArray("blacklight_on");
        DataArray *grammarsOn = blacklightOnArr->FindArray("grammars");
        int numOnGrammars = grammarsOn->Size();
        for (int j = 1; j < numOnGrammars; j++) {
            g2->mGrammars.push_back(grammarsOn->Sym(j));
        }
        g2->mDefaultConfThreshold =
            blacklightOnArr->FindFloat("default_confidence_threshold");
        g2->mAltConfThresholds =
            blacklightOnArr->FindArray("alternate_confidence_thresholds");
        ctx->mBlacklightOnConfig = g2;
        mVoiceContexts.push_back(ctx);
    }
}

void VoiceInputPanel::OnConfidenceChange(float conf) {
    if (mActiveVoiceContext) {
        float newConf = conf + mActiveVoiceContext->mConfThreshold;
        mActiveVoiceContext->mConfThreshold = Clamp(0.0f, 1.0f, newConf);
        if (TheSpeechMgr->Overlay()->Showing()) {
            TheSpeechMgr->Overlay()->Print(MakeString(
                "voice context %s, confidence: %f\n",
                mActiveVoiceContext->mName.Str(),
                mActiveVoiceContext->mConfThreshold
            ));
        }
    }
}

void VoiceInputPanel::CreateSongSelectGrammar(Symbol language) const {
    if (!TheSpeechMgr->Enabled()) {
        MILO_NOTIFY(
            "----- VoiceInputPanel::CreateSongSelectGrammar() - speechMgr not enabled\n"
        );
    } else {
        bool recognizing = TheSpeechMgr->IsRecognizing();
        TheSpeechMgr->SetRecognizing(false);
        if (TheSpeechMgr->HasGrammar("song_select_grammar")) {
            TheSpeechMgr->UnloadGrammar("song_select_grammar");
        }
        TheSpeechMgr->CreateGrammar("song_select_grammar");
        void *va0;
        void *v98;
        void *v94;
        TheSpeechMgr->AddDynamicRule("song_select_grammar", "select_song", &v98);
        if (!TheHamUI.IsBlacklightMode()) {
            static Symbol voice_command_xbox("voice_command_xbox");
            TheSpeechMgr->AddDynamicRuleWord(
                "song_select_grammar",
                Localize(voice_command_xbox, nullptr, TheLocale),
                gNullStr,
                &v98,
                &va0
            );
            static Symbol voice_command_song("voice_command_song");
            TheSpeechMgr->AddDynamicRuleWord(
                "song_select_grammar",
                Localize(voice_command_song, nullptr, TheLocale),
                gNullStr,
                &va0,
                &v94
            );
        } else {
            static Symbol voice_command_song("voice_command_song");
            TheSpeechMgr->AddDynamicRuleWord(
                "song_select_grammar",
                Localize(voice_command_song, nullptr, TheLocale),
                gNullStr,
                &v98,
                &v94
            );
        }
        const std::set<int> &songs = TheHamSongMgr.GetAvailableSongSet();
        FOREACH (it, songs) {
            const HamSongMetadata *data = TheHamSongMgr.Data(*it);
            const std::vector<PronunciationsLoc> &locs = data->PronunciationsLocalized();
            const std::vector<String> *pronsPtr = &data->Pronunciations();
            FOREACH (loc, locs) {
                if (loc->mLanguage == language) {
                    pronsPtr = &loc->mPronunciations;
                    break;
                }
            }
            for (int i = 0; i < pronsPtr->size(); i++) {
                TheSpeechMgr->AddDynamicRuleWord(
                    "song_select_grammar",
                    (*pronsPtr)[i].c_str(),
                    data->ShortName().Str(),
                    &v94,
                    nullptr
                );
            }
        }
        TheSpeechMgr->CommitGrammar("song_select_grammar");
        TheSpeechMgr->SetRecognizing(recognizing);
    }
}

#ifdef HX_NATIVE
void VoiceInputPanel::ActivateVoiceContext(Symbol) {
    // Kinect voice not available on native
}

DataNode VoiceInputPanel::OnMsg(const SpeechRecoMessage &) {
    return DataNode(kDataUnhandled, true);
}

void VoiceInputPanel::CreatePlaylistEditorGrammar() const {
    // Kinect voice not available on native
}
#else

void VoiceInputPanel::ActivateVoiceContext(Symbol sym) {
    VoiceContext **it;
    if (!sym.Null()) {
        it = mVoiceContexts.begin();
        VoiceContext **end = mVoiceContexts.end();
        if (it != end) {
            do {
                if ((*it)->mName == sym)
                    break;
                it++;
            } while (it != end);
            if (it != end)
                goto found;
        }
        MILO_NOTIFY("Couldn't find voice context %s", sym);
        return;
    }
found:
    if (!TheSpeechMgr->Enabled()) {
        MILO_NOTIFY(
            "----- VoiceInputPanel::ActivateVoiceContext() - speechMgr not enabled"
        );
        return;
    }
    if (mActiveVoiceContext != NULL) {
        TheDebug << MakeString(
            "----- Deactivating voice context %s\n", mActiveVoiceContext->mName
        );
        if (TheSpeechMgr->Overlay()->Showing()) {
            TheSpeechMgr->Overlay()->Print(
                MakeString("Deactivating voice context %s\n", mActiveVoiceContext->mName)
            );
        }
        int numGrammars =
            mActiveVoiceContext->mActiveConfig->mGrammars.size();
        for (int i = 0; i < numGrammars; i++) {
            TheSpeechMgr->SetGrammarState(
                mActiveVoiceContext->GetGrammarSym(i), false
            );
        }
    }
    if (sym.Null()) {
        mActiveVoiceContext = NULL;
        return;
    }
    mActiveVoiceContext = *it;
    mActiveVoiceContext->SetActiveConfig(unk3c);
    int numGrammars2 = mActiveVoiceContext->mActiveConfig->mGrammars.size();
    for (int i = 0; i < numGrammars2; i++) {
        TheSpeechMgr->SetGrammarState(
            mActiveVoiceContext->GetGrammarSym(i), true
        );
    }
    TheDebug << MakeString("----- Activating voice context %s\n", sym.Str());
    if (TheSpeechMgr->Overlay()->Showing()) {
        TheSpeechMgr->Overlay()->Print(MakeString(
            "Activating voice context %s, confidence: %f\n",
            sym.Str(),
            mActiveVoiceContext->mConfThreshold
        ));
    }
}

void VoiceInputPanel::CreatePlaylistEditorGrammar() const {
    if (!TheSpeechMgr->Enabled()) {
        MILO_NOTIFY(
            "----- VoiceInputPanel::CreatePlaylistEditorGrammar() - speechMgr not enabled\n"
        );
    } else {
        if (TheSpeechMgr->HasGrammar("playlist_editor_grammar")) {
            TheSpeechMgr->UnloadGrammar("playlist_editor_grammar");
        }
        TheSpeechMgr->CreateGrammar("playlist_editor_grammar");
        void *state;
        TheSpeechMgr->AddDynamicRule(
            "playlist_editor_grammar", "select_playlist_song", &state
        );
        if (TheSongSortMgr->mSongRecordMap.size() == 0) {
            TheSongSortMgr->OnEnter();
        }
        for (std::map<Symbol, SongRecord>::iterator it =
                 TheSongSortMgr->mSongRecordMap.begin();
             it != TheSongSortMgr->mSongRecordMap.end();
             ++it) {
            const std::vector<String> &prons =
                it->second.Metadata()->Pronunciations();
            for (unsigned int i = 0; i < prons.size(); i++) {
                String pron(prons[i]);
                TheSpeechMgr->AddDynamicRuleWord(
                    "playlist_editor_grammar",
                    pron.c_str(),
                    it->second.ShortName().Str(),
                    &state,
                    NULL
                );
            }
        }
        TheSpeechMgr->CommitGrammar("playlist_editor_grammar");
    }
}

DataNode VoiceInputPanel::OnMsg(const SpeechRecoMessage &msg) {
    if (mActiveVoiceContext) {
        float conf = msg.Confidence();
        DataArray *tags = msg.Tags();
        Symbol rule = msg.RuleName();
        Symbol tag = tags->Sym(0);
        bool within = conf >= mActiveVoiceContext->mConfThreshold;
        if (within) {
            MetaPerformer::SendSpeechDatapoint(tags, conf, rule);
        }
        MILO_LOG(
            "----- Voice Msg - tag: %s; confidence: %f (%s); rule: %s\n",
            tag.Str(),
            conf,
            within ? "recognized" : "not recognized",
            rule.Str()
        );
        TheSpeechMgr->Overlay()->Print(within ? "recognized\n" : "not recognized\n");
        LetterboxPanel *letter = TheHamUI.GetLetterboxPanel();
        if (letter) {
            if (letter->InBlacklightTransition()) {
                return 0;
            } else {
                letter->VoiceInput(TheSpeechMgr->VoiceDirection(), within);
            }
        }
        if (within) {
            TheGestureMgr->SetInVoiceMode(true);
            if (letter) {
                letter->SetBlacklightMode(true);
            }
            if (rule == "select_artist") {
                if (TheHamSongMgr.GetSongIDFromShortName(tag)) {
                    static Symbol by_artist("by_artist");
                    TheSongSortMgr->SetSort(by_artist);
                    HandleType(Message("refresh_sorting", tag));
                }
            } else if (rule == "select_song") {
                Symbol song = tags->Sym(0);
                if (TheHamSongMgr.GetSongIDFromShortName(song, false)) {
                    HandleType(Message("on_say_song_name", song));
                }
            } else if (rule == "select_playlist_song") {
                Symbol song = tags->Sym(0);
                if (TheHamSongMgr.GetSongIDFromShortName(song, false)) {
                    HandleType(Message("select_playlist_song", song));
                }
            } else {
                bool b9 = false;
                bool b6 = false;
                if (strstr(tag.Str(), "hidden_global")) {
                    b9 = true;
                    static Symbol handle_global_commands("handle_global_commands");
                    const DataNode *prop =
                        TheHamUI.CurrentScreen()->Property(handle_global_commands, false);
                    if (!prop) {
                        UIPanel *panel = TheHamUI.CurrentScreen()->FocusPanel();
                        if (panel) {
                            prop = TheHamUI.CurrentScreen()->FocusPanel()->Property(
                                handle_global_commands, false
                            );
                        }
                    }
                    if (prop && prop->Type() == kDataInt) {
                        b6 = prop->Int();
                    }
                }
                if (b9) {
                    if (!b6) {
                        static Symbol on_global_voice_command("on_global_voice_command");
                        static DataArrayPtr onGlobalVoiceCommandFunc(new DataArray(2));
                        onGlobalVoiceCommandFunc->Node(0) = on_global_voice_command;
                        onGlobalVoiceCommandFunc->Node(1) = tag;
                        onGlobalVoiceCommandFunc->Execute();
                    } else {
                        HandleType(Message("on_global_voice_command", tag));
                    }
                } else {
                    HandleType(Message("on_voice_command", tag));
                }
            }
        } else {
            return DATA_UNHANDLED;
        }
        return 0;
    } else {
        return DATA_UNHANDLED;
    }
}
#endif

void LetterboxPanel::Exit() { UIPanel::Exit(); }
