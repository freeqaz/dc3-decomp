#include "flow/DrivenPropertyMathOps.h"
#include "math/Decibels.h"
#include "math/Rand.h"
#include "obj/DataFile.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "utl/MakeString.h"
#include "utl/BinStream.h"
#include "flow/FlowNode.h"
#include <math.h>

FlowMathOp &FlowMathOp::operator=(const FlowMathOp &other) {
    mDefault = other.mDefault;
    mOp = other.mOp;
    mLhs = other.mLhs;
    mRhs = other.mRhs;
    mDrivenObj = other.mDrivenObj;
    return *this;
}

FlowMathOp::~FlowMathOp() {}

void FlowMathOp::Save(BinStream &bs) {
    bs << 6;
    bs << (int)mOp;
    bs << mDefault;
    bs << mDrivenObj;
    bs << mRhs;
    bs << mLhs;
}

INIT_REVS(6, 0)

void FlowMathOp::Load(BinStream &bs, ObjectDir *dir) {
    int revs;
    bs >> revs;
    BinStreamRev d(bs, revs);

    if (d.rev > gRev) {
        const char *dirPath = dir ? PathName(dir) : "";
        TheDebug.Fail(
            MakeString(
                "%s can't load new %s version %d > %d",
                dirPath,
                "FlowMathOp",
                d.rev,
                gRev
            ),
            nullptr
        );
    }
    if (d.altRev > gAltRev) {
        const char *dirPath = dir ? PathName(dir) : "";
        TheDebug.Fail(
            MakeString(
                "%s can't load new %s alt version %d > %d",
                dirPath,
                "FlowMathOp",
                d.altRev,
                gAltRev
            ),
            nullptr
        );
    }

    bs >> (int &)mOp;

    if (d.rev < 1 && mOp == 8) {
        mOp = (MathOpType)-1;
    }
    if (d.rev < 2) {
        if (mOp == 4) {
            mOp = (MathOpType)8;
        } else if (mOp > 4) {
            mOp = (MathOpType)((int)mOp - 1);
        }
    }
    if (d.rev < 3) {
        if (mOp > 2) {
            mOp = (MathOpType)((int)mOp + 1);
        }
    }

    bs >> mDefault;

    if (d.rev < 6) {
        bool has_obj;
        d >> has_obj;
        Hmx::Object *obj = has_obj ? FlowNode::LoadObjectFromMainOrDir(bs, dir) : nullptr;
        mDrivenObj = obj;
        mRhs.Load(bs);
        Symbol sym = gNullStr;
        if (d.rev > 3) {
            bs >> sym;
        }
        if (d.rev < 5) {
            return;
        }
    } else {
        bs >> mDrivenObj;
        mRhs.Load(bs);
    }
    mLhs.Load(bs);
}

FlowMathOp::FlowMathOp(Hmx::Object *obj)
    : mDefault(0.0f), mOp(kMathOp_Add), mDrivenObj(obj, nullptr) {}

float FlowMathOp::Apply(float val) {
    float rhs = mDefault;
    if ((Hmx::Object *)mDrivenObj && mRhs.Type() == kDataArray) {
        const DataNode *prop = mDrivenObj->Property(mRhs.Array(0), false);
        if (prop && prop->CompatibleType(kDataInt)) {
            rhs = prop->LiteralFloat(0);
        }
    }

    switch ((int)mOp) {
    case kMathOp_Add:
        rhs = rhs + val;
        break;
    case kMathOp_Subtract:
        rhs = val - rhs;
        break;
    case kMathOp_Multiply:
        rhs = rhs * val;
        break;
    case kMathOp_Divide:
        if (rhs == 0.0f) {
            rhs = 0.0001f;
        }
        rhs = val / rhs;
        break;
    case kMathOp_Random: {
        float r = RandomFloat(0, rhs * 2.0f);
        rhs = (r - rhs) + val;
        break;
    }
    case kMathOp_Min:
        if (val >= rhs) {
            return val;
        }
        break;
    case kMathOp_Max:
        if (val <= rhs) {
            return val;
        }
        break;
    case kMathOp_Mod:
        if (rhs <= 0.0f) {
            return val;
        }
        rhs = (float)fmod(val, rhs);
        break;
    case kMathOp_Round:
        if (rhs == 0.0f) {
            rhs = 1.0f;
        }
        val = (float)(int)((val + rhs * 0.5f) / rhs);
        rhs = val * rhs;
        break;
    case kMathOp_Floor:
        if (rhs <= 0.0f) {
            rhs = 1.0f;
        }
        val = (float)floor(val / rhs);
        rhs = val * rhs;
        break;
    case kMathOp_Ceil:
        if (rhs <= 0.0f) {
            rhs = 1.0f;
        }
        val = (float)ceil(val / rhs);
        rhs = val * rhs;
        break;
    case kMathOp_NormalizeDb: {
        float absVal = 0.0f;
        float clamped = rhs;
        if (-val < 0.0f) {
            absVal = val;
        }
        if (absVal - rhs < 0.0f) {
            clamped = absVal;
        }
        if (rhs != 0.0f) {
            clamped = clamped / rhs;
        }
        rhs = RatioToDb(clamped);
        break;
    }
    case kMathOp_InvNormalizeDb: {
        float absVal = 0.0f;
        if (-val < 0.0f) {
            absVal = val;
        }
        float clamped = rhs;
        if (absVal - rhs < 0.0f) {
            clamped = absVal;
        }
        if (rhs != 0.0f) {
            clamped = clamped / rhs;
        }
        clamped = 1.0f - clamped;
        rhs = RatioToDb(clamped);
        break;
    }
    case kMathOp_Abs:
        rhs = fabsf(val);
        break;
    case kMathOp_Sin:
        rhs = (float)sin(val);
        break;
    case kMathOp_Cos:
        rhs = (float)cos(val);
        break;
    case kMathOp_Pow:
        rhs = (float)pow(val, rhs);
        break;
    case kMathOp_Script:
        if (mLhs.Type() == kDataString) {
            String str(mLhs.Str(0));
            if (*str.c_str() != '\0') {
                DataNode varSave = DataVariable("flow_math_op_result");
                DataNode valSave = DataVariable("flow_math_op_val");
                TheDebug.SetTry(true);
                DataArray *script = DataReadString(str.c_str());
                DataNode scriptNode(script, kDataArray);
                DataArray *arr = scriptNode.Array(0);
                if (!(arr->Node(0).Type() == kDataCommand && arr->Size() == 1)) {
                    DataNode result = arr->Execute(true);
                    val = result.Float(0);
                } else {
                    DataNode result = arr->Node(0).Command(arr)->Execute(true);
                    val = result.Float(0);
                }
                TheDebug.SetTry(false);
                DataVariable("flow_math_op_result") = varSave;
                DataVariable("flow_math_op_val") = valSave;
            }
            return val;
        }
        // fall through to lookup
    case 100: {
        if (mLhs.Type() != kDataSymbol) {
            return val;
        }
        DataArray *config = SystemConfig("flow", "math_ops", "driven_property");
        DataArray *found = config->FindArray(mLhs.Sym(0), false);
        if (!found) {
            return val;
        }
        DataVariable("flow_math_op_result") = DataNode(0);
        DataVariable("flow_math_op_val") = DataNode(0);
        return found->Node(1).Float(found);
    }
    case kMathOp_ClampMin:
        break;
    default:
        MILO_NOTIFY_ONCE("Unknown math op type %d", mOp);
        return val;
    }
    return rhs;
}
