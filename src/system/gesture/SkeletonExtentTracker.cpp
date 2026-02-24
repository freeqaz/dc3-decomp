#include "gesture/SkeletonExtentTracker.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "math/Geo.h"
#include "obj/Data.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Mesh.h"
#include <float.h>

SkeletonExtentTracker::SkeletonExtentTracker() : mTrackingID(-1) {
    SetName("skeleton_extent_tracker", ObjectDir::Main());
}

BEGIN_HANDLERS(SkeletonExtentTracker)
    HANDLE_ACTION(start_tracking, StartTracking(_msg->Int(2)))
    HANDLE_ACTION(stop_tracking, mTrackingID = -1)
    HANDLE_ACTION(
        apply_to_mesh_verts, ApplyToMeshVerts(_msg->Obj<RndMesh>(2), _msg->Int(3))
    )
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void SkeletonExtentTracker::StartTracking(int i1) {
    mTrackingID = i1;
    mMaxX = FLT_MIN;
    mMaxY = FLT_MIN;
    mMinX = FLT_MAX;
    mMinY = FLT_MAX;
}

void SkeletonExtentTracker::Poll() {
    if (mTrackingID != -1) {
        Skeleton *skeleton = TheGestureMgr->GetSkeletonByTrackingID(mTrackingID);
        if (skeleton) {
            for (int i = 0; i < kNumJoints; i++) {
                Vector2 pos;
                skeleton->ScreenPos((SkeletonJoint)i, pos);
                mMinX = Min(mMinX, pos.x);
                mMinY = Min(mMinY, pos.y - 0.10f);
                mMaxX = Max(mMaxX, pos.x);
                mMaxY = Max(mMaxY, pos.y);
            }
            mMinX = Max(0.0f, mMinX);
            mMinY = Max(0.0f, mMinY);
            mMaxX = Min(1.0f, mMaxX);
            mMaxY = Min(1.0f, mMaxY);
        }
    }
}

Hmx::Rect SkeletonExtentTracker::GetViewBox() const {
    Hmx::Rect ret;
    if (mMinX != FLT_MIN && mMinX != FLT_MAX && mMinY != FLT_MIN && mMinY != FLT_MAX) {
        float val = Min(mMaxY - mMinY, 1.0f);
        ret.Set(((mMaxX + mMinX) / 2.0f) - (val / 2.0f), mMaxY, val, val);
    } else {
        ret.Set(0, 0, 1, 1);
    }
    return ret;
}

void SkeletonExtentTracker::ApplyToMeshVerts(RndMesh *mesh, bool mirrored) const {
    Hmx::Rect box = GetViewBox();
    MILO_ASSERT(mesh->Verts().size() == 16, 0x43);

    float xFractions[4] = { 0.0f, 0.2f, 0.8f, 1.0f };
    float yFractions[4] = { 0.0f, 0.2f, 0.8f, 1.0f };

    int direction = mirrored ? -1 : 1;

    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int idx = row * 4 + col;
            RndMesh::Vert &vert = mesh->Verts()[idx];

            float xFrac = xFractions[col];
            float yFrac = yFractions[row];

            vert.pos.x = (box.x + xFrac * box.w) * (float)direction;
            vert.pos.z = -(box.y - yFrac * box.h);
        }
    }
}
