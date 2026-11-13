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

// Lane history (6 lines moved): docs/decomp/notes/lazer/meta_ham/CursorPanel.md
void CursorPanel::Poll() {
    // Lane history (26 lines moved): docs/decomp/notes/lazer/meta_ham/CursorPanel.md
    // > RESIDUAL (w7-ao, 96.52 canonical): 3 of the remaining rows are one extra spill of
    // > `this`.
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
        // Lane history (12 lines moved): docs/decomp/notes/lazer/meta_ham/CursorPanel.md
        RndTex *tex = TheRockCentral.GetMiscArt();
        RndTex *useTex;
        if (tex) {
            useTex = tex;
        } else {
            useTex = pBufferLeftTex;
        }
        pMat->SetDiffuseTex(useTex);

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
            // Lane history (7 lines moved): docs/decomp/notes/lazer/meta_ham/CursorPanel.md
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
