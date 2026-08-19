#pragma once
#include "flow\FlowPtr.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "types.h"
#include "utl/BinStream.h"

enum MathOpType {
    kMathOp_Add = 0,
    kMathOp_Subtract = 1,
    kMathOp_Multiply = 2,
    kMathOp_Divide = 3,
    kMathOp_Random = 4,
    kMathOp_Min = 5,
    kMathOp_Max = 6,
    kMathOp_Mod = 7,
    kMathOp_Round = 8,
    kMathOp_Floor = 9,
    kMathOp_Ceil = 10,
    kMathOp_NormalizeDb = 11,
    kMathOp_InvNormalizeDb = 12,
    kMathOp_Abs = 13,
    kMathOp_Sin = 14,
    kMathOp_Cos = 15,
    kMathOp_Pow = 16,
    kMathOp_Script = 17,
    kMathOp_ClampMin = 18,
    kNumMathOps
};

class FlowMathOp {
    friend class FlowNode;

    float mDefault; // 0x0
    MathOpType mOp; // 0x4
    DataNode mLhs; // 0x8
    DataNode mRhs; // 0x10
    FlowPtr<Hmx::Object> mDrivenObj; // 0x18

public:
    FlowMathOp(Hmx::Object *);
    FlowMathOp(const FlowMathOp &other)
        : mDefault(other.mDefault), mOp(other.mOp), mLhs(other.mLhs), mRhs(other.mRhs),
          mDrivenObj(other.mDrivenObj) {}
    // In-class (implicitly inline), not out-of-line in DrivenPropertyMathOps.cpp:
    // the target's only copy of ??4FlowMathOp@@QAAAAV0@ABV0@@Z is a folded header
    // COMDAT (`f i` in ham_xbox_r.map) parked in flow:FlowNode.obj's range, which
    // is the TU that odr-uses it (ObjVector<FlowMathOp> assignment). Body unchanged.
    FlowMathOp &operator=(const FlowMathOp &other) {
        mDefault = other.mDefault;
        mOp = other.mOp;
        mLhs = other.mLhs;
        mRhs = other.mRhs;
        mDrivenObj = other.mDrivenObj;
        return *this;
    }
    ~FlowMathOp();
    void Save(BinStream &);
    void Load(BinStream &, ObjectDir *);
    float Apply(float val);

    const DataNode &Rhs() const { return mRhs; }
    Hmx::Object *DrivenObj() { return mDrivenObj; }
    float Default() const { return mDefault; }
};
