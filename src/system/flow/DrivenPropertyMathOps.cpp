#include "flow\DrivenPropertyMathOps.h"
#include "math\Decibels.h"
#include "math/Rand.h"
#include "obj\DataFile.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "utl\MakeString.h"
#include "utl/BinStream.h"
#include "flow\FlowNode.h"
#include <math.h>

// FlowMathOp::operator= is defined in-class in DrivenPropertyMathOps.h -- see the
// note there.

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
    LOAD_REVS(bs)
    if (d.rev > 6) {
        MILO_FAIL(
            "%s can't load new %s version %d > %d",
            PathName(dir),
            "FlowMathOp",
            d.rev,
            gRev
        );
    }
    if (d.altRev > 0) {
        MILO_FAIL(
            "%s can't load new %s alt version %d > %d",
            PathName(dir),
            "FlowMathOp",
            d.altRev,
            gAltRev
        );
    }
    d >> (int &)mOp;
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
    if (d.rev < 3 && mOp > 2) {
        mOp = (MathOpType)((int)mOp + 1);
    }
    d >> mDefault;
    if (d.rev < 6) {
        bool b;
        d >> b;
        mDrivenObj = b ? FlowNode::LoadObjectFromMainOrDir(bs, dir) : nullptr;
        d >> mRhs;
        Symbol s;
        if (d.rev > 3) {
            d >> s;
        }
        if (d.rev > 4) {
            d >> mLhs;
        }
    } else {
        d >> mDrivenObj;
        d >> mRhs;
        d >> mLhs;
    }
}

FlowMathOp::FlowMathOp(Hmx::Object *obj)
    : mDefault(0.0f), mOp(kMathOp_Add), mDrivenObj(obj, nullptr) {}

float FlowMathOp::Apply(float val) {
    float rhs = mDefault;
    if ((Hmx::Object *)mDrivenObj && mRhs.Type() == kDataArray) {
        const DataNode *prop = mDrivenObj->Property(mRhs.Array(0), false);
        if (prop && prop->CompatibleType(kDataFloat)) {
            rhs = prop->LiteralFloat(0);
        }
    }

    switch ((int)mOp) {
    case kMathOp_Script:
        if (mLhs.Type() == kDataString) {
            String str = mLhs.Str(0);
            DataNode n;
            if (!str.empty()) {
                DataVariable("val") = val;
                MILO_TRY {
                    n = DataReadString(str.c_str());
                    n.Array(0)->Release();
                    if (n.Array(0)->Type(0) == kDataCommand && n.Array(0)->Size() == 1) {
                        val = n.Array(0)->Command(0)->Execute().Float(0);
                    } else {
                        val = n.Array(0)->Execute().Float(0);
                    }
                }
                MILO_CATCH(msg) {
                    MILO_NOTIFY(
                        "Bad script expression in mathop : %s, expression is: %s", n.Str(0)
                    );
                }
            }
            return val;
        }
        // fall through to lookup
    case 100: {
        if (mLhs.Type() != kDataSymbol) {
            return val;
        }
        DataArray *config = SystemConfig("objects", "FlowNode", "mathops");
        DataArray *found = config->FindArray(mLhs.Sym(0), false);
        if (found) {
            DataArray *script = found->FindArray("script", true);
            DataVariable("val") = DataNode(val);
            DataVariable("prop_val") = DataNode(rhs);
            return script->Node(1).Float(script);
        }
        break;
    }
    case kMathOp_Add:
        return rhs + val;
    case kMathOp_Subtract:
        return val - rhs;
    case kMathOp_Multiply:
        return rhs * val;
    case kMathOp_Divide:
        if (rhs == 0.0f) {
            rhs = 0.0001f;
        }
        return val / rhs;
    case kMathOp_Random: {
        float r = RandomFloat(0, rhs * 2.0f);
        return (r - rhs) + val;
    }
    case kMathOp_Min:
        if (val >= rhs) {
            return val;
        }
        return rhs;
    case kMathOp_Max:
        if (val <= rhs) {
            return val;
        }
        return rhs;
    case kMathOp_Mod:
        if (rhs <= 0.0f) {
            return val;
        }
        return (float)fmod(val, rhs);
    case kMathOp_Round:
        if (rhs == 0.0f) {
            rhs = 1.0f;
        }
        val = (float)(int)((val + rhs * 0.5f) / rhs);
        return val * rhs;
    case kMathOp_Floor:
        if (rhs <= 0.0f) {
            rhs = 1.0f;
        }
        val = (float)floor(val / rhs);
        return val * rhs;
    case kMathOp_Ceil:
        if (rhs <= 0.0f) {
            rhs = 1.0f;
        }
        val = (float)ceil(val / rhs);
        return val * rhs;
    case kMathOp_NormalizeDb: {
        float neg_val = -val;
        float absVal = (neg_val >= 0.0f) ? 0.0f : val;
        float clamped = (absVal - rhs >= 0.0f) ? rhs : absVal;
        if (rhs != 0.0f) {
            clamped = clamped / rhs;
        }
        return RatioToDb(clamped);
    }
    case kMathOp_InvNormalizeDb: {
        float neg_val = -val;
        float absVal = (neg_val >= 0.0f) ? 0.0f : val;
        float clamped = (absVal - rhs >= 0.0f) ? rhs : absVal;
        if (rhs != 0.0f) {
            clamped = clamped / rhs;
        }
        clamped = 1.0f - clamped;
        return RatioToDb(clamped);
    }
    case kMathOp_Abs:
        return fabsf(val);
    case kMathOp_Sin:
        return (float)sin(val);
    case kMathOp_Cos:
        return (float)cos(val);
    case kMathOp_Pow:
        return (float)pow(val, rhs);
    case -1:
        break;
    default:
        MILO_NOTIFY_ONCE("Bad mathop operation value");
        return val;
    }
    return rhs;
}
