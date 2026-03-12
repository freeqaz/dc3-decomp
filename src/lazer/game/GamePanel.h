#pragma once
#include "game/Game.h"
#include "gesture/FitnessFilter.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Timer.h"
#include "rndobj/Overlay.h"
#include "ui/UIPanel.h"
#include "utl/DebugMeter.h"
#include "utl/Profiler.h"

class GamePanel : public UIPanel {
public:
    enum State {
        kGameInIntro = 1,
        kGamePlaying = 2,
        kGameOver = 3,
    };
    GamePanel();
    // Hmx::Object
    virtual ~GamePanel();
    OBJ_CLASSNAME(GamePanel);
    OBJ_SET_TYPE(GamePanel);
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);
    virtual void SetTypeDef(DataArray *);
    // UIPanel
    virtual void Enter();
    virtual void Exit();
    virtual void Poll();
    virtual void SetPaused(bool);
    virtual void FinishLoad();

    NEW_OBJ(GamePanel)

    void SetGameOver(bool);
    bool IsPastStreamJumpPointOfNoReturn();
    void ResetLimbFeedback();
    void SetLimbFeedbackVisible(bool);
    FitnessFilter *GetFitnessFilter(int);
    void ResetJitter();
    float DeJitter(float);

    DataNode OnGetFitnessData(const DataArray *);
    bool IsGameOver() const { return mState == kGameOver; }
    bool Unkf8() const { return unkf8; }

private:
    void CreateGame();
    void StartGame();
    void SetPausedHelper(bool paused, bool pauseSound);
    void CheatPause(bool);
    void Reset();
    void UpdateFitnessOverlay();
    void StartIntro();
    void SetSoundEventReceiver();
    void UpdateNowBar();
    void UpdateLatency();

    DataNode OnStartLoadSong(DataArray *);
    DataNode OnStartSongNow(DataArray *);

protected:
    virtual void Load();
    virtual bool IsLoaded() const;
    virtual void Unload();
    virtual void PollForLoading();

    void ClearDrawGlitch();
    void ReloadData();

    DataNode OnMsg(const EndGameMsg &);

    Game *mGame; // 0x38
    FitnessFilter mFitnessFilters[2]; // 0x3c
    RndOverlay *mTimeOverlay; // 0x6c
    RndOverlay *mLatencyOverlay; // 0x70
    RndOverlay *mFitnessOverlay; // 0x74
    RndOverlay *mLoopVizOverlay; // 0x78
    bool mStartPaused; // 0x7c
    State mState; // 0x80
    int mEndGameResult;
    Profiler mPerformanceProfiler;
    bool mIsReplay; // 0xd8
    std::vector<float> mFrameTimeSamples;
    int mJitterBufferIndex;
    int mJitterSampleCount;
    float mCurrentJitterValue;
    float unkf4;
    bool unkf8;
    Timer *mPauseCountInTimer;
    bool mNormalPauseEnabled;
    bool mCheatPaused;
    int mPollLoadState;
    bool mSoundEventReceiverSet;
};

extern GamePanel *TheGamePanel;

class LatencyCallback : public RndOverlay::Callback {
public:
    LatencyCallback() : unk4(0) {}
    virtual ~LatencyCallback() {}
    virtual float UpdateOverlay(RndOverlay *o, float y);

    friend class GamePanel;

private:
    bool unk4;
};

class LoopVizCallback : public RndOverlay::Callback {
public:
    LoopVizCallback();
    virtual ~LoopVizCallback() {}
    virtual float UpdateOverlay(RndOverlay *o, float y);

    void DrawHashMarks(float, float, float, int, int, bool);

private:
    DebugMeter mDebugMeter1; // 0x4
    DebugMeter mDebugMeter2; // 0x24
    int mCurrLoopStart;
    int mCurrLoopEnd;
    int mPrevLoopStart;
    int mPrevLoopEnd;
    float mLoopStartChangeTimer;
    float mLoopEndChangeTimer;
};
