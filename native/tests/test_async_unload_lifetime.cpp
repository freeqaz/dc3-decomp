// Async-unload object lifetime: refs to a dir's objects must stay valid until
// each object is actually destroyed.
//
// On the Xbox, `~ObjectDir` under `TheLoadMgr.AsyncUnload()` hands every
// object to a DirUnloader, which deletes ONE object per LoadMgr poll over the
// following frames.  Until an object's own turn comes it is alive, and every
// ObjPtr to it still points at it -- `~Object`'s ReplaceRefs(nullptr) is the
// only thing that ever nulls them, and it runs when that object dies.
//
// The native port used to nullify every ref to every object in the dir
// eagerly, inside `~ObjectDir`, BEFORE creating the DirUnloader (an
// HX_NATIVE-only block in Dir.cpp).  For the frames between that and each
// object's real destruction, live objects saw their refs to still-live
// objects read NULL.  The world panel (`ui/game.dta`: `unload_async TRUE`) is
// unloaded this way when leaving gameplay, and the dance-battle exit crashed
// on it:
//
//     SIGSEGV at 0x80  SynthSample::GetSampleRate
//                      SampleInst::SynthPoll   <- mSample NULL, inst still polled
//                      SynthPollable::PollAll <- Synth::Poll
//
// SampleInst is safe on the Xbox by construction: `~SynthSample` Stop(true)s
// every registered inst (cancelling its polling) before `~Object` nulls their
// mSample, and `~SampleInst` unregisters itself through mSample.  Both halves
// of that contract assume mSample is non-null while the sample lives.  These
// tests state the contract from the holder's side, without a game run.

#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
#include "synth/Pollable.h"
#include "synth/SampleInst.h"
#include "synth/SynthSample.h"
#include "utl/Loader.h"

#include <algorithm>

namespace {

// A SampleInst whose playback state is fully under the test's control.  The
// real SynthPoll (SampleInst::SynthPoll, 100% matched) is what runs.
class PolledInst : public SampleInst {
public:
    explicit PolledInst(SynthSample *sample) : SampleInst(sample) {}

    bool IsPlaying() const override { return mPlaying; }
    void SetFXCore(FXCore) override {}
    void StartImpl() override { mPlaying = true; }
    void StopImpl(bool) override { mPlaying = false; }
    void SetVolumeImpl(float) override {}
    void SetPanImpl(float) override {}
    void SetSpeedImpl(float) override {}
    void Pause(bool) override {}
    void SetADSR(const ADSRImpl &) override {}

    SynthSample *Sample() const { return mSample; }
    bool IsPolled() const {
        auto &all = SynthPollable::Pollables();
        return std::find(all.begin(), all.end(), (SynthPollable *)this) != all.end();
    }

private:
    bool mPlaying = false;
};

// Exposes the sample's registration list (protected).
class ProbeSample : public SynthSample {
public:
    size_t NumInsts() const { return mSampleInsts.size(); }
    // Failure-path hygiene only: lets a failing run drop a dangling entry so
    // the drain below does not call through freed memory and take the whole
    // suite down with it.
    void ForgetInsts() { mSampleInsts.clear(); }
};

// Poll the load manager until nothing is left in its loading queue, i.e. the
// DirUnloader has deleted every object and itself.  Bounded.
void DrainLoaders() {
    float saved = TheLoadMgr.SetLoaderPeriod(10.0f);
    for (int i = 0; i < 1000 && !TheLoadMgr.Loading().empty(); i++)
        TheLoadMgr.Poll();
    TheLoadMgr.SetLoaderPeriod(saved);
}

// `delete dir` exactly as UIPanel::Unload does for an `unload_async` panel.
void DeleteDirAsync(ObjectDir *dir) {
    TheLoadMgr.StartAsyncUnload();
    delete dir;
    TheLoadMgr.FinishAsyncUnload();
}

class AsyncUnloadLifetimeTest : public EngineTestFixture {
protected:
    void SetUp() override {
        // Anything a previous test left queued would be drained by ours.
        DrainLoaders();
        ASSERT_TRUE(TheLoadMgr.Loading().empty());
    }
};

} // namespace

// Control: the DirUnloader really does defer destruction.  If this fails the
// rest of the file is not testing the async path at all.
TEST_F(AsyncUnloadLifetimeTest, DirUnloaderDefersDestruction) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("async_ctrl_dir", ObjectDir::Main());
    Hmx::Object *resident = Hmx::Object::New<Hmx::Object>();
    resident->SetName("resident", dir);
    ObjPtr<Hmx::Object> external(nullptr, resident);

    DeleteDirAsync(dir);
    EXPECT_FALSE(TheLoadMgr.Loading().empty()) << "no DirUnloader was queued";

    DrainLoaders();
    EXPECT_TRUE(TheLoadMgr.Loading().empty());
    EXPECT_EQ(external.Ptr(), nullptr)
        << "the resident's destruction did not null an external ref to it";
}

// The dance-battle exit crash, reduced.  A sound instance plays a sample that
// lives in an async-unloaded dir and reports marker events to an object that
// does not.  The frame after the unload starts, Synth::Poll polls it.
TEST_F(AsyncUnloadLifetimeTest, PolledSampleInstKeepsItsSampleUntilTheSampleDies) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("async_synth_dir", ObjectDir::Main());
    SynthSample *sample = static_cast<SynthSample *>(SynthSample::NewObject());
    sample->SetName("battle_sfx.wav", dir);

    Hmx::Object receiver; // outside the dir, like a panel or flow receiver
    PolledInst *inst = new PolledInst(sample);
    inst->SetEventReceiver(&receiver);
    inst->Play(0);
    ASSERT_TRUE(inst->IsPolled());

    DeleteDirAsync(dir);
    ASSERT_FALSE(TheLoadMgr.Loading().empty()) << "no DirUnloader was queued";

    // The sample has not been destroyed yet -- it is waiting in the
    // DirUnloader -- so the inst's ref to it must still be intact.
    SynthSample *seen = inst->Sample();
    EXPECT_EQ(seen, sample)
        << "a live SampleInst's mSample was nulled while its sample is still "
           "alive and the inst is still being polled; the next "
           "SynthPollable::PollAll dereferences NULL in "
           "SampleInst::SynthPoll -> SynthSample::GetSampleRate";
    if (seen)
        SynthPollable::PollAll(); // the frame the game crashed on
    else
        inst->Stop(true); // failing run: do not segfault the suite

    // Now let the DirUnloader destroy the sample.  ~SynthSample must stop the
    // inst (cancel its polling) and only then is mSample nulled.
    DrainLoaders();
    EXPECT_FALSE(inst->IsPolled()) << "the inst outlived its sample while polled";
    EXPECT_EQ(inst->Sample(), nullptr);
    SynthPollable::PollAll();

    delete inst;
}

// The other half of the same contract.  An inst that is deleted while its
// sample is still pending in the DirUnloader (a Sound zombie culled by
// Synth::Poll, an SfxInst torn down) must unregister from the sample --
// ~SampleInst does that through mSample.  If mSample was nulled early the
// sample keeps a dangling entry and ~SynthSample calls Stop(true) through it.
TEST_F(AsyncUnloadLifetimeTest, InstDeletedDuringAsyncUnloadUnregistersFromSample) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("async_synth_dir2", ObjectDir::Main());
    ProbeSample *sample = new ProbeSample();
    sample->SetName("crowd_loop.wav", dir);

    PolledInst *inst = new PolledInst(sample);
    inst->Play(0);
    ASSERT_EQ(sample->NumInsts(), 1u);

    DeleteDirAsync(dir);
    ASSERT_FALSE(TheLoadMgr.Loading().empty()) << "no DirUnloader was queued";

    delete inst; // the sample is still alive, queued for destruction

    EXPECT_EQ(sample->NumInsts(), 0u)
        << "~SampleInst could not unregister: its mSample had been nulled "
           "while the sample was alive, so the sample still lists a freed inst "
           "and ~SynthSample will call Stop(true) through it";
    if (sample->NumInsts() != 0)
        sample->ForgetInsts(); // failing run: keep the drain from a use-after-free

    DrainLoaders();
    EXPECT_TRUE(TheLoadMgr.Loading().empty());
}

// Same contract, second holder: RndTransformable's parent/child links.  The
// child's mParent is an ObjOwnerPtr in the PARENT's ref ring, and
// ~RndTransformable removes the child from the parent's mChildren through it.
// If the async unload nulls mParent while both are still queued, the
// DirUnloader's later delete of the child cannot unlink it, and the parent's
// ~RndTransformable walks mChildren into the freed child -- the
// "SIGSEGV in RndTransformable::~RndTransformable via RndGroup <-
// DirUnloader::PollLoading" seen once leaving a song (2026-09-30).
TEST_F(AsyncUnloadLifetimeTest, ChildDeletedDuringAsyncUnloadLeavesItsParent) {
    ObjectDir *dir = Hmx::Object::New<ObjectDir>();
    dir->SetName("async_trans_dir", ObjectDir::Main());
    RndTransformable *parent = Hmx::Object::New<RndTransformable>();
    parent->SetName("group", dir);
    RndTransformable *child = Hmx::Object::New<RndTransformable>();
    child->SetName("member", dir);
    child->SetTransParent(parent, false);
    ASSERT_EQ(parent->Children().size(), 1u);

    DeleteDirAsync(dir);
    ASSERT_FALSE(TheLoadMgr.Loading().empty()) << "no DirUnloader was queued";

    EXPECT_EQ(child->TransParent(), parent)
        << "a live child's mParent was nulled while its parent is still alive";
    delete child; // as the DirUnloader would, one object per poll

    EXPECT_TRUE(parent->Children().empty())
        << "the deleted child is still in its parent's mChildren; the parent's "
           "~RndTransformable will write through it";
    if (!parent->Children().empty())
        const_cast<std::list<RndTransformable *> &>(parent->Children()).clear();

    DrainLoaders();
    EXPECT_TRUE(TheLoadMgr.Loading().empty());
}
