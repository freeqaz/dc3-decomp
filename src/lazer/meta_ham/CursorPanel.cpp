#include "meta_ham\CursorPanel.h"
#include "flow\PropertyEventProvider.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "meta_ham\PassiveMessagesPanel.h"
#include "net_ham\RockCentral.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Mat.h"
#include "rndobj\Tex.h"
#include "ui\PanelDir.h"
#include "utl\Symbol.h"
static int sInt = -1;

CursorPanel::CursorPanel() {}

CursorPanel::~CursorPanel() {}

// w16-e: `CursorPanel *self = this;` (calls through self) is inert.
// w20-t (96.6, branch-scan rows 129/269 adjudicated ARTIFACT): row 129 is the
// image hoisting `addi r3, r3, 0x40` (&pMat->mDiffuseTex) above `bne` on
// GetMiscArt(), both arms reach the same SetObjConcrete with the same r3/r4;
// row 269's back-edge lands one row earlier in ours only because of the
// `lwz r29, 0x1a4(r31)` reload of `this` described below. No behaviour change.
void CursorPanel::Poll() {
    // RESIDUAL (w7-ao, 96.52 canonical): 3 of the remaining rows are one extra
    // spill of `this`. The image keeps it in r14 for the whole function; we
    // keep it in r29 AND store it to 0x1a4(r31), then reload it with an extra
    // `b` + `lwz r29, 0x1a4(r31)` at the top of the loop. The image spends its
    // one frame word on the ui_crown_player Symbol instead (`stw r30, 0x5c(r31)`
    // / `lwz r15, 0x5c(r31)`), which we hold in r14 with no store -- so this is
    // one register too few, not a missing statement. The rest is the r16..r21
    // constant-pointer permutation and the `trans.m.x *= 4` scheduling below.
    // w21-e: the `trans.m.x *= 4` block is closed (Scale, below).  Re-tested
    // the `this` spill: still there, and `Skeleton *skeleton` hoisted to
    // function scope is byte-identical.  Left: the spill/split (rows 6-12,
    // 82-83, 184-188), the r16..r21 constant permutation and the r28..r30
    // permutation that follows from it -- all register-only, behaviour
    // checked: every call gets the same values as the image.
    // w21-aa (96.57, not reworked): re-read every branch target and argument
    // against the image (crown-loss ||, MILO_LOG arg = &sCrownPlayerIndex,
    // SetProperty(ui_crown_player, -1) via the 0x5c spill, ScreenPos joints
    // 3/2, MakeRotMatrix(v, m, true), Scale x4): no behaviour difference.
    // The three name rows (MakeString<char[19],int,char[5]>, <_D3DFORMAT>,
    // SetObjConcrete<AnimTask>) are ICF aliases of ours.  Left as w21-e left it.
    // w24-c1 (96.52, unchanged; same 43 rows: the `this` home-slot spill
    // 6-12/82-83/184-188 and the r16..r21 / r28..r30 permutations).
    // Re-read the crown-loss branch targets (8292F8FC..) against the image:
    // same truth table.  Measured: reading `mDir` directly instead of
    // LoadedDir() (one inline level fewer) at all three Find sites is
    // byte-inert.  og-dc3-decomp spells the function identically.
    PassiveMessagesPanel::Poll();
    static Symbol ui_crown_player("ui_crown_player");
    const DataNode *pCrownPlayerNode = TheHamProvider->Property(ui_crown_player);
    MILO_ASSERT(pCrownPlayerNode, 0x1f);
    int crownPlayer = pCrownPlayerNode->Int();
    for (int i = 0; i < 2; i++) {
        SkeletonSide side = TheGameData->Player(i)->Side();
        static Symbol player_present("player_present");
        bool present =
            TheGameData->Player(i)->Provider()->Property(player_present)->Int();
        RndTex *pBufferLeftTex = LoadedDir()->Find<RndTex>("depth_buffer_left_crown.tex");
        RndMat *pMat = nullptr;
        if (side == kSkeletonLeft) {
            pMat = LoadedDir()->Find<RndMat>("depth_buffer_left_crown.mat");
        } else {
            pMat = LoadedDir()->Find<RndMat>("depth_buffer_right_crown.mat");
        }
        RndTex *tex = TheRockCentral.GetMiscArt();
        if (!tex) {
            tex = pBufferLeftTex;
        }
        pMat->SetDiffuseTex(tex);

        Transform trans = pMat->TexXfm();
        int skeletonID = TheGestureMgr->GetPlayerSkeletonID(i);
        Skeleton *skeleton;
        bool check = present && side == crownPlayer && skeletonID >= 0
            && (skeleton = TheGestureMgr->GetSkeletonByTrackingID(skeletonID), skeleton);
        static int sCrownPlayerIndex = -1;
        if (check && sCrownPlayerIndex == -1) {
            sCrownPlayerIndex = i;
        }
        // NOT the ternary `i == sCrownPlayerIndex ? !check : check`: 8292F8FC
        // branches through the test in BOTH arms, which is a short-circuited ||.
        // The ternary makes MSVC materialise the flag (cntlzw/extrwi) instead and
        // costs 4 rows.  Same truth table either way.
        if ((i == sCrownPlayerIndex && !check) || (i != sCrownPlayerIndex && check)) {
            MILO_LOG("player %d lost his crown\n", sCrownPlayerIndex);
            sCrownPlayerIndex = -1;
            TheHamProvider->SetProperty(ui_crown_player, -1);
            check = false;
        }
        if (check) {
            Vector2 v120;
            skeleton->ScreenPos(kJointHead, v120);
            Vector2 v128;
            skeleton->ScreenPos(kJointShoulderCenter, v128);
            trans.v.x = v120.x;
            trans.v.y = -v120.y;
            v128.x = v120.x - v128.x;
            v128.y = v120.y - v128.y;
            float tanned = atan2(v128.y, v128.x);
            float angle = tanned + (PI / 2);
            Vector3 v110(0, 0, angle);
            MakeRotMatrix(v110, trans.m, true);
            // w21-e: the image's scrambled 9-load/9-store block (loads 0xa8,0xa4,
            // 0x90,0xb8,...; stores 0x90,0x94,0x98,0xa0,0xb0,...) is the math/Mtx.h
            // Scale(const Vector3 &, const Matrix3 &, Matrix3 &) inline with a
            // (4,4,4) vector -- closes all 18 rows.  Same values as scaling each
            // row by 4.  (Scale(trans.m, Vector3(4,4,4), trans.m), the other
            // overload, is far worse: 85.1.)  Earlier refuted spellings:
            // `trans.m.x *= 4` x3 (95.5 reversed), Scale(trans.m.x, 4, ...) x3.
            Scale(Vector3(4, 4, 4), trans.m, trans.m);
            pMat->SetTexXfm(trans);
        } else {
            trans.v.x = 2;
            trans.v.y = 2;
            pMat->SetTexXfm(trans);
        }
    }
}

BEGIN_HANDLERS(CursorPanel)
    HANDLE_SUPERCLASS(PassiveMessagesPanel)
END_HANDLERS
