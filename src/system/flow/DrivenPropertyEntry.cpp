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
    bs >> mNode;
    int numOps;
    bs >> numOps;
#ifdef HX_NATIVE
    // Cap at reasonable count to avoid garbage-driven allocation
    if (numOps < 0 || numOps > 256) {
        numOps = 0;
    }
#endif
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
