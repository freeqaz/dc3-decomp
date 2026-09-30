// Native-ADDITION regression tests: `#ifdef HX_NATIVE` blocks with no `#else`.
//
// A (b) ADDS region in scripts/analysis/native_shadow_audit.py is code the
// native build runs IN ADDITION to the decompiled body, so objdiff -- which
// measures only the PPC build -- cannot see it at all. Each test here pins the
// image's behaviour at a place where a native-only addition used to diverge,
// cites the decisive instructions of the target listing
// (build/373307D9/asm/**), and was watched FAILING against the pre-fix native
// body before the fix landed (see the commit that introduced it).

#include "test_helpers.h"

#include "platform/StreamReceiver_Native.h"
#include "synth/StandardStream.h"
#include "synth/StreamReader.h"
#include "audio/AudioDevice.h"
#include "synth/Stream.h"
#include "rndobj/Cam.h"
#include "rndobj/Trans.h"
#include "world/CameraShot.h"
#include "ui/UI.h"
#include "ui/UIScreen.h"
#include "obj/Msg.h"

#include <chrono>
#include <thread>

namespace {

// ---------------------------------------------------------------------------
// StandardStream::UpdateTime.
//
// Image (?UpdateTime@StandardStream@@UAAXXZ, 8276FD80): GetRawTime (8276FDC8
// bctrl) straight into the quantized drift correction of mTimer; mTimer is a
// VarTimer that Stop() pauses and Init() resets. There is no other clock.
//
// The native addition ran a SECOND, independent wall clock (mWallClock),
// started on the first kPlaying poll and never paused, never reset -- not by
// Stop(), not by Init()/Resync() -- and, whenever that clock read > 500 ms
// while the audio clock had advanced < 10% of it, switched the stream to
// "timer fallback" for good and set song time to the wall clock. It was meant
// for headless runs with no audio device; it fired on every pause longer than
// ~9x the time played so far, and on every Resync, with a real device too: the
// song time jumped forward by the whole pause and drift correction stayed off.
// ---------------------------------------------------------------------------

// A reader that reads nothing: lets Play()'s native prefill pump run.
class IdleReader : public StreamReader {
public:
    void Poll(float) override {}
    void Seek(int) override {}
    void EnableReads(bool) override {}
    bool Done() override { return false; }
    bool Fail() override { return false; }
    void Init() override {}
};

class TimeProbeStream : public StandardStream {
public:
    TimeProbeStream()
        : StandardStream(new NullFile(), 0.0f, 1.0f, "none", false, false, false) {
        InitInfo(1, 44100, false, -1);
        mRdr = new IdleReader(); // "none" builds no reader; ~StandardStream deletes mRdr
        mState = kReady;         // buffered: the image's Play() precondition
    }
};

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

class NativeAdditionsStreamTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mSavedFactory = StreamReceiver::sFactory;
        StreamReceiver::sFactory = StreamReceiverNative::Create;
    }
    void TearDown() override { StreamReceiver::sFactory = mSavedFactory; }
    StreamReceiverFactoryFunc *mSavedFactory = nullptr;
};

TEST_F(NativeAdditionsStreamTest, PauseDoesNotAdvanceSongTime) {
    TimeProbeStream s;
    s.Play();
    s.UpdateTime();
    const float atStart = s.GetTime() - StandardStream::sAudioOffsetMs;
    s.Stop();
    SleepMs(700); // paused: the image's mTimer is stopped, so song time holds
    s.Play();
    s.UpdateTime();
    const float afterPause = s.GetTime() - StandardStream::sAudioOffsetMs;
    EXPECT_LT(afterPause - atStart, 150.0f)
        << "700 ms of PAUSE was added to song time (" << atStart << " -> "
        << afterPause << " ms); the image's UpdateTime only reads mTimer, which "
           "Stop() paused";
}

} // namespace

// Control: what the wall-clock block existed for still works. With no audio
// device nothing renders the receivers, so the audio clock never moves; the
// song must still advance, on mTimer.
TEST_F(NativeAdditionsStreamTest, NoAudioDevicePlaybackStillAdvances) {
    ASSERT_FALSE(AudioDevice::GetInstance().IsInitialized())
        << "precondition: milo-tests runs with no audio device";
    TimeProbeStream s;
    s.Play();
    SleepMs(300);
    s.UpdateTime();
    const float t = s.GetTime() - StandardStream::sAudioOffsetMs;
    EXPECT_GT(t, 250.0f) << "headless playback froze: song time " << t << " ms after 300 ms";
    EXPECT_LT(t, 1000.0f);
}


// ---------------------------------------------------------------------------
// CamShotFrame::BuildTransform.
//
// Image (?BuildTransform@CamShotFrame@@QBAXPAVRndCam@@AAVTransform@@_N@Z):
// 82812D28 bl GetCurrentTargetPosition -> 82812D38 bl WorldToScreen -> the
// filter math (82812D74, 82812DA0 fsel) -> the path / mWorldOffset pick -> the
// mParent block (live parent transform, filter, clamp height) -> Multiply by
// the shot's world -> the dynamic offsets and ApplyScreenOffset. There is no
// early exit anywhere.
//
// A native-only block returned `mWorldOffset * shot world` for every frame
// with no targets, skipping the path, the whole mParent block and the dynamic
// offsets -- so a targetless shot parented to a moving object (or driven by a
// path) froze in place. Its stated reason (a NaN from WorldToScreen on a zero
// target) is covered by the NaN sanitizer that follows WorldToScreen.
// ---------------------------------------------------------------------------

class NativeAdditionsCamShotTest : public EngineTestFixture {};

TEST_F(NativeAdditionsCamShotTest, TargetlessFrameFollowsItsParent) {
    CamShot *shot = Hmx::Object::New<CamShot>();
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 0.6f, 1.0f);
    RndTransformable *parent = Hmx::Object::New<RndTransformable>();
    parent->SetLocalPos(Vector3(100.0f, 0.0f, 0.0f));
    {
        CamShotFrame frame(shot);
        frame.mParent = parent;
        ASSERT_FALSE(frame.HasTargets());
        // CamShot::SetFrame calls UpdateTarget() on the keys it is between
        // before BuildTransform: it latches mTargetXfm = parent world.
        frame.UpdateTarget();
        Transform tf;
        frame.BuildTransform(cam, tf, false);
        EXPECT_NEAR(tf.v.x, 100.0f, 1.0e-3f)
            << "a targetless keyframe parented to a trans at x=100 must sit at the "
               "parent (image: tf = mWorldOffset, then tf.v += parent world.v); "
               "the native early-out skipped the parent block";
    }
    delete parent;
    delete cam;
    delete shot;
}

// ---------------------------------------------------------------------------
// UIManager message dispatch after a screen transition.
//
// Image, ?Handle@UIManager@@ (UI.s): BlockHandlerDuringTransition (8277FB58),
// then mSink (8277FB78 lwz r4,0x50(r31)) -- a member only the `set_sink`
// handler writes, and no shipped DTA calls it -- then the C++ handlers, the
// `ui` typedef (82780694) and, LAST, mCurrentScreen (827806D0 lwz r4,0x48).
// UIManager::Poll never stores to +0x50.
//
// A native-only line in UIManager::Poll set mSink = the entering screen on
// every transition ("DTA set_sink never fires"), so the screen saw every
// message BEFORE the ui typedef and the C++ handlers, and anything it left
// unhandled was delivered to it a second time by the trailing mCurrentScreen
// dispatch -- which was always there, so button input never needed it.
// ---------------------------------------------------------------------------

namespace {

class CountingScreen : public UIScreen {
public:
    DataNode Handle(DataArray *msg, bool warn) override {
        if (msg->Size() > 1 && msg->Type(1) == kDataSymbol && msg->Sym(1) == "na_probe")
            mProbes++;
        return UIScreen::Handle(msg, warn);
    }
    int mProbes = 0;
};

class NativeAdditionsUITest : public EngineTestFixture {};

} // namespace

TEST_F(NativeAdditionsUITest, TransitionDoesNotMakeTheScreenTheSink) {
    UIManager ui;
    UIManager *savedUI = TheUI;
    TheUI = &ui;
    CountingScreen *scr = new CountingScreen();
    ui.GotoScreen(scr, false, false);
    for (int i = 0; i < 16 && (ui.InTransition() || ui.CurrentScreen() != scr); i++)
        ui.Poll();
    ASSERT_EQ(ui.CurrentScreen(), scr) << "precondition: the transition completed";
    ASSERT_FALSE(ui.InTransition());

    static Message probe("na_probe");
    ui.Handle(probe, false);
    EXPECT_EQ(scr->mProbes, 1)
        << "a message the screen leaves unhandled reached it " << scr->mProbes
        << " times: the image routes to mCurrentScreen once, last; mSink is "
           "only ever set by set_sink";

    TheUI = savedUI;
    delete scr;
}
