#include "gesture/HighFiveGestureFilter.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/JointUtl.h"
#include "gesture/Skeleton.h"
#include "math/Vec.h"
#include "obj/Object.h"

HighFiveGestureFilter::HighFiveGestureFilter() : mHighFived(false) {}

HighFiveGestureFilter::~HighFiveGestureFilter() {}

bool HighFiveGestureFilter::CheckHighFive() {
    // One-shot check: returns true once after a high-five is detected,
    // then resets for the next detection
    if (mHighFived) {
        mHighFived = false;
        return true;
    }
    return false;
}

void HighFiveGestureFilter::Update(Skeleton const *skeleton1, Skeleton const *skeleton2) {
    if (skeleton1 && skeleton2) {
        Vector3 shoulderCenter1, shoulderCenter2;
        skeleton1->JointPos(kCoordCamera, kJointShoulderCenter, shoulderCenter1);
        skeleton2->JointPos(kCoordCamera, kJointShoulderCenter, shoulderCenter2);

        // Check all 4 hand pair combinations (left/left, right/left, left/right, right/right)
        // using bit tricks: i&1 cycles skeleton1's hands, i>>1 cycles skeleton2's hands
        for (int i = 0; i < 4; i++) {
            const TrackedJoint &joint1 = skeleton1->HandJoint((SkeletonSide)(i & 1));
            const TrackedJoint &joint2 = skeleton2->HandJoint((SkeletonSide)(i >> 1));

            Vector3 pos1 = joint1.mJointPos[0];
            Vector3 pos2 = joint2.mJointPos[0];

            // Condition 1: At least one hand raised above shoulder, and hands are close
            // Threshold: 0.2m above shoulder, within 0.25m distance
            if ((pos1.y - 0.2f > shoulderCenter1.y) ||
                (pos2.y - 0.2f > shoulderCenter2.y)) {
                // Component order: dz, dx, dy (affects codegen - do not reorder)
                float dz = pos1.z - pos2.z;
                float dx = pos1.x - pos2.x;
                float dy = pos1.y - pos2.y;

                if ((dy * dy + (dx * dx + dz * dz)) < 0.25f * 0.25f) {
                    mHighFived = true;
                    return;
                }
            }

            // Condition 2: Both hands behind camera (depth-based detection)
            // Threshold: within 0.3m distance
            Vector2 screenPos1, screenPos2;
            JointScreenPos(joint1, screenPos1);
            JointScreenPos(joint2, screenPos2);

            if ((screenPos1.x < 0.0f) && (screenPos2.x < 0.0f)) {
                // Component order: dy, dz, dx (different from above - affects codegen)
                float dy = pos1.y - pos2.y;
                float dz = pos1.z - pos2.z;
                float dx = pos1.x - pos2.x;

                if ((dx * dx + (dz * dz + dy * dy)) < 0.3f * 0.3f) {
                    mHighFived = true;
                    return;
                }
            }
        }
    }
}
