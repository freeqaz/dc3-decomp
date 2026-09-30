// Native-only (HX_NATIVE) engine code that diverged from the Xbox image, pinned
// one lead per test.  Branch `native-engineleads`, the engine leads left open by
// the (b) ADDS triage in docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
#include "test_helpers.h"

#include "game/GameMode.h"
#include "hamobj/HamGameData.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Anim.h"
#include "rndobj/Dir.h"
#include "rndobj/Poll.h"

namespace {

// ----------------------------------------------------------------------------
// AnimTask::Poll
// ----------------------------------------------------------------------------

// An animatable whose anim target is a separate object -- the TransAnim /
// MatAnim / CamAnim shape, where two different anims drive one target and
// AnimTask's ctor finds the task to blend out of through the TARGET's refs.
class ProbeTargetAnim : public RndAnimatable {
public:
    Hmx::Object *mTarget;
    explicit ProbeTargetAnim(Hmx::Object *target) : mTarget(target) {}
    virtual Hmx::Object *AnimTarget() { return mTarget; }
    virtual void SetFrame(float, float) {}
};

// Starts `next` on the shared target from the `ended` callback, the way a flow
// or a DTA handler chains one anim onto the end of another.
class ChainOnEnded : public Hmx::Object {
public:
    RndAnimatable *mNext = nullptr;
    AnimTask *mStarted = nullptr;
    int mEnded = 0;
    virtual DataNode Handle(DataArray *msg, bool warn) {
        if (msg->Size() > 2 && msg->Type(2) == kDataSymbol
            && msg->Sym(2) == Symbol("ended")) {
            mEnded++;
            if (mNext && !mStarted) {
                mStarted = new AnimTask(
                    mNext, 0.0f, 10.0f, 30.0f, false, 0.5f, nullptr, kEaseLinear, 0.0f,
                    false
                );
            }
        }
        return DataNode(0);
    }
};

} // namespace

class NativeEngineLeadsTest : public EngineTestFixture {};

// The image's AnimTask::Poll keeps mAnimTarget until the task is deleted, so an
// AnimTask started from the `ended` callback on the same target finds the
// finishing task (through the target's ref ring) as its mBlendTask.  Native
// nulled mAnimTarget first, which unlinked the finishing task from the target
// and left the new task with no blend task.
TEST_F(NativeEngineLeadsTest, AnimTaskEndedKeepsTargetForBlendChaining) {
    Hmx::Object *target = new Hmx::Object();
    ProbeTargetAnim *first = new ProbeTargetAnim(target);
    ProbeTargetAnim *second = new ProbeTargetAnim(target);
    ChainOnEnded *listener = new ChainOnEnded();
    listener->mNext = second;

    AnimTask *finishing = new AnimTask(
        first, 0.0f, 10.0f, 30.0f, false, 0.0f, listener, kEaseLinear, 0.0f, false
    );
    ASSERT_EQ(finishing->AnimTarget(), target);

    // Control: a task started while `finishing` runs finds it through the target.
    AnimTask *probe = new AnimTask(
        second, 0.0f, 10.0f, 30.0f, false, 0.0f, nullptr, kEaseLinear, 0.0f, false
    );
    ASSERT_EQ(probe->BlendTask(), finishing)
        << "control: the blend-task lookup through the target's refs must work "
           "while the task runs, or the ended case below proves nothing";
    delete probe; // queues `finishing` for deletion (deduplicated below)

    finishing->Poll(0.5f); // past the 10/30 s span: `ended` fires
    ASSERT_EQ(listener->mEnded, 1);
    ASSERT_NE(listener->mStarted, nullptr);
    EXPECT_EQ(listener->mStarted->BlendTask(), finishing)
        << "an AnimTask started from `ended` did not find the finishing task as "
           "its blend task: native AnimTask::Poll nulled mAnimTarget before "
           "sending `ended`.  The image (no such store) keeps the target until "
           "the task is deleted.";

    // `finishing` queued itself for deletion; deleting it here nulls the
    // queue's ObjPtr, so the next TaskMgr::Poll drains a null entry.
    delete listener->mStarted;
    delete finishing;
    delete listener;
    delete second;
    delete first;
    delete target;
}

// ----------------------------------------------------------------------------
// ObjectDir::FindObject
// ----------------------------------------------------------------------------

// While a proxy dir loads, its Dir() is itself (DirLoader::LoadHeader names it
// into itself; Cleanup names it back into the proxy dir).  The image's
// FindObject(name, false, true) searches only the dir and its subdirs -- no
// parent, no proxy dir -- and callers that want the proxy dir ask for it
// themselves (FlowPtrBase::LoadObject via FlowPtrGetLoadingDir; the
// gLoadingProxyFromDisk loads of RndDir::mEnv / trans parent read into a
// discarded temporary).  Native added a fallback into the loader's ProxyDir /
// ParentDir that bound names the Xbox leaves null: e.g. CharacterTest::mDriver
// in every outfit and skeleton character got its PARENT's main.drv, and a flow
// in a proxy-of-a-proxy (results_cluster inside perform_endgame) bound
// bg_*_color.anim two dirs up.
TEST_F(NativeEngineLeadsTest, FindObjectDoesNotSearchALoadingProxysParent) {
    ObjectDir *parent = Hmx::Object::New<ObjectDir>();
    parent->SetName("lead_parent", ObjectDir::Main());
    Hmx::Object *onlyInParent = parent->New<Hmx::Object>("only_in_parent.obj");
    ASSERT_NE(onlyInParent, nullptr);

    ObjectDir *proxy = Hmx::Object::New<ObjectDir>();
    proxy->SetName("lead_proxy", parent);
    DirLoader *loader = new DirLoader(
        FilePath("lead_proxy.milo"), kLoadFront, nullptr, nullptr, proxy, false, nullptr
    );
    ASSERT_EQ(loader->ProxyDir(), parent);
    ASSERT_EQ(proxy->Loader(), loader);
    proxy->SetName("lead_proxy", proxy); // what LoadHeader does mid-load
    ASSERT_EQ(proxy->Dir(), proxy);

    EXPECT_EQ(parent->FindObject("only_in_parent.obj", false, true), onlyInParent)
        << "control: the parent must find its own object";
    EXPECT_EQ(proxy->FindObject("only_in_parent.obj", false, true), nullptr)
        << "a loading proxy's FindObject(name, false, true) returned an object from "
           "its proxy dir.  The image (ObjectDir::FindObject, 100% matched) searches "
           "only the dir and its subdirs; native bound what the Xbox leaves null.";

    delete loader; // Cleanup names the proxy back into its proxy dir
    delete proxy;
    delete parent;
}

// ----------------------------------------------------------------------------
// RndDir::SyncObjects
// ----------------------------------------------------------------------------

namespace {
class ProbePollable : public RndPollable {};
class ProbeRndDir : public RndDir {
public:
    const std::vector<RndPollable *> &Polls() const { return mPolls; }
};
} // namespace

// The image's RndDir::SyncObjects leaves mPolls in HarvestPollables order
// (SortPolls: enabled first, then by name).  Native moved every pollable whose
// name contains "ikfoot"/"feetandhands" to the end by default (opt-out
// DC3_FEET_PLANT_FIX_OFF), a leftover of the 2026-06-09 opt-in foot-plant
// experiment.  On characters Character::SyncObjects re-sorts right after, so
// the move was dead there (measured: 53/53 sorted orders identical with and
// without it); on any other RndDir it was a pure divergence.
TEST_F(NativeEngineLeadsTest, RndDirSyncObjectsKeepsHarvestOrder) {
    ProbeRndDir *dir = new ProbeRndDir();
    dir->SetName("lead_rnddir", ObjectDir::Main());
    ProbePollable *feet = new ProbePollable();
    feet->SetName("a_feetandhands.pgrp", dir);
    ProbePollable *other = new ProbePollable();
    other->SetName("b_other.poll", dir);

    dir->SyncObjects();
    ASSERT_EQ(dir->Polls().size(), 2u);
    EXPECT_EQ(dir->Polls()[0], feet)
        << "RndDir::SyncObjects moved the *feetandhands* pollable after its "
           "name-sorted successor; the image keeps the SortPolls order";
    EXPECT_EQ(dir->Polls()[1], other);

    delete other;
    delete feet;
    delete dir;
}

// ----------------------------------------------------------------------------
// GameMode::GameMode / SkeletonChooser::DoesRequireHandRaise
// ----------------------------------------------------------------------------

// The image's GameMode ctor calls SetMode("init", "none"), which installs the
// merged mode config (init + parents + defaults) as the object's properties.
// Native only stored mMode = "init" ("SystemConfig / TheHamProvider not ready
// during GameInit"), so TheGameMode had no properties until the first DTA
// set_mode -- and SkeletonChooser::DoesRequireHandRaise, which the image runs
// from the attract screen on, read raise_hand_to_join through a null DataNode.
// That crash is why native stubbed DoesRequireHandRaise to `return false`.
TEST_F(NativeEngineLeadsTest, NewGameModeInstallsTheInitModeProperties) {
    ASSERT_NE(TheHamProvider, nullptr);
    if (!TheGameData)
        GTEST_SKIP() << "no TheGameData in this engine init";
    if (TheGameMode)
        GTEST_SKIP() << "a GameMode already exists; a second would collide on its name";
    GameMode *mode = new GameMode();
    EXPECT_EQ(mode->Mode(), Symbol("init"));
    EXPECT_NE(mode->Property("raise_hand_to_join", false), nullptr)
        << "a freshly constructed GameMode has no mode properties: the ctor did "
           "not run SetMode(\"init\", \"none\") as the image does";
    delete mode;
}

