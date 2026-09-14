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

void CursorPanel::Poll() {
    // RESIDUAL (w7-ao, 96.52 canonical): 3 of the remaining rows are one extra
    // spill of `this`. The image keeps it in r14 for the whole function; we
    // keep it in r29 AND store it to 0x1a4(r31), then reload it with an extra
    // `b` + `lwz r29, 0x1a4(r31)` at the top of the loop. The image spends its
    // one frame word on the ui_crown_player Symbol instead (`stw r30, 0x5c(r31)`
    // / `lwz r15, 0x5c(r31)`), which we hold in r14 with no store -- so this is
    // one register too few, not a missing statement. The rest is the r16..r21
    // constant-pointer permutation and the `trans.m.x *= 4` scheduling below.
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
            // 18 rows of residual live here and neither obvious lever moves them:
            // `Scale(trans.m.x, 4, trans.m.x)` x3 (Vector3::Set batched form) is
            // byte-identical, and reversing the three statements to z,y,x measures
            // 95.5.  The image loads 0xa8,0xa4,0x90,0xb8,0x94,0xb4,0x98,0xa0,0xb0
            // and stores 0x90,0x94,0x98,0xa0,0xb0,0xa8,0xa4,0xb8,0xb4 -- neither
            // sequence is a program order, so this reads as MSVC scheduling.
            trans.m.x *= 4;
            trans.m.y *= 4;
            trans.m.z *= 4;
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
