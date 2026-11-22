#pragma once
#include "obj/Object.h"
#include "flow\FlowNode.h"
#include "rndobj\Overlay.h"
#include <map>

#ifdef HX_NATIVE
// Native only: the flow queue's key. The console build keys the queue by the
// node's address, so FlowManager::Poll runs the queued nodes in address order.
// Its pool allocator hands out ascending addresses, which made that order
// repeatable and close to the order the nodes were created in. A host
// allocator gives neither. On macOS the order changed from launch to launch,
// and whenever a venue's start.flow ran before chars_base's reset.flow (which
// hides what start shows) Break It Down had no tunnel for the whole launch: 8
// of 19 launches. On Linux, of the pairs queued together in Break It Down,
// about 6% ran against creation order in a launch and 11% did not run the
// same way in every launch.
//
// Native keys the queue by creation order instead; nothing else about the
// queue changes. The key keeps the number, so an entry left behind by a
// destroyed node (CancelCommand in its destructor) is never dereferenced.
// It converts from FlowNode * and points like one, so the code that uses the
// queue is the console's.
struct FlowQueueKey {
    FlowQueueKey(FlowNode *n) : mOrder(n->CreateOrder()), mNode(n) {}
    bool operator<(const FlowQueueKey &o) const { return mOrder < o.mOrder; }
    FlowNode *operator->() const { return mNode; }

    unsigned long long mOrder; // unique per node, so it is the whole order
    FlowNode *mNode;
};
#endif

class FlowManager : public Hmx::Object {
public:
    FlowManager();
    virtual ~FlowManager();

    void AddPollable(FlowNode *);
    void RemovePollable(FlowNode *);
    void Poll();
    void AddEventTime(Symbol, float);
    void QueueCommand(FlowNode *, FlowNode::QueueState);
    void CancelCommand(FlowNode *);
    void AddMs(float ms) { mFrameTimeAccumulator += ms; }

protected:
    bool unk2c;
    bool mExecuting;
#ifdef HX_NATIVE
    std::map<FlowQueueKey, FlowNode::QueueState> mFlowQueue;
#else
    std::map<FlowNode *, FlowNode::QueueState> mFlowQueue; // 0x30
#endif
    ObjPtrVec<FlowNode> mPollables; // 0x48
    std::map<Symbol, DataNode> mEventTimes; // 0x64
    float mFrameTimeAccumulator; // 0x7c
    float mPeakFrameTime; // 0x80
    RndOverlay *mFlowOverlay; // 0x84
    RndOverlay *mFlowPeakOverlay; // 0x88
    RndOverlay *mFlowTaskOverlay; // 0x8c
    RndOverlay *mFlowEventOverlay; // 0x90
    int mFrameCounterModulo; // 0x94
    float mFrameTimeSamples[60]; // 0x98
    float mAvgFrameTime; // 0x188
    float mLastFrameTime; // 0x18c
    float mElapsedTime; // 0x190
    DataNode mPeakFrameInfo; // 0x194
};

extern FlowManager *TheFlowMgr;
