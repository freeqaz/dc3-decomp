#include "flow/FlowTimer.h"
#include "flow/Flow.h"
#include "flow/FlowManager.h"
#include "flow/FlowNode.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "rndobj/Anim.h"

FlowTimer::FlowTimer() : mStopMode(0), mTask(this), mRate(0), mTotalTime(0.0f) {}

FlowTimer::~FlowTimer() { TheFlowMgr->CancelCommand(this); }

BEGIN_PROPSYNCS(FlowTimer)
    SYNC_PROP(total_time, mTotalTime)
    SYNC_PROP(rate, mRate)
    SYNC_PROP(stop_mode, mStopMode)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowTimer)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mTotalTime;
    bs << mRate;
    bs << mStopMode;
END_SAVES

BEGIN_COPYS(FlowTimer)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY_AS(FlowTimer, node)
    BEGIN_COPYING_MEMBERS_FROM(node)
        COPY_MEMBER_FROM(node, mTotalTime)
        COPY_MEMBER_FROM(node, mRate)
        COPY_MEMBER_FROM(node, mStopMode)
    END_COPYING_MEMBERS

END_COPYS

INIT_REVS(1, 0)

BEGIN_LOADS(FlowTimer)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(FlowNode)
    bs >> mTotalTime >> mRate;
    if (d.rev > 0)
        bs >> mStopMode;
END_LOADS

bool FlowTimer::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    FlowNode::PushDrivenProperties();
    if (0.0f >= mTotalTime) {
        return false;
    }
    TheFlowMgr->QueueCommand(this, kQueue);
    return true;
}

void FlowTimer::Deactivate(bool b) {
    FLOW_LOG("Deactivated\n");
#ifdef HX_NATIVE
    if ((Task *)mTask)
#else
    if ((int)(Task *)mTask)
#endif
        delete (Task *)mTask;
    TheFlowMgr->CancelCommand(this);
    FlowNode::Deactivate(b);
}

void FlowTimer::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    mRunningNodes.remove(node);

    if (mStopMode == 0 && mRunningNodes.empty()) {
        MILO_ASSERT(mFlowParent->HasRunningNode(this), 0x10d);
        FLOW_LOG("Timed Release From Parent \n");
        Timer t;
        t.Reset();
        static int depth = 0;
        static unsigned int start = 0;
        static unsigned long long cycles = 0;
        depth++;
        if (depth == 1) {
            start = __mftb();
        }
        mFlowParent->ChildFinished(this);
        depth--;
        if (depth == 0) {
            unsigned int end = __mftb();
            cycles += end - start;
            float ms = Timer::CyclesToMs(cycles);
            TheFlowMgr->AddMs(ms);
        }
    }
}

void FlowTimer::RequestStop() {
    FLOW_LOG("RequestStop\n");
    if (mStopMode == 0) {
        mStopRequested = true;
        TheFlowMgr->QueueCommand(this, kIgnore);
        FlowNode::RequestStop();
    }
}

void FlowTimer::RequestStopCancel() {
    FLOW_LOG("RequestStopC\n");
    mStopRequested = false;
    TheFlowMgr->QueueCommand(this, kQueue);
    FlowNode::RequestStopCancel();
}

void FlowTimer::Execute(FlowNode::QueueState state) {
    FLOW_LOG("Execute: state = %i\n", state);

    if (IsRunning()) {
        if (state == kIgnore) {
            delete mTask;
            FLOW_LOG("Timed Release From Parent \n");
            Timer timer;
            timer.Reset();
            timer.Start();
            mFlowParent->ChildFinished(this);
            timer.Stop();
            TheFlowMgr->AddMs(timer.Ms());
        }
    } else {
        if (state == kQueue) {
            EventTask *task = new EventTask(
                this, &mChildNodes,
                RndAnimatable::RateToTaskUnits((RndAnimatable::Rate)mRate),
                mTotalTime
            );
            mTask = task;
        } else if (state == kIgnore) {
            mFlowParent->ChildFinished(this);
        }
    }
}

bool FlowTimer::IsRunning() { return mTask || FlowNode::IsRunning(); }

void FlowTimer::OnKeyframe(FlowNode *node) {
    if (!node->IsRunning())
        FlowNode::ActivateChild(node);
}

void FlowTimer::OnTimerEnd() {
    if (FlowNode::IsRunning()) {
        // Running
    } else {
        MILO_ASSERT(mFlowParent->HasRunningNode(this), 0x10d);
        FLOW_LOG("Timed Release From Parent \n");
        int depth = 0;
        unsigned int start = 0;
        unsigned long long cycles = 0;
        Timer t;
        t.Reset();
        depth++;
        if ((unsigned int)depth == 1) {
            start = bool(__mftb());
        }
        mFlowParent->ChildFinished(this);
        depth--;
        if (0 == depth) {
            unsigned int end = __mftb();
            cycles += end - start;
            float ms = Timer::CyclesToMs(cycles);
            if (TheFlowMgr)
                TheFlowMgr->AddMs(ms);
        }
    }
}

BEGIN_HANDLERS(FlowTimer)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS
