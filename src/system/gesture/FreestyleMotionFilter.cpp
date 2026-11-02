#include "gesture\FreestyleMotionFilter.h"
#include "Skeleton.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "obj/Object.h"
#include "os\Debug.h"

FreestyleMotionFilter::FreestyleMotionFilter() : mIsActive(false) { Clear(); }

FreestyleMotionFilter::~FreestyleMotionFilter() {}

BEGIN_PROPSYNCS(FreestyleMotionFilter)
    SYNC_PROP(velocity_threshold, mVelocityThreshold)
    SYNC_PROP(move_time, mMoveTime)
    SYNC_PROP(movement_amount, mMovementAmount)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void FreestyleMotionFilter::Clear() {
    mVelocityThreshold = 0.0f;
    mMoveTime = 0.0f;
    mMovementAmount = 0.0f;
}

void FreestyleMotionFilter::Activate() {
    mIsActive = true;
    mMoveTime = 0.0f;
    mMovementAmount = 0.0f;
}

void FreestyleMotionFilter::Deactivate() { mIsActive = false; }

bool FreestyleMotionFilter::IsActive() const { return mIsActive; }

bool FreestyleMotionFilter::Detected() { return 0 < mMoveTime; }

void FreestyleMotionFilter::UpdateFilters(SkeletonUpdateData const &skeletonData) {
    HamPlayerData *player = TheGameData->Player(0);
    MILO_ASSERT(player, 0x44);
    Skeleton *skeleton;
    if (player->IsPlaying() && (skeleton = *skeletonData.mSkeletonsLeft) != 0) {
        mMoveTime = 0.0f;
        for (int i = 0; i < 20; i++) {
            Vector3 velocity;
            int elapsed;
            if (skeleton->Velocity(*skeletonData.mHistory, kCoordCamera, (SkeletonJoint)i, skeleton->ElapsedMs(), velocity, elapsed) &&
                skeleton->JointConf((SkeletonJoint)i) == kConfidenceTracked) {
                float vx = velocity.x;
                float vy = velocity.y;
                float vz = velocity.z;
                float x2 = vx * vx;
                float y2 = vy * vy;
                float z2 = vz * vz;
                float sum = x2 + z2;
                if (sum + y2 > mVelocityThreshold) {
                    // STILL OPEN (w26-f8, fuzzy 99.78494, canonical 100.0): the
                    // two residual rows are the ADD TREE, not the registers.
                    // Image: f0 = f13 + f0 then + f12, i.e. (y2 + x2) + z2.
                    // Ours: f13 = f13 + f12 then + f0, i.e. (y2 + z2) + x2.
                    // Source-ORDER spelling is inert (x2+y2+z2, y2+x2+z2,
                    // z2+y2+x2, a named `total`, a named first sum -- all five
                    // exactly 99.78494), so c2 picks the pairing, not the
                    // source.  What DOES move it is explicit parentheses, which
                    // change c1's folding before contraction: `(x2 + y2) + z2`
                    // and `(y2 + x2) + z2` reach 99.89247 (one row left), while
                    // `(x2 + z2) + y2` -- the tree the source already parses to
                    // -- collapses to 96.18279.  Not landed: 99.89247 still
                    // emits a different association from the image's, so it
                    // changes float rounding without reaching the image's.
                    mMovementAmount = sqrtf(x2 + z2 + y2);
                    mMoveTime = (float)skeleton->ElapsedMs();
                    return;
                }
            }
        }
    }
}

BEGIN_HANDLERS(FreestyleMotionFilter)
    HANDLE_ACTION(activate, Activate())
    HANDLE_ACTION(deactivate, Deactivate())
    HANDLE_EXPR(detected, Detected())
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
