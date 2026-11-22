// The flow queue's execution order on native (FlowManager::Poll).
//
// The console build keys the queue by the node's address and runs it in
// address order. A host allocator makes that order differ between launches:
// on macOS the venue's start.flow ran before chars_base's reset.flow in 8 of
// 19 launches, and Break It Down then had no tunnel for the whole launch
// (reset hides what start shows). Native runs the queue in the order the nodes
// were created.
//
// The nodes here are built in one buffer so that address order, creation order
// and queueing order are three different orders, whatever the allocator does.
//
// Seen red before the fix (address order): RunsInCreationOrder gave 1,2,0.

#include "flow/FlowManager.h"
#include "flow/FlowNode.h"
#include "test_helpers.h"

#include <new>
#include <vector>

namespace {

class FlowQueueOrderTest : public EngineTestFixture {};

// A node that records when the manager runs it. Like every queueable node in
// src/system/flow it cancels its queued command when it is destroyed.
class ProbeNode : public FlowNode {
public:
    ProbeNode(std::vector<int> &log, int id) : mLog(log), mId(id), mCancelOnRun(nullptr) {}
    virtual ~ProbeNode() { TheFlowMgr->CancelCommand(this); }
    virtual void Execute(QueueState) {
        mLog.push_back(mId);
        if (mCancelOnRun)
            TheFlowMgr->CancelCommand(mCancelOnRun);
    }
    void CancelOnRun(FlowNode *n) { mCancelOnRun = n; }

private:
    std::vector<int> &mLog;
    int mId;
    FlowNode *mCancelOnRun;
};

// Three slots at ascending addresses. Nodes are placed in a chosen slot, so
// the test decides both the address order and the creation order.
struct Slots {
    alignas(ProbeNode) unsigned char mem[3][sizeof(ProbeNode)];
    ProbeNode *Make(int slot, std::vector<int> &log, int id) {
        return ::new (static_cast<void *>(mem[slot])) ProbeNode(log, id);
    }
    static void Destroy(ProbeNode *n) { n->~ProbeNode(); }
};

std::vector<int> Ids(int a, int b, int c) {
    std::vector<int> v;
    v.push_back(a);
    v.push_back(b);
    v.push_back(c);
    return v;
}

} // namespace

TEST_F(FlowQueueOrderTest, RunsInCreationOrder) {
    ASSERT_NE(TheFlowMgr, nullptr);
    TheFlowMgr->Poll(); // start from an empty queue
    std::vector<int> log;
    Slots slots;
    // Created 0, 1, 2 at the highest, lowest and middle address.
    ProbeNode *n0 = slots.Make(2, log, 0);
    ProbeNode *n1 = slots.Make(0, log, 1);
    ProbeNode *n2 = slots.Make(1, log, 2);
    ASSERT_LT((void *)n1, (void *)n2);
    ASSERT_LT((void *)n2, (void *)n0);
    // Queued 2, 0, 1.
    TheFlowMgr->QueueCommand(n2, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n0, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n1, FlowNode::kQueue);
    TheFlowMgr->Poll();
    // Address order would be 1,2,0 and queueing order 2,0,1.
    EXPECT_EQ(log, Ids(0, 1, 2));
    // The queue is empty afterwards: a second poll runs nothing.
    TheFlowMgr->Poll();
    EXPECT_EQ(log.size(), 3u);
    Slots::Destroy(n2);
    Slots::Destroy(n1);
    Slots::Destroy(n0);
    TheFlowMgr->Poll();
}

TEST_F(FlowQueueOrderTest, ANodeQueuedTwiceRunsOnceInItsCreationPlace) {
    ASSERT_NE(TheFlowMgr, nullptr);
    TheFlowMgr->Poll();
    std::vector<int> log;
    Slots slots;
    ProbeNode *n0 = slots.Make(1, log, 0);
    ProbeNode *n1 = slots.Make(0, log, 1);
    TheFlowMgr->QueueCommand(n1, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n0, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n1, FlowNode::kIgnore);
    TheFlowMgr->Poll();
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], 0);
    EXPECT_EQ(log[1], 1);
    Slots::Destroy(n1);
    Slots::Destroy(n0);
    TheFlowMgr->Poll();
}

TEST_F(FlowQueueOrderTest, CancelledNodesDoNotRun) {
    ASSERT_NE(TheFlowMgr, nullptr);
    TheFlowMgr->Poll();
    std::vector<int> log;
    Slots slots;
    ProbeNode *n0 = slots.Make(2, log, 0);
    ProbeNode *n1 = slots.Make(0, log, 1);
    ProbeNode *n2 = slots.Make(1, log, 2);
    TheFlowMgr->QueueCommand(n0, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n1, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n2, FlowNode::kQueue);
    // Cancelled before the poll: never runs.
    TheFlowMgr->CancelCommand(n1);
    // Cancelled by an earlier node while the queue runs: never runs. In
    // address order 2 would have run before 0 could cancel it.
    n0->CancelOnRun(n2);
    TheFlowMgr->Poll();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 0);
    // A cancel is not sticky: queued again, each runs.
    n0->CancelOnRun(nullptr);
    TheFlowMgr->QueueCommand(n2, FlowNode::kQueue);
    TheFlowMgr->QueueCommand(n1, FlowNode::kQueue);
    TheFlowMgr->Poll();
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[1], 1);
    EXPECT_EQ(log[2], 2);
    Slots::Destroy(n2);
    Slots::Destroy(n1);
    Slots::Destroy(n0);
    TheFlowMgr->Poll();
}

TEST_F(FlowQueueOrderTest, ADestroyedNodeIsSkippedAndItsAddressCanBeReused) {
    ASSERT_NE(TheFlowMgr, nullptr);
    TheFlowMgr->Poll();
    std::vector<int> log;
    Slots slots;
    ProbeNode *gone = slots.Make(0, log, 7);
    TheFlowMgr->QueueCommand(gone, FlowNode::kQueue);
    Slots::Destroy(gone); // its destructor cancels the command
    // A new node at the same address, queued in the same frame, still runs.
    ProbeNode *again = slots.Make(0, log, 8);
    ASSERT_EQ((void *)again, (void *)gone);
    TheFlowMgr->QueueCommand(again, FlowNode::kQueue);
    TheFlowMgr->Poll();
    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log[0], 8);
    Slots::Destroy(again);
    TheFlowMgr->Poll();
}
