#include "movie/Splash.h"
#include "Splash.h"
#include "obj/Object.h"
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
      mCurrentMovie(0), mCurrentTrigger(0), unk58(-1), unk60(0), mThreaded(1), mThreadId(-1), mState(0) {}

Splash::~Splash() { MILO_ASSERT(!gSplashing, 0x57); }

void Splash::SetWaitForSplash(bool b) {
    MILO_ASSERT(!gSplashing, 0x16e);
    mWaitForSplash = b;
}

void Splash::Suspend() {
    MILO_ASSERT(MainThread(), 0xcf);
    if (++unk60 <= 1) {
        if (mThreaded) {
            if (SetMutableState(SplashState::s1)) {
                WaitForState(SplashState::s2);
                TheNgRnd.Suspend();
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(true);
                    mCurrentMovie->GetMovie().LockThread();
                }
                *(u8 *)&unk5c = 0;
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
            SetMutableState(SplashState::s2);
        }

        unk200.Reset();
    }
}

void Splash::Resume() {
    MILO_ASSERT(MainThread(), 0x257);
    if (--unk60 <= 0) {
        MILO_ASSERT(unk60 == 0, 0x264);
        if (mThreaded != 0) {
            // Threaded mode: resume rendering and signal render thread
            if (SetMutableState(SplashState::s3)) {
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(false);
                    mCurrentMovie->GetMovie().UnlockThread();
                }
                MILO_ASSERT(SetMutableState(SplashState::kResuming), 0x279);
                WaitForState(SplashState::kResumed);
            } else {
                MILO_ASSERT(mState == SplashState::kWaitingForTerminating, 0x285);
                if (mCurrentMovie != NULL) {
                    mCurrentMovie->SetShowing(false);
                    mCurrentMovie->GetMovie().UnlockThread();
                }
            }
        } else {
            // Non-threaded mode: resume drawing immediately
            if (SetMutableState(SplashState::kResumed) == 0)
                return;
            unk5c = 0;
            Draw();
        }
    }
}

void Splash::AddScreen(char const *c, int i) {
    MILO_ASSERT(!gSplashing, 0x175);
    ScreenParams sp;
    sp.fname = c;
    sp.msecs = i;
    CritSecTracker tracker(&unk98);
    mScreens.push_back(sp);
}

bool Splash::PrepareNext() {
    CritSecTracker tracker(&unk98);
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
    CritSecTracker tracker2(&unk98);
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
        for (std::list<RndDir *>::iterator it = unkc0.begin(); it != unkc0.end(); ++it) {
            delete *it;
        }
        Movie::Validate();
        MemFree(mThreadStack, __FILE__, __LINE__, "");
    }
}

void Splash::Poll() {
    static bool finished;
    if (!mThreaded || unk60) {
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
    ((Rnd *)&TheRnd)->unk1b4 = 1;
}

void Splash::Draw() {}

bool Splash::SetMutableState(Splash::SplashState state) {
    MILO_ASSERT(state <= kResumed, 0x13b);
    CritSecTracker tracker(&unk6c);
    // Only allow transition if we're in a mutable state
    if (mState <= kResumed) {
        mState = state;
        // Signal appropriate event for main or worker thread
        MainThread() ? unk90.Set() : unk8c.Set();
        return true;
    }
    else {
        return false;
    }
}

bool Splash::SetImmutableState(Splash::SplashState state) {
    MILO_ASSERT(state > kResumed, 0x150);
    CritSecTracker tracker(&unk6c);
    // Only allow transition to terminal states in specific sequences
    if (mState < kResumed || state <= mState) {
        // Allow WaitingForTerminating -> kTerminating transition
        if (state != kWaitingForTerminating || mState != kTerminating) {
            return false;
        }
    }
    else {
        mState = state;
        MainThread() ? unk90.Set() : unk8c.Set();
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
        MainThread() ? unk8c.Wait(-1) : unk90.Wait(-1);
    }
}

void Splash::CheckWorkerSuspend(bool) {}

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
        unkc0.push_back(mCurrentDir);
        mCurrentDir = NULL;
    }
    mCurrentCam = 0;
    mCurrentTrigger = 0;
    CritSecTracker tracker(&unk98);

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
        // If only one screen remains, signal that we're done
        if (num == 1U) {
            return true;
        }
    }

    // Display the next screen
    mPreparedScreens.clear();
    return Show();
}

bool Splash::Show() {
    if (&unk98) {
        unk98.Enter();
    }
    MILO_ASSERT(!mPreparedScreens.empty(), 0x283);
    if (&unk98) {
        unk98.Exit();
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
    unk5c = 0;
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
    unk6c.Enter();
    MILO_ASSERT(mState == kResuming, 0x221);
    mState = kResumed;
    unk8c.Set();
    unk6c.Exit();

    mTimer.Start();

    // Initialize time reference for performance monitoring
    if (mTimer.SplitMs() == 0) {
        unk60 = __mftb();
    }

    Show();

    while (UpdateThreadLoop()) {
        CheckWorkerSuspend(true);
    }

    MILO_ASSERT(mPreparedScreens.empty(), 0x23a);

    for (int i = 0; i < 2; i++) {
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();
    }

    if (!SetImmutableState(kTerminating)) {
        while (mState != s1) {
            CheckWorkerSuspend(false);
        }
        SetImmutableState(kTerminating);
    }

    TheNgRnd.Suspend();

    float elapsed = mTimer.SplitMs();
    if (TheArchive && Archive::DebugArkOrder()) {
        TheDebug << MakeString("Splash Time: %f", elapsed);
    }

    WaitForState(kWaitingForTerminating);

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