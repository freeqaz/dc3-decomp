#pragma once
#include "char\Character.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\CameraInput.h"
#include "gesture\Skeleton.h"
#include "rndobj\Trans.h"

class CharCameraInput : public CameraInput {
public:
    CharCameraInput(Character *);
    // CameraInput
    virtual float DrawScale() const { return kDrawScale; }
    virtual bool NatalToWorld(Transform &) const;
    /** ??_7CharCameraInput@@6BCameraInput@@@ slot +0x18 -- the only virtual
     *  CharCameraInput adds to CameraInput's six (CameraInput,
     *  LiveCameraInput and StubCameraInput are all 6 slots on both sides).
     *  ?UsingTiltCorrection@CharCameraInput@@UBA_NXZ is in ham_xbox_r.map at
     *  0x82AEAE70 ("li r3,0; blr"). */
    virtual bool UsingTiltCorrection() const { return false; }

    void ResetSkeletonCharOrigin();
    void SetUnk2430(bool b) { unk2430 = b; }

    static float const kDrawScale;

protected:
    virtual const SkeletonFrame *PollNewFrame();

    Character *mChar; // 0x11d4
    SkeletonFrame mCharFrame; // 0x11d8
    RndTransformable *mBoneNames[kNumJoints]; // 0x23a0
    Transform mNatalXfm; // 0x23f0
    bool unk2430; // 0x2430
};
