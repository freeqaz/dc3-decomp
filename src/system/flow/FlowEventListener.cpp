#include "flow/FlowEventListener.h"
#include "FlowEventListener.h"
#include "FlowTrigger.h"
#include "flow/FlowManager.h"
#include "flow/FlowNode.h"
#include "flow/FlowQueueable.h"
#include "obj/Object.h"

FlowEventListener::FlowEventListener()
    : mListening(0), mStartOnActivate(0), mEventCount(0), mEventsFired(0) {
    mAutoRegister = true;
}

FlowEventListener::~FlowEventListener() {}

BEGIN_HANDLERS(FlowEventListener)
    HANDLE_SUPERCLASS(FlowTrigger)
END_HANDLERS

BEGIN_PROPSYNCS(FlowEventListener)
    SYNC_PROP(event_count, mEventCount)
    SYNC_PROP(start_on_activate, mStartOnActivate)
    SYNC_SUPERCLASS(FlowTrigger)
END_PROPSYNCS

BEGIN_SAVES(FlowEventListener)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(FlowTrigger)
    bs << mEventCount;
    bs << mStartOnActivate;
END_SAVES

BEGIN_COPYS(FlowEventListener)
    COPY_SUPERCLASS(FlowTrigger)
    CREATE_COPY(FlowEventListener)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mEventCount)
        COPY_MEMBER(mStartOnActivate)
    END_COPYING_MEMBERS
END_COPYS

bool FlowEventListener::Activate() {
    FLOW_LOG("Activate\n");
    mListening = true;
    mEventsFired = 0;
    RegisterEvents();
    if (mStartOnActivate) {
        ActivateTrigger();
    }
    return true;
}

void FlowEventListener::Deactivate(bool b1) {
    FLOW_LOG("Deactivated\n");
    FlowQueueable::Deactivate(b1);
    if (mListening && !b1) {
        mListening = false;
        UnregisterEvents();
    }
}

void FlowEventListener::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    if (!mListening) {
        FlowNode::ChildFinished(node);
    } else {
        FlowQueueable::ChildFinished(node);
    }
}

void FlowEventListener::RequestStop() {
    FLOW_LOG("RequestStop\n");
    if (mListening) {
        mListening = false;
        UnregisterEvents();
        if (mRunningNodes.empty()) {
            mFlowParent->ChildFinished(this);
        } else {
            FlowQueueable::RequestStop();
        }
    }
}

void FlowEventListener::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    if (!mListening) {
        mListening = true;
        FlowQueueable::RequestStopCancel();
        RegisterEvents();
    }
}

bool FlowEventListener::IsRunning() { return mListening || !mRunningNodes.empty(); }

bool FlowEventListener::ActivateTrigger() {
    FLOW_LOG("Reactivate\n");
    mStopRequested = false;
    mEventsFired++;
    if (mEventCount > 0 && mEventsFired >= mEventCount) {
        UnregisterEvents();
        mListening = false;
    }
    FlowNode::Activate();
    if (!FlowNode::IsRunning() && mEventCount > 0 && mEventsFired >= mEventCount) {
        FLOW_LOG("releasing\n");
        FLOW_LOG("Timed Release From Parent \n");
        Timer timer;
        timer.Reset();
        timer.Start();
        mFlowParent->ChildFinished(this);
        timer.Stop();
        TheFlowMgr->AddMs(timer.Ms());
    }
    return FlowNode::IsRunning();
}
