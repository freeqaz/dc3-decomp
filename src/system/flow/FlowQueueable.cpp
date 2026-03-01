#include "flow/FlowQueueable.h"
#include "flow/FlowNode.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include <list>

FlowQueueable::FlowQueueable() : mInterrupt(kImmediate) {}
FlowQueueable::~FlowQueueable() {}

BEGIN_HANDLERS(FlowQueueable)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowQueueable)
    SYNC_PROP(interrupt, (int &)mInterrupt)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowQueueable)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mInterrupt;
END_SAVES

BEGIN_COPYS(FlowQueueable)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY(FlowQueueable)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mInterrupt)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(FlowQueueable)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(FlowNode)
    d >> (int &)mInterrupt;
END_LOADS

void FlowQueueable::ReleaseListener(Hmx::Object *obj) {
    if (obj) {
        obj->Handle(Message("on_flow_finished", this), true);
    }
}

void FlowQueueable::Deactivate(bool b) {
    FLOW_LOG("Deactivated\n");
    // Copy listeners and clear the member list
    std::list<Hmx::Object *> temp = mListeners;
    mListeners.clear();
    // Release all queued listeners
    while (!temp.empty()) {
        Hmx::Object *listener = temp.front();
        temp.pop_front();
        ReleaseListener(listener);
    }
    FlowNode::Deactivate(b);
}

void FlowQueueable::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    if (mInterrupt == kWhenAble) {
        FlowNode::ChildFinished(node);
        return;
    }
    mRunningNodes.remove(node);
    if (!mRunningNodes.empty())
        return;

    if (mStopRequested) {
        // Stopped: release listeners and notify parent
        while (!mListeners.empty()) {
            Hmx::Object *listener = mListeners.front();
            mListeners.pop_front();
            ReleaseListener(listener);
        }
        if (mFlowParent)
            mFlowParent->ChildFinished(this);
        return;
    }

    // Not stopped: process next listener in queue
    if (mListeners.size() > 1) {
        // Multiple listeners queued - release the first, re-activate with next
        Hmx::Object *firstListener = mListeners.front();
        mListeners.pop_front();
        ReleaseListener(firstListener);
        ActivateTrigger();
    } else if (!mListeners.empty()) {
        // Single listener - release and notify parent
        Hmx::Object *listener = mListeners.front();
        mListeners.pop_front();
        ReleaseListener(listener);
        if (mFlowParent)
            mFlowParent->ChildFinished(this);
    } else {
        // No listeners queued
        if (mFlowParent)
            mFlowParent->ChildFinished(this);
    }
}

bool FlowQueueable::Activate(Hmx::Object *listener) {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    if (mRunningNodes.empty()) {
        // Not running - start immediately
        mListeners.push_front(listener);
        bool active = ActivateTrigger();
        if (!active) {
            mListeners.clear();
            return false;
        }
        return true;
    }

    // Already running, handle based on mInterrupt
    switch (mInterrupt) {
    case kIgnore:
        FLOW_LOG("Ignoring re-trigger\n");
        ReleaseListener(listener);
        return false;
    case kQueue:
        FLOW_LOG("Queuing re-trigger\n");
        mListeners.push_back(listener);
        return true;
    case kQueueOne:
        FLOW_LOG("Queue-one re-trigger\n");
        // Keep at most 1 item queued beyond the active one
        if (mListeners.size() > 1) {
            Hmx::Object *old = mListeners.back();
            mListeners.pop_back();
            ReleaseListener(old);
        }
        mListeners.push_back(listener);
        return true;
    case kImmediate:
        FLOW_LOG("Immediate re-trigger\n");
        Deactivate(false);
        mListeners.push_back(listener);
        ActivateTrigger();
        return !mRunningNodes.empty();
    case kWhenAble:
        FLOW_LOG("When-able re-trigger\n");
        // Trim to at most 1 queued item, then add
        while (mListeners.size() > 1) {
            Hmx::Object *old = mListeners.back();
            mListeners.pop_back();
            ReleaseListener(old);
        }
        mListeners.push_back(listener);
        return true;
    default:
        MILO_NOTIFY_ONCE("Bad FlowQueueable interrupt mode!");
        ReleaseListener(listener);
        return false;
    }
}

void FlowQueueable::RequestStopCancel() {
    if (!mStopRequested)
        return;
    FlowNode::RequestStopCancel();
}

void FlowQueueable::RequestStop() { FlowNode::RequestStop(); }
