#include "flow/FlowNode.h"
#include "flow/DrivenPropertyEntry.h"
#include "flow/FlowLabel.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "obj/Utl.h"
#include "os/Debug.h"
#include "flow/Flow.h"

float FlowNode::sIntensity = 1.0f;
bool FlowNode::sPushDrivenProperties = false;

#pragma region Hmx::Object

FlowNode::FlowNode()
    : mChildNodes(this, (EraseMode)0, kObjListNoNull), mRunningNodes(this),
      mFlowParent(nullptr), mDrivenPropEntries(this), mStopRequested(0) {
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
    FOREACH (it, mChildNodes) {
        (*it)->SetParent(this, false);
    }

    int numEntries;
    d >> numEntries;
#ifdef HX_NATIVE
    if (numEntries < 0 || numEntries > 256) {
        fprintf(stderr, "FlowNode::Load ABORT: bad numEntries=%d for %s '%s'\n", numEntries, ClassName(), Name());
        abort();
    }
#endif
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
        mDebugOutput = unk;
    }
    if (d.rev > 1) {
        String debugComment;
        d.stream >> debugComment;
        mDebugComment = debugComment;
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
    mStopRequested = false;
    FOREACH (it, mChildNodes) {
        ActivateChild(*it);
        if (mStopRequested)
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
    mStopRequested = true;
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
    mStopRequested = false;
    FOREACH (it, mRunningNodes) {
        (*it)->RequestStopCancel();
    }
}

Flow *FlowNode::GetOwnerFlow() {
    ObjectDir *dir = Dir();
    if (dir != 0)
        return static_cast<Flow *>(dir);
    return 0;
}

void FlowNode::MiloPreRun() {
    FOREACH (it, mChildNodes) {
        (*it)->MiloPreRun();
    }
}

void FlowNode::MoveIntoDir(ObjectDir *from, ObjectDir *to) {
    // Move all child nodes into the new directory
    FOREACH (it, mChildNodes) {
        (*it)->MoveIntoDir(from, to);
    }
}

void FlowNode::UpdateIntensity() {
    FOREACH (it, mRunningNodes) {
        (*it)->UpdateIntensity();
    }
}

FlowNode *FlowNode::DuplicateChild(FlowNode *child) {
    // If child is a Flow (directory), create a duplicate Flow in the same dir
    Flow *childFlow = dynamic_cast<Flow *>(child);
    if (!childFlow) {
        // Duplicate non-flow child: create same type and copy
        Symbol sym = child->ClassName();
        Hmx::Object *newObj = Hmx::Object::NewObject(sym);
        FlowNode *newNode = dynamic_cast<FlowNode *>(newObj);
        if (newNode) {
            newNode->Copy(child, kCopyDeep);
            // Generate unique name
            ObjectDir *dir = child->Dir();
            if (dir) {
                const char *uniqueName = NextName(child->Name(), dir);
                newNode->SetName(uniqueName, dir);
            }
        }
        return newNode;
    } else {
        // Duplicate Flow child
        Symbol flowClass = Flow::StaticClassName();
        Hmx::Object *newObj = Hmx::Object::NewObject(flowClass);
        FlowNode *newNode = dynamic_cast<FlowNode *>(newObj);
        if (newNode) {
            newNode->Copy(child, kCopyDeep);
        }
        return newNode;
    }
}

void FlowNode::PushDrivenProperties() {
    sPushDrivenProperties = true;
    FOREACH (it, mDrivenPropEntries) {
        DrivenPropertyEntry &entry = *it;
        ObjVector<FlowMathOp> &mathOps =
            const_cast<ObjVector<FlowMathOp> &>(entry.MathOps());
        DataNode targetValue(kDataUndef, 0);

        if (!mathOps.empty()) {
            FlowMathOp &firstOp = mathOps[0];
            Hmx::Object *drivenObj = firstOp.DrivenObj();

            if (drivenObj) {
                const DataNode &rhsNode = firstOp.Rhs();
                if (rhsNode.Type() == kDataArray) {
                    const DataNode *drivenVal =
                        drivenObj->Property(rhsNode.Array(), false);
                    if (drivenVal)
                        targetValue = *drivenVal;
                }
            }
        }

        DataArray *propPath = entry.Node().Array();
        if (propPath) {
            SetProperty(propPath, targetValue);
        }
    }
}

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
                    return &(*it);
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
    mStopRequested = false;
    mRunningNodes.push_back(label);
    if (!label->Activate(this)) {
        mRunningNodes.remove(label);
    }
}

Hmx::Object *FlowNode::LoadObjectFromMainOrDir(BinStream &bs, ObjectDir *dir) {
    Symbol sym;
    bs >> sym;
    if (sym == "")
        return nullptr;

    // Try main dir first
    Hmx::Object *obj = ObjectDir::Main()->FindObject(sym.Str(), false, true);
    if (obj)
        return obj;

    // Try the passed dir
    if (dir) {
        obj = dir->FindObject(sym.Str(), false, true);
        if (obj)
            return obj;

        // Walk up the dir hierarchy via Loader chain
        DirLoader *loader = dir->Loader();
        ObjectDir *parentDir = nullptr;
        if (loader) {
            parentDir = loader->GetDir();
        } else {
            parentDir = dir->Dir();
        }
        if (parentDir && parentDir != dir) {
            obj = parentDir->FindObject(sym.Str(), false, true);
            if (obj)
                return obj;

            // One more level up
            DirLoader *parentLoader = parentDir->Loader();
            ObjectDir *grandparentDir = nullptr;
            if (parentLoader) {
                grandparentDir = parentLoader->GetDir();
            } else {
                grandparentDir = parentDir->Dir();
            }
            if (grandparentDir && grandparentDir != parentDir) {
                obj = grandparentDir->FindObject(sym.Str(), false, true);
                if (obj)
                    return obj;
            }
        }
    }

    return nullptr;
}
