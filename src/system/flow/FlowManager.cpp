#include "flow/FlowManager.h"
#include "flow/FlowNode.h"
#include "obj/Data.h"
#include "rndobj/Overlay.h"

FlowManager *TheFlowMgr;

FlowManager::FlowManager() : unk2c(0), mExecuting(0), mPollables(this) {
    mFlowQueue.clear();
    mFrameCounterModulo = 0;
    mFrameTimeAccumulator = 0;
    mPeakFrameTime = 0;
    mLastFrameTime = 0;
    mElapsedTime = 0;
    for (int i = 0; i < 60; i++) {
        mFrameTimeSamples[i] = 0;
    }
    mFlowOverlay = RndOverlay::Find("flow", false);
    mFlowPeakOverlay = RndOverlay::Find("flow_peak", false);
    mFlowTaskOverlay = RndOverlay::Find("flow_task", false);
    mFlowEventOverlay = RndOverlay::Find("flow_event", false);
}

FlowManager::~FlowManager() {}

void FlowManager::AddPollable(FlowNode *n) { mPollables.push_back(n); }
void FlowManager::RemovePollable(FlowNode *n) { mPollables.remove(n); }

void FlowManager::QueueCommand(FlowNode *n, FlowNode::QueueState q) {
    if (mExecuting && q != FlowNode::kQueueOne) {
        n->Execute(q);
    } else
        mFlowQueue[n] = q;
}

void FlowManager::CancelCommand(FlowNode *n) { mFlowQueue[n] = FlowNode::kImmediate; }

void FlowManager::AddEventTime(Symbol s, float f1) {
    float fsub = f1 - mElapsedTime;
    if (mEventTimes.find(s) != mEventTimes.end()) {
        DataNode &n = mEventTimes[s];
        float f7 = n.Array()->Float(0);
        int i5 = n.Array()->Int(1);
        float f8 = n.Array()->Float(2) + mElapsedTime;
        i5++;
        n.Array()->Node(0) = f7 + fsub;
        n.Array()->Node(1) = i5;
        n.Array()->Node(2) = f8;
    } else {
        DataArrayPtr ptr(fsub, 1, mElapsedTime);
        mEventTimes[s] = ptr;
    }
    mElapsedTime = 0;
}

void FlowManager::Poll() {
    float f31 = mLastFrameTime;
    mLastFrameTime = 0;
    Timer timer;
    timer.Reset();
    mExecuting = true;

    for (std::map<FlowNode *, FlowNode::QueueState>::iterator it = mFlowQueue.begin();
         it != mFlowQueue.end();
         ++it) {
        if (it->second != FlowNode::kImmediate) {
            it->first->Execute(it->second);
        }
    }
    mFlowQueue.clear();

    ObjPtrVec<FlowNode> polls(mPollables);
    for (ObjPtrVec<FlowNode>::iterator it = polls.begin(); it != polls.end(); ++it) {
        (*it)->Execute(FlowNode::kWhenAble);
    }

    mExecuting = false;
    float f27 = timer.Ms() - mLastFrameTime;
    mElapsedTime = f27 + mFrameTimeAccumulator + f31;

    float f30 = -1.0f;
    float f29 = -1.0f;

    for (std::map<Symbol, DataNode>::iterator it = mEventTimes.begin();
         it != mEventTimes.end();
         ++it) {
        DataNode &node = it->second;
        float fval = node.Array()->Float(0);
        float fval2 = node.Array()->Float(2);

        if (!(fval < f30)) {
            f30 = fval;
            mFrameTimeAccumulator = fval;
        }
        if (!(fval2 < f29)) {
            f29 = fval2;
        }
    }

    float total = f27 + f31 + mFrameTimeAccumulator;

    if (total > mPeakFrameTime) {
        mPeakFrameTime = total;
        mFrameCounterModulo++;
        if (mFrameCounterModulo >= 0x3C) {
            mPeakFrameTime = 0;
            float avg = 0;
            for (int i = 0; i < 60; i++) {
                avg += mFrameTimeSamples[i];
            }
            mFrameCounterModulo = 0;
            mElapsedTime = avg * 0.01666667f;
        }
    }

    mEventTimes.clear();
    mFrameTimeAccumulator = 0;
    mLastFrameTime = 0;
    unk2c = false;
}
