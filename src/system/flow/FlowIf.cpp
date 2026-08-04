#include "flow/FlowIf.h"
#include "flow/FlowNode.h"
#include "flow/Flow.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"

FlowIf::FlowIf() : mValue1(0), mValue2(0), mOperator(kEqual) {}
FlowIf::~FlowIf() {}

BEGIN_HANDLERS(FlowIf)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowIf)
    SYNC_PROP(first_value, mValue1)
    SYNC_PROP(second_value, mValue2)
    SYNC_PROP(operator,(int &) mOperator)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowIf)
    SAVE_REVS(3, 0)
    SAVE_SUPERCLASS(FlowNode)
    if (mValue1.Node().Type() == kDataObject) {
        mValue1.Node().Save(bs);
    } else {
        bs << mValue1.Node().Type();
        mValue1.Node().Save(bs);
    }
    if (mValue2.Node().Type() == kDataObject) {
        mValue2.Node().Save(bs);
    } else {
        bs << mValue2.Node().Type();
        mValue2.Node().Save(bs);
    }
    bs << mOperator;
END_SAVES

BEGIN_COPYS(FlowIf)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY(FlowIf)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mValue1)
        COPY_MEMBER(mValue2)
        COPY_MEMBER(mOperator)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(3, 0)

BEGIN_LOADS(FlowIf)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)
    LOAD_SUPERCLASS(FlowNode)
    if (d.rev < 3) {
        DataNode n1, n2;
        n1.Load(bs);
        n2.Load(bs);
        mValue1 = n1;
        mValue2 = n2;
    } else {
        int type1;
        bs >> type1;
        if (type1 == kDataObject) {
            Flow *owner = GetOwnerFlow();
            if (!owner) {
                owner = dynamic_cast<Flow *>(this);
            }
            DirLoader *dl = owner->Loader();
            ObjectDir *dir = dl ? dl->ProxyDir() : owner->Dir();
            mValue1 = LoadObjectFromMainOrDir(d.stream, dir);
        } else {
            DataNode val;
            val.Load(bs);
            mValue1 = val;
        }
        int type2;
        bs >> type2;
        if (type2 == kDataObject) {
            Flow *owner = GetOwnerFlow();
            if (!owner) {
                owner = dynamic_cast<Flow *>(this);
            }
            DirLoader *dl = owner->Loader();
            ObjectDir *dir = dl ? dl->ProxyDir() : owner->Dir();
            mValue2 = LoadObjectFromMainOrDir(d.stream, dir);
        } else {
            DataNode val;
            val.Load(bs);
            mValue2 = val;
        }
    }
    bs >> (int &)mOperator;
    if (d.rev == 1) {
        int x;
        bs >> x;
    }
END_LOADS

/** Compare two numeric DataNodes.
 *
 *  Kept as a helper taking both operands by reference: the target evaluates
 *  mValue1.Node()/mValue2.Node() as unnamed argument temporaries (so
 *  right-to-left, mValue2 first) and drives every use off the pointer Node()
 *  returned, exactly the shape the kNotEqual case's operator!= call produces.
 *  Named DataNode locals instead pin stack slots and re-materialise their
 *  addresses. */
__forceinline static bool CompareLiterals(
    const DataNode &n1, const DataNode &n2, FlowNode::OperatorType op
) {
    if (n1.Type() != kDataInt && n1.Type() != kDataFloat) {
        return false;
    } else if (n2.Type() != kDataInt && n2.Type() != kDataFloat) {
        return false;
    }
    switch (op) {
    case FlowNode::kGreaterThan:
        return n1.LiteralFloat() > n2.LiteralFloat();
    case FlowNode::kGreaterThanOrEqual:
        return n1.LiteralFloat() >= n2.LiteralFloat();
    case FlowNode::kLessThan:
        return n1.LiteralFloat() < n2.LiteralFloat();
    default:
        return n1.LiteralFloat() <= n2.LiteralFloat();
    }
}

bool FlowIf::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    if (IsRunning()) {
        MILO_NOTIFY(
            "FlowIf re-entrance error, activated when already running, deactivating and aborting, check your logic"
        );
        Deactivate(false);
        return false;
    }
    PushDrivenProperties();
    bool activate = false;
    switch (mOperator) {
    case kEqual:
        activate = mValue1.Node().Equal(mValue2.Node(), nullptr, true);
        break;
    case kNotEqual:
        activate = mValue1.Node() != mValue2.Node();
        break;
    case kGreaterThan:
        activate = CompareLiterals(mValue1.Node(), mValue2.Node(), kGreaterThan);
        break;
    case kGreaterThanOrEqual:
        activate = CompareLiterals(mValue1.Node(), mValue2.Node(), kGreaterThanOrEqual);
        break;
    case kLessThan:
        activate = CompareLiterals(mValue1.Node(), mValue2.Node(), kLessThan);
        break;
    case kLessThanOrEqual:
        activate = CompareLiterals(mValue1.Node(), mValue2.Node(), kLessThanOrEqual);
        break;
    default:
        break;
    }
    if (activate) {
        FlowNode::Activate();
    }
    return !mRunningNodes.empty();
}
