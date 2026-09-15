#include "hamobj\FilterVersion.h"
#include "hamobj\ErrorNode.h"
#include "hamobj\DetectFrame.h"
#include "hamobj\HamMove.h"
#include "obj\Data.h"
#include "os\Debug.h"

int FilterVersion::sNumHam2Nodes;

FilterVersion::FilterVersion(FilterVersionType t, const DataArray *cfg)
    : mVersionSym(cfg->Sym(0)), mType(t) {
    static Symbol time_error("time_error");
    mScaleOp.Set(cfg->FindArray(time_error));
    static Symbol nodes("nodes");
    DataArray *error_nodes_data = cfg->FindArray(nodes);
    MILO_ASSERT(error_nodes_data->Size()-1 <= kMaxNumErrorNodes, 0x4E);
    int i;
    for (i = 0; i < error_nodes_data->Size() - 1; i++) {
        mErrorNodes[i] = ErrorNode::Create(error_nodes_data->Array(i + 1));
    }
    if (mType == kFilterVersionHam2) {
        sNumHam2Nodes = i;
    }
    for (; i < kMaxNumErrorNodes; i++) {
        mErrorNodes[i] = nullptr;
    }
}

FilterVersion::~FilterVersion() {
    for (int i = 0; i < kMaxNumErrorNodes; i++) {
        RELEASE(mErrorNodes[i]);
    }
}

FilterVersion *FilterVersion::Create(const DataArray *cfg) {
    FilterVersionType t = (FilterVersionType)cfg->FindInt("type");
    if (t == kFilterVersionHam1) {
        return new Ham1FilterVersion(t, cfg);
    } else if (t == kFilterVersionHam2) {
        return new Ham2FilterVersion(t, cfg);
    } else {
        MILO_FAIL("could not create filter version");
        return nullptr;
    }
}

void Ham1FilterVersion::NodeInput(
    int x, const DetectFrame *detectFrame, MoveMode mode, ErrorNodeInput &input
) const {
    // RESIDUAL (w8-i, 85.00 canonical / 84.50000 fuzzy): 4 rows of 22, one
    // scheduling decision.  The image sets up the NodeWeightHam1 arguments as
    // `mr r5, r6` (mode) FIRST and only then loads the mirror straight into its
    // argument register off the saved copy of detectFrame -- `lwz r6, 0xc(r30)`,
    // one instruction before the call.  We read the mirror off the INCOMING r5
    // before mode clobbers it (`lwz r11, 0xc(r5)`) and then need `mr r6, r11`,
    // which is the extra instruction making our body 84 bytes against 80.
    // REFUTED, both bit-identical to this body: hoisting the move frame into
    // `const MoveFrame *mf = ...` (which would pull the `lwz r3, 0x4(r30)` the
    // image emits LAST even earlier), and hoisting the mirror into a named
    // MoveMirrored local.  Argument evaluation order is not reachable from here.
    const Ham1NodeWeight &ham1 =
        detectFrame->GetMoveFrame()->NodeWeightHam1(x, mode, detectFrame->Mirror());
    input.Set(detectFrame->NodeComponentWeight(x), &ham1);
}

void Ham2FilterVersion::NodeInput(
    int x, const DetectFrame *detectFrame, MoveMode mode, ErrorNodeInput &input
) const {
    input.Set(
        detectFrame->GetMoveFrame()->NodeInverseScale(x, detectFrame->Mirror()), nullptr
    );
}
