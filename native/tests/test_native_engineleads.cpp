// Native-only (HX_NATIVE) engine code that diverged from the Xbox image, pinned
// one lead per test.  Branch `native-engineleads`, the engine leads left open by
// the (b) ADDS triage in docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
#include "test_helpers.h"

#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Anim.h"

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
