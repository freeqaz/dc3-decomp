#include "flow\FlowSwitchCase.h"
#include "flow\DrivenPropertyEntry.h"
#include "flow\Flow.h"
#include "flow\FlowManager.h"
#include "flow\FlowNode.h"
#include "flow\FlowWhile.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "world\CameraShot.h"

FlowSwitchCase::FlowSwitchCase()
    : mToValue(0), mFromValue(0), mOperator(kEqual), mUseLastValue(0),
      mUnregisterParent(0), mContinuous(0) {
    mFlowParent = nullptr;
}

FlowSwitchCase::~FlowSwitchCase() { TheFlowMgr->CancelCommand(this); }

bool FlowSwitchCase::IsValidCase(
    FlowNode *node, DataNode *curValue, const DataNode *lastValue, bool hasLast
) {
    // ONE result variable for the whole function, held in r30 and returned by the
    // single `mr r3, r30` at 0x82408E8C.  Only the two hard `return false`s -- the
    // type mismatch and the switch default -- bypass it, and they share the
    // `li r3, 0` / `b` at 0x82408AE8 (the default case branches into it from
    // 0x82408BD0).  Writing each arm as its own `return` duplicates the inlined
    // ~DataNode into every false path instead of funnelling through one tail.
    bool result;
    PushDrivenProperties();
    if (mOperator == kTransition) {
        if (mUseLastValue) {
            mFromValue = *lastValue;
        }
        if (curValue->Type() != mToValue.Node().Type()
            || curValue->Type() != mFromValue.Node().Type()) {
            return false;
        }
        result = curValue->Equal(mToValue.Node(), nullptr, true)
            && lastValue->Equal(mFromValue.Node(), nullptr, true);
    } else {
        if (mUseLastValue) {
            mToValue = *lastValue;
        }
        // Each comparison arm binds Node()'s return buffer to a REFERENCE, not to a
        // by-value `DataNode to`.  0x82408BE0 `mr r30, r3` keeps the returned
        // pointer and 0x82408C00 reads `lwz r11, 0x4(r30)` through it; a by-value
        // local is addressed at a fixed r31 displacement instead, which lets MSVC
        // hoist the load above the first type test and kills the pair of home
        // stores at 0x82408C08/0x82408C10 that mark `to.Type()` being written
        // twice in the source.
        switch (mOperator) {
        case kEqual:
            result = curValue->Equal(mToValue.Node(), nullptr, true);
            break;
        case kNotEqual:
            result = *curValue != mToValue.Node();
            break;
        case kGreaterThan: {
            const DataNode &to = mToValue.Node();
            if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
                && (to.Type() == kDataInt || to.Type() == kDataFloat)) {
                result = curValue->LiteralFloat() > to.LiteralFloat();
            } else {
                result = false;
            }
            break;
        }
        case kGreaterThanOrEqual: {
            const DataNode &to = mToValue.Node();
            if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
                && (to.Type() == kDataInt || to.Type() == kDataFloat)) {
                result = curValue->LiteralFloat() >= to.LiteralFloat();
            } else {
                result = false;
            }
            break;
        }
        case kLessThan: {
            const DataNode &to = mToValue.Node();
            if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
                && (to.Type() == kDataInt || to.Type() == kDataFloat)) {
                result = curValue->LiteralFloat() < to.LiteralFloat();
            } else {
                result = false;
            }
            break;
        }
        case kLessThanOrEqual: {
            const DataNode &to = mToValue.Node();
            if ((curValue->Type() == kDataInt || curValue->Type() == kDataFloat)
                && (to.Type() == kDataInt || to.Type() == kDataFloat)) {
                result = curValue->LiteralFloat() <= to.LiteralFloat();
            } else {
                result = false;
            }
            break;
        }
        default:
            return false;
        }
    }
    return result;
}

BEGIN_HANDLERS(FlowSwitchCase)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowSwitchCase)
    SYNC_PROP_MODIFY(use_last_value, mUseLastValue, UseLastValueChanged())
    SYNC_PROP(operator,(int &) mOperator)
    SYNC_PROP(to_value, mToValue)
    SYNC_PROP(from_value, mFromValue)
    SYNC_PROP(unregister_parent, mUnregisterParent)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowSwitchCase)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(FlowNode)
    if (mToValue.Node().Type() == kDataObject) {
        mToValue.Node().Save(bs);
    } else {
        bs << mToValue.Node().Type();
        mToValue.Node().Save(bs);
    }
    bs << mOperator;
    if (mFromValue.Node().Type() == kDataObject) {
        mFromValue.Node().Save(bs);
    } else {
        bs << mFromValue.Node().Type();
        mFromValue.Node().Save(bs);
    }
    bs << mUseLastValue;
    bs << mUnregisterParent;
END_SAVES

BEGIN_COPYS(FlowSwitchCase)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY(FlowSwitchCase)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mToValue)
        COPY_MEMBER(mOperator)
        COPY_MEMBER(mFromValue)
        COPY_MEMBER(mUseLastValue)
        COPY_MEMBER(mUnregisterParent)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(3, 0)

BEGIN_LOADS(FlowSwitchCase)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(FlowNode)

    if (d.rev < 2) {
        DataNode n;
        d >> n;
        mToValue = n;
    } else {
        int type;
        d >> type;
        if (type == kDataObject) {
            Flow *owner = GetOwnerFlow();
            if (!owner) {
                owner = dynamic_cast<Flow *>(this);
            }
            DirLoader *loader = owner->Loader();
            ObjectDir *dir = loader ? loader->ProxyDir() : owner->Dir();
            mToValue = LoadObjectFromMainOrDir(d.stream, dir);
        } else {
            DataNode n;
            d >> n;
            mToValue = n;
        }
    }

    d >> (int &)mOperator;

    if (d.rev < 2) {
        DataNode n;
        d >> n;
        mFromValue = n;
    } else {
        int type;
        d >> type;
        if (type == kDataObject) {
            Flow *owner = GetOwnerFlow();
            if (!owner) {
                owner = dynamic_cast<Flow *>(this);
            }
            DirLoader *loader = owner->Loader();
            ObjectDir *dir = loader ? loader->ProxyDir() : owner->Dir();
            mFromValue = LoadObjectFromMainOrDir(d.stream, dir);
        } else {
            DataNode n;
            d >> n;
            mFromValue = n;
        }
    }

    if (d.rev > 0) {
        d >> mUseLastValue;
    }

    if (d.rev > 2) {
        d >> mUnregisterParent;
    }
END_LOADS

bool FlowSwitchCase::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    if (mFlowParent->ClassName() == FlowWhile::StaticClassName()
        && mOperator != kTransition) {
        mContinuous = true;
        FlowNode::Activate();
        if (mUnregisterParent) {
            TheFlowMgr->QueueCommand(this, kQueue);
        }
        return true;
    } else {
        return FlowNode::Activate();
    }
}

void FlowSwitchCase::Deactivate(bool b1) {
    mContinuous = false;
    FlowNode::Deactivate(b1);
}

void FlowSwitchCase::ChildFinished(FlowNode *n) {
    FLOW_LOG("Child Finished of class:%s\n", n->ClassName());
    if (mContinuous && mOperator != kTransition) {
        mRunningNodes.remove(n);
    } else {
        mContinuous = false;
        FlowNode::ChildFinished(n);
    }
}

void FlowSwitchCase::RequestStop() {
    FLOW_LOG("RequestStop\n");
    FlowNode::RequestStop();
    if (mContinuous) {
        TheFlowMgr->QueueCommand(this, kIgnore);
    }
}

void FlowSwitchCase::RequestStopCancel() {
    FLOW_LOG("RequestStopCancel\n");
    TheFlowMgr->CancelCommand(this);
    FlowNode::RequestStopCancel();
}

void FlowSwitchCase::Execute(QueueState qs) {
    FLOW_LOG("Execute: state = %i\n", (CamShotFrame::BlendEaseMode)qs);
    if (qs == kQueue) {
        FlowWhile *propEventListener = static_cast<FlowWhile *>(mFlowParent);
        propEventListener->UnregisterEvents(propEventListener);
        if (!mContinuous)
            return;
    } else if (qs != kIgnore) {
        return;
    }
    mContinuous = false;
    if (!FlowNode::IsRunning() && mFlowParent->HasRunningNode(this)) {
        mFlowParent->ChildFinished(this);
    }
}

bool FlowSwitchCase::IsRunning() {
    if (mContinuous)
        return true;
    else
        return FlowNode::IsRunning();
}

void FlowSwitchCase::UseLastValueChanged() {
    if (mUseLastValue) {
        DrivenPropertyEntry *entry = GetDrivenEntry("to_value");
        if (entry) {
            auto it = mDrivenPropEntries.begin();
            for (; it != mDrivenPropEntries.end() && entry != &(*it); ++it)
                ;
            if (it != mDrivenPropEntries.end()) {
                mDrivenPropEntries.erase(it);
            }
        }
    }
}
