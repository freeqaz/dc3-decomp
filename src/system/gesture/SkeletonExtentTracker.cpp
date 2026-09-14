#include "gesture\SkeletonExtentTracker.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "math/Geo.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Mesh.h"
#include <float.h>

SkeletonExtentTracker::SkeletonExtentTracker() : mTrackingID(-1) {
    SetName("skeleton_extent_tracker", ObjectDir::Main());
}

BEGIN_HANDLERS(SkeletonExtentTracker)
    char _slotpad[96]; (void)_slotpad;
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

    int vertIdx = 0;
    // A SELECT, not an arithmetic negation.  The image lowers `mirrored ? -1 : 1`
    // to the 0/-1 mask idiom -- `subfic r11, r11, 0` + `subfe r11, r11, r11`
    // (0x82624xxx), then `clrrwi r11, r11, 1` and `addi r11, r11, 1`.  Writing
    // the mask out by hand as `(-(unsigned)mirrored & 0xFFFFFFFEu) + 1` gives a
    // bare `neg` instead, because that negates the byte rather than testing it.
    int direction = mirrored ? -1 : 1;
    float dir = (float)(long long)direction;
    // Both fractions are 4-case SWITCHES with no default, not ternary chains.
    // The image lowers each to MSVC's binary comparison tree for the dense
    // range {0,1,2,3} -- `cmplwi rN, 1` / blt -> case 0, beq -> case 1,
    // `cmplwi rN, 3` / blt -> case 2, `bne` -> past the whole chain, fall
    // through to case 3 -- which lays the arms out in REVERSE source order
    // (1.0, 0.8, 0.2, 0.0) with every branch forward.  A ternary chain lays
    // them out in source order and has no bne-to-end arm at all.  The missing
    // default is why the image seeds each fraction with a junk `lfs fN,
    // 0x50(r1)` (the int64 scratch it just used for fcfid) before the tree.
    // The counters are signed ints: the loop test is `cmpwi cr6, rN, 0x4`,
    // while the switch's own range tests stay unsigned (`cmplwi`).
    for (int i = 0; i < 4; i++) {
        float yFrac;
        switch (i) {
        case 0:
            yFrac = 0.0f;
            break;
        case 1:
            yFrac = 0.2f;
            break;
        case 2:
            yFrac = 0.8f;
            break;
        case 3:
            yFrac = 1.0f;
            break;
        }
        // Addend first: the image loads box.y (0x64(r1)) BEFORE box.h
        // (0x6c(r1)) and folds with `fmadds f0, f6, f13, f0`.  Written as
        // `box.h * yFrac + box.y` the loads come out in the other order.
        float texY = (box.y + box.h * yFrac) * dir;

        for (int j = 0; j < 4; j++) {
            float xFrac;
            switch (j) {
            case 0:
                xFrac = 0.0f;
                break;
            case 1:
                xFrac = 0.2f;
                break;
            case 2:
                xFrac = 0.8f;
                break;
            case 3:
                xFrac = 1.0f;
                break;
            }
            // Same addend-first rule: box.x (0x60(r1)) loads before box.w.
            mesh->Verts()[vertIdx].tex.x = box.x + box.w * xFrac;
            mesh->Verts()[vertIdx].tex.y = texY;
            // Post-increment: the image computes `mulli r10, r9, 0x60` once in
            // the preheader and bumps r9/r10 at the BOTTOM of the body, so the
            // two stores use positive 0x40/0x44 displacements.  Incrementing
            // first makes MSVC pre-bump the byte offset and store at -0x20/-0x1c.
            vertIdx++;
        }
    }
}
