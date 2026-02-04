#include "flow/FlowNode.h"
#include "flow/DrivenPropertyEntry.h"
#include "flow/FlowLabel.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "flow/Flow.h"

float FlowNode::sIntensity = 1.0f;
bool FlowNode::sPushDrivenProperties = false;

#pragma region Hmx::Object

FlowNode::FlowNode()
    : mChildNodes(this, (EraseMode)0, kObjListNoNull), mRunningNodes(this),
      mFlowParent(nullptr), mDrivenPropEntries(this), unk58(0) {
    mDebugOutput = false;
}

FlowNode::~FlowNode() {
    if (!mRunningNodes.empty()) {
        Deactivate(true);
    }
    while (!mChildNodes.empty()) {
        FlowNode *cur = mChildNodes.front();
        delete cur;
    }
}

BEGIN_HANDLERS(FlowNode)
    HANDLE_ACTION(activate, Activate());
    HANDLE_ACTION(deactivate, Deactivate(false));
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(FlowNode)
    SYNC_PROP_SET(comment, Note(), SetNote(_val.Str()))
    SYNC_PROP(debug_output, mDebugOutput)
    SYNC_PROP(debug_comment, mDebugComment)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(FlowNode)
    SAVE_REVS(2, 0)
    if (!dynamic_cast<Flow *>(this)) {
        SAVE_SUPERCLASS(Hmx::Object)
    }
    ObjPtrVec<FlowNode> flowNodes(this);
    FOREACH (it, mChildNodes) {
        if ((*it)->Dir() == Dir()) {
            flowNodes.push_back(*it);
        }
    }
    bs << flowNodes;
    bs << (int)mDrivenPropEntries.size();
    FOREACH (it, mDrivenPropEntries) {
        it->Save(bs);
    }
    bs << mDebugOutput;
    bs << mDebugComment;
END_SAVES

BEGIN_COPYS(FlowNode)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(FlowNode)
    BEGIN_COPYING_MEMBERS
        if (!dynamic_cast<Flow *>(this)) {
            FOREACH (it, c->mChildNodes) {
                FlowNode *n = DuplicateChild(*it);
                if (n) {
                    n->SetParent(this, true);
                }
            }
        }
        COPY_MEMBER(mDrivenPropEntries)
    END_COPYING_MEMBERS
END_COPYS

void FlowNode::Load(BinStream &bs) {
    int revs;
    bs >> revs;
    BinStreamRev d(bs, revs);

    static const unsigned short gRevs[4] = { 2, 0, 0, 0 };
    if (d.rev > 2) {
        MILO_FAIL(
            "%s can't load new %s version %d > %d",
            PathName(this),
            ClassName(),
            d.rev,
            gRevs[0]
        );
    }
    if (d.altRev > 0) {
        MILO_FAIL(
            "%s can't load new %s alt version %d > %d",
            PathName(this),
            ClassName(),
            d.altRev,
            gRevs[2]
        );
    }

    if (!dynamic_cast<Flow *>(this)) {
        Hmx::Object::Load(d.stream);
    }

    mChildNodes.Load(d.stream, true, nullptr);

    // Call SetParent on each loaded child node
    for (auto it = mChildNodes.begin(); it != mChildNodes.end(); ++it) {
        (*it)->SetParent(this, false);
    }

    int numEntries;
    d >> numEntries;
    mDrivenPropEntries.clear();
    mDrivenPropEntries.reserve(numEntries);
    for (int i = 0; i < numEntries; i++) {
        DrivenPropertyEntry entry(this);
        entry.Load(d.stream, this);
        mDrivenPropEntries.push_back(entry);
    }

    if (d.rev > 0) {
        bool unk;
        d >> unk;
        unk58 = unk;
    }
    if (d.rev > 1) {
        d >> mDebugComment;
    }
}

const char *FlowNode::FindPathName() {
    ObjectDir *dir = dynamic_cast<ObjectDir *>(this);
    if (dir) {
        return dir->Hmx::Object::FindPathName();
    } else {
        Flow *flow = GetOwnerFlow();
        return MakeString("%s:%s:%s", Name(), ClassName(), flow->FindPathName());
    }
}

#pragma endregion
#pragma region FlowNode

void FlowNode::SetParent(class FlowNode *new_parent, bool b) {
    if (mFlowParent != new_parent) {
        if (mFlowParent != nullptr) {
            mFlowParent->mChildNodes.remove(this);
        }
        mFlowParent = new_parent;
        if (new_parent != nullptr && b) {
            new_parent->mChildNodes.push_back(this);
        }
    }
}

bool FlowNode::Activate() {
    FLOW_LOG("Activating Children\n");
    unk58 = false;
    FOREACH (it, mChildNodes) {
        ActivateChild(*it);
        if (unk58)
            break;
    }
    return !mRunningNodes.empty();
}

void FlowNode::Deactivate(bool b1) {
    FLOW_LOG("Deactivated\n");
    // Manually iterate with pre-increment to handle iterator invalidation
    // when ChildFinished() is called during node->Deactivate()
    auto it = mRunningNodes.begin();
    while (it != mRunningNodes.end()) {
        auto node = *it;
        ++it;
        node->Deactivate(b1);
    }
    mRunningNodes.clear();
}

void FlowNode::ChildFinished(FlowNode *node) {
    FLOW_LOG("Child Finished of class:%s\n", node->ClassName());
    mRunningNodes.remove(node);
    if (mRunningNodes.empty()) {
        FLOW_LOG("Releasing\n");
        if (mFlowParent)
            mFlowParent->ChildFinished(this);
    }
}

void FlowNode::RequestStop() {
    FLOW_LOG("RequestStop\n");
    unk58 = true;
    auto it = mRunningNodes.begin();
    while (it != mRunningNodes.end()) {
        auto next_it = it;
        next_it++;
        (*it)->RequestStop();
        it = next_it;
    }
}

void FlowNode::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    unk58 = false;
    FOREACH (it, mRunningNodes) {
        (*it)->RequestStopCancel();
    }
}

Flow *FlowNode::GetOwnerFlow() {
    ObjectDir* dir = Dir();
    if (dir) {
        return static_cast<Flow *>(dir);
    } else
        return nullptr;
}

void FlowNode::MiloPreRun() {
    FOREACH (it, mChildNodes) {
        (*it)->MiloPreRun();
    }
}

// void FlowNode::MoveIntoDir(ObjectDir *, ObjectDir *) {}

void FlowNode::UpdateIntensity() {
    FOREACH (it, mRunningNodes) {
        (*it)->UpdateIntensity();
    }
}

// FlowNode *FlowNode::DuplicateChild(FlowNode *) { return nullptr; }

// void FlowNode::PushDrivenProperties() { sPushDrivenProperties = true; }

void FlowNode::ActivateChild(FlowNode *child) {
    mRunningNodes.push_back(child);
    if (!child->Activate()) {
        FLOW_LOG(
            "Activated Child %s, which ran in full immediately.\n", child->ClassName()
        );
        mRunningNodes.remove(child);
    }
}

bool FlowNode::HasRunningNode(FlowNode *node) {
    return mRunningNodes.find(node) != mRunningNodes.end();
}

DrivenPropertyEntry *FlowNode::GetDrivenEntry(Symbol s) {
    DataArrayPtr ptr(new DataArray(1));
    ptr->Node(0) = s;
    return GetDrivenEntry(ptr);
}

DrivenPropertyEntry *FlowNode::GetDrivenEntry(DataArray *a) {
    FOREACH (it, mDrivenPropEntries) {
        if (it->Node().Type() == kDataArray) {
            DataArray *curArr = it->Node().Array();
            if (curArr->Size() == a->Size()) {
                bool b1 = true;
                for (int i = 0; i < curArr->Size(); i++) {
                    if (curArr->Node(i) != a->Node(i)) {
                        b1 = false;
                    }
                }
                if (b1) {
                    return it;
                }
            }
        }
    }
    return nullptr;
}

Flow *FlowNode::GetTopFlow() {
    Flow *pFlow = GetOwnerFlow();
    if (!pFlow)
        return static_cast<Flow *>(this);
    for (; pFlow->GetOwnerFlow() && pFlow->GetOwnerFlow() != pFlow;
         pFlow = pFlow->GetOwnerFlow())
        ;
    return pFlow;
}

void FlowNode::ActivateLabel(FlowLabel *label) {
    FLOW_LOG("Activating Label:%s\n", label->Label());
    unk58 = false;
    mRunningNodes.push_back(label);
    if (!label->Activate(this)) {
        mRunningNodes.remove(label);
    }
}
