#pragma once
#include "movie/TexMovie.h"
#include "os/CritSec.h"
#include "os/SynchronizationEvent.h"
#include "rndobj/Dir.h"
#include "rndobj/EventTrigger.h"

class Splash {
public:
    enum SplashState {
        s0,
        s1,
        s2,
        s3,
        kResuming,
        kResumed,
        kWaitingForTerminating,
        kTerminating,
        kTerminated
    };

    struct ScreenParams {
        const char *fname;
        int msecs;
    };

    struct PreparedScreenParams {
        RndDir *dir;
        int durationMs;
    };
    virtual ~Splash();

    Splash();
    void SetWaitForSplash(bool);
    void Suspend();
    void Resume();
    void AddScreen(char const *, int);
    bool PrepareNext();
    void PrepareRemaining();
    void EndSplasher();
    void Poll();
    void BeginSplasher();
    DWORD SplashThreadId() const { return mThreadId; }

    int mSplashDurationMs;
    bool mWaitForSplash;
    std::list<ScreenParams> mScreens; // 0x10
    Timer mTimer;
    RndDir *mCurrentDir;
    RndCam *mCurrentCam;
    TexMovie *mCurrentMovie;
    EventTrigger *mCurrentTrigger;
    int unk58;
    u32 mHasDrawn;
    int mSuspendCount;
    bool mThreaded;
    DWORD mThreadId;
    CriticalSection mStateLock;
    SynchronizationEvent mWorkerEvent;
    SynchronizationEvent mMainEvent;
    int mState; // 0x94
    CriticalSection mScreenLock;
    std::list<PreparedScreenParams> mPreparedScreens;
    std::list<RndDir *> mOldDirs;
    Timer mFrameTimer;
    void *mThreadStack;

protected:
    virtual void Draw();

    bool SetMutableState(Splash::SplashState);
    bool SetImmutableState(Splash::SplashState);
    void WaitForState(Splash::SplashState);
    void CheckWorkerSuspend(bool);
    bool ShowNext();
    bool Show();
    bool UpdateThreadLoop();
    void UpdateThread();
    static unsigned long ThreadStart(void *);
};

extern Splash *TheSplasher;

void SuspendFunc();
void ResumeFunc();
void PollFunc();

const char *kSplashMovie = "s_splash.tmov";
const char *kSplashCam = "s_splash.cam";