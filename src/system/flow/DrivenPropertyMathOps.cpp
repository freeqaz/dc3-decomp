#include "flow/DrivenPropertyMathOps.h"

FlowMathOp &FlowMathOp::operator=(const FlowMathOp &other) {
    unk_0x0 = other.unk_0x0;
    unk_0x4 = other.unk_0x4;
    lhs = other.lhs;
    rhs = other.rhs;
    unk_0x18 = other.unk_0x18;
    return *this;
}

FlowMathOp::~FlowMathOp() {}

void FlowMathOp::Save(BinStream &bs) {
    bs << 6;
    bs << unk_0x4;
    bs << unk_0x0;
    bs << unk_0x18;
    bs << rhs;
    bs << lhs;
}

void FlowMathOp::Load(BinStream &bs, ObjectDir *dir) {
    int rev;
    bs >> rev;
#ifdef HX_NATIVE
    // Validate revision to detect stream desync early
    if (rev > 20) {
        return;
    }
#endif
    bs >> unk_0x4;
    bs >> unk_0x0;
    bs >> unk_0x18;
    bs >> rhs;
    bs >> lhs;
}

FlowMathOp::FlowMathOp(Hmx::Object *obj)
    : unk_0x0(0.0f), unk_0x4(0), unk_0x18(obj, nullptr) {}
