#include "game/GamePanel.h"
#include "char/FileMerger.h"
#include "rndobj/Dir.h"
#include "utl/Loader.h"
#include "flow/PropertyEventProvider.h"
#include "math/Utl.h"
#include "game/Game.h"
#include "game/GameMode.h"
#include "game/PresenceMgr.h"
#include "game/SongDB.h"
#include "gesture/FitnessFilter.h"
#include "gesture/WaveToTurnOnLight.h"
#include "hamobj/CharFeedback.h"
#include "hamobj/HamDirector.h"
#include "hamobj/HamPhraseMeter.h"
#include "hamobj/TransConstraint.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamMaster.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/HamWardrobe.h"
#include "hamobj/MoveDir.h"
#include "hamobj/MoveMgr.h"
#include "meta/HAQManager.h"
#include "meta/PreloadPanel.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamSongMgr.h"
#include "meta_ham/MetaPerformer.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "synth/MetaMusic.h"
#include "movie/TexMovie.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "obj/DataUtl.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "obj/DirLoader.h"
#include "obj/PropSync.h"
#include "obj/Task.h"
#include "obj/Utl.h"
#include "os/Debug.h"
#include "os/Joypad.h"
#include "os/File.h"
#include "os/System.h"
#include "os/Timer.h"
#include "rndobj/Anim.h"
#include "rndobj/Dir.h"
#include "rndobj/PropAnim.h"
#include "rndobj/Overlay.h"
#include "rndobj/PostProc.h"
#include "rndobj/Rnd.h"
#include "synth/Sound.h"
#include "synth/StandardStream.h"
#include "synth/Synth.h"
#include "ui/UI.h"
#include "ui/UIPanel.h"
#include "ui/UIScreen.h"
#include "utl/MBT.h"
#include "utl/TimeConversion.h"
#include "world/Dir.h"
#ifdef HX_NATIVE
#include "rndobj/Text.h"
#include "ui/UILabel.h"
#include <cstdio>
#endif
GamePanel *TheGamePanel = nullptr;
LoopVizCallback gLoopVizCallback;
LatencyCallback gGamePanelCallback;
static float sFloat1 = 0;
static float sFloat2 = 0;

float LatencyCallback::UpdateOverlay(RndOverlay *o, float f2) {
    Hmx::Color color = unk4 ? Hmx::Color(1, 1, 1) : Hmx::Color(0, 0, 0);
    TheRnd.DrawRectScreen(
        Hmx::Rect(0, 0.875f, 0.125f, 0.125f), color, nullptr, nullptr, nullptr
    );
    TheRnd.DrawRectScreen(
        Hmx::Rect(0, 0.125f, 0.125f, 0.125f), color, nullptr, nullptr, nullptr
    );
    return f2;
}

#pragma region LoopVizCallback

LoopVizCallback::LoopVizCallback()
    : mDebugMeter1(0.1f, 0.12f, 0.8f, 0.03f, Hmx::Color(0.1f, 0.1f, 0.1f)),
      mDebugMeter2(0.25f, 0.19f, 0.5f, 0.03f, Hmx::Color(0, 0, 0.8f)), mCurrLoopStart(0), mCurrLoopEnd(0),
      mPrevLoopStart(0), mPrevLoopEnd(0), mLoopStartChangeTimer(0), mLoopEndChangeTimer(0) {}

void LoopVizCallback::DrawHashMarks(
    float f1, float f2, float f3, int i4, int i5, bool b6
) {
    if (i4 <= 3000) {
        for (int i = 0; i <= i4; i++) {
            int u4 = i + i5;
            float fvar1 = ((float)i / (float)i4) * f3 + f2;
            if (u4 % 4 == 0) {
                TheRnd.DrawRectScreen(
                    Hmx::Rect(fvar1, f1 - 0.01f, 0.001f, 0.01f),
                    Hmx::Color(1, 1, 0),
                    nullptr,
                    nullptr,
                    nullptr
                );
            } else if (b6) {
                TheRnd.DrawRectScreen(
                    Hmx::Rect(fvar1, f1 - 0.003f, 0.001f, 0.003f),
                    Hmx::Color(1, 1, 0),
                    nullptr,
                    nullptr,
                    nullptr
                );
            }
        }
    }
}

// Debug visualization for music loop timing
// Shows two meters: song-wide loop position and detailed loop progress
float LoopVizCallback::UpdateOverlay(RndOverlay *o, float y) {
    if (!TheMaster || !TheMaster->GetAudio() || !TheMaster->GetAudio()->GetSongStream()) {
        return y;
    }

    // Decrement change notification timers
    mLoopStartChangeTimer -= TheTaskMgr.DeltaSeconds();
    mLoopEndChangeTimer -= TheTaskMgr.DeltaSeconds();

    // Draw semi-transparent background
    Hmx::Color bgColor(0.0f, 0, 0, 0.6f);
    Hmx::Rect bgRect(0.05f, 0.1f, 0.9f, 0.2f);
    TheRnd.DrawRectScreen(
        bgRect, bgColor, nullptr, nullptr, nullptr
    );

    // Get current loop boundaries
    int loopStart, loopEnd;
    TheMaster->GetAudio()->GetCurrLoopBeats(loopStart, loopEnd);

    // Detect and track loop boundary changes
    if (loopStart != mCurrLoopStart) {
        mLoopStartChangeTimer = 3.0f; // Show change indicator for 3 seconds
        mCurrLoopStart = loopStart;
    }
    if (loopEnd != mCurrLoopEnd) {
        mLoopEndChangeTimer = 3.0f;
        mCurrLoopEnd = loopEnd;
    }

    // Calculate current playback position
    float currentBeat = MsToBeat(TheMaster->StreamMs());
    float loopProgress = (currentBeat - (float)loopStart) / (float)(int)(loopEnd - loopStart);

    // Get stream info for buffer-ahead visualization
    StandardStream *stream =
        dynamic_cast<StandardStream *>(TheMaster->GetAudio()->GetSongStream());
    float bufferAheadBeat = MsToBeat(stream->GetBufferAheadTime());
    float bufferAheadDelta = bufferAheadBeat - currentBeat;
    float bufferAheadProgress = bufferAheadDelta / (float)(int)(loopEnd - loopStart);

    // Calculate normalized positions relative to full song
    static Symbol end("end");
    int songEndBeat = TheMaster->EventBeat(end);
    float loopEndNorm = (float)(int)loopEnd / (float)(int)songEndBeat;
    float loopStartNorm = (float)(int)loopStart / (float)(int)songEndBeat;
    float loopRangeNorm = loopEndNorm - loopStartNorm;

    // === FIRST METER: Song-wide loop visualization ===
    mDebugMeter1.Draw();

    // Draw loop region in gray
    mDebugMeter1.DrawBar(loopStartNorm, loopRangeNorm, Hmx::Color(0.8f, 0.8f, 0.8f));
    // Draw progress within loop
    mDebugMeter1.DrawBar(loopStartNorm, loopRangeNorm * loopProgress, Hmx::Color(0.8f, 0.8f, 0.8f));

    // Handle stream jump point (for looping audio streams)
    bool pastJumpPoint = stream->IsPastStreamJumpPointOfNoReturn();
    float playheadPos = loopStartNorm + loopRangeNorm * loopProgress;

    float bufferStart, bufferWidth;
    if (!pastJumpPoint) {
        // Normal case: buffer ahead of playhead
        bufferStart = playheadPos;
        bufferWidth = bufferAheadProgress * loopRangeNorm;
    } else {
        // Show gap from playhead to loop end (wraparound imminent)
        mDebugMeter1.DrawBar(playheadPos, loopEndNorm - playheadPos, Hmx::Color(1.0f, 1.0f, 1.0f));
        // Buffer visualization starts from loop beginning
        bufferStart = loopStartNorm;
        bufferWidth = ((bufferAheadBeat - (float)(int)loopStart) / (float)(int)(loopEnd - loopStart)) * loopRangeNorm;
    }
    mDebugMeter1.DrawBar(bufferStart, bufferWidth, Hmx::Color(0.5f, 1.0f, 1.0f));

    // Draw playhead line
    mDebugMeter1.DrawLine(playheadPos, Hmx::Color(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);

    // Draw loop boundary labels (highlight if recently changed)
    Hmx::Color startColor = mLoopStartChangeTimer > 0.0f ? Hmx::Color(0, 0, 0) : Hmx::Color(1.0f, 1.0f, 1.0f);
    mDebugMeter1.DrawText(MakeString("%d", loopStart), loopStartNorm, 0.0f, startColor);

    Hmx::Color endColor = mLoopStartChangeTimer > 0.0f ? Hmx::Color(0, 0, 0) : Hmx::Color(1.0f, 1.0f, 1.0f); // NOTE: likely original game bug - should be mLoopEndChangeTimer (was unk54, same as start)
    mDebugMeter1.DrawText(MakeString("%d", loopEnd), loopEndNorm, 0.0f, endColor);

    // Draw current beat label
    mDebugMeter1.DrawText(MakeString("%d", (int)currentBeat), playheadPos, 1.0f, Hmx::Color(1.0f, 1.0f, 1.0f));

    // Draw tick marks for beats
    DrawHashMarks(0.12f + 0.03f, 0.1f, 0.8f, (int)(loopEnd - loopStart), loopStart, true);

    // === SECOND METER: Detailed loop progress ===
    mDebugMeter2.Draw();

    // Draw progress bar (blue)
    mDebugMeter2.DrawBar(0.0f, loopProgress, Hmx::Color(0, 0, 0.8f));

    // Buffer ahead visualization
    float bufferStart2, bufferWidth2;
    if (pastJumpPoint) {
        // Show remaining loop portion in white
        mDebugMeter2.DrawBar(loopProgress, 1.0f - loopProgress, Hmx::Color(1.0f, 1.0f, 1.0f));
        // Buffer wraps around to start
        bufferStart2 = 0.0f;
        bufferWidth2 = (bufferAheadBeat - (float)(int)loopStart) / (float)(int)(loopEnd - loopStart);
    } else {
        // Normal buffer ahead display
        bufferStart2 = loopProgress;
        bufferWidth2 = bufferAheadProgress;
    }
    mDebugMeter2.DrawBar(bufferStart2, bufferWidth2, Hmx::Color(0.5f, 1.0f, 1.0f));

    // Draw playhead line
    mDebugMeter2.DrawLine(loopProgress, Hmx::Color(0.5f, 0.5f, 0.5f), 0.5f, -0.5f);

    // Draw latency indicator
    float latency = SecondsToBeat(1.0f) / (float)(int)(loopEnd - loopStart);
    mDebugMeter2.DrawLine(loopProgress + latency, Hmx::Color(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);

    // Draw loop boundary labels with change markers
    Hmx::Color startColor2 = mLoopStartChangeTimer > 0.0f ? Hmx::Color(0, 0, 0) : Hmx::Color(1.0f, 1.0f, 1.0f);
    char startMarker = mLoopStartChangeTimer > 0.0f ? '*' : ' ';
    mDebugMeter2.DrawText(MakeString("%d%c", loopStart, startMarker), 0.0f, 0.0f, startColor2);

    Hmx::Color endColor2 = mLoopEndChangeTimer > 0.0f ? Hmx::Color(0, 0, 0) : Hmx::Color(1.0f, 1.0f, 1.0f);
    char endMarker = mLoopEndChangeTimer > 0.0f ? '*' : ' ';
    mDebugMeter2.DrawText(MakeString("%d%c", loopEnd, endMarker), 1.0f, 0.0f, endColor2);

    // Draw current beat label
    mDebugMeter2.DrawText(MakeString("%d", (int)currentBeat), loopProgress, 1.0f, Hmx::Color(1.0f, 1.0f, 1.0f));

    // Draw tick marks
    DrawHashMarks(0.19f + 0.03f, 0.25f, 0.5f, (int)(loopEnd - loopStart), loopStart, true);

    // Show change notifications at top of screen
    if (mLoopStartChangeTimer > 0.0f) {
        TheRnd.DrawStringScreen(
            MakeString("Loop start changed from %d to %d", mPrevLoopStart, loopStart),
            Vector2(0.1f, 0.25f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );
    } else {
        mPrevLoopStart = loopStart;
    }

    if (mLoopEndChangeTimer > 0.0f) {
        TheRnd.DrawStringScreen(
            MakeString("Loop end changed from %d to %d", mPrevLoopEnd, loopEnd),
            Vector2(0.1f, 0.27f),
            Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f),
            true
        );
    } else {
        mPrevLoopEnd = loopEnd;
    }

    return y;
}

#pragma endregion
#pragma region GamePanel

GamePanel::GamePanel()
    : mGame(nullptr), mTimeOverlay(RndOverlay::Find("time")),
      mLatencyOverlay(RndOverlay::Find("latency")),
      mFitnessOverlay(RndOverlay::Find("fitness")),
      mLoopVizOverlay(RndOverlay::Find("loop_viz")), mStartPaused(false), mState(), mEndGameResult(0),
      mPerformanceProfiler("game_panel_load", 1), mIsReplay(false), mJitterSampleCount(0), mJitterBufferIndex(-2), mCurrentJitterValue(0), unkf8(true),
      mPauseCountInTimer(new Timer()), mNormalPauseEnabled(true), mCheatPaused(false), mPollLoadState(0), mSoundEventReceiverSet(false) {
    mFitnessFilters[0].SetPlayerIndex(0);
    mFitnessFilters[1].SetPlayerIndex(1);
    mFrameTimeSamples.resize(32);
    sFloat1 = sFloat2 = 0;
    MILO_ASSERT(!TheGamePanel, 0x9E);
    TheGamePanel = this;
    SetType("none");
}

GamePanel::~GamePanel() {
    TheGamePanel = nullptr;
    RELEASE(mPauseCountInTimer);
}

BEGIN_HANDLERS(GamePanel)
    HANDLE_ACTION(set_start_paused, mStartPaused = _msg->Int(2))
    HANDLE_EXPR(in_intro, mState == kGameInIntro)
    HANDLE_EXPR(is_game_over, mState == kGameOver)
    HANDLE_EXPR(is_playing, mState == kGamePlaying)
    HANDLE_ACTION(start_game, StartGame())
    HANDLE(start_load_song, OnStartLoadSong)
    HANDLE(start_song_now, OnStartSongNow)
    HANDLE_ACTION(set_paused_except_sound, SetPausedHelper(_msg->Int(2), false))
    HANDLE_ACTION(cheat_pause, CheatPause(_msg->Int(2)))
    HANDLE_ACTION(clear_draw_glitch, ClearDrawGlitch())
    HANDLE_ACTION(reload_data, ReloadData())
    HANDLE_ACTION(win, SetGameOver(true))
    HANDLE_EXPR(is_past_stream_jump_point_of_no_return, IsPastStreamJumpPointOfNoReturn())
    HANDLE_ACTION(reset_limb_feedback, ResetLimbFeedback())
    HANDLE_ACTION(set_limb_feedback_visible, SetLimbFeedbackVisible(_msg->Int(2)))
    HANDLE(get_fitness_data, OnGetFitnessData)
    HANDLE_MESSAGE(EndGameMsg)
    HANDLE_SUPERCLASS(UIPanel)
END_HANDLERS

BEGIN_PROPSYNCS(GamePanel)
    {
        static Symbol _s("replay");
        if (sym == _s && (_op & kPropGet)) {
            return PropSync(mIsReplay, _val, _prop, _i + 1, _op);
        }
    }
    SYNC_SUPERCLASS(UIPanel)
END_PROPSYNCS

void GamePanel::SetTypeDef(DataArray *def) {
    TheHamProvider->SetProperty("game_stage", Symbol("intro"));
    static Message exit("exit_mode");
    Handle(exit, false);
    UIPanel::SetTypeDef(def);
#ifdef HX_NATIVE
    // On native, the HUD loads asynchronously via FileMerger and $hud_panel
    // is set before the game_panel type changes to "perform". The DTA init
    // handler fires here but common_reset hasn't run with $hud_panel set.
    // Re-trigger common_reset now that the type definition (with the handler)
    // is active and $hud_panel points to the HUD.
    if (def) {
        DataNode &hp = DataVariable("hud_panel");
        if (hp.Type() == kDataObject && hp.GetObj()) {
            static Message resetMsg("common_reset");
            DataNode result = Handle(resetMsg, false);
            fprintf(stderr, "DC3 GamePanel::SetTypeDef('%s') — re-triggered common_reset (result type=%d)\n",
                    def->Sym(0).Str(), result.Type());
        }
    }
#endif
}

void GamePanel::Load() {
    mIsReplay = false;
    mPerformanceProfiler.Start();
    CreateGame();
    UIPanel::Load();
}

void GamePanel::Enter() {
    TheTaskMgr.ClearTimelineTasks(kTaskSeconds);
    TheTaskMgr.ClearTimelineTasks(kTaskBeats);
#ifdef HX_NATIVE
    // On Xbox, DTA scripts fire {metamusic stop} during screen transitions.
    // MetaPanel is shared between menu and game_screen, so its Exit() is
    // never called by the panel lifecycle. Kill() immediately stops the
    // stream — Stop() only fades, and MetaMusic::Poll() (which finalizes
    // the stop) is suppressed during game_screen.
    if (TheMetaMusic) {
        TheMetaMusic->Kill();
    }
#endif
    UIPanel::Enter();
    mPerformanceProfiler.Stop();
    Reset();
    SetPaused(false);
    ThePresenceMgr.SetInGame(TheHamSongMgr.GetSongIDFromShortName(TheGameData->GetSong()));
    sFloat1 = TheMaster->StreamMs();
    sFloat2 = 0;
}

void GamePanel::Exit() {
    TheTaskMgr.ClearTimelineTasks(kTaskSeconds);
    TheTaskMgr.ClearTimelineTasks(kTaskBeats);
    ThePresenceMgr.SetNotInGame();
    UIPanel::Exit();
    mIsReplay = true;
    for (int i = 0; i < 2; i++) {
        FitnessFilter *filter = GetFitnessFilter(i);
        if (filter) {
            filter->StopTracking();
        }
    }
    RndAnimatable *beatRepeatAnim = TheSynth->Find<RndAnimatable>("beat_repeat.anim");
    if (beatRepeatAnim) {
        beatRepeatAnim->SetFrame(4.0f, 1.0f);
    }
    mSoundEventReceiverSet = false;
}

void GamePanel::Poll() {
    START_AUTO_TIMER("game_poll");
    SetSoundEventReceiver();
    if (!IsLoaded()) {
        return;
    } else {
        if (mPauseCountInTimer->SplitMs() >= 100.0f) {
            while (!FileDiscSpinUp()) {
                MILO_LOG("Spinning up disc took longer than count in timer\n");
            }
            MILO_ASSERT(mState == kGamePlaying, 0x1C5);
            mGame->SetGamePaused(false, true, true);
            mPauseCountInTimer->Reset();
        } else if (mPauseCountInTimer->Running()) {
            FileDiscSpinUp();
        }
        if (!mGame->Paused() && TheUIEventMgr->HasActiveDialogEvent()) {
            static Message pauseGameMsg("pause_game");
            Handle(pauseGameMsg, true);
        }
        UIPanel::Poll();
        if (mState == 0) {
            StartIntro();
        }
        if (!mPauseCountInTimer->Running()) {
            mGame->Poll();
        }
        if (mState == kGameInIntro && TheTaskMgr.Seconds(TaskMgr::kRealTime) > -0.025f
            && !TheHamDirector->IsGameStartHold()) {
            StartGame();
        }
#ifdef HX_NATIVE
        // TODO(native): autoplay scoring — remove when real move detection is implemented
        // Without Kinect, no move_passed events fire, so score stays 0.
        // Simulate scoring by awarding points on each beat during gameplay.
        if (mState == kGamePlaying) {
            static int sLastBeat = -1;
            static int sNativeScore = 0;
            int curBeat = (int)TheTaskMgr.Beat();
            if (curBeat != sLastBeat && curBeat >= 0) {
                sLastBeat = curBeat;
                // Award 100-500 points per beat depending on beat position
                int points = 100 + (curBeat % 5) * 100;
                sNativeScore += points;
                // Update player provider score (this is what the DTA flow reads)
                for (int p = 0; p < 2; p++) {
                    HamPlayerData *pd = TheGameData->Player(p);
                    if (pd && pd->IsPlaying() && pd->Provider()) {
                        static Symbol scoreSym("score");
                        pd->Provider()->SetProperty(scoreSym, DataNode(sNativeScore));
                    }
                }
                // Update score labels directly on HUD panel subdirs.
                // The DTA set_score handler is an empty stub in the ark DTB,
                // so we set score1.lbl inside score_left/score_right from C++.
                static UILabel *sScoreLabels[2] = {nullptr, nullptr};
                static UILabel *sShadowLabels[2] = {nullptr, nullptr};
                if (!sScoreLabels[0]) {
                    DataNode &hp = DataVariable("hud_panel");
                    if (hp.Type() == kDataObject && hp.GetObj()) {
                        ObjectDir *hd = dynamic_cast<ObjectDir *>(hp.GetObj());
                        if (hd) {
                            const char *sides[] = {"score_left", "score_right"};
                            for (int s = 0; s < 2; s++) {
                                RndDir *sd = hd->Find<RndDir>(sides[s], false);
                                if (sd) {
                                    sScoreLabels[s] =
                                        sd->Find<UILabel>("score1.lbl", false);
                                    sShadowLabels[s] =
                                        sd->Find<UILabel>("score2.lbl", false);
                                    sd->SetShowing(true);
                                }
                            }
                        }
                    }
                }
                if (sScoreLabels[0] || sScoreLabels[1]) {
                    // Format score with comma separators
                    char buf[32];
                    if (sNativeScore >= 1000000)
                        snprintf(buf, sizeof(buf), "%d,%03d,%03d",
                                 sNativeScore / 1000000,
                                 (sNativeScore / 1000) % 1000,
                                 sNativeScore % 1000);
                    else if (sNativeScore >= 1000)
                        snprintf(buf, sizeof(buf), "%d,%03d",
                                 sNativeScore / 1000,
                                 sNativeScore % 1000);
                    else
                        snprintf(buf, sizeof(buf), "%d", sNativeScore);
                    for (int s = 0; s < 2; s++) {
                        if (sScoreLabels[s]) {
                            sScoreLabels[s]->RndText::SetText(buf);
                            sScoreLabels[s]->UpdateText();
                            sScoreLabels[s]->SetShowing(true);
                            // Negate X scale to counter parent mirror flip
                            Transform lx = sScoreLabels[s]->LocalXfm();
                            lx.m.x.x = -std::abs(lx.m.x.x);
                            sScoreLabels[s]->SetLocalXfm(lx);
                        }
                        // Hide score2.lbl (shadow) to prevent overlap
                        if (sShadowLabels[s])
                            sShadowLabels[s]->SetShowing(false);
                    }
                }
            }
        }
#endif
        for (int i = 0; i < 2; i++) {
            FitnessFilter *filt = GetFitnessFilter(i);
            if (filt) {
                filt->Poll();
            }
        }
        MoveDir *moves = TheHamDirector->GetWorld()->Find<MoveDir>("moves", false);
        static Message isTrackingScoreMsg("is_tracking_score");
        DataNode handled = Handle(isTrackingScoreMsg, false);
        if (moves && handled.Type() != kDataUnhandled) {
            moves->SetFiltersEnabled(handled.Int());
        }
        if (mTimeOverlay->Showing()) {
            UpdateNowBar();
        }
        UpdateLatency();
        if (mFitnessOverlay->Showing()) {
            UpdateFitnessOverlay();
        }
        if (mLoopVizOverlay->Showing()) {
            mLoopVizOverlay->SetCallback(&gLoopVizCallback);
        }
        if (TheMaster) {
            float ms = TheMaster->StreamMs();
            sFloat2 = (ms - sFloat1) / 1000.0f;
            sFloat1 = ms;
        } else {
            sFloat2 = 0;
        }
    }
}

void GamePanel::SetPaused(bool b1) { SetPausedHelper(b1, true); }

bool GamePanel::IsLoaded() const {
    if (!UIPanel::IsLoaded()) {
        return false;
    } else {
        return mPollLoadState == 4;
    }
}

void GamePanel::Unload() {
    UIPanel::Unload();
    RELEASE(mGame);
    mPollLoadState = 0;
    mPaused = false;
}

void GamePanel::FinishLoad() {
    UIPanel::FinishLoad();
    PreloadPanel::sCache->Clear();
    HAQManager::PrintSongInfo(TheGameData->GetSong(), TheSongDB->GetSongDurationMs());
}

FitnessFilter *GamePanel::GetFitnessFilter(int i1) {
    static Symbol is_in_party_mode("is_in_party_mode");
    static Symbol is_in_infinite_party_mode("is_in_infinite_party_mode");
    if (TheHamProvider->Property(is_in_party_mode)->Int() == 0
        && TheHamProvider->Property(is_in_infinite_party_mode)->Int() == 0) {
        HamPlayerData *pPlayerData = TheGameData->Player(i1);
        MILO_ASSERT(pPlayerData, 0x4A1);
        HamProfile *profile = TheProfileMgr.GetProfileFromPad(pPlayerData->PadNum());
        if (profile && profile->InFitnessMode()) {
            return &mFitnessFilters[i1];
        }
    }
    return nullptr;
}

void GamePanel::ResetJitter() {
    mJitterSampleCount = 0;
    mJitterBufferIndex = -2;
    mCurrentJitterValue = 0;
}

void GamePanel::UpdateNowBar() {
    MILO_ASSERT(mGame, 0x23d);
    float songSec = TheTaskMgr.Seconds(TaskMgr::kRealTime);
    float songDurMs = TheSongDB->GetSongDurationMs();
    float timeRemaining = songDurMs * 0.001f - songSec;
    char sign = '-';
    if (timeRemaining < 0.0f) {
        timeRemaining = -timeRemaining;
        sign = '+';
    }
    int totalTick = (int)TheTaskMgr.TotalTick();
    float pct;
    if (songDurMs > 0.0f) {
        pct = 100.0f;
        float computed = songSec * 100.0f / (songDurMs * 0.001f);
        if (computed <= 100.0f)
            pct = computed;
    }
    *mTimeOverlay << MakeString(
        "MBT %d:%d:%03d [%s %c%s %4.1f%%] (%.2fsec %dtk)\n",
        TheTaskMgr.CurrentMeasure() + 1,
        TheTaskMgr.CurrentBeat() + 1,
        TheTaskMgr.CurrentTick(),
        FormatTimeMSH(songSec * 1000.0f),
        sign,
        FormatTimeMSH(timeRemaining * 1000.0f),
        pct,
        songSec,
        totalTick
    );
}

float GamePanel::DeJitter(float ms) {
    float sentinel = 1.0000000150474662e+30f;
    static DataNode &noJitter = DataVariable("no_jitter");
    float result = sentinel;
    if (noJitter.Int() == 0 && mJitterBufferIndex > 8) {
        int prevPos = (mJitterSampleCount - 1) & 0x1F;
        int historyPos = (prevPos - mJitterBufferIndex) & 0x1F;
        float avgDelta =
            (mFrameTimeSamples[prevPos] - mFrameTimeSamples[historyPos])
            / (float)mJitterBufferIndex;
        if (mCurrentJitterValue == 0.0f) {
            mCurrentJitterValue = avgDelta;
        }
        float filtered =
            (avgDelta - mCurrentJitterValue) * 0.1f + mCurrentJitterValue;
        mCurrentJitterValue = filtered;
        result = unkf4 + filtered;
        result = Max(ms - 16.0f, result);
        result = Min(result, ms + 16.0f);
        if (result < unkf4) {
            result = unkf4;
        }
    }
#ifdef HX_NATIVE
    mFrameTimeSamples[mJitterSampleCount & 0x1F] = ms;
#else
    mFrameTimeSamples[mJitterSampleCount] = ms;
#endif
    if (result != sentinel) {
        ms = result;
    }
    mJitterSampleCount = (mJitterSampleCount + 1) & 0x1F;
    if (mJitterBufferIndex < 30) {
        mJitterBufferIndex++;
    }
    unkf4 = ms;
    return ms;
}

void GamePanel::CreateGame() {
    RELEASE(mGame);
    mGame = new Game();
}

void GamePanel::StartGame() {
    AutoTimer::SetCollectStats(true, TheRnd.VerboseTimers());
#ifdef HX_NATIVE
    // On native, always start (no intro gating). Character outfits are loaded
    // by HamDirector::OnFileLoaded('song') via the DTA flow — do NOT call
    // LoadCharacters here, as it would trigger a redundant async FileMerger
    // Clear→Merge cycle that destroys character meshes/animation mid-gameplay.
    mGame->Start();
#else
    if (mGame->HasIntro()) {
        mGame->Start();
    }
#endif
    ThePresenceMgr.SetInGame(TheHamSongMgr.GetSongIDFromShortName(TheGameData->GetSong()));
    mState = kGamePlaying;
#ifdef HX_NATIVE
    // SongSequence::Play also sets game_stage to "playing", but on native
    // the intro sequence may be skipped — set it explicitly as a fallback.
    TheHamProvider->SetProperty("game_stage", Symbol("playing"));
    fprintf(stderr, "DC3 Native: StartGame() — game_stage set to 'playing'\n");
#endif
}

void GamePanel::CheatPause(bool b1) {
    mCheatPaused = b1;
    mNormalPauseEnabled = false;
    SetPaused(mCheatPaused);
    mNormalPauseEnabled = true;
}

void GamePanel::UpdateFitnessOverlay() {
    HamProfile *profile = TheProfileMgr.GetActiveProfile(true);
    if (profile) {
        bool fitness = profile->InFitnessMode();
        float f1, f2, f3;
        profile->GetFitnessStats(f1, f2, f3);
        *mFitnessOverlay << MakeString(
            "Fitness %s: %.2f cal for this song, %.2f cal total, %s total time\n",
            fitness ? "on" : "off",
            f3,
            f2,
            FormatTimeMSH(f1 * 1000.0f)
        );
    }
}

void GamePanel::UpdateLatency() {
    static DataNode &latency_test = DataVariable("latency_test");
    static DataNode &pad_button = DataVariable("pad_button");
    if (latency_test.Int(nullptr) == 0) {
        mLatencyOverlay->SetShowingOnly(false);
        mLatencyOverlay->TimerRef().Restart();
        return;
    }
    bool bFlash = false;
    bool bJustPressed = false;
    int joyNum = latency_test.Int(nullptr) - 2;
    if (joyNum == -1) {
        static int sFlashCnt = 0;
        float f30 = (float)floor(TheTaskMgr.Beat() * 2.0f);
        float beat2 = TheTaskMgr.Beat();
        float delta = TheTaskMgr.DeltaBeat();
        if (f30 != (float)floor((beat2 - delta) * 2.0f)) {
            sFlashCnt = 3;
        }
        if (sFlashCnt > 0) {
            bFlash = true;
            sFlashCnt--;
        }
    } else {
        static bool sLastBtn = false;
        JoypadData *pad = JoypadGetPadData(joyNum);
        if (pad != nullptr) {
            bool pressed = ((1 << pad_button.Int(nullptr)) & pad->mButtons) != 0;
            bFlash = pressed;
            bJustPressed = pressed && !sLastBtn;
            sLastBtn = pressed;
        }
    }
    gGamePanelCallback.unk4 = bFlash;
    if (bJustPressed) {
        static Hmx::Object *sBeep = nullptr;
        if (sBeep == nullptr) {
            ObjectDir *dir;
            {
                FilePath path("test/latency.milo");
                dir = DirLoader::LoadObjects(path, nullptr, nullptr);
            }
            sBeep = dir->Find<Hmx::Object>("beep.cue", true);
        }
        static Message sPlayMsg("play");
        DataNode result = sBeep->Handle(sPlayMsg, true);
    }
    static int sToggle = 0;
    static float sMs[2] = {0, 0};
    int idx = sToggle;
    sToggle = 1 - sToggle;
    sMs[idx] = TheRnd.DrawMs();
    mLatencyOverlay->SetShowingOnly(true);
    mLatencyOverlay->TimerRef().Restart();
    mLatencyOverlay->Clear();
    float beat = TheTaskMgr.Beat();
    *mLatencyOverlay << MakeString("Joy %d Beat %.3f\nms %.2f last %.2f", joyNum, beat, sMs[0], sMs[1]);
    mLatencyOverlay->SetCallback(&gGamePanelCallback);
}

void GamePanel::StartIntro() {
    mState = kGameInIntro;
    static Message pick_intro("pick_intro");
    HandleType(pick_intro);
    if (mStartPaused) {
        mGame->SetTimePaused(true);
    }
    mGame->StartIntro();
}

void GamePanel::SetGameOver(bool b1) {
    if (mState != kGameOver) {
        AutoTimer::SetCollectStats(false, TheRnd.VerboseTimers());
        EndGameResult r = mGame->GetResult(b1);
        static EndGameMsg msg((EndGameResult)3);
        msg[0] = r;
        Handle(msg, false);
    }
}

void GamePanel::ReloadData() {
    ObjectDir *hudPanel = DataVariable("hud_panel").Obj<ObjectDir>();
    DataMacroWarning(false);
    DataArray *fileData = DataReadFile(SystemConfig()->File(), true);
    DataArray *objArr = fileData->FindArray("objects");
    ReloadObjectType(this, objArr);
    Hmx::Object *newObj = Hmx::Object::New<Hmx::Object>();
    newObj->SetType("point_value_chase");
    ReloadObjectType(newObj, objArr);
    delete newObj;
    FilePath proxy = hudPanel->ProxyFile();
    hudPanel->SetProxyFile(proxy, false);
    ReloadObjectType(hudPanel, objArr);
    fileData->Release();
    DataMacroWarning(true);
    static Message entermsg("enter");
    hudPanel->HandleType(entermsg);
    EndGameMsg msg((EndGameResult)0);
    Handle(msg, true);
}

void GamePanel::Reset() {
    for (int i = 0; i < 2; i++) {
        FitnessFilter *filt = GetFitnessFilter(i);
        if (filt) {
            filt->StartTracking();
        }
    }
    mGame->Reset();
    mState = (State)0;
    mEndGameResult = 0;
    mPauseCountInTimer->Reset();
    mCheatPaused = false;
    WorldDir *dir = TheHamDirector->GetVenueWorld();
    for (ObjDirItr<TexMovie> it(dir, true); it != nullptr; ++it) {
        it->Reset();
    }
    mGame->Restart(true);
    static Message resetMsg("reset");
    Export(resetMsg, true);
}

void GamePanel::SetSoundEventReceiver() {
    if (!mSoundEventReceiverSet) {
        ObjectDir *hudPanel = DataVariable("hud_panel").Obj<ObjectDir>();
        ObjectDir *soundBank = hudPanel->Find<ObjectDir>("sound_bank", false);
        if (soundBank) {
            for (ObjDirItr<Sound> it(soundBank, true); it != nullptr; ++it) {
                if (it->NumMarkers() > 0) {
                    it->SetSoundEventReceiver(this);
                }
            }
            mSoundEventReceiverSet = true;
        }
    }
}

void GamePanel::SetPausedHelper(bool paused, bool pauseSound) {
    // Pause/unpause fitness tracking for all players
    for (int i = 0; i < 2; i++) {
        FitnessFilter *filt = GetFitnessFilter(i);
        if (filt) {
            filt->SetPaused(paused);
        }
    }

    // Guard: can't pause unless panel is up
    if (GetState() != kUp) {
        MILO_NOTIFY("trying to pause while not up");
        return;
    }

    // Wait for synth to finish any pending voices
    while (TheSynth->HasPendingVoices()) {
        TheSynth->Poll();
    }

    // Guard: if unpausing while cheat-paused, don't unpause
    if (!paused && mCheatPaused) {
        return;
    }

    // Guard: already in desired pause state
    if (mPaused == paused) {
        return;
    }

    mPaused = paused;

    // Handle pause count-in timer
    if (mPauseCountInTimer->Running()) {
        if (!paused) {
            MILO_NOTIFY(
                "Trying to unpause while the count in is active; should not be possible!"
            );
        }
        mPauseCountInTimer->Reset();
    } else {
        // Check if we should start pause count-in when unpausing during gameplay
        if (mNormalPauseEnabled && mState == kGamePlaying && !paused) {
            if (TheGameMode->Property("pause_count_in")->Int() != 0) {
                // Start the count-in timer instead of immediately unpausing
                mPauseCountInTimer->Start();
            } else {
                // No count-in configured, unpause immediately
                bool isIntroOrPlaying = (mState <= kGamePlaying);
                mGame->SetGamePaused(paused, isIntroOrPlaying, pauseSound);

                // Pause/unpause venue movie textures
                WorldDir *dir = TheHamDirector->GetVenueWorld();
                for (ObjDirItr<TexMovie> it(dir, true); it != nullptr; ++it) {
                    if (it->IsOpen()) {
                        it->SetPaused(paused);
                    }
                }

                TheWaveToTurnOnLight->SetPaused(paused);

                // When unpausing, wait for disc to spin up
                while (!paused && !FileDiscSpinUp())
                    ;
            }
        }
    }
    UpdateNowBar();
}

void GamePanel::ResetLimbFeedback() {
    WorldDir *dir = TheHamDirector->GetVenueWorld();
    for (ObjDirItr<CharFeedback> it(dir, true); it != nullptr; ++it) {
        it->ResetErrors();
    }
}

void GamePanel::SetLimbFeedbackVisible(bool visible) {
    WorldDir *dir = TheHamDirector->GetVenueWorld();
    for (ObjDirItr<CharFeedback> it(dir, true); it != nullptr; ++it) {
        it->SetShowing(visible);
    }
}

DataNode GamePanel::OnStartLoadSong(DataArray *a) {
    Symbol song = a->ForceSym(2);
    QuickplayPerformer *qp = dynamic_cast<QuickplayPerformer *>(MetaPerformer::Current());
    MILO_ASSERT(qp, 0x446);
    qp->SetSong(song);
    CreateGame();
    return 0;
}

DataNode GamePanel::OnStartSongNow(DataArray *a) {
    Reset();
    StartIntro();
    return 0;
}

DataNode GamePanel::OnGetFitnessData(const DataArray *a) {
    int index = a->Int(2);
    FitnessFilter *filter = GetFitnessFilter(index);
    if (!filter) {
        return 0;
    } else if (a->Size() > 3) {
        float f1, f2;
        filter->GetFitnessData(f1, f2);
        bool b3 = a->Int(3);
        if (b3) {
            HamPlayerData *pPlayerData = TheGameData->Player(index);
            MILO_ASSERT(pPlayerData, 0x4BC);
            HamProfile *profile = TheProfileMgr.GetProfileFromPad(pPlayerData->PadNum());
            f1 += profile->FitnessCalories();
            f2 += profile->FitnessTime();
        }
        if (a->Size() > 4) {
            *a->Node(4).Var() = f1;
        }
        if (a->Size() > 5) {
            *a->Node(5).Var() = f2;
        }
    }
    return 1;
}

DataNode GamePanel::OnMsg(const EndGameMsg &msg) {
    for (int i = 0; i < 2; i++) {
        HamPlayerData *pPlayerData = TheGameData->Player(i);
        MILO_ASSERT(pPlayerData, 0x3F5);
        HamProfile *profile = TheProfileMgr.GetProfileFromPad(pPlayerData->PadNum());
        FitnessFilter *filter = GetFitnessFilter(i);
        float f1, f2;
        if (filter && filter->GetFitnessDataAndReset(f2, f1)) {
            profile->SetFitnessStats(i, f1, f2);
        }
    }
    EndGameResult r = msg.Result();
    if (r != 1 && r != 2) {
        MetaPerformer::Current()->HandleGameplayEnded(r);
    }
    if (mGame) {
        mGame->ClearState();
    }
    if (msg.Result() == 0) {
        static Symbol game_restart("game_restart");
        static DataArrayPtr restart(game_restart);
        restart->Execute();
    } else {
        mState = kGameOver;
        mEndGameResult = msg.Result();
        switch (mEndGameResult) {
        case 1: {
            Export(Message("game_won"), true);
            break;
        }
        case 2: {
            Export(Message("game_won_finale"), true);
            break;
        }
        case 3: {
            Export(Message("game_over"), true);
            break;
        }
        default:
            MILO_NOTIFY("bad game over state");
            break;
        }
    }
    return 1;
}

bool GamePanel::IsPastStreamJumpPointOfNoReturn() {
    StandardStream *stream =
        dynamic_cast<StandardStream *>(TheMaster->GetAudio()->GetSongStream());
    return stream->IsPastStreamJumpPointOfNoReturn();
}

void GamePanel::PollForLoading() {
#ifdef HX_NATIVE
    static int sPollCount = 0;
    static int sLastState = -1;
#endif
    mPollLoadState = 0;
    UIPanel::PollForLoading();
    if (UIPanel::IsLoaded()) {
        mPollLoadState = 1;
        UIPanel *worldPanel = ObjectDir::Main()->Find<UIPanel>("world_panel");
        if (TheUI->TransitionScreen()
            && TheUI->TransitionScreen()->HasPanel(worldPanel)) {
            if (!TheHamDirector) {
#ifdef HX_NATIVE
                if (sPollCount++ < 5) fprintf(stderr, "DC3 GamePanel::PollForLoading() — BLOCKED at gate 1: no TheHamDirector\n");
#endif
                return;
            }
            if (!TheHamDirector->IsWorldLoaded()) {
#ifdef HX_NATIVE
                if (sPollCount++ < 5) fprintf(stderr, "DC3 GamePanel::PollForLoading() — BLOCKED at gate 1: world not loaded\n");
#endif
                return;
            }
        }
        mPollLoadState = 2;
        const DataNode *prop = TheGameMode->Property("load_chars");
        if (prop->Int() != 0 && !TheHamWardrobe->AllCharsLoaded()) {
#ifdef HX_NATIVE
            if (sPollCount++ < 5) fprintf(stderr, "DC3 GamePanel::PollForLoading() — BLOCKED at gate 2: chars not loaded (load_chars=%d)\n", prop->Int());
#endif
            return;
        }
        mPollLoadState = 3;
        if (mGame->IsReady()) {
            mPollLoadState = 4;
#ifdef HX_NATIVE
            if (sLastState != 4) { fprintf(stderr, "DC3 GamePanel::PollForLoading() — DONE (state 4)!\n"); sLastState = 4; }
#endif
        }
#ifdef HX_NATIVE
        else {
            if (sPollCount++ < 5) fprintf(stderr, "DC3 GamePanel::PollForLoading() — BLOCKED at gate 3: game not ready (state=%d)\n", mPollLoadState);
        }
#endif
    }
#ifdef HX_NATIVE
    else {
        if (sPollCount++ < 5) fprintf(stderr, "DC3 GamePanel::PollForLoading() — BLOCKED at gate 0: UIPanel not loaded\n");
    }
#endif
}

void GamePanel::ClearDrawGlitch() {
    UIScreen *gameScreen = ObjectDir::Main()->Find<UIScreen>("game_screen");
    gameScreen->SetShowing(false);
    RndPostProc::Reset();
    for (int i = 0; i < 2; i++) {
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
    }
}

#pragma endregion
