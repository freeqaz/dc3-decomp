#include "movie/Splash.h"
#include "Splash.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Archive.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/System.h"
#include "utl/MakeString.h"
#include "rndobj/EventTrigger.h"
#include "rndobj/Movie.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/Utl.h"
#include "xdk/xapilibi/processthreadsapi.h"

bool gSplashing = false;
Splash *TheSplasher;

Splash::Splash()
    : mSplashDurationMs(SystemConfig("ui")->FindArray("splash_time")->Float(1) * 1000),
      mWaitForSplash(SystemConfig("ui")->FindArray("wait_for_splash")->Int(1)), mCurrentDir(0), mCurrentCam(0),
      mCurrentMovie(0), mCurrentTrigger(0), unk58(-1), mSuspendCount(0), mThreaded(1), mThreadId(-1), mState(0) {}

Splash::~Splash() { MILO_ASSERT(!gSplashing, 0x57); }

void Splash::SetWaitForSplash(bool b) {
    MILO_ASSERT(!gSplashing, 0x16e);
    mWaitForSplash = b;
}

void Splash::Suspend() {
    MILO_ASSERT(MainThread(), 0xcf);
    if (++mSuspendCount <= 1) {
        if (mThreaded) {
            if (SetMutableState(s1)) {
                WaitForState(s2);
                TheNgRnd.Suspend();
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(true);
                    mCurrentMovie->GetMovie().LockThread();
                }
                *(u8 *)&mHasDrawn = 0;
                Draw();
            } else {
                MILO_ASSERT(mState == kWaitingForTerminating, 0xeb);
                TheNgRnd.Suspend();
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(true);
                    mCurrentMovie->GetMovie().LockThread();
                }
            }
        } else {
            SetMutableState(s2);
        }

        mFrameTimer.Reset();
    }
}

void Splash::Resume() {
    MILO_ASSERT(MainThread(), 0x257);
    if (--mSuspendCount <= 0) {
        MILO_ASSERT(mSuspendCount == 0, 0x264);
        if (mThreaded != 0) {
            // Threaded mode: resume rendering and signal render thread
            if (SetMutableState(s3)) {
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(false);
                    mCurrentMovie->GetMovie().UnlockThread();
                }
                MILO_ASSERT(SetMutableState(kResuming), 0x279);
                WaitForState(kResumed);
            } else {
                MILO_ASSERT(mState == kWaitingForTerminating, 0x285);
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(false);
                    mCurrentMovie->GetMovie().UnlockThread();
                }
            }
        } else {
            // Non-threaded mode: resume drawing immediately
            if (SetMutableState(kResumed) == 0)
                return;
            mHasDrawn = 0;
            Draw();
        }
    }
}

void Splash::AddScreen(char const *c, int i) {
    MILO_ASSERT(!gSplashing, 0x175);
    ScreenParams sp;
    sp.fname = c;
    sp.msecs = i;
    CritSecTracker tracker(&mScreenLock);
    mScreens.push_back(sp);
}

bool Splash::PrepareNext() {
    CritSecTracker tracker(&mScreenLock);
    if (mScreens.empty()) {
        return false;
    }

    // Load and prepare the next screen from the queue
    auto fname = mScreens.back().fname;
    FilePath fp = fname;
    auto loadedObj = DirLoader::LoadObjects(fp, 0, 0);
    RndDir *rndDir = dynamic_cast<RndDir *>(loadedObj);
    if (!rndDir) {
        MILO_FAIL("Missing file %s", fname);
    }

    // Pre-check if movie exists
    auto splashMovie = rndDir->Find<TexMovie>(kSplashMovie, false);
    if (splashMovie) {
        splashMovie->GetMovie().CheckOpen(false);
    }

    // Queue the prepared screen
    CritSecTracker tracker2(&mScreenLock);
    PreparedScreenParams psp = {rndDir};
    mPreparedScreens.push_back(psp);
    mScreens.clear();
    return true;
}

void Splash::PrepareRemaining() {
    for (bool b = PrepareNext(); b; b = PrepareNext()) {}
}

void Splash::EndSplasher() {
    if (TheSplasher) {
        if (mThreaded) {
            // Threaded mode: signal termination and wait for worker thread
            MILO_ASSERT(mScreens.empty(), 0xa6);
            MILO_ASSERT(gSplashing, 0xa7);
            MILO_ASSERT(SetImmutableState(kTerminating), 0xa9);
            WaitForState(kTerminated);
            gSplashing = false;
        } else {
            // Non-threaded mode: manually process remaining screens
            while (ShowNext())
                ;
            MILO_ASSERT(SetImmutableState(kTerminated), 0xb6);
        }
        TheSplasher = NULL;
        SetRndSplasherCallback(0, 0, 0);
        *(bool *)((char *)&TheRnd + 0x1b4) = false;
        // Clean up archived screen directories
        auto _tmp3 = mOldDirs.end();
        for (std::list<RndDir *>::iterator it = mOldDirs.begin(); _tmp3 != it; ++it) {
            delete *it;
        }
        Movie::Validate();
        MemFree(mThreadStack, __FILE__, __LINE__, "");
    }
}

void Splash::Poll() {
    static bool finished;
    if (!mThreaded || mSuspendCount) {
        if (!finished) {
            if (!UpdateThreadLoop()) {
                finished = true;
                int i = 0;
                do {
                    TheRnd.BeginDrawing();
                    TheRnd.EndDrawing();
                    i++;
                } while (i != 2);
            }
        }
    }
}

void Splash::BeginSplasher() {
    if (mThreaded) {
        MILO_ASSERT(!gSplashing, 0x6B);
        gSplashing = true;
        MILO_ASSERT(!mPreparedScreens.empty(), 0x6D);

        MILO_ASSERT(SetMutableState(kResuming), 0x6F);
        HANDLE thread = CreateThread(0, 0, ThreadStart, this, 4, 0);
        XSetThreadProcessor(thread, 5);
        SetThreadPriority(thread, 1);
        ResumeThread(thread);
        WaitForState(kResumed);
    } else {
        SetMutableState(kResumed);
        Show();
        Draw();
    }
    TheSplasher = this;
    SetRndSplasherCallback(PollFunc, SuspendFunc, ResumeFunc);
    ((Rnd *)&TheRnd)->mReleaseImmediate = 1;
}

void Splash::Draw() {
    float splitMs = mTimer.SplitMs();
    if (splitMs <= (float)(long long)mSplashDurationMs) {
        if (mHasDrawn == 0 || mCurrentMovie != NULL || mCurrentTrigger != NULL) {
            if (mCurrentTrigger != NULL) {
                TheTaskMgr.Poll();
                mCurrentDir->Poll();
            }
            if (mCurrentMovie != NULL) {
                if (MainThread()) {
                    float msPerFrame = mCurrentMovie->GetMovie().MsPerFrame() - 1.0f;
                    if (mFrameTimer.Running() && mFrameTimer.SplitMs() < msPerFrame) {
                        return;
                    }
                    mFrameTimer.Restart();
                }
                if (!mCurrentMovie->GetMovie().Poll()) {
                    mSplashDurationMs = 0;
                    return;
                }
            } else {
                int i = 0;
                do {
                    TheRnd.BeginDrawing();
                    mCurrentCam->Select();
                    mCurrentDir->DrawShowing();
                    TheRnd.EndDrawing();
                    if (mCurrentMovie != NULL) goto splashing_done;
                } while (mCurrentTrigger == NULL && ++i < 2);
                if (mCurrentTrigger == NULL) {
                    TheNgRnd.Suspend();
                }
            }
splashing_done:
            mHasDrawn = 1;
        }
        if (!MainThread()) {
            int waitMs = mSplashDurationMs - (int)mTimer.SplitMs();
            if (mCurrentMovie != NULL) {
                int movieWait = (int)(mCurrentMovie->GetMovie().MsPerFrame() - 1.0f);
                if (movieWait < waitMs) waitMs = movieWait;
                if (waitMs < 0) waitMs = 0;
            } else if (mCurrentTrigger != NULL) {
                waitMs = 0x10;
            }
            mMainEvent.Wait(waitMs);
        }
    }
}

bool Splash::SetMutableState(Splash::SplashState state) {
    MILO_ASSERT(state <= kResumed, 0x13b);
    CritSecTracker tracker(&mStateLock);
    // Only allow transition if we're in a mutable state
    if (mState <= kResumed) {
        mState = state;
        // Signal appropriate event for main or worker thread
        MainThread() ? mMainEvent.Set() : mWorkerEvent.Set();
        return true;
    }
    else {
        return false;
    }
}

bool Splash::SetImmutableState(Splash::SplashState state) {
    MILO_ASSERT(state > kResumed, 0x150);
    CritSecTracker tracker(&mStateLock);
    // Only allow transition to terminal states in specific sequences
    if (mState < kResumed || state <= mState) {
        // Allow WaitingForTerminating -> kTerminating transition
        if (state != kWaitingForTerminating || mState != kTerminating) {
            return false;
        }
    }
    else {
        mState = state;
        MainThread() ? mMainEvent.Set() : mWorkerEvent.Set();
        return true;
    }
    return true;
}

void Splash::WaitForState(Splash::SplashState state) {
    // Can only wait in threaded mode
    if (mThreaded == 0) {
        MILO_FAIL("Can\'t WaitForState");
    }
    // Wait for state change, allowing intermediate states for kResumed
    while (mState != state && (state != kResumed || mState <= kResumed)) {
        MainThread() ? mWorkerEvent.Wait(-1) : mMainEvent.Wait(-1);
    }
}

void Splash::CheckWorkerSuspend(bool b) {
    MILO_ASSERT(!MainThread(), 0x1f0);
    while (mState == s1) {
        TheNgRnd.Suspend();
        if (mCurrentMovie != NULL) {
            mCurrentMovie->SetShowing(false);
            mCurrentMovie->GetMovie().UnlockThread();
        }
        {
            CritSecTracker cst(&mStateLock);
            MILO_ASSERT(mState == s1, 0x1ff);
            mState = s2;
            mWorkerEvent.Set();
        }
        WaitForState(kResuming);
        TheNgRnd.Resume();
        {
            CritSecTracker cst(&mStateLock);
            MILO_ASSERT(mState == kResuming, 0x209);
            mState = kResumed;
            mWorkerEvent.Set();
        }
        if (mCurrentMovie != NULL) {
            mCurrentMovie->SetShowing(true);
            mCurrentMovie->GetMovie().LockThread();
        }
        if (b) {
            mHasDrawn = 0;
            Draw();
        }
    }
}

bool Splash::ShowNext() {
    // Clean up previous splash screen
    if (mCurrentMovie != NULL) {
        mCurrentMovie->SetShowing(false);
        mCurrentMovie->GetMovie().SetPaused(true);
        mCurrentMovie = NULL;
    }
    // Clean up and archive previous splash directory
    if (mCurrentDir != NULL) {
        mCurrentDir->Exit();
        mOldDirs.push_back(mCurrentDir);
        mCurrentDir = NULL;
    }
    mCurrentCam = 0;
    mCurrentTrigger = 0;
    {
        CritSecTracker tracker(&mScreenLock);

        // Count prepared screens to determine if we're done
        std::list<PreparedScreenParams>::iterator begin = mPreparedScreens.begin();
        std::list<PreparedScreenParams>::iterator end = mPreparedScreens.end();
        std::list<PreparedScreenParams>::iterator node = begin;
        unsigned int num = 0;

        if (node != end) {
            do {
                ++node;
                ++num;
            } while (node != end);
            // If only one screen remains, check if more screens are queued
            if (num == 1U) {
                return !mScreens.empty();
            }
        }

        // Remove the front screen and display the next one
        mPreparedScreens.erase(mPreparedScreens.begin());
    }
    return Show();
}

bool Splash::Show() {
    if (&mScreenLock) {
        mScreenLock.Enter();
    }
    MILO_ASSERT(!mPreparedScreens.empty(), 0x283);
    if (&mScreenLock) {
        mScreenLock.Exit();
    }
    mCurrentDir = mPreparedScreens.begin()->dir;
    mCurrentDir->Enter();
    mCurrentCam = mCurrentDir->Find<RndCam>(kSplashCam, true);
    mCurrentMovie = mCurrentDir->Find<TexMovie>(kSplashMovie, true);
    if (mCurrentMovie) {
        if (mThreaded) {
            mCurrentMovie->SetShowing(true);
            mCurrentMovie->GetMovie().SetPaused(false);
            mSplashDurationMs = ceil(mCurrentMovie->GetMovie().MsPerFrame() * mCurrentMovie->GetMovie().NumFrames());
        } else {
            return ShowNext();
        }
    } else {
        mSplashDurationMs = mPreparedScreens.begin()->durationMs;
    }
    mCurrentTrigger = mCurrentDir->Find<EventTrigger>("splash.trig", false);
    if (mCurrentTrigger) {
        mCurrentTrigger->Trigger();
    }
    mTimer.Restart();
    mHasDrawn = 0;
    return true;
}

// Main loop for splash screen rendering thread. Returns false when splash sequence is complete.
bool Splash::UpdateThreadLoop() {
    if (mTimer.SplitMs() > mSplashDurationMs && !ShowNext()) {
        return false;
    }
    Draw();
    if (mState == kTerminating && !mWaitForSplash) {
        while (ShowNext()) {}
        return false;
    }
    return true;
}

void Splash::UpdateThread() {
    mThreadId = GetCurrentThreadId();
    MILO_ASSERT(!MainThread(), 0x21d);
    {
        CritSecTracker cst(&mStateLock);
        MILO_ASSERT(mState == kResuming, 0x221);
        mState = kResumed;
        mWorkerEvent.Set();
    }

    Timer timer;
    timer.Start();

    Show();

    while (UpdateThreadLoop()) {
        CheckWorkerSuspend(true);
    }

    MILO_ASSERT(mScreens.empty(), 0x23a);

    for (int i = 0; i < 2; i++) {
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
    }

    if (!SetImmutableState(kWaitingForTerminating)) {
        do {
            MILO_ASSERT(mState == s1, 0x246);
            CheckWorkerSuspend(false);
        } while (!SetImmutableState(kWaitingForTerminating));
    }

    TheNgRnd.Suspend();

    float elapsed = timer.SplitMs();
    if (TheArchive && Archive::DebugArkOrder()) {
        TheDebug << MakeString("Splash Time: %f\n", elapsed);
    }

    WaitForState(kTerminating);

    MILO_ASSERT(SetImmutableState(kTerminated), 0x257);
}

unsigned long Splash::ThreadStart(void *v) {
    static_cast<Splash *>(v)->UpdateThread();
    return 0;
}

void SuspendFunc() {
    TheSplasher->Suspend();
}

void ResumeFunc() {
    TheSplasher->Resume();
}

void PollFunc() { TheSplasher->Poll(); }