// Native-shadow regression tests.
//
// Every test here pins a place where the native build compiled a DIFFERENT
// body from the one the decomp tools measure -- an `#ifdef HX_NATIVE` branch
// (or a native-only definition) whose semantics had drifted from the Xbox
// image. No objdiff ruler can see these by construction: objdiff measures
// only the non-native branch. Inventory: scripts/analysis/native_shadow_audit.py;
// write-up: docs/decomp/patterns/native-shadow-bodies-are-unmeasured.md.
//
// Each test was watched FAILING against the pre-fix native body before the
// fix landed (see the commit that introduced it).

#include "test_helpers.h"

#include "flow/FlowNode.h"
#include "flow/FlowQueueable.h"
#include "obj/Object.h"

namespace {

class NativeShadowTest : public EngineTestFixture {};

// ---------------------------------------------------------------------------
// FlowQueueable::mListeners is the queue of pending re-triggers. The image
// keeps it as std::list<Hmx::Object *> (FlowQueueable.s, Activate: the
// obj==null arm stores 0 and calls list::insert), so a trigger with no
// listener is ONE queued entry. The native build had swapped it for an
// ObjPtrList in kObjListNoNull mode, whose insert() silently drops null --
// so every listener-less trigger (Flow::Activate(), FlowRun) vanished from
// the queue and a kQueue flow re-triggered while running never re-ran.
// ---------------------------------------------------------------------------
class ProbeQueueable : public FlowQueueable {
public:
    ProbeQueueable() : mTriggers(0), mChild(nullptr) {}
    // Stand-in for "activating our children leaves one running": record the
    // trigger and keep mChild in the running set, as a long-running child
    // (an animation, a wait) would.
    virtual bool ActivateTrigger() {
        mTriggers++;
        mRunningNodes.push_back(mChild);
        return true;
    }
    void SetInterrupt(QueueState q) { mInterrupt = q; }
    int NumListeners() const { return mListeners.size(); }

    int mTriggers;
    FlowNode *mChild;
};

class ProbeChild : public FlowNode {
public:
    ProbeChild() {}
};

TEST_F(NativeShadowTest, FlowQueueableQueuesListenerlessTriggers) {
    ProbeQueueable *q = new ProbeQueueable();
    ProbeChild *child = new ProbeChild();
    q->mChild = child;
    q->SetInterrupt(FlowNode::kQueue);

    // First trigger starts the flow; second arrives while it is running and
    // must be queued -- both with no listener, as Flow::Activate() does.
    EXPECT_TRUE(q->Activate(nullptr));
    EXPECT_EQ(q->mTriggers, 1);
    EXPECT_EQ(q->NumListeners(), 1) << "a null listener is one queued entry on Xbox";
    EXPECT_TRUE(q->Activate(nullptr));
    EXPECT_EQ(q->NumListeners(), 2);

    // The running child finishes: the queued trigger must re-run the flow.
    q->ChildFinished(child);
    EXPECT_EQ(q->mTriggers, 2) << "the queued listener-less trigger was lost";
    EXPECT_EQ(q->NumListeners(), 1);

    // And when that run finishes, the queue drains with no further run.
    q->ChildFinished(child);
    EXPECT_EQ(q->mTriggers, 2);
    EXPECT_EQ(q->NumListeners(), 0);

    delete q;
    delete child;
}

} // namespace
