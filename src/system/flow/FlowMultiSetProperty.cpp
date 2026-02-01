#include "flow/FlowMultiSetProperty.h"
#include "flow/DrivenPropertyEntry.h"
#include "flow/FlowNode.h"
#include "obj/Data.h"
#include "obj/Object.h"

FlowMultiSetProperty::FlowMultiSetProperty()
    : unk5c(this, (EraseMode)1, kObjListNoNull) {}

FlowMultiSetProperty::~FlowMultiSetProperty() {}

BEGIN_PROPSYNCS(FlowMultiSetProperty)
    SYNC_PROP(targets, unk5c)
    SYNC_PROP(value, unk78)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowMultiSetProperty)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(FlowNode)
    bs << unk5c;
    bs << unk78 << unk80;
END_SAVES

BEGIN_COPYS(FlowMultiSetProperty)
    COPY_SUPERCLASS(FlowNode)
    CREATE_COPY_AS(FlowMultiSetProperty, c)
    BEGIN_COPYING_MEMBERS_FROM(c)
        COPY_MEMBER(unk5c)
        COPY_MEMBER(unk78)
        COPY_MEMBER(unk80)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(FlowMultiSetProperty)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(FlowNode)
    bs >> unk5c;
    bs >> unk78 >> unk80;
END_LOADS

bool FlowMultiSetProperty::Activate() {
    FLOW_LOG("Activate\n");
    unk58 = false;
    if (!unk5c.empty()) {
        DrivenPropertyEntry *node = GetDrivenEntry("value");
        if (node != nullptr) {
            unk80 = unk5c.front()->Property(unk78.Array(), true)->Evaluate();
        }
    }
    FlowNode::PushDrivenProperties();
    for (ObjPtrVec<Hmx::Object>::iterator it = unk5c.begin(); it != unk5c.end(); ++it) {
        Hmx::Object *obj = *it;
        if (obj != nullptr) {
            obj->SetProperty(unk78.Array(), unk80);
        }
    }
    return false;
}

BEGIN_HANDLERS(FlowMultiSetProperty)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS
