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
#include "char/CharEyeDartRuleset.h"
#include "rndobj/Draw.h"

CursorPanel::CursorPanel() {}

CursorPanel::~CursorPanel() {}

void CursorPanel::Poll() {
    PassiveMessagesPanel::Poll();

    static Symbol ui_crown_player("ui_crown_player");
    const DataNode *pCrownPlayerNode = TheHamProvider->Property(ui_crown_player, true);
    MILO_ASSERT(pCrownPlayerNode, 0x1f);

    int crownPlayerIdx = pCrownPlayerNode->Int();

    static Symbol player_present("player_present");

    float pi_over_2 = 1.5707963705062866f;
    float scale_y = 4.0f;

    for (int playerIdx = 0; playerIdx < 2; playerIdx++) {
        HamPlayerData *player = TheGameData->Player(playerIdx);
        int side = player->Side();

        const DataNode *playerPresentNode = player->Property(player_present, true);
        int playerPresent = playerPresentNode->Int();

        ObjectDir *objDir = DataDir();
        RndTex *crownTex = objDir->Find<RndTex>("depth_buffer_left_crown.tex", true);
        const char *matName = (side == 0) ? "depth_buffer_left_crown.mat" : "depth_buffer_right_crown.mat";
        RndMat *crownMat = objDir->Find<RndMat>(matName, true);

        // RockCentral::mMiscArt (RndTex* at 0xD8) reused as depth tex reference
        CharEyeDartRuleset *dartRuleset = *(CharEyeDartRuleset **)(((char *)&TheRockCentral) + 0xD8);
        if (dartRuleset == 0) {
            dartRuleset = (CharEyeDartRuleset *)crownTex;
        }

        // BaseMaterial::mDiffuseTex (ObjPtr<RndTex> at 0x40)
        void **matRef = (void **)(((char *)crownMat) + 0x40);
        *matRef = dartRuleset;

        crownMat->MarkDirty(2);

        int skeletonId = TheGestureMgr->GetPlayerSkeletonID(playerIdx);

        bool hasCrown = true;
        Skeleton *skeleton = 0;
        if (playerPresent == 0 || side != crownPlayerIdx || skeletonId < 0) {
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
            if (!hasCrown) {
                TheDebug << MakeString("player %d lost his crown\n", &lastCrownPlayer);
                lastCrownPlayer = -1;
                DataNode emptyNode;
                TheHamProvider->SetProperty(ui_crown_player, &emptyNode);
            }
        } else if (hasCrown) {
            TheDebug << MakeString("player %d lost his crown\n", &lastCrownPlayer);
            lastCrownPlayer = -1;
            DataNode emptyNode;
            TheHamProvider->SetProperty(ui_crown_player, &emptyNode);
        }

        if (hasCrown) {
            Vector2 joint3Pos, joint2Pos;
            skeleton->ScreenPos((SkeletonJoint)3, joint3Pos);
            skeleton->ScreenPos((SkeletonJoint)2, joint2Pos);

            float dx = joint3Pos.x - joint2Pos.x;
            float dy = joint3Pos.y - joint2Pos.y;

            float angle = atan2f(dy, dx) + pi_over_2;

            Vector3 rotAxis(0.0f, 0.0f, angle);
            Hmx::Matrix3 rotMatrix;
            MakeRotMatrix(rotAxis, rotMatrix, true);

            float *matPtr = (float *)&rotMatrix;
            for (int i = 0; i < 9; i++) {
                matPtr[i] *= scale_y;
            }

            memcpy(((char *)crownMat) + 0x74, &rotMatrix, 0x40);
        } else {
            float scaleMatrix[9] = {2.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 2.0f};
            memcpy(((char *)crownMat) + 0x74, scaleMatrix, 0x40);
        }

        crownMat->MarkDirty(2);
    }
}

BEGIN_HANDLERS(CursorPanel)
    HANDLE_SUPERCLASS(PassiveMessagesPanel)
END_HANDLERS
