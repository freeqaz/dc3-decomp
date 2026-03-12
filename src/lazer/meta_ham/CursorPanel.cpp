#include "meta_ham/CursorPanel.h"
#include "flow/PropertyEventProvider.h"
#include "meta_ham/PassiveMessagesPanel.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "utl/Symbol.h"
#include "hamobj/HamGameData.h"
#include "gesture/GestureMgr.h"
#include "gesture/Skeleton.h"
#include "net_ham/RockCentral.h"
#include "math/Rot.h"
#include "rndobj/Mat.h"
#include "ui/PanelDir.h"

CursorPanel::CursorPanel() {}

CursorPanel::~CursorPanel() {}

void CursorPanel::Poll() {
    PassiveMessagesPanel::Poll();
#ifdef HX_NATIVE
    // Cursor/Kinect tracking not available on native — skip gesture-driven cursor logic
    return;
#endif

    static Symbol ui_crown_player("ui_crown_player");
    const DataNode *pCrownPlayerNode = TheHamProvider->Property(ui_crown_player, true);
    MILO_ASSERT(pCrownPlayerNode, 0x1f);

    int crownPlayerIdx = pCrownPlayerNode->Int();

    for (int playerIdx = 0; playerIdx < 2; playerIdx++) {
        SkeletonSide side = TheGameData->Player(playerIdx)->Side();

        static Symbol player_present("player_present");
        bool playerPresent = TheGameData->Player(playerIdx)->Provider()->Property(player_present, true)->Int();

        RndTex *crownTex = mDir->Find<RndTex>("depth_buffer_left_crown.tex", true);
        const char *matName = (side == 0) ? "depth_buffer_left_crown.mat" : "depth_buffer_right_crown.mat";
        RndMat *crownMat = mDir->Find<RndMat>(matName, true);

        RndTex *miscArt = TheRockCentral.GetMiscArt();
        if (miscArt == 0) {
            miscArt = crownTex;
        }

        crownMat->SetDiffuseTex(miscArt);

        Transform localXfm;
        memcpy(&localXfm, ((char *)crownMat) + 0x74, 0x40);

        int skeletonId = TheGestureMgr->GetPlayerSkeletonID(playerIdx);

        bool hasCrown = true;
        Skeleton *skeleton = 0;
        if (!playerPresent || side != crownPlayerIdx || skeletonId < 0) {
            hasCrown = false;
        } else {
            skeleton = TheGestureMgr->GetSkeletonByTrackingID(skeletonId);
            if (skeleton == 0) {
                hasCrown = false;
            }
        }

        static int lastCrownPlayer = -1;
        if (hasCrown && lastCrownPlayer == -1) {
            lastCrownPlayer = playerIdx;
        }

        if (playerIdx == lastCrownPlayer) {
            if (!hasCrown) goto crown_loss;
        } else if (hasCrown) {
        crown_loss:
            TheDebug << MakeString("player %d lost his crown\n", lastCrownPlayer);
            lastCrownPlayer = -1;
            DataNode node(-1);
            TheHamProvider->SetProperty(ui_crown_player, node);
            hasCrown = false;
        }

        if (hasCrown) {
            Vector2 joint2Pos, joint3Pos;
            skeleton->ScreenPos((SkeletonJoint)3, joint3Pos);
            skeleton->ScreenPos((SkeletonJoint)2, joint2Pos);

            localXfm.v.x = joint3Pos.x;
            localXfm.v.y = -joint3Pos.y;

            float dx = joint3Pos.x - joint2Pos.x;
            float dy = joint3Pos.y - joint2Pos.y;

            float angle = atan2f(dy, dx) + 1.5707963705062866f;

            Vector3 rotAxis(0.0f, 0.0f, angle);
            MakeRotMatrix(rotAxis, localXfm.m, true);

            localXfm.m.z *= 4.0f;
            localXfm.m.y *= 4.0f;
            localXfm.m.x *= 4.0f;
        } else {
            localXfm.v.x = 2.0f;
            localXfm.v.y = 2.0f;
        }

        memcpy(((char *)crownMat) + 0x74, &localXfm, 0x40);
        crownMat->MarkDirty(2);
    }
}

BEGIN_HANDLERS(CursorPanel)
    HANDLE_SUPERCLASS(PassiveMessagesPanel)
END_HANDLERS
