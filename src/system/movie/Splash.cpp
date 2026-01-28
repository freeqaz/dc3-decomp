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
    : unk8(SystemConfig("ui")->FindArray("splash_time")->Float(1) * 1000),
      unkc(SystemConfig("ui")->FindArray("wait_for_splash")->Int(1)), unk48(0), unk4c(0),
      unk50(0), unk54(0), unk58(-1), unk60(0), unk64(1), unk68(-1), mState(0) {}

Splash::~Splash() { MILO_ASSERT(!gSplashing, 0x57); }

void Splash::SetWaitForSplash(bool b) {
    MILO_ASSERT(!gSplashing, 0x16e);
    unkc = b;
}

void Splash::Suspend() {
    MILO_ASSERT(MainThread(), 0xcf);
    unk60 += 1;
    if (unk60 < 2) {
        if (!unk64) {
            SetMutableState(SplashState::s2);
        }
        else {
            bool b = SetMutableState(SplashState::s1);
            if (b) {
                WaitForState(SplashState::s2);
                TheNgRnd.Suspend();
            }
        }
    }
}

void Splash::Resume() {
    MILO_ASSERT(MainThread(), 0x257);
    if (--unk60 <= 0) {
        MILO_ASSERT(unk60 == 0, 0x264);
        if (unk64 != 0) {
            if (SetMutableState(SplashState::s3)) {
                if (unk50 != NULL) {
                    unk50->SetShowing(false);
                    unk50->GetMovie().UnlockThread();
                }
                MILO_ASSERT(SetMutableState(SplashState::kResuming), 0x279);
                WaitForState(SplashState::kResumed);
            } else {
                MILO_ASSERT(mState == SplashState::kWaitingForTerminating, 0x285);
                if (unk50 != NULL) {
                    unk50->SetShowing(false);
                    unk50->GetMovie().UnlockThread();
                }
            }
        } else {
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
    //CriticalSection *cs = &unk98;
    CritSecTracker tracker(&unk98);
    if (mScreens.empty()) {
        return false;
    }
    else {
        auto local58 = mScreens.back().fname;
        FilePath fp = local58;
        auto loadedObj = DirLoader::LoadObjects(fp, 0, 0);
        RndDir *rndDir = dynamic_cast<RndDir *>(loadedObj);
        if (!rndDir) {
            MILO_FAIL("Missing file %s", local58);
        }
        auto splashMovie = rndDir->Find<TexMovie>(kSplashMovie, false);
        if (splashMovie) {
            splashMovie->GetMovie().CheckOpen(false);
        }
        CritSecTracker tracker2(&unk98);
        PreparedScreenParams psp = {rndDir};
        mPreparedScreens.push_back(psp);
        mScreens.clear();
        return true;
    }
}

void Splash::PrepareRemaining() {
    for (bool b = PrepareNext(); b; b = PrepareNext()) {}
}

void Splash::EndSplasher() {
    if (TheSplasher) {
        if (unk64) {
            MILO_ASSERT(mScreens.empty(), 0xa6);
            MILO_ASSERT(gSplashing, 0xa7);
            MILO_ASSERT(SetImmutableState(kTerminating), 0xa9);
            WaitForState(kTerminated);
            gSplashing = false;
        } else {
            while (ShowNext())
                ;
            MILO_ASSERT(SetImmutableState(kTerminated), 0xb6);
        }
        TheSplasher = NULL;
        SetRndSplasherCallback(0, 0, 0);
        *(bool *)((char *)&TheRnd + 0x1b4) = false;
        for (std::list<RndDir *>::iterator it = unkc0.begin(); it != unkc0.end(); ++it) {
            delete *it;
        }
        Movie::Validate();
        MemFree(mThreadStack, __FILE__, __LINE__, "");
    }
}

void Splash::Poll() {
    static bool finished;
    if (!unk64 || unk60) {
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
    if (unk64) {
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
    if (mState <= kResumed) {
        mState = state;
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
    if (mState < kResumed || state <= mState) {
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
    if (unk64 == 0) {
        MILO_FAIL("Can\'t WaitForState");
    }
    while (mState != state && (state != kResumed || mState <= kResumed)) {
        MainThread() ? unk8c.Wait(-1) : unk90.Wait(-1);
    }
}

void Splash::CheckWorkerSuspend(bool) {}

bool Splash::ShowNext() {
    if (unk50) {
        unk50->SetShowing(false);
        unk50->GetMovie().SetPaused(true);
        unk50 = nullptr;
    }
    if (unk48) {
        unk48->Exit();
        unkc0.push_back(unk48);
        unk48 = nullptr;
    }
    unk4c = 0;
    unk54 = 0;
    CritSecTracker tracker(&unk98);
    FOREACH(it, mPreparedScreens) {
        // not really sure whats going on here
    }
    mPreparedScreens.clear();
    return Show();
}

bool Splash::Show() {
    CritSecTracker tracker(&unk98);
    MILO_ASSERT(!mPreparedScreens.empty(), 0x283);
    tracker.mCritSec->Exit();
    auto rndDir = mPreparedScreens.end()->unk0;
    rndDir->Exit();
    unk4c = unk48->Find<RndCam>(kSplashCam, true);
    unk50 = unk48->Find<TexMovie>(kSplashMovie, true);
    if (!unk50) {
        unk8 = mPreparedScreens.end()->unk4;
    }
    else {
        if (!unk64) {
            return ShowNext();
        }
        unk50->SetShowing(true);
        unk50->GetMovie().SetPaused(false);
        unk8 = ceil(unk50->GetMovie().MsPerFrame() * unk50->GetMovie().NumFrames());
    }
    unk54 = unk48->Find<EventTrigger>("splash.trig", false);
    if (unk54) {
        unk54->Trigger();
    }
    unk18.Restart();
    unk5c = false;
    return true;
}

bool Splash::UpdateThreadLoop() {
    if (unk18.SplitMs() <= unk8 || ShowNext()) {
        Draw();
        if (mState != kTerminating || unkc) {
            return true;
        }
        for (bool b = ShowNext(); b; b = ShowNext()) {}
    }
    return false;
}

void Splash::UpdateThread() {
    unk68 = GetCurrentThreadId();
    MILO_ASSERT(!MainThread(), 0x21d);
    unk6c.Enter();
    MILO_ASSERT(mState == kResuming, 0x221);
    mState = kResumed;
    unk8c.Set();
    unk6c.Exit();

    unk18.Start();

    if (unk18.SplitMs() == 0) {
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

    float elapsed = unk18.SplitMs();
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