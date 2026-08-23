#include "flow\FlowRun.h"
#include "FlowRun.h"
#include "flow\Flow.h"
#include "flow\FlowNode.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"

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
        Hmx::Object *obj = FlowNode::LoadObjectFromMainOrDir(bs, Dir());
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
    PushDrivenProperties();
    ResolveTarget();
    Flow *target = mTarget;
    if (target) {
        if (mStop) {
            mTarget->RequestStop();
        } else if (mImmediateRelease) {
            mTarget->Activate(nullptr);
        } else {
            Flow *t = mTarget;
            mRunningNodes.push_back(t);
            bool running = mTarget->Activate(this);
            if (running) {
                return true;
            }
            Flow *t2 = mTarget;
            mRunningNodes.remove(t2);
        }
    }
    return false;
}


void FlowRun::ResolveTarget() {
    if (mTarget)
        return;
    if (!mTargetName.c_str()[0])
        return;
    ObjectDir *targetDir = mTargetDir;
    if (!targetDir) {
        Flow *ownerFlow = GetOwnerFlow();
        DirLoader *loader = ownerFlow->Loader();
        if (loader) {
            targetDir = loader->ProxyDir();
        } else {
            targetDir = ownerFlow->Dir();
        }
        MILO_ASSERT(targetDir, 0x72);
    }
#ifdef HX_NATIVE
    // MILO_ASSERT is the guard here, and on native it does not stop: Debug::Fail
    // prints and returns, so a null `targetDir` reaches the Find<Flow>() below.
    // ObjectDir::Find is a non-virtual template that immediately calls
    // FindObject -> FindEntry -> mHashTable.Find(name) through a null `this`.
    // The 360 maps guest page 0 readable/writable/zeroed, so those reads return
    // zeroes rather than faulting; Linux has no page 0 and the same read
    // SIGSEGVs. This is a MEMORY-MAP difference, not a pointer-width one -- the
    // offsets stay far inside 0x10000 either way. Leave mTarget null (it
    // already is: the early-out at the top of this function returned if it was
    // set) and give up on resolving, which is what the assert meant.
    if (targetDir == nullptr) {
        return;
    }
#endif
    mTarget = targetDir->Find<Flow>(mTargetName.c_str(), false);
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
