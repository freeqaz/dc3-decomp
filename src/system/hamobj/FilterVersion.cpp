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
    // RESIDUAL (w8-i, re-measured w9-c 2026-09-30): 85.000 canonical / 84.500
    // fuzzy, 4 rows of 22, one scheduling decision.  The image sets up the
    // NodeWeightHam1 arguments LEFT TO RIGHT -- `mr r5, r6` (mode) first, and only
    // then loads the mirror straight into its own argument register off the saved
    // copy of detectFrame, `lwz r6, 0xc(r30)`, one instruction before the call.
    // MSVC gives us RIGHT TO LEFT: it reads the mirror off the still-live incoming
    // r5 before mode overwrites it (`lwz r11, 0xc(r5)`) and then needs `mr r6, r11`
    // to shuffle it into place.  That third instruction is the whole 84-vs-80-byte
    // difference; every other row, both calls and the __savegprlr_28/__restgprlr_28
    // pair, is equal.
    //
    // og-dc3-decomp carries this function with a BYTE-IDENTICAL body, so the source
    // shape is not the variable -- argument evaluation order simply is not
    // reachable from C++ here.  Nine spellings refuted, one full ninja each, all
    // scoring exactly 85.000 / 84.500:
    //   - hoist the move frame into `const MoveFrame *mf` (w8-i)
    //   - hoist the mirror into a named `MoveMirrored` local (w8-i)
    //   - inline the whole NodeWeightHam1 call into input.Set()'s second argument
    //   - `MoveMode m = mode;` first
    //   - take `const Ham1NodeWeight *` instead of a reference
    //   - `int node = x;` first
    //   - `const DetectFrame *df = detectFrame;` and read everything through df
    //   - `const MoveFrame *mf = ...` AND inline into input.Set() together
    //   - `ErrorNodeInput &out = input;` first
    // Do not re-derive.  Ham2FilterVersion::NodeInput below is 100% because its
    // two-argument call has no register-swap conflict to resolve.
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
