// Behaviour pins for three functions whose PPC listings were read by hand
// (lane w-five-clean, 2026-11-13).  Each test pins a value-level property the
// target listing shows -- which object a field is copied from, which argument
// goes to which parameter, which branch returns what -- because objdiff cannot
// see an argument SWAP or a copy from the wrong owner when the shape matches.
//
//   RndMatAnim::Copy   (rndobj/MatAnim.cpp)
//     ty == kCopyShallow, or kCopyFromMax with a source whose keys live in
//     another MatAnim  -> share the source's keys owner.
//     otherwise         -> become our own keys owner and copy the six key
//     vectors from the SOURCE'S OWNER (`lwz r11, 0x30(r30)` re-read before each
//     vector assignment), not from the source's own (possibly empty) vectors.
//   JoypadClient::Poll (os/JoypadClient.cpp)
//     guide showing -> reset both timers of all four repeats, send nothing.
//     otherwise     -> JoypadRepeat::Poll(mHoldMs, mRepeatMs, mSink, pad):
//     f1 = mHoldMs (0x38), f2 = mRepeatMs (0x3c).
//   CamShot::GetCam    (world/CameraShot.cpp)
//     the camera of GetCrowdDir() (mParentDir if set, else Dir(), as a
//     WorldDir), or null when there is no WorldDir or it has no camera.

#include <cstring>

// PlatformMgr::mGuideShowing has no setter; the guide path is only reachable
// by flipping it (same idiom as test_threadtask_replace.cpp).
#define private public
#include "os/PlatformMgr.h"
#undef private

#include "test_helpers.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/JoypadClient.h"
#include "os/JoypadMsgs.h"
#include "rndobj/Cam.h"
#include "rndobj/MatAnim.h"
#include "hamobj/HamCamShot.h"
#include "world/CameraShot.h"
#include "world/Dir.h"

namespace {
    // A MatAnim whose keys owner the test can point somewhere else, as a
    // loaded "keys_owner" reference would.
    class MatAnimProbe : public RndMatAnim {
    public:
        MatAnimProbe() {}
        void SetKeysOwner(RndMatAnim *owner) { mKeysOwner = owner; }
    };

    // Records every button_down it is sent.
    class SinkProbe : public Hmx::Object {
    public:
        int mDowns = 0;
        int mOther = 0;
        virtual DataNode Handle(DataArray *msg, bool) {
            if (msg->Sym(1) == ButtonDownMsg::Type())
                mDowns++;
            else
                mOther++;
            return DataNode(0);
        }
    };

    struct GuideShowing {
        bool mOld;
        explicit GuideShowing(bool on) : mOld(ThePlatformMgr.mGuideShowing) {
            ThePlatformMgr.mGuideShowing = on;
        }
        ~GuideShowing() { ThePlatformMgr.mGuideShowing = mOld; }
    };

    const Hmx::Color kRed(1, 0, 0, 1);
    const Hmx::Color kBlue(0, 0, 1, 1);
}

class CopyPollSemanticsTest : public EngineTestFixture {};

// --- RndMatAnim::Copy --------------------------------------------------------

TEST_F(CopyPollSemanticsTest, MatAnimDeepCopyOwnsKeysCopiedFromSourceOwner) {
    // src keeps one red key in its OWN vectors but delegates to `owner`,
    // which holds two blue keys.  A deep copy must take the owner's keys.
    MatAnimProbe *src = new MatAnimProbe();
    RndMatAnim *owner = Hmx::Object::New<RndMatAnim>();
    RndMatAnim *dst = Hmx::Object::New<RndMatAnim>();
    src->ColorKeys().Add(kRed, 0, false); // src owns itself at this point
    src->SetKeysOwner(owner);
    owner->ColorKeys().Add(kBlue, 0, false);
    owner->ColorKeys().Add(kBlue, 10, false);
    owner->AlphaKeys().Add(0.5f, 3, false);

    dst->Copy(src, Hmx::Object::kCopyDeep);

    EXPECT_EQ(dst->KeysOwner(), dst) << "deep copy must make dst its own keys owner";
    ASSERT_EQ(dst->ColorKeys().NumKeys(), 2)
        << "deep copy must copy the keys of src's OWNER, not src's own vectors";
    EXPECT_EQ(dst->ColorKeys()[0].value.blue, 1.0f);
    EXPECT_EQ(dst->ColorKeys()[1].frame, 10.0f);
    EXPECT_EQ(dst->AlphaKeys().NumKeys(), 1);
    // A copy, not an alias: editing dst leaves the owner alone.
    dst->ColorKeys().Add(kRed, 20, false);
    EXPECT_EQ(owner->ColorKeys().NumKeys(), 2);

    delete dst;
    delete src;
    delete owner;
}

TEST_F(CopyPollSemanticsTest, MatAnimShallowCopySharesSourceOwner) {
    RndMatAnim *src = Hmx::Object::New<RndMatAnim>();
    RndMatAnim *dst = Hmx::Object::New<RndMatAnim>();
    src->ColorKeys().Add(kRed, 0, false);

    dst->Copy(src, Hmx::Object::kCopyShallow);

    EXPECT_EQ(dst->KeysOwner(), src);
    EXPECT_EQ(dst->ColorKeys().NumKeys(), 1); // read through the shared owner

    delete dst;
    delete src;
}

TEST_F(CopyPollSemanticsTest, MatAnimFromMaxSharesOnlyAForeignOwner) {
    // FromMax with a self-owned source: deep path (dst owns its keys).
    RndMatAnim *selfOwned = Hmx::Object::New<RndMatAnim>();
    selfOwned->ColorKeys().Add(kRed, 0, false);
    RndMatAnim *dst1 = Hmx::Object::New<RndMatAnim>();
    dst1->Copy(selfOwned, Hmx::Object::kCopyFromMax);
    EXPECT_EQ(dst1->KeysOwner(), dst1);
    EXPECT_EQ(dst1->ColorKeys().NumKeys(), 1);

    // FromMax with a source that delegates: share that delegate.
    MatAnimProbe *delegating = new MatAnimProbe();
    delegating->SetKeysOwner(selfOwned);
    RndMatAnim *dst2 = Hmx::Object::New<RndMatAnim>();
    dst2->Copy(delegating, Hmx::Object::kCopyFromMax);
    EXPECT_EQ(dst2->KeysOwner(), selfOwned);

    delete dst2;
    delete delegating;
    delete dst1;
    delete selfOwned;
}

// --- JoypadClient::Poll ------------------------------------------------------

static void StartAllRepeats(JoypadClient &client) {
    for (int pad = 0; pad < 4; pad++) {
        client.mRepeats[pad].Start(kPad_Xbox_A, kAction_Confirm, pad);
    }
}

TEST_F(CopyPollSemanticsTest, JoypadPollWithGuideShowingResetsEveryRepeat) {
    SinkProbe sink;
    JoypadClient client(&sink);
    StartAllRepeats(client);
    for (int pad = 0; pad < 4; pad++) {
        client.mRepeats[pad].mRepeatTimer.Start();
        ASSERT_TRUE(client.mRepeats[pad].mHoldTimer.Running());
        ASSERT_TRUE(client.mRepeats[pad].mRepeatTimer.Running());
    }
    client.mHoldMs = -1; // would fire a repeat on every pad if Poll ran
    client.mRepeatMs = -1;
    {
        GuideShowing guide(true);
        client.PollClient();
    }
    for (int pad = 0; pad < 4; pad++) {
        EXPECT_FALSE(client.mRepeats[pad].mHoldTimer.Running()) << "pad " << pad;
        EXPECT_FALSE(client.mRepeats[pad].mRepeatTimer.Running()) << "pad " << pad;
    }
    EXPECT_EQ(sink.mDowns, 0) << "nothing may repeat while the guide is up";
}

TEST_F(CopyPollSemanticsTest, JoypadPollPassesHoldThenRepeatThreshold) {
    // Hold threshold unreachable, repeat threshold already met: only the
    // repeat branch of JoypadRepeat::Poll may fire, which leaves the hold
    // timer running.  With the two thresholds swapped the hold branch fires
    // instead and resets the hold timer.
    SinkProbe sink;
    JoypadClient client(&sink);
    StartAllRepeats(client);
    client.mHoldMs = 1.0e9f;
    client.mRepeatMs = -1;
    {
        GuideShowing guide(false);
        client.PollClient();
    }
    EXPECT_EQ(sink.mDowns, 4) << "one repeat per pad";
    for (int pad = 0; pad < 4; pad++) {
        EXPECT_TRUE(client.mRepeats[pad].mHoldTimer.Running()) << "pad " << pad;
        EXPECT_TRUE(client.mRepeats[pad].mRepeatTimer.Running()) << "pad " << pad;
    }

    // And the mirror: hold threshold met -> the hold branch resets the hold timer.
    StartAllRepeats(client);
    client.mHoldMs = -1;
    client.mRepeatMs = 1.0e9f;
    {
        GuideShowing guide(false);
        client.PollClient();
    }
    EXPECT_EQ(sink.mDowns, 8);
    for (int pad = 0; pad < 4; pad++) {
        EXPECT_FALSE(client.mRepeats[pad].mHoldTimer.Running()) << "pad " << pad;
        EXPECT_TRUE(client.mRepeats[pad].mRepeatTimer.Running()) << "pad " << pad;
    }
}

// --- CamShot::GetCam ---------------------------------------------------------

TEST_F(CopyPollSemanticsTest, CamShotGetCamReturnsCrowdDirCamera) {
    WorldDir *world = Hmx::Object::New<WorldDir>();
    world->SetName("cps_world", ObjectDir::Main());
    HamCamShot *shot = Hmx::Object::New<HamCamShot>();
    shot->SetName("cps.shot", world);
    ASSERT_EQ(shot->GetCrowdDir(), world);

    EXPECT_EQ(shot->GetCam(), nullptr) << "a WorldDir without a camera yields null";

    RndCam *cam = Hmx::Object::New<RndCam>();
    world->SetCam(cam);
    EXPECT_EQ(shot->GetCam(), cam);

    // mParentDir overrides Dir(): a shot living in a plain ObjectDir finds the
    // camera through its parent WorldDir, and none without it.
    ObjectDir *plain = Hmx::Object::New<ObjectDir>();
    plain->SetName("cps_plain", ObjectDir::Main());
    HamCamShot *loose = Hmx::Object::New<HamCamShot>();
    loose->SetName("cps_loose.shot", plain);
    EXPECT_EQ(loose->GetCam(), nullptr) << "no WorldDir -> null";
    loose->SetParent(world);
    EXPECT_EQ(loose->GetCam(), cam);

    delete loose;
    delete plain;
    delete shot;
    delete cam;
    delete world;
}
