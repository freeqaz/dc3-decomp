#include "flow/DrivenPropertyEntry.h"
#include "flow/FlowNode.h"
#include "obj/Object.h"
#include "utl/BinStream.h"

DrivenPropertyEntry::DrivenPropertyEntry(Hmx::Object *owner) : mMathOps(owner) {
    static Symbol none("none");
    mNode = none;
}

DrivenPropertyEntry::~DrivenPropertyEntry() { mMathOps.clear(); }

void DrivenPropertyEntry::Load(BinStream &bs, FlowNode *node) {
    int rev;
    bs >> rev;

    int revLow = (int)(u16)rev;
    int revHigh = (int)rev >> 16;

    if (revLow > 0) {
        TheDebug.Fail(MakeString("can't load new %s version %d", PathName(node->Dir()), revLow), 0);
    }
    if (revHigh > 0) {
        TheDebug.Fail(MakeString("can't load new %s alt version", PathName(node->Dir())), 0);
    }

    bs >> mNode;
    int numOps;
    bs >> numOps;

    mMathOps.clear();
    mMathOps.reserve(numOps);
    for (int i = 0; i < numOps; i++) {
        FlowMathOp op(node);
        op.Load(bs, node->Dir());
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
