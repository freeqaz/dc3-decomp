#include "hamobj\PhotoSpotlightPositioner.h"
#include "gesture\GestureMgr.h"
#include "hamobj\HamGameData.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include "utl/Loader.h"

PhotoSpotlightPositioner::PhotoSpotlightPositioner()
    : mPlayer(0), mSpotlight(this), mRefImage(this) {}
PhotoSpotlightPositioner::~PhotoSpotlightPositioner() {}

// Handle is in CharBoneOffset.cpp (cross-unit)

BEGIN_PROPSYNCS(PhotoSpotlightPositioner)
    SYNC_PROP(player, mPlayer)
    SYNC_PROP(spotlight, mSpotlight)
    SYNC_PROP(ref_image, mRefImage)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(PhotoSpotlightPositioner)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mPlayer;
    bs << mSpotlight;
    bs << mRefImage;
END_SAVES

BEGIN_COPYS(PhotoSpotlightPositioner)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(PhotoSpotlightPositioner)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mPlayer)
        COPY_MEMBER(mSpotlight)
        COPY_MEMBER(mRefImage)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(1, 0)

BEGIN_LOADS(PhotoSpotlightPositioner)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    d >> mPlayer;
    d >> mSpotlight;
    d >> mRefImage;
END_LOADS

void PhotoSpotlightPositioner::Init() { REGISTER_OBJ_FACTORY(PhotoSpotlightPositioner); }

void PhotoSpotlightPositioner::Poll() {
    HamPlayerData *player = TheGameData->Player(mPlayer);
    Skeleton *skel = TheGestureMgr->GetSkeletonByTrackingID(player->GetSkeletonTrackingID());
    if (mSpotlight && !TheLoadMgr.EditMode()) {
        if (skel) {
            Vector2 rightFoot, leftFoot;
            skel->ScreenPos(kJointFootRight, rightFoot);
            skel->ScreenPos(kJointFootLeft, leftFoot);
            float y = Max(leftFoot.y, rightFoot.y);
            Vector3 pos = GetImagePos(Vector2((leftFoot.x + rightFoot.x) * 0.5f, y));
            mSpotlight->SetWorldPos(pos);
        } else {
            mSpotlight->SetWorldPos(GetImagePos(Vector2(-10.0f, -10.0f)));
        }
    }
}

Vector3 PhotoSpotlightPositioner::GetImagePos(Vector2 v2) const {
    RndMesh *mesh = mRefImage;
    // Plain WorldXfm(), no outer dirty test of our own.  WorldXfm() is itself
    // `!mDirty ? mWorldXfm : WorldXfm_Force()`, so hand-rolling the check
    // around it made MSVC emit the `lbz 0xfd / cmplwi / bne` diamond TWICE;
    // the image has exactly one, and its non-dirty arm is `addi r4, r3, 0x48`
    // off the single `addi r3, r11, 0x40` RndTransformable base at 0x8250988C.
    const Transform &xfm = mesh->WorldXfm();

    Transform localCopy;
    memcpy(&localCopy, &xfm, sizeof(Transform));

    Vector3 result;
    result.y = 0.0f;
    result.x = -((1.0f - v2.x) * localCopy.m.x.x - (localCopy.m.x.x * 0.5f + localCopy.v.x));
    // m.z.z, not m.x.y: the image loads 0x78(r1) at 0x825098CC -- the mesh's
    // Z-axis scale.  0x54(r1) is m.x.y, an off-diagonal shear term.
    result.z = -(v2.y * localCopy.m.z.z - (localCopy.m.z.z * 0.5f + localCopy.v.z));
    return result;
}
