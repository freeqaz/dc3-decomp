#include "game/Game.h"
#include "SongDB.h"
#include "char/FileMerger.h"
#include "flow/PropertyEventProvider.h"
#include "game/BustAMovePanel.h"
#include "game/GameMode.h"
#include "game/GamePanel.h"
#include "game/HamUser.h"
#include "game/LiveInput.h"
#include "game/Shuttle.h"
#include "game/SongDB.h"
#include "game/SongSequence.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "gesture/SkeletonClip.h"
#include "gesture/SkeletonUpdate.h"
#include "hamobj/CharFeedback.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMaster.h"
#include "hamobj/HamMove.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/HamSongData.h"
#include "hamobj/MoveDir.h"
#include "hamobj/MoveMgr.h"
#include "hamobj/ScoreUtl.h"
#include "hamobj/SuperEasyRemixer.h"
#include "macros.h"
#include "meta_ham/HamSongMetadata.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/Overshell.h"
#include "meta_ham/ProfileMgr.h"
#include "midi/MidiParserMgr.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "synth/Faders.h"
#include "synth/Sequence.h"
#include "synth/Synth.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "utl/MultiTempoTempoMap.h"
#include "utl/SongInfoCopy.h"
#include "utl/SongPos.h"
#include "utl/Symbol.h"
#include "utl/TempoMap.h"
#include "utl/TimeConversion.h"
#include "world/Dir.h"
#ifdef HX_NATIVE
#include "audio/AudioDevice.h"
#endif

Game *TheGame;
static bool sMoveOverlayToggle;
#if defined(HX_NATIVE) || defined(__EMSCRIPTEN__)
static bool sNativeAudioInitiated = false;
static int sNativeAudioPollCount = 0;
#endif
std::vector<Symbol> sAutoplayStates;

Game::Game()
    : mSongDB(new SongDB()), mSongInfo(0), mGameInput(0), mRestartCount(0), unk5c(false),
      mUseMoveGraph(false), mPaused(true), mTimePaused(false), mRealTime(false), unk64(0),
      unk68(false), mMusicSpeed(1), mNeverAllowInput(false), mPauseRequested(false), mOvershell(0), mMoveDir(this),
      mLoadState(0), mShuttle(new Shuttle()), mLoadedSongAudio(gNullStr), mWaitState(0), unka8(0), mAltTempoMap(0) {
    if (TheSongDB) {
        RELEASE(TheSongDB);
    }
    TheSongDB = mSongDB;
    TheGame = this;
    SetName("game", ObjectDir::Main());
    MidiParserMgr *lol = new MidiParserMgr(nullptr, "biteme");
    TheMaster = new HamMaster(mSongDB->SongData(), TheMidiParserMgr);
    mMaster = TheMaster;
    TheMaster->SetName("master", ObjectDir::Main());
    TheMaster->GetAudio()->SetName("audio", ObjectDir::Main());
    SetBackgroundVolume(TheProfileMgr.GetMusicVolumeDb());
    SetForegroundVolume(TheProfileMgr.GetMusicVolumeDb());
    mMaster->GetAudio()->SetStereo(!TheProfileMgr.Mono());
    LoadSong();
    mDeferredPausePending = false;
    mDeferredPauseSoundArg = false;
    mDeferredPauseGameArg = true;
    SkeletonUpdateHandle h = SkeletonUpdate::InstanceHandle();
    h.AddCallback(this);
}

Game::~Game() {
    SkeletonUpdateHandle h = SkeletonUpdate::InstanceHandle();
    h.RemoveCallback(this);
    SetHamMove(0, nullptr, false);
    TheSongSequence.Clear();
    RELEASE(mGameInput);
    RELEASE(mShuttle);
    TheGame = nullptr;
    TheSongDB = nullptr;
    TheMaster = nullptr;
    RELEASE(mMaster);
    RELEASE(mSongDB);
    RELEASE(mSongInfo);
    RELEASE(TheMidiParserMgr);
    RELEASE(mOvershell);
}

BEGIN_HANDLERS(Game)
    HANDLE_ACTION(start, Start())
    HANDLE_EXPR(get_song_ms, mMaster->GetAudio()->GetTime())
    HANDLE_ACTION(set_music_volume, SetMusicVolume(_msg->Float(2)))
    HANDLE_ACTION(
        set_paused,
        SetGamePaused(_msg->Int(2), true, _msg->Size() > 3 ? _msg->Int(3) : false)
    )
    HANDLE_EXPR(get_paused, mPaused)
    HANDLE_ACTION(never_allow_input, mNeverAllowInput = _msg->Int(2))
    HANDLE_ACTION(set_time_paused, SetTimePaused(_msg->Int(2)))
    HANDLE_EXPR(time_paused, mTimePaused)
    HANDLE(set_shuttle, OnSetShuttle)
    HANDLE_EXPR(shuttle_active, mShuttle->IsActive())
    HANDLE_ACTION(jump, Jump(_msg->Float(2), true))
    HANDLE_ACTION(set_intro_real_time, SetIntroRealTime(_msg->Float(2)))
    HANDLE_ACTION(set_realtime, SetRealTime(_msg->Int(2)))
    HANDLE_EXPR(get_realtime, mRealTime)
    HANDLE_ACTION(is_active_user, _msg->Obj<HamUser>(2))
    HANDLE_EXPR(
        ms_per_beat, TheTempoMap ? TheTempoMap->GetTempo(TheTaskMgr.CurrentTick()) : 0.0f
    )
    HANDLE_EXPR(get_result, GetResult(true))
    HANDLE_EXPR(get_result_for_user, (_msg->Obj<HamUser>(2), GetResult(true)))
    HANDLE_EXPR(is_waiting, IsWaiting())
    HANDLE_ACTION(reset_audio, ResetAudio())
    HANDLE_ACTION(set_loop, SetLoop(_msg->Int(2)))
    HANDLE_ACTION(
        force_serial_sequences, RandomGroupSeq::ForceSerialSequences(_msg->Int(2))
    )
    HANDLE_EXPR(using_serial_sequences, RandomGroupSeq::UsingSerialSequences())
    HANDLE(reset_detection, OnResetDetection)
    HANDLE_ACTION(set_cur_move, SetHamMove(_msg->Int(2), _msg->Obj<HamMove>(3), true))
    HANDLE_EXPR(get_cur_move, mMoveDir ? mMoveDir->CurrentMove(_msg->Int(2)) : (Hmx::Object*)0)
    HANDLE_ACTION(reload_song, ReloadSong())
    HANDLE_EXPR(is_ready, IsReady())
    HANDLE_ACTION(
        load_new_song, LoadNewSong(_msg->Sym(2), _msg->Size() > 2 ? _msg->Sym(3) : (Symbol)0)
    )
    HANDLE_ACTION(load_new_song_audio, LoadNewSongAudio(_msg->Sym(2)))
    HANDLE_ACTION(load_new_song_moves, LoadNewSongMoves(_msg->Sym(2), true))
    HANDLE_ACTION(load_new_venue, LoadNewVenue(_msg->Sym(2)))
    HANDLE_ACTION(swap_move_record, SwapMoveRecord())
    HANDLE_ACTION(flush_move_record, FlushMoveRecord())
    HANDLE_EXPR(is_song_default_player_playing, IsSongDefaultPlayerPlaying())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(Game)
    SYNC_PROP_SET(music_speed, mMusicSpeed, SetMusicSpeed(_val.Float()))
END_PROPSYNCS

void Game::PostUpdate(const SkeletonUpdateData *data) {
    if (data) {
        if (TheTaskMgr.Seconds(TaskMgr::kRealTime) >= 0 && !TheGamePanel->IsGameOver()) {
            if (!mPaused) {
                static Symbol practice("practice");
                static Symbol gameplay_mode("gameplay_mode");
                if (TheGameMode->Property(gameplay_mode)->Sym() != practice) {
                    mOvershell->Poll(*(const Skeleton *const(*)[6])(data->mSkeletonsRight));
                }
                CheckForSkeletonLoss(*(const Skeleton *const(*)[6])(data->mSkeletonsRight));
            }
        }
    }
}

void Game::Start() {
    mHasIntro = false;
    mWaitState = mWaitState == 3 ? 4 : 1;
}

bool Game::HasIntro() { return mHasIntro; }

void Game::ClearState() {
    if (mMoveDir) {
        mMoveDir->FinishGameRecord();
    }
}

void Game::PostWaitRestart() {
    SetMusicSpeed(1.0f);
    if (!mHasIntro)
        PostWaitStart();
}

void Game::LoadNewVenue(Symbol newVenue) {
    static Symbol venue("venue");
    TheGameData->SetVenue(newVenue);
    SetPaused(true, true);
    UIPanel *gamePanel = ObjectDir::Main()->Find<UIPanel>("game_panel", false);
    gamePanel->SetPaused(true);
    TheHamDirector->StartStopVisualizer(true, 0);
    TheMaster->GetAudio()->SetPaused(true);
    TheGame->SetTimePaused(true);
    TheUI->GotoScreen("loading_screen", false, false);
}

void Game::SetIntroRealTime(float f) {
    TheTaskMgr.SetSeconds(f, true);
    mHasIntro = f < 0;
    mRealTime = true;
    mGameInput->SetTimeOffset();
    TheGamePanel->ResetJitter();
}

void Game::PostLoad() {
    WorldDir *world = TheHamDirector->GetWorld();
    MILO_ASSERT(world, 0x259);
#ifdef HX_NATIVE
    if (!world) {
        mMoveDir = nullptr;
        return;
    }
    mMoveDir = world->Find<MoveDir>("moves", false);
#else
    mMoveDir = world->Find<MoveDir>("moves");
#endif
    RELEASE(mOvershell);
    mOvershell = new Overshell();
    mOvershell->Init();
    if (mMoveDir)
        mMoveDir->SetMoveOverlay(sMoveOverlayToggle);
}

void Game::CheckPauseRequest() {
    mPauseRequested = false;
    if (mDeferredPausePending) {
        SetGamePaused(true, mDeferredPauseSoundArg, mDeferredPauseGameArg);
        mDeferredPausePending = false;
    }
}

void Game::LoadNewSongAudio(Symbol s) {
    if (mLoadedSongAudio != s) {
        mLoadedSongAudio = s;
        HamSongDataValidate hsvd = (HamSongDataValidate)0;
        static Symbol dcimindcontrol("dcimindcontrol");
        if (s != dcimindcontrol) {
            MILO_LOG("new_songaudio_name = '%s'\n", s.Str());
            int songID = TheHamSongMgr.GetSongIDFromShortName(s);
            const HamSongMetadata *pMetadata = TheHamSongMgr.Data(songID);
            if (pMetadata && pMetadata->IsOnDisc()) {
                hsvd = (HamSongDataValidate)2;
            }
        }
        RELEASE(mSongInfo);
        SongInfo *audioData = TheHamSongMgr.SongMgr::SongAudioData(s);
#ifdef HX_NATIVE
        if (!audioData) {
            MILO_WARN("Game::LoadNewSongAudio: no audio data for '%s'\n", s.Str());
            return;
        }
#endif
        mSongInfo = new SongInfoCopy(audioData);
        mMaster->Load(mSongInfo, false, 0, false, hsvd, nullptr);
        Fader *fader = TheSynth->Find<Fader>("per_song_sfx_level.fade", false);
        if (fader) {
            fader->SetVolume(0);
        }
    }
}

void Game::FlushMoveRecord() {
#ifdef HX_NATIVE
    if (!mMoveDir) return;
#endif
    MILO_ASSERT(mMoveDir, 0x3a7);
    mMoveDir->FlushMoveRecord();
}

void Game::SwapMoveRecord() {
#ifdef HX_NATIVE
    if (!mMoveDir) return;
#endif
    MILO_ASSERT(mMoveDir, 0x3af);
    mMoveDir->SwapMoveRecord();
}

void Game::ReloadSong() {
    WorldDir *world = TheHamDirector->GetWorld();
    MILO_ASSERT(world, 0x1c7);
#ifdef HX_NATIVE
    if (!world) return;
#endif
    mMoveDir = world->Find<MoveDir>("moves");
    mLoadState = 0;
    LoadSong();
}

bool Game::IsReady() { return IsLoaded() != false; }

void Game::Restart(bool b) {
    mRestartCount++;
    TheGamePanel->ResetJitter();
#ifdef HX_NATIVE
    // Suspend audio rendering before destroying audio objects to prevent
    // the audio callback from accessing freed memory (race condition)
    AudioDevice::GetInstance().Suspend();
#endif
    TheSynth->StopAllSfx(false);
    TheSynth->StopAllSounds();
    if (b) {
        mMaster->Reset();
    }
#ifdef HX_NATIVE
    AudioDevice::GetInstance().Resume();
#endif
    if (mWaitState != 5) {
        mWaitState = 3;
    }
    if (TheHamDirector)
        TheHamDirector->ResetFacialAnimation();
#ifdef HX_NATIVE
    // StopAllSounds destroys the song streams (via MoggClip::KillStream).
    // The song will be reloaded by LoaderPoll, but mLoadState stays at 3,
    // so Game::IsLoaded() never re-polls HamAudio::IsReady() to trigger
    // FinishLoad. Reset to 0 so the load state machine runs again.
    mLoadState = 0;
#endif
}

void Game::SetTimePaused(bool b) {
    mTimePaused = b;
    SetPaused(b, true);
    if (!b && mRealTime) {
        mGameInput->SetTimeOffset();
    }
}

void Game::PostWaitStart() {
    if (!mMaster->GetAudio()->Fail()) {
        static Symbol gameplay_mode("gameplay_mode");
        static Symbol just_intro("just_intro");
        if (TheHamProvider->Property(gameplay_mode, true)->Sym() == just_intro) {
            mMaster->GetAudio()->SetMuteMaster(true);
        }
        mMaster->GetAudio()->Play();
        mPaused = false;
        MetaPerformer::Current()->StartGameplayTimer();
        mRealTime = false;
    }
}

void Game::SetMusicVolume(float vol) {
    MILO_ASSERT(mMaster->GetAudio(), 0x432);
    mMaster->GetAudio()->SetMasterVolume(vol);
}

void Game::Poll() {
    static float sLastBeat;

    if (!HandleWait()) {
        if (!TheSongSequence.Done()) {
#ifdef HX_NATIVE
            if (!mGameInput) return;
#endif
            float songMs = mGameInput->CurrentMs(mRealTime);
            TheTaskMgr.SetSeconds(songMs * 0.001f, false);
        }
        return;
    }
    float drift = 0;
    float songMs = 0;
    if (TheGamePanel->Unkf8()) {
        songMs = mGameInput->CurrentMs(mRealTime);
#ifdef HX_NATIVE
        static int sPollLog = 0;
        if (sPollLog++ < 5 || (sPollLog % 500 == 0 && sPollLog < 5000)) {
            fprintf(stderr, "DC3 Game::Poll — songMs=%.1f realTime=%d paused=%d beat=%.2f\n",
                    songMs, mRealTime, mPaused, MsToBeat(songMs));
        }
#endif
        if (!mRealTime && mShuttle->IsActive()) {
            songMs = PollShuttle();
        }
        drift = 0;
        if (!mRealTime && TheMaster && TheMaster->GetAudio()) {
            if (TheMaster->GetAudio()->GetSongStream()) {
                drift = TheMaster->GetAudio()->GetSongStream()->GetJumpBackTotalTime(songMs);
            }
        }
        float beat = MsToBeat(songMs + drift);
        if (fabs(beat - sLastBeat) > 4.0f) {
            TheTaskMgr.ResetBeatTaskTime(beat);
        }
        sLastBeat = beat;
        if (!unk68 && songMs >= 0
#ifdef HX_NATIVE
            && TheHamDirector
#endif
            && !TheHamDirector->GetGameStartHold()) {
            MILO_LOG("Game::Poll: intro timer expired\n");
            static Message intro_over("intro_over");
            TheGamePanel->Handle(intro_over, true);
            unk68 = true;
        }
        TheTaskMgr.SetSecondsAndBeat(songMs * 0.001f, beat, false);
    }
    if (!mPaused && !mRealTime && IsLoaded()) {
        float seconds = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        float ms = seconds * 1000.0f;
        mSongPos = mSongDB->CalcSongPos(TheMaster, ms);
        TheTaskMgr.SetSongPos(mSongPos);
    }
    if (songMs >= 0) {
        mMaster->Poll(songMs);
        float jumpFrom, jumpTo, jumpBack;
        if (TheMaster->DetectStreamJump(jumpFrom, jumpTo, jumpBack)) {
            float streamBeat = MsToBeat(TheMaster->StreamMs());
            float fromBeat = MsToBeat(jumpFrom);
            float toBeat = MsToBeat(jumpTo);
            float backBeat = MsToBeat(jumpBack);
            TheTaskMgr.SetDeltaTime(kTaskBeats,
                (streamBeat - backBeat) + (toBeat - fromBeat));
        }
    } else {
        mMaster->GetMidiParserMgr()->Poll();
    }
    unk64 = songMs;
}

void Game::StartIntro() {}

void Game::SetHamMove(int i1, HamMove *move, bool b3) {
    if (mMoveDir) {
        HamMove *current = mMoveDir->CurrentMove(i1);
        if (current) {
            int currentBeat = TheTaskMgr.CurrentBeat();
            int i5 = TheTaskMgr.CurrentMeasure();
            if (currentBeat == 0) {
                i5 = i5 - 1;
            } else if (currentBeat != 3) {
                current->IsRest();
            }
            MILO_ASSERT(TheGameData, 0x2C8);
            HamPlayerData *player_data = TheGameData->Player(i1);
            MILO_ASSERT(player_data, 0x2CA);
            if (player_data->IsPlaying()) {
                float frac = mMoveDir->DetectFrac(i1, i5);
                static Message move_passed("move_passed", -1, 0, 0, 0);
                move_passed[0] = i1;
                move_passed[1] = current;
                move_passed[2] = frac;
                move_passed[3] = b3;
                TheGamePanel->Handle(move_passed, false);
            }
        }
        mMoveDir->SetCurrentMove(i1, move);
    }
}

void Game::SetRealTime(bool b1) {
    mRealTime = b1;
    if (mRealTime) {
        mGameInput->SetTimeOffset();
    }
}

EndGameResult Game::GetResult(bool) {
    EndGameResult res = (EndGameResult)1;
    if (MetaPerformer::Current()->SongEndsWithEndgameSequence()) {
        res = (EndGameResult)2;
    }
    return res;
}

int Game::GetNumRestarts() const { return mRestartCount; }

void Game::ResetAudio() {
    mWaitState = 0;
    mMaster->ResetAudio();
}

void Game::SetLoop(bool b1) {
    if (mMoveDir) {
        mMoveDir->SetDebugLoop(b1);
    }
}

void Game::SetMusicSpeed(float f1) {
    mMusicSpeed = f1;
    mMaster->GetAudio()->GetSongStream()->SetSpeed(f1);
}

void Game::Jump(float f1, bool b2) {
    if (b2) {
        mMaster->Jump(f1);
    }
    TheTaskMgr.ResetTaskTime(f1 / 1000.0f, MsToBeat(f1));
    mJumpMs = f1;
    mWaitState = 2;
}

bool Game::IsWaiting() {
    HamAudio *audio = mMaster->GetAudio();
    if (audio->Fail()) {
        return false;
    } else if (mWaitState != 0) {
        return true;
    } else if (audio->IsReady()) {
        return false;
    } else {
        return !audio->IsFinished();
    }
}

void Game::Reset() {
    SongPos pos;
    mRealTime = false;
    mTimePaused = false;
    mSongPos = pos;
    mHasIntro = false;
    unk68 = false;
    TheHamDirector->SetPickingDisabled(false);
#ifdef HX_NATIVE
    if (mMoveDir)
#endif
    {
        for (int i = 0; i < 2; i++) {
            mMoveDir->SetCurrentMove(i, nullptr);
        }
    }
    TheGamePanel->ResetJitter();
    RELEASE(mGameInput);
    mGameInput = new LiveInput(*mMaster->GetAudio());
    TheTaskMgr.SetAVOffset(mGameInput->GetSongToTaskMgrMs() / 1000.0f);
    TheTaskMgr.SetSeconds(0, true);
    mMaster->SetMaps();
}

void Game::SetForegroundVolume(float volume) {
    mMaster->GetAudio()->SetForegroundVolume(volume);
}

void Game::SetBackgroundVolume(float volume) {
    mMaster->GetAudio()->SetBackgroundVolume(volume);
}

float Game::PollShuttle() {
    mShuttle->Poll();
    float ms = mShuttle->Ms();
    mSongPos = mSongDB->CalcSongPos(TheMaster, ms);
    TheTaskMgr.SetSongPos(mSongPos);
    Jump(ms, false);
    return ms;
}

void Game::PostWaitJump() {
    TheGamePanel->ResetJitter();
    if (mRealTime) {
        mGameInput->SetPostWaitJumpOffset(mJumpMs);
    }
    if (TheSongSequence.CurrentIndex() > 0 && !TheSongSequence.GetVenueEntered()) {
        TheSongSequence.SetVenueEntered(true);
        TheHamDirector->VenueEnter(TheHamDirector->GetVenueWorld());
    }
    if (!mHasIntro) {
        PostWaitStart();
    }
}

bool Game::IsSongDefaultPlayerPlaying() {
    static Symbol song("song");
    Symbol symSong = TheGameData->GetSong();
    Symbol symDefaultCharacter = TheHamSongMgr.GetCharacter(symSong);
    Symbol symPrimaryCharacter = TheGameData->Player(0)->Char();
    bool ret = symDefaultCharacter == symPrimaryCharacter;
    const char *songStr = symSong.Str();
    const char *defaultStr = symDefaultCharacter.Str();
    const char *primaryStr = symPrimaryCharacter.Str();
    MILO_LOG(
        "Game::IsSongDefaultPlayerPlaying() : symSong = '%s' symDefaultCharacter = '%s' symPrimaryCharacter = '%s' ret = %d\n",
        songStr,
        defaultStr,
        primaryStr,
        ret
    );
    return ret;
}

void Game::LoadSong() {
    if (!TheSongSequence.Done() && TheSongSequence.CurrentIndex() < 0) {
        TheSongSequence.DoNext(true, false);
        return;
    }
    Symbol song = TheGameData->GetSong();
    MetaPerformer::Current()->Handle(Message("on_load_song", 0), true);
    mUseMoveGraph = false;
    static Symbol cascade("cascade");
    if (TheGameMode->Property("use_movegraph")->Int() != 0
        || TheHamProvider->Property("microgame")->Sym() == cascade
        || TheGameMode->Property("battle_mode")->Sym() == cascade) {
        mUseMoveGraph = true;
    }
    const HamSongMetadata *data = TheHamSongMgr.Data(TheHamSongMgr.GetSongIDFromShortName(song));
    bool isOnDisc = data && data->IsOnDisc();
    HamSongDataValidate v = (HamSongDataValidate)0;
    if (isOnDisc) {
        v = (HamSongDataValidate)2;
    }
    Fader *fader = TheSynth->Find<Fader>("per_song_sfx_level.fade", false);
    if (fader) {
        fader->SetVolume(0);
    }
#ifdef HX_NATIVE
    if (TheMoveMgr)
#endif
    {
        TheMoveMgr->Clear();
        if (mUseMoveGraph) {
            TheMoveMgr->SetSong(song);
        }
    }
    RELEASE(mSongInfo);
    SongInfo *songAudioData = TheHamSongMgr.SongMgr::SongAudioData(song);
#ifdef HX_NATIVE
    if (!songAudioData) {
        MILO_WARN("Game::LoadSong: no audio data for '%s', skipping load\n", song.Str());
        return;
    }
#endif
    mSongInfo = new SongInfoCopy(songAudioData);
#ifdef HX_NATIVE
    if (mMaster && mMaster->GetAudio()) {
        mMaster->GetAudio()->SetPracticeMode(false);
    }
#endif
    mMaster->Load(mSongInfo, false, 0, false, v, nullptr);
}

void Game::SetPaused(bool b1, bool b2) {
    if (!b1) {
        TheTaskMgr.SetAVOffset(mGameInput->GetSongToTaskMgrMs() / 1000.0f);
    }
    if (b2) {
        mGameInput->SetPaused(b1);
        mPaused = b1;
        if (mPaused) {
            MetaPerformer::Current()->StopGameplayTimer();
        } else {
            MetaPerformer::Current()->StartGameplayTimer();
        }
    }
    if (b1) {
        static Message msg("world_pause");
        TheGamePanel->Export(msg, true);
    } else {
        static Message msg("world_unpause");
        TheGamePanel->Export(msg, true);
    }
}

void Game::SetGamePaused(bool b1, bool b2, bool b3) {
    if (mPauseRequested && b1) {
        mDeferredPauseSoundArg = b2;
        mDeferredPauseGameArg = b3;
        mDeferredPausePending = true;
    } else {
        if (!b1 || b3) {
            TheSynth->PauseAllSfx(b1);
        }
        SetPaused(b1, b2);
        mPauseRequested = true;
        if (b1) {
            TheTaskMgr.SetSecondsAndBeat(
                TheTaskMgr.Seconds(TaskMgr::kRealTime), TheTaskMgr.Beat(), false
            );
        } else if (mRealTime) {
            mGameInput->SetTimeOffset();
        }
    }
}

void Game::CheckForSkeletonLoss(const Skeleton *const (&skeletons)[6]) {
    int numPlaying = 0;
    for (int i = 0; i < 2; i++) {
        if (TheGameData->Player(i)->IsPlaying()) {
            numPlaying++;
        }
    }
    int threshold = 1;
    if (TheHamProvider->Property("requires_2_players")->Int() != 0
        || TheHamProvider->Property("is_in_party_mode")->Int() != 0) {
        threshold = 2;
    }
    if (numPlaying < threshold) {
        PauseForSkeletonLoss();
    }
}

void Game::LoadNewSongMoves(Symbol s1, bool b2) {
    bool loaded = TheGame->IsLoaded();
    Symbol song = TheGameData->GetSong();
    Symbol s50 = TheMaster->GetAudio()->Name();
    if (b2 || song != s1 || s50 != song) {
        TheGameData->SetSong(s1);
        const char *milo = MakeString("%s.milo", TheHamSongMgr.SongPath(s1, 0));
        if (loaded) {
            static Symbol song("song");
            FileMerger *fm = TheHamDirector->GetWorld()->Find<FileMerger>("world.fm");
            FileMerger::Merger *merger = fm->FindMerger(song, true);
            merger->Clear(true);
            merger->SetSelected(milo, true);
            fm->StartLoad(true);
        }
    }
}

void Game::LoadNewSong(Symbol s1, Symbol s2) {
    bool loaded = TheGame->IsLoaded();
    bool isNull = s2.Null();
    if (isNull) {
        s2 = s1;
    }
    mWaitState = 5;
    if (loaded) {
        TheHamDirector->SetPollEnabled(false);
    }
    mUseMoveGraph = false;
    // NOTE: These static Symbols appear unused but are required for match
    static Symbol cascade("cascade");
    static Symbol holla_back("holla_back");
    mUseMoveGraph = TheGameMode->Property("use_movegraph")->Int();
    if (s1 != s2) {
        RELEASE(mSongInfo);
        mSongInfo = new SongInfoCopy(TheHamSongMgr.SongMgr::SongAudioData(s2));
        mMaster->LoadOnlySongData(mSongInfo, true, (HamSongDataValidate)0);
        MultiTempoTempoMap *other =
            static_cast<MultiTempoTempoMap *>(HamSongData::sInstance->GetTempoMap());
        mAltTempoMap = new MultiTempoTempoMap(*other);
    } else {
        RELEASE(mAltTempoMap);
    }
    LoadNewSongAudio(s1);
    Symbol s48(TheMaster->GetAudio()->Name());
    LoadNewSongMoves(s2, true);
#ifdef HX_NATIVE
    if (TheMoveMgr)
#endif
    {
        if (mUseMoveGraph) {
            TheMoveMgr->SetSong(s2);
        } else {
            TheMoveMgr->Graph().Clear();
        }
    }
    mLoadState = 0;
}

void Game::PauseForSkeletonLoss() {
    if (!mPaused) {
        int gestureVal = TheGestureMgr->GetPauseOnSkeletonLossMode();
        if (gestureVal != 0 && gestureVal != 1 && !TheSynth->HasPendingVoices()
            && !TheUI->InTransition()) {
            static Message pauseOnSkeletonLossMsg("pause_on_skeleton_loss");
            DataNode handled = TheGamePanel->HandleType(pauseOnSkeletonLossMsg);
            if (handled.Type() == kDataInt && handled.Int() <= 0) {
                return;
            } else {
                static Message pause_game("pause_game");
                TheGamePanel->Handle(pause_game, true);
            }
        }
    }
}
bool Game::IsLoaded() {
    if (mLoadState == 3) {
        return true;
    } else {
        if ((int)mMaster && !mMaster->IsLoaded()) {
            return false;
        }
        if (mLoadState == 0) {
            if (!mMaster->IsLoaded()) {
                return false;
            }
            if (mUseMoveGraph && !TheHamDirector->IsWorldLoaded()) {
                return false;
            }
            TheSongDB->PostLoad(mMaster->GetMidiParserMgr()->GetEventsList());
            PostLoad();
            if (mUseMoveGraph) {
#ifdef HX_NATIVE
                if (!mMoveDir) {
                    MILO_LOG("Game::IsLoaded() - mMoveDir is null, proceeding without MoveGraph\n");
                    mUseMoveGraph = false;
                } else {
                    ObjectDir *moveData = mMoveDir->Find<ObjectDir>("move_data", false);
                    if (moveData) {
                        MILO_LOG("Game::IsLoaded() - Loading MoveGraph from move_data dir\n");
                        TheMoveMgr->LoadMoveData(moveData);
                        SuperEasyRemixer::LoadAllVariants();
                    } else {
                        MILO_LOG("Game::IsLoaded() - move_data not found in moves dir\n");
                        mUseMoveGraph = false;
                    }
                }
#else
                MILO_ASSERT(mMoveDir, 0x224);
                ObjectDir *moveData = mMoveDir->Find<ObjectDir>("move_data", false);
                MILO_ASSERT_FMT(
                    moveData,
                    "move_data.milo is not in moves.milo; re-run update_move_data_proxy.dta"
                );
                TheMoveMgr->LoadMoveData(moveData);
                SuperEasyRemixer::LoadAllVariants();
#endif
            } else {
                MILO_LOG("Game::IsLoaded() - not using MoveGraph");
            }
            mLoadState = 1;
        }
        if (mLoadState == 1) {
            if (mUseMoveGraph && !TheHamDirector->IsMoveMergerFinished()) {
                return false;
            }
            MILO_LOG("Game::IsLoaded() - Done waiting for MoveGraph\n");
            mLoadState = 2;
#if defined(HX_NATIVE) || defined(__EMSCRIPTEN__)
            sNativeAudioInitiated = false;
            sNativeAudioPollCount = 0;
#endif
        }
        if (mLoadState == 2) {
            if (mMaster->GetAudio()->Fail()) {
                return true;
            }
            if (!mMaster->GetAudio()->IsReady()) {
#if defined(HX_NATIVE) || defined(__EMSCRIPTEN__)
                // On native/web, the DTA SongSequence/load_new_song flow may not
                // trigger before PollForLoading reaches this point. Start audio
                // loading here, then give it time to buffer. Bypass after timeout
                // so gameplay loads even if audio is slow (it continues in bg).
                if (!sNativeAudioInitiated) {
                    sNativeAudioInitiated = true;
                    Symbol song = TheGameData->GetSong();
                    fprintf(stderr, "Game::IsLoaded() — initiating audio load for '%s'\n", song.Str());
                    LoadNewSongAudio(song);
                }
                if (sNativeAudioPollCount++ < 120) {
                    TheSynth->Poll();
                    return false;
                }
                fprintf(stderr, "Game::IsLoaded() — audio timeout, proceeding without audio ready\n");
#else
                TheSynth->Poll();
                return false;
#endif
            }
            mLoadState = 3;
            TheProfileMgr.PushAllOptions();
        }
        return mLoadState == 3;
    }
}

DataNode Game::OnSetShuttle(DataArray *arr) {
    auto& _ref0 = mShuttle;
    if (arr->Size() > 3) {
        _ref0->SetController(arr->Int(3));
    }
    bool active = arr->Int(2);
    if (active) {
        auto _tmp0 = mMaster->GetAudio()->GetTime();
        _ref0->SetMs(_tmp0);
        _ref0->SetEndMs(mSongDB->GetSongDurationMs());
        if (_ref0)
            _ref0->SetActive(active);
    } else {
        Jump(_ref0->Ms(), true);
        do {
            TheSynth->Poll();
        } while (!IsLoaded());
        _ref0->SetActive(active);
    }
    return 0;
}

DataNode Game::OnResetDetection(DataArray *a) {
#ifdef HX_NATIVE
    if (!mMoveDir) return 0;
#endif
    MILO_ASSERT(mMoveDir, 0x392);
    if (a->Size() > 2) {
        int index = a->Int(2);
        HamPlayerData *player_data = TheGameData->Player(index);
        MILO_ASSERT(player_data, 0x398);
        MILO_ASSERT(player_data->IsPlaying(), 0x399);
        mMoveDir->ResetDetectFrames(index, player_data->GetDifficulty());
    } else {
        mMoveDir->ResetDetection();
    }
    return 0;
}

DataNode OnToggleMoveOverlay(DataArray *a) {
    sMoveOverlayToggle = !sMoveOverlayToggle;
    if (TheGame && TheGame->GetMoveDir()) {
        TheGame->GetMoveDir()->SetMoveOverlay(sMoveOverlayToggle);
    }
    return sMoveOverlayToggle ? "ON" : "OFF";
}

DataNode OnToggleAutoplay(DataArray *a) {
    HamPlayerData *player_data = TheGameData->Player(a->Int(1));
    MILO_ASSERT(player_data, 0x6F);
    if (!player_data->IsAutoplaying()) {
        player_data->SetAutoplay(sAutoplayStates[0]);
    } else {
        player_data->SetAutoplay(gNullStr);
    }
    return player_data->IsAutoplaying();
}

bool Game::HandleWait() {
    if (mWaitState != unka8) {
        unka8 = mWaitState;
    }
    if (mWaitState == 0) {
        return true;
    }
    // State 3 early return: still in intro countdown
    if (mWaitState == 3) {
        if (mHasIntro && TheTaskMgr.Seconds(TaskMgr::kRealTime) < 0.0f) {
            return true;
        }
    }
    // Common audio readiness check for all non-zero states
    HamAudio *audio = mMaster->GetAudio();
#ifdef HX_NATIVE
    static int sWaitLog = 0;
    if (sWaitLog++ < 10) {
        fprintf(stderr, "DC3 Game::HandleWait — state=%d audioFail=%d audioReady=%d audio=%p\n",
                mWaitState, audio->Fail(), audio->IsReady(), (void*)audio);
    }
#endif
    if (audio->Fail()) {
        return true;
    }
    if (!audio->IsReady()) {
        TheSynth->Poll();
        return false;
    }
#ifdef HX_NATIVE
    fprintf(stderr, "DC3 Game::HandleWait — audio ready! dispatching state=%d\n", mWaitState);
#endif
    // Audio is ready, dispatch based on state
    switch (mWaitState) {
    case 0:
        MILO_ASSERT(false, 0x555);
        // fall through
    case 1:
        PostWaitStart();
        break;
    case 2:
        PostWaitJump();
        break;
    case 3:
        PostWaitRestart();
        break;
    case 4:
        PostWaitRestart();
        PostWaitStart();
        break;
    case 5: {
        // Full song load wait — check all subsystems
        if (!HamSongData::sInstance->GetTempoMap()) {
            return false;
        }
        if (!mMaster->GetAudio()->IsReady()) {
            return false;
        }
        if (!TheHamDirector->IsWorldLoaded()) {
            return false;
        }
        if (TheHamDirector->GetGameModeMerger()->HasPendingFiles()) {
            return false;
        }
        FileMerger *worldFm =
            TheHamDirector->GetWorld()->Find<FileMerger>("world.fm", true);
        if (worldFm->HasPendingFiles()) {
            return false;
        }
#ifdef HX_NATIVE
        mMoveDir = TheHamDirector->GetWorld()->Find<MoveDir>("moves", false);
        if (mMoveDir) {
            mMoveDir->Enter();
            mMoveDir->ResetDetection();
        }
#else
        if (!TheHamDirector->GetWorld()->Find<MoveDir>("moves", false)) {
            return false;
        }
        mMoveDir = TheHamDirector->GetWorld()->Find<MoveDir>("moves", true);
        mMoveDir->Enter();
        mMoveDir->ResetDetection();
#endif
        TheHamDirector->SetupAnims();
        if (mAltTempoMap) {
            TheHamDirector->RemapSongAnimToTempoMap(mAltTempoMap);
            delete mAltTempoMap;
            mAltTempoMap = nullptr;
        }
        TheSongSequence.OnSongLoaded();
        TheHamDirector->SetPollEnabled(true);
        if (mWaitState == 5) {
            mWaitState = 0;
        }
        return false;
    }
    default:
        break;
    }
    mWaitState = 0;
    return true;
}

DataNode OnCycleAutoplay(DataArray *arr) {
    HamPlayerData *player_data = TheGameData->Player(arr->Int(1));
    MILO_ASSERT(player_data, 0x7e);
    Symbol newState;
    if (!player_data->IsAutoplaying()) {
        newState = sAutoplayStates.back();
    } else {
        int size = sAutoplayStates.size();
        int i = 0;
        for (; (unsigned)i < (unsigned)size; i++) {
            if (sAutoplayStates[i] == player_data->Autoplay()) {
                break;
            }
        }
        if (size != 0) {
            i = (i + 1) % size;
        } else {
            i = 0;
        }
        newState = sAutoplayStates[i];
    }
    player_data->SetAutoplay(newState);
    return DataNode(newState);
}

DataNode OnToggleCharFeedback(DataArray *a) {
    ReserveFrames();
    return CharFeedback::sEnabled = !CharFeedback::sEnabled;
}

DataNode OnToggleSongRecord(DataArray *a) {
    ReserveFrames();
    MoveDir::sGameRecord2Player = false;
    return MoveDir::sGameRecord = !MoveDir::sGameRecord;
}

DataNode OnToggleSongRecordDouble(DataArray *a) {
    MoveDir::sGameRecord2Player = !MoveDir::sGameRecord2Player;
    MoveDir::sGameRecord = MoveDir::sGameRecord2Player;
    return MoveDir::sGameRecord;
}

DataNode OnCycleTestDancer(DataArray *) {
    HamPlayerData *player_data = TheGameData->Player(0);
    MILO_ASSERT(player_data, 0xaf);
    String name(player_data->CurrentDancer());
    std::vector<String> &dancers = player_data->AvailableDancers();
    int i = 0;
    if (!dancers.empty()) {
        for (; (unsigned)i < (unsigned)dancers.size(); i++) {
            if (dancers[i] == name) {
                break;
            }
        }
        int size = (int)dancers.size();
        if (size != 0) {
            i = (i + 1) % size;
        } else {
            i = 0;
        }
    }
    name = dancers[i];
    player_data->CurrentDancer() = name;
    return DataNode(name);
}
DataNode OnDumpMoves(DataArray *) {
    std::vector<HamMoveKey> keys;
    TheHamDirector->MoveKeys(kDifficultyExpert, TheHamDirector->GetMoveDir(), keys);
    int i = 0;
    Debug &dbg = TheDebug;
    for (std::vector<HamMoveKey>::iterator it = keys.begin(); it != keys.end(); ++it) {
        const char *name = it->move ? it->move->Name() : "NULL";
        dbg << MakeString("move %d: beat %.2f: name: '%s'\n", i++, it->beat, name);
    }
    return DataNode(0);
}

void GameInit() {
    GameModeInit();
    REGISTER_OBJ_FACTORY(GamePanel)
    REGISTER_OBJ_FACTORY(BustAMovePanel)
    TheDebug.AddExitCallback(GameTerminate);
    TheSongSequence.Init();
    sAutoplayStates.push_back("maximum");
#ifdef HX_NATIVE
    sAutoplayStates.push_back("move_perfect");
    sAutoplayStates.push_back("move_awesome");
    sAutoplayStates.push_back("move_ok");
    sAutoplayStates.push_back("move_bad");
#else
    for (int i = 0; i < 4; i++) {
        sAutoplayStates.push_back(RatingState(i));
    }
#endif
    DataRegisterFunc("toggle_move_overlay", OnToggleMoveOverlay);
    DataRegisterFunc("toggle_autoplay", OnToggleAutoplay);
    DataRegisterFunc("cycle_autoplay", OnCycleAutoplay);
    DataRegisterFunc("toggle_char_feedback", OnToggleCharFeedback);
    DataRegisterFunc("toggle_song_record", OnToggleSongRecord);
    DataRegisterFunc("toggle_song_record_double", OnToggleSongRecordDouble);
    DataRegisterFunc("cycle_test_dancer", OnCycleTestDancer);
    DataRegisterFunc("dump_moves", OnDumpMoves);
}

void GameTerminate() {
    TheHamSongMgr.Terminate();
    GameModeTerminate();
}
