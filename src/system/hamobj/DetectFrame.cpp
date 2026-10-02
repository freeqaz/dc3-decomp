#include "hamobj\DetectFrame.h"
#include "ErrorNode.h"
#include "FilterVersion.h"
#include "hamobj\ErrorNode.h"
#include "hamobj\HamMove.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "os\Debug.h"

DetectFrame::DetectFrame() { Reset(); }

void DetectFrame::Reset() {
    for (int i = 0; i < kMaxNumErrorNodes; i++) {
        mBestNodeErrors[i].Set(kHugeFloat, kHugeFloat, kHugeFloat);
    }
}

void DetectFrame::Reset(
    const FilterVersion *fv,
    float secs,
    const MoveFrame *mf,
    const DancerFrame *df,
    MoveMirrored mirror
) {
    Reset();
    mSeconds = secs;
    mMoveFrame = mf;
    mDancerFrame = df;
    mMirror = mirror;
    const ErrorNode *const *nodes = fv->mErrorNodes;
    if (fv->mType == kFilterVersionHam1) {
        for (int i = 0; i < MoveFrame::kNumHam1Nodes; i++) {
            // w8-i: the `Vector3 &w` hoist is load-bearing, and it is a SCHEDULING
            // lever, not a codegen-size one -- the 47 instructions were already
            // identical apart from the two induction-variable bumps, emitted in the
            // opposite order (image: `addi r31,r31,0x10` then `addi r30,r30,0x4`;
            // ours: r30 first).  Subscripting mNodeComponentWeights[i] three times
            // makes the weights pointer a strength-reduction result DERIVED inside
            // the body, so MSVC sinks its bump below the nodes[] bump; binding the
            // element to a reference at the top of the body makes it an induction
            // variable in its own right, created in source order, and the two bumps
            // come out in the image's order.  99.96 -> 100.0 (188 B).
            Vector3 &w = mNodeComponentWeights[i];
            w.y = 1;
            Vector3 v;
            if (nodes[i]->XZErrorAxis(v, df->mSkeleton)) {
                XZErrorWeight(v, w.x, w.z);
            } else {
                w.x = w.z = 1;
            }
        }
    }
}

void DetectFrame::SetSecondsAndReset(float secs) {
    mSeconds = secs;
    Reset();
}

bool DetectFrame::HasScore() const { return mBestNodeErrors[0].x != kHugeFloat; }

const Vector3 &DetectFrame::BestNodeError(int node) const {
    MILO_ASSERT_RANGE(node, 0, kMaxNumErrorNodes, 0x72);
    return mBestNodeErrors[node];
}

const Vector3 &DetectFrame::NodeComponentWeight(int node) const {
    MILO_ASSERT_RANGE(node, 0, MoveFrame::kNumHam1Nodes, 0x80);
    return mNodeComponentWeights[node];
}

void DetectFrame::AddError(const Vector3 (&errors)[kMaxNumErrorNodes], float f) {
    for (int i = 0; i < kMaxNumErrorNodes; i++) {
        for (int j = 0; j < 3; j++) {
            float sum = errors[i][j] + f;
            if (sum < mBestNodeErrors[i][j]) {
                mBestNodeErrors[i][j] = sum;
            }
        }
    }
}

float DetectFrame::Score(const FilterVersion *fv, MoveMode mode) const {
    if (fv->mType == kFilterVersionHam1) {
        float f5 = 0;
        int numNodes = fv->NumNodes();
        for (int i = 0; i < numNodes; i++) {
            if (mMoveFrame->NodeWeightHam1(i, mode, mMirror).mActive) {
                f5 += mBestNodeErrors[i].x;
            }
        }
        return Max(0.0f, 1.0f - f5);
    } else {
        return LimbPSNR(fv, -1);
    }
}

float DetectFrame::LimbPSNR(const FilterVersion *filter_version, int i2) const {
    MILO_ASSERT(filter_version->mType == kFilterVersionHam2, 0x53);
    float f13 = 0;
    float f12 = 0;
    int numNodes = filter_version->NumNodes();
    int typeMask = mMoveFrame->TypeMask();
    for (int i = 0; i < numNodes; i++) {
        ErrorNode *curErrorNode = filter_version->mErrorNodes[i];
        auto _tmp0 = curErrorNode->GetFeedbackLimbs();
        // Image: (i2 == -1 || limbs & i2) && (Type() & typeMask) -- the type
        // mask is tested even when i2 == -1 (beq lands on the Type() load).
        if ((i2 == -1 || _tmp0 & i2) && curErrorNode->Type() & typeMask) {
            const Vector3 &nodeWeight = mMoveFrame->NodeWeight(i, mMirror);
            // RESIDUAL (w13-b, 99.985): 4 rows, the image consumes the Dot terms
            // as z, x, y (`lfs f13, -0x4(r31)` / `lfs f12, 0x0(r31)`), we as
            // z, y, x.  Spelling the dot product out in x,y,z or y,x,z order is
            // byte-identical to Dot() -- MSVC canonicalises the sum.  This is the
            // math/Vec.h component-order family; the 3 MakeString name rows are
            // ICF naming noise (same strings both sides).
            float d = Dot(nodeWeight, mBestNodeErrors[i]);
            f12 += d * d;
            f13 += Length(nodeWeight);
        }
    }
    if (f13 != 0) {
        float max_mse = 1;
        float mse = Min(max_mse, f12 / f13);
        MILO_ASSERT(mse >= 0, 0x20);
        MILO_ASSERT(mse <= max_mse, 0x21);
        if (mse == 0) {
            return 1000;
        } else {
            float mse_log = log(max_mse / mse);
            return (mse_log * 10.0f) / (float)log(10.0f);
        }
    }
    return 0;
}

bool DetectFrameMoveIdxCmp::operator()(const DetectFrame &frame, int idx) const {
    return frame.GetDancerFrame()->mMoveIdx < idx;
}

bool DetectFrameMoveIdxCmp::operator()(int idx, const DetectFrame &frame) const {
    return idx < frame.GetDancerFrame()->mMoveIdx;
}
