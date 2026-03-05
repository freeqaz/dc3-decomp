#include "flow/FlowRun.h"
#include "FlowRun.h"
#include "flow/Flow.h"
#include "flow/FlowNode.h"
#include "obj/Dir.h"
#include "obj/Object.h"

FlowRun::FlowRun()
    : mTargetDir(this), mTarget(this), mTargetName(""), mStop(false),
      mImmediateRelease(false) {}

FlowRun::~FlowRun() {}

BEGIN_HANDLERS(FlowRun)
    HANDLE_ACTION(on_flow_finished, ChildFinished(_msg->Obj<FlowNode>(2)))
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowRun)
    SYNC_PROP_MODIFY(target_dir, mTargetDir, OnTargetDirChange())
    SYNC_PROP_MODIFY(target, mTarget, OnTargetChange())
    SYNC_PROP(stop, mStop)
    SYNC_PROP(immediate_release, mImmediateRelease)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowRun)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << mTargetDir;
    ResolveTarget();
    bs << mTargetName;
    bs << mStop;
    bs << mImmediateRelease;
END_SAVES

void FlowRun::Copy(const Hmx::Object *o, Hmx::Object::CopyType ty) {
    FlowNode::Copy(o, ty);
    const FlowRun *c = dynamic_cast<const FlowRun *>(o);
    if (c) {
        mTargetDir = c->mTargetDir;
        mTargetName = c->mTargetName;
        mTarget = c->mTarget;
        mStop = c->mStop;
        mImmediateRelease = c->mImmediateRelease;
    }
}

INIT_REVS(2, 0)

BEGIN_LOADS(FlowRun)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(FlowNode)
    if (d.rev < 2) {
        Hmx::Object *obj = LoadObjectFromMainOrDir(bs, Dir());
        if (obj) {
            mTargetDir = dynamic_cast<ObjectDir *>(obj);
        }
        mTarget = mTarget.LoadFromMainOrDir(bs);
    } else {
        mTargetDir.LoadFromMainOrDir(bs);
        bs >> mTargetName;
        mTarget = (Flow *)0;
    }
    d >> mStop;
    d >> mImmediateRelease;
END_LOADS

bool FlowRun::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    ResolveTarget();
    Flow *target = mTarget;
    if (!target)
        return false;
    if (mStop) {
        target->RequestStop();
        return false;
    }
    if (mImmediateRelease) {
        target->Activate(nullptr);
        return false;
    } else {
        mRunningNodes.push_back(target);
        bool running = target->Activate(this);
        if (!running) {
            FLOW_LOG("Target ran in full immediately.\n");
            mRunningNodes.remove(target);
        }
        return running;
    }
}

void FlowRun::ResolveTarget() {
    Flow *target = mTarget;
    auto& targetName = mTargetName;
    if (!target && targetName.length() > 0) {
        ObjectDir *dir = mTargetDir;
        if (!dir) {
            // Find the containing flow's dir
            Flow *ownerFlow = GetOwnerFlow();
            if (ownerFlow) {
                dir = ownerFlow->Dir();
                if (!dir) {
                    MILO_ASSERT(false, 0x72);
                }
            }
        }
        if (dir) {
            Hmx::Object *found = dir->Find<Hmx::Object>(targetName.c_str(), false);
            mTarget = dynamic_cast<Flow *>(found);
        }
    }
}

void FlowRun::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    if (!mRunningNodes.empty()) {
        FlowNode::ChildFinished(node);
    }
}

void FlowRun::RequestStop() {
    FLOW_LOG("RequestStop\n");
    mStopRequested = true;
    mTarget->RequestStop();
}

void FlowRun::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    mStopRequested = false;
    mTarget->RequestStopCancel();
}

void FlowRun::OnTargetDirChange() {
    mTarget = (Flow *)0;
    mTargetName = "";
}

void FlowRun::OnTargetChange() {
    if (mTarget)
        mTargetName = mTarget->Name();
    else
        mTargetName = "";
    return;
}
