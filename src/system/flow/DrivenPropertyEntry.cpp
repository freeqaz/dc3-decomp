#include "flow\DrivenPropertyEntry.h"
#include "flow\Flow.h"
#include "flow\FlowNode.h"
#include "obj\Dir.h"
#include "obj\Object.h"
#include "utl\BinStream.h"

DrivenPropertyEntry::DrivenPropertyEntry(Hmx::Object *owner) : mMathOps(owner) {
    static Symbol none("none");
    mNode = none;
}

DrivenPropertyEntry::~DrivenPropertyEntry() { mMathOps.clear(); }

INIT_REVS(0, 0)

void DrivenPropertyEntry::Load(BinStream &bs, FlowNode *n) {
    ObjectDir *dir = n->GetOwnerFlow();
    LOAD_REVS(bs)
    if (d.rev > 0) {
        MILO_FAIL(
            "%s can't load new %s version %d > %d",
            PathName(dir),
            "DrivenPropertyEntry",
            d.rev,
            gRev
        );
    }
    if (d.altRev > 0) {
        MILO_FAIL(
            "%s can't load new %s alt version %d > %d",
            PathName(dir),
            "DrivenPropertyEntry",
            d.altRev,
            gAltRev
        );
    }
    d >> mNode;
    int numOps;
    d >> numOps;
    mMathOps.clear();
    for (int i = 0; i < numOps; i++) {
        FlowMathOp op(n);
        op.Load(bs, dir);
        mMathOps.push_back(op);
    }
}

void DrivenPropertyEntry::Save(BinStream &bs) {
    bs << 0;
    bs << mNode;
    bs << mMathOps.size();
    for (ObjVector<FlowMathOp>::iterator it = mMathOps.begin(); it != mMathOps.end();
         ++it) {
        it->Save(bs);
    }
}
