#include "hamobj\DancerSequence.h"
#include "DancerSkeleton.h"
#include "gesture\BaseSkeleton.h"
#include "hamobj\MoveDir.h"
#include "math/Key.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Anim.h"

DancerSequence::DancerSequence() {}

BEGIN_HANDLERS(DancerSequence)
    HANDLE_SUPERCLASS(RndAnimatable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(DancerSequence)
    SYNC_SUPERCLASS(RndAnimatable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(DancerSequence)
    SAVE_REVS(8, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndAnimatable)
    int numFrames = mDancerFrames.size();
    bs << numFrames;
    for (int i = 0; i < numFrames; i++) {
        const DancerFrame &curFrame = mDancerFrames[i];
        bs << curFrame.mMoveIdx;
        bs << curFrame.mMoveFrameIdx;
        const DancerSkeleton &skeleton = curFrame.mSkeleton;
        for (int j = 0; j < kNumJoints; j++) {
            bs << skeleton.CamJointPos((SkeletonJoint)j);
            bs << skeleton.CamJointDisplacement((SkeletonJoint)j);
        }
        bs << skeleton.ElapsedMs();
    }
END_SAVES

BEGIN_COPYS(DancerSequence)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndAnimatable)
    CREATE_COPY_AS(DancerSequence, seq)
    MILO_ASSERT(seq, 0xF2);
    COPY_MEMBER_FROM(seq, mDancerFrames)
END_COPYS

INIT_REVS(8, 0)

BEGIN_LOADS(DancerSequence)
    LOAD_REVS(bs)
    ASSERT_REVS(8, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndAnimatable)
    int numFrames;
    d >> numFrames;
    mDancerFrames.resize(numFrames);
    for (int i = 0; i < numFrames; i++) {
        DancerFrame &curFrame = mDancerFrames[i];
        if (d.rev < 1) {
            int val;
            d >> val;
            curFrame.mMoveIdx = curFrame.mMoveFrameIdx = -1;
        } else if (d.rev < 7) {
            int val0, val1;
            d >> val0;
            d >> val1;
            curFrame.mMoveIdx = val0;
            curFrame.mMoveFrameIdx = val1;
        } else {
            d >> curFrame.mMoveIdx;
            d >> curFrame.mMoveFrameIdx;
        }
        DancerSkeleton &skeleton = curFrame.mSkeleton;
        if (d.rev < 7) {
            int skeletonRev = 5;
            // The image funnels all five arms into a single
            // SetDisplacementElapsedMs(ms) with `ms` a stack local (0x5c(r31)):
            // -1 on the pre-rev-5 arms, `bs.ReadEndian(&ms, 4)` on BOTH the rev<6
            // arm and the rev>=6 arm. The rev>=6 arm reading the int is not
            // cosmetic -- without it a rev-6 DancerSequence desyncs the stream by
            // four bytes for the rest of the frame, and mElapsedMs is never set.
            int ms;
            if (d.rev < 2) {
                skeletonRev = 0;
                ms = -1;
            } else if (d.rev < 3) {
                skeletonRev = 1;
                ms = -1;
            } else if (d.rev < 4) {
                skeletonRev = 2;
                ms = -1;
            } else if (d.rev < 5) {
                skeletonRev = 3;
                ms = -1;
            } else {
                if (d.rev < 6) {
                    skeletonRev = 4;
                }
                d >> ms;
            }
            skeleton.SetDisplacementElapsedMs(ms);
            if (skeletonRev < 3) {
                bool unusedBool;
                d >> unusedBool;
                int unusedInt;
                d >> unusedInt;
            }
            for (int jointIdx = 0; jointIdx < kNumJoints; jointIdx++) {
                int count = 6;
                if (skeletonRev < 1) {
                    count = 4;
                } else if (skeletonRev < 2) {
                    count = 9;
                }
                for (int dataIdx = 0; dataIdx < count; dataIdx++) {
                    if (dataIdx >= 6) {
                        Vector3 v;
                        d >> v >> v >> v;
                    } else {
                        if (dataIdx == 0) {
                            Vector3 pos, disp;
                            d >> pos;
                            d >> disp;
                            skeleton.SetCamJointPos((SkeletonJoint)jointIdx, pos);
                            skeleton.SetCamJointDisplacement((SkeletonJoint)jointIdx, disp);
                        } else {
                            Vector3 v1, v2;
                            d >> v1 >> v2;
                        }
                        if (skeletonRev < 4) {
                            Vector3 v;
                            d >> v;
                        }
                    }
                }
                if (skeletonRev < 3) {
                    Key<float> unusedKey;
                    d.stream >> unusedKey;
                    int unusedVal;
                    d >> unusedVal;
                } else if (skeletonRev >= 5) {
                    int unusedVal;
                    d >> unusedVal;
                }
            }
            if (skeletonRev < 2) {
                for (int row = 0; row < 2; row++) {
                    for (int col = 0; col < 3; col++) {
                        std::vector<float> floats;
                        d >> floats;
                    }
                }
            }
        } else {
            for (int jointIdx = 0; jointIdx < kNumJoints; jointIdx++) {
                Vector3 pos;
                Vector3 disp;
                d >> pos;
                d >> disp;
                skeleton.SetCamJointPos((SkeletonJoint)jointIdx, pos);
                skeleton.SetCamJointDisplacement((SkeletonJoint)jointIdx, disp);
                if (d.rev < 8) {
                    int unusedVal;
                    d >> unusedVal;
                }
            }
            int ms;
            d >> ms;
            skeleton.SetDisplacementElapsedMs(ms);
        }
    }
END_LOADS

void DancerSequence::SetFrame(float frame, float blend) {
    RndAnimatable::SetFrame(frame, blend);
    MoveDir *m = dynamic_cast<MoveDir *>(this->Dir());
    if (m)
        m->SetDancerSequence(this);
}

float DancerSequence::EndFrame() { return mDancerFrames.size() - 1.0f; }

const std::vector<DancerFrame> &DancerSequence::GetDancerFrames() const {
    return mDancerFrames;
}

const DancerSkeleton *DancerSequence::CurSkeleton() const {
    int idx = GetFrame();
    if (idx >= 0 && idx < mDancerFrames.size()) {
        return &mDancerFrames[idx].mSkeleton;
    } else {
        return nullptr;
    }
}
