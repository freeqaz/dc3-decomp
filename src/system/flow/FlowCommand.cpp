#include "flow\FlowCommand.h"
#include "flow\FlowNode.h"
#include "obj\Data.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "flow\Flow.h"
#include "obj\Utl.h"

FlowCommand::FlowCommand() : mObject(this), mHandler(0) {}
FlowCommand::~FlowCommand() {}

BEGIN_HANDLERS(FlowCommand)
    HANDLE_SUPERCLASS(FlowNode)
END_HANDLERS

BEGIN_PROPSYNCS(FlowCommand)
    SYNC_SUPERCLASS(FlowNode)
END_PROPSYNCS

BEGIN_SAVES(FlowCommand)
    SAVE_REVS(3, 0)
    int size = mTypeProps ? mTypeProps->Map()->Size() : 0;
    DataArrayPtr ptr(new DataArray(size));
    bs << size;
    for (int i = 0; i < size; i++) {
        bs << mTypeProps->Map()->Evaluate(i);
        ptr->Node(i) = mTypeProps->Map()->Evaluate(i);
    }
    ClearAllTypeProps();
    SAVE_SUPERCLASS(FlowNode)
    for (int i = 0; i < size; i += 2) {
        SetProperty(ptr->Sym(i), ptr->Node(i + 1));
    }
    bs << mObject;
    bs << mHandler;
END_SAVES

void FlowCommand::Copy(const Hmx::Object *o, Hmx::Object::CopyType ty) {
    FlowNode::Copy(o, ty);
    const FlowCommand *c = dynamic_cast<const FlowCommand *>(o);
    if (c) {
        mObject = c->mObject;
        mHandler = c->mHandler;
        PushDrivenProperties();
    }
}

INIT_REVS(3, 0)

BEGIN_LOADS(FlowCommand)
    LOAD_REVS(bs)
    ASSERT_REVS(3, 0)

    // MEASURED, 2026-09-14 (lane w7-aa).  The residual 6 rows of this function
    // are these two lists sitting in each other's frame slot: the image has
    // list<Symbol> at r31+0x70 and list<DataNode> at r31+0x80 (read the two
    // insert() call sites at 0x8241D9DC / 0x8241DA08), we have them the other
    // way round.  Swapping THESE TWO DECLARATIONS does not move the slots --
    // it flips only the construction/destruction ORDER, taking the ctor block
    // (idx 86-100) and the dtor block (408/410) with it: 6 rows -> 20 rows,
    // 99.96619 -> 99.90.  So MSVC is not assigning these slots by declaration
    // order here, and the lever is something else.  Do not re-try the swap.
    std::list<DataNode> datanodes;
    std::list<Symbol> symbols;
    if (d.rev > 2) {
        int count;
        bs >> count;
        Flow *owner = GetOwnerFlow();
        // The owner's *loading* dir, not its current one: while a proxy is being
        // streamed in, DirLoader::ProxyDir() is where the objects these DataNodes
        // name actually live. Same spelling as FlowIf::Load / Flow::PostLoad; the
        // target inlines it here (lwz r11,0xb4(r3) = owner->Loader(), then 0xac =
        // ProxyDir()) at all three DataNode-loading sites in this function.
        DirLoader *loader = owner->Loader();
        ObjectDir *dir = loader ? loader->ProxyDir() : owner->Dir();
        for (int i = 0; i < count; i += 2) {
            DataNode n;
            n.Load(bs, dir);
            symbols.push_back(n.Sym());
            DataNode n2;
            n2.Load(bs, dir);
            datanodes.push_back(n2);
        }
    }
    FlowNode::Load(bs);
    ClearAllTypeProps();
    auto sit = symbols.begin();
    for (auto dit = datanodes.begin(); dit != datanodes.end(); ++sit, ++dit) {
        SetProperty(*sit, *dit);
    }
    if (d.rev < 1) {
        mObject = LoadObjectFromMainOrDir(bs, Dir());
    } else {
        bs >> mObject;
    }
    bs >> mHandler;
    if (d.rev < 2) {
        DataNode n;
        Flow *owner = GetOwnerFlow();
        DirLoader *loader = owner->Loader();
        ObjectDir *dir = loader ? loader->ProxyDir() : owner->Dir();
        n.Load(bs, dir);
        if (n.Type() == kDataArray) {
            for (int i = 0; i < n.Array()->Size(); i++) {
                SetProperty(n.Array()->Array(i)->Sym(0), n.Array()->Array(i)->Node(1));
            }
        }
    } else if (d.rev < 3) {
        // The `= 0` is the target's: it stores the zero constant into this slot
        // before the ReadEndian, which the rev>2 arm above does not do.
        int count = 0;
        bs >> count;
        Flow *owner = GetOwnerFlow();
        DirLoader *loader = owner->Loader();
        ObjectDir *dir = loader ? loader->ProxyDir() : owner->Dir();
        for (int i = 0; i < count; i += 2) {
            DataNode n1;
            n1.Load(bs, dir);
            DataNode n2;
            n2.Load(bs, dir);
            SetProperty(n1.Sym(), n2.Evaluate());
        }
    }
    DataNode handlerDef = GetHandlerDef();
    if (handlerDef.Type() == kDataArray) {
        DataArray *a = handlerDef.Array();
        if (a && a->Size() > 2) {
            for (int i = 2; i < a->Size(); i++) {
                DataArray *prop = a->Array(i);
                if (!Property(prop->Sym(0), false)) {
                    SetProperty(prop->Sym(0), prop->Node(1));
                }
            }
        }
    }
END_LOADS

bool FlowCommand::Activate() {
    FLOW_LOG("Activate\n");
    mStopRequested = false;
    PushDrivenProperties();
    if (mObject && !mHandler.Null()) {
        int size = mTypeProps ? mTypeProps->Size() : 0;
        Message msg(size);
        msg.SetType(mHandler);
        for (int i = 0; i < size; i++) {
            msg[i] = mTypeProps->Map()->Evaluate(2 * i + 1);
        }
        mObject->Handle(msg, false);
    }
    return false;
}

DataNode FlowCommand::GetHandlerDef() {
    if (!mObject || mHandler.Null()) {
        return 0;
    }
    DataArray *typeDef = mObject->TypeDef();
    if (typeDef && typeDef->FindArray("flow_commands", false)) {
        DataArray *cmds = typeDef->FindArray("flow_commands");
        if (cmds->FindArray(mHandler, false)) {
            return cmds->FindArray(mHandler);
        } else {
            for (int i = 1; i < cmds->Size(); i++) {
                if (cmds->Type(i) == kDataArray) {
                    if (cmds->Array(i)->Sym(0) == mHandler) {
                        return cmds->Array(i);
                    }
                }
            }
        }
    }
    std::vector<Symbol> superClasses;
    superClasses.push_back(mObject->ClassName());
    ListSuperClasses(mObject->ClassName(), superClasses);
    for (int i = 0; i < superClasses.size(); i++) {
        DataArray *cmds =
            mObject->ObjectDef(superClasses[i])->FindArray("flow_commands", false);
        if (cmds) {
            DataArray *ret = cmds->FindArray(mHandler, false);
            if (ret) {
                return ret;
            }
            for (int i = 1; i < cmds->Size(); i++) {
                if (cmds->Type(i) == kDataArray) {
                    if (cmds->Array(i)->Sym(0) == mHandler) {
                        return cmds->Array(i);
                    }
                }
            }
        }
    }
    return 0;
}
