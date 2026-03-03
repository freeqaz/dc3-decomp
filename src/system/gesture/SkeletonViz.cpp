#include "gesture/SkeletonViz.h"
#include "SkeletonViz.h"
#include "gesture/BaseSkeleton.h"
#include "hamobj/HamCharacter.h"
#include "math/Geo.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "os/File.h"
#include "rndobj/Cam.h"
#include "rndobj/Draw.h"
#include "rndobj/Env.h"
#include "rndobj/Line.h"
#include "rndobj/Mat.h"
#include "rndobj/Utl.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include <algorithm>

SkeletonViz::SkeletonViz()
    : mUsePhysicalCam(0), mPhysicalCamRotation(0), mCurrentCamRotation(0), mAxesCoordSys(kCoordCamera),
      mUtlLine(0), mSkeletonEnv(0), mCamMesh(0), mJointMesh(0), mJointMat(0),
      mPhysicalCam(0), mLineWidthScale(0), unk218(true) {
    unk194.Reset();
    Multiply(Hmx::Matrix3(1, 0, 0, 0, 0, 1, 0, 1, 0), unk194.m, unk194.m);
    unk1d4 = unk194;
    for (int i = 0; i < kNumBones; i++) {
        mBoneLines[i] = nullptr;
    }
}

SkeletonViz::~SkeletonViz() {
    for (int i = 0; i < kNumBones; i++) {
        delete mBoneLines[i];
    }
}

BEGIN_HANDLERS(SkeletonViz)
    HANDLE_ACTION(rotate, Rotate(_msg->Float(2)))
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(SkeletonViz)
    SYNC_PROP(use_physical_cam, mUsePhysicalCam)
    SYNC_PROP_SET(
        physical_cam_rotation, mPhysicalCamRotation, SetPhysicalCamRotation(_val.Float())
    )
    SYNC_PROP_SET(
        axes_coord_sys, mAxesCoordSys, SetAxesCoordSys((SkeletonCoordSys)_val.Int())
    )
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(SkeletonViz)
    SAVE_REVS(6, 1)
    SAVE_SUPERCLASS(RndPollable)
    SAVE_SUPERCLASS(RndDrawable)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mUsePhysicalCam;
    bs << mAxesCoordSys;
    bs << mPhysicalCamRotation;
END_SAVES

BEGIN_COPYS(SkeletonViz)
    COPY_SUPERCLASS(RndPollable)
    COPY_SUPERCLASS(RndDrawable)
    COPY_SUPERCLASS(RndTransformable)
    CREATE_COPY(SkeletonViz)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mUsePhysicalCam)
        COPY_MEMBER(mAxesCoordSys)
        COPY_MEMBER(mPhysicalCamRotation)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_LOADS(SkeletonViz)
    PreLoad(bs);
    PostLoad(bs);
END_LOADS

INIT_REVS(6, 1)

void SkeletonViz::PreLoad(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(6, 1)
    if (d.rev > 5) {
        Hmx::Object::Load(bs);
    }
    RndDrawable::Load(bs);
    RndTransformable::Load(bs);
    if (d.rev > 0) {
        d >> mUsePhysicalCam;
    }
    if (d.rev > 1) {
        int cs;
        d >> cs;
        mAxesCoordSys = (SkeletonCoordSys)cs;
    }
    if (d.rev > 2 && d.rev < 4) {
        int x, y;
        d >> x;
        d >> y;
    }
    if (d.altRev > 0) {
        d >> mPhysicalCamRotation;
    }
    mCurrentCamRotation = mPhysicalCamRotation;
    if (d.rev > 4 && d.altRev < 1) {
        ObjPtr<HamCharacter> hChar(this);
        d >> hChar;
    }
    if (TheLoadMgr.EditMode()) {
        LoadResource(true);
    }
}

void SkeletonViz::PostLoad(BinStream &bs) {
    if (TheLoadMgr.EditMode()) {
        mResource.PostLoad(nullptr);
        UpdateResource();
    }
}

float SkeletonViz::PhysicalCamRotation() const { return mPhysicalCamRotation; }
void SkeletonViz::SetUsePhysicalCam(bool use) { mUsePhysicalCam = use; }
void SkeletonViz::SetPhysicalCamRotation(float rotation) {
    mPhysicalCamRotation = rotation;
    mCurrentCamRotation = rotation;
}
void SkeletonViz::Rotate(float amt) { mCurrentCamRotation += amt; }
void SkeletonViz::SetAxesCoordSys(SkeletonCoordSys cs) { mAxesCoordSys = cs; }

void SkeletonViz::Init() {
    if (!mResource) {
        for (int i = 0; i < kNumBones; i++) {
            mBoneLines[i] = Hmx::Object::New<RndLine>();
        }
        LoadResource(false);
        UpdateResource();
    }
}

void SkeletonViz::LoadResource(bool postload) {
    static Symbol objects("objects");
    mResource.LoadFile(
        FilePath(FileSystemRoot(), "ham/skeleton.milo"), postload, true, kLoadFront, false
    );
    if (!postload) {
        mResource.PostLoad(nullptr);
    }
}

void SkeletonViz::UpdateResource() {
    Transform xfm;
    xfm.Reset();
    MILO_ASSERT(mResource.IsLoaded(), 0x1E8);
    mSkeletonEnv = mResource->Find<RndEnviron>("skeleton.env", true);
    mCamMesh = mResource->Find<RndMesh>("camera.mesh", true);
    mCamMesh->SetTransParent(this, false);
    mCamMesh->SetLocalPos(xfm.v);
    mPhysicalCam = mResource->Find<RndCam>("physical.cam", true);
    mPhysicalCam->SetTransParent(this, false);
    mPhysicalCam->SetLocalXfm(xfm);
    mJointMesh = mResource->Find<RndMesh>("joint.mesh", true);
    mJointMesh->SetTransParent(this, false);
    mJointMesh->SetLocalPos(xfm.v);
    mJointMat = mResource->Find<RndMat>("joint.mat", true);
    mUtlLine = mResource->Find<RndLine>("utl.line", true);
    mUtlLine->SetTransParent(this, false);
    mUtlLine->SetLocalXfm(xfm);
    mSphereMesh = mResource->Find<RndMesh>("sphere.mesh", true);
    RndLine *boneLine = mResource->Find<RndLine>("bone.line", true);
    for (int i = 0; i < kNumBones; i++) {
        if (!mBoneLines[i])
            mBoneLines[i] = Hmx::Object::New<RndLine>();
        mBoneLines[i]->Copy(boneLine, kCopyShallow);
        mBoneLines[i]->SetTransParent(this, false);
        mBoneLines[i]->SetLocalXfm(xfm);
    }
}

void SkeletonViz::SetPhysicalCamScreenRect(const Hmx::Rect &r) {
    MILO_ASSERT(r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0, 0x64);
    MILO_ASSERT(mPhysicalCam, 0x65);
    mPhysicalCam->SetScreenRect(r);
}

void SkeletonViz::DrawLine3D(
    const Vector3 &vec1,
    const Vector3 &vec2,
    float f,
    const Hmx::Color &color1,
    Hmx::Color *color2
) {
    Vector3 localVec1, localVec2;
    Multiply(vec1, unk1d4, localVec1);
    Multiply(vec2, unk1d4, localVec2);
    mUtlLine->SetPointPos(0, localVec1);
    mUtlLine->SetPointPos(1, localVec2);
    RndMat *mat = mUtlLine->Mat();
    MILO_ASSERT(mat, 0x178);

    if (!color2) {
        mat->SetColor(color1.red, color1.green, color1.blue);
    } else {
        mUtlLine->SetMat(0);
        mUtlLine->SetPointColor(0, *color2, true);
        mUtlLine->SetPointColor(1, color1, true);
    }
    mUtlLine->SetWidth(mLineWidthScale * f);
    mUtlLine->DrawShowing();
    mUtlLine->SetMat(mat);
}

void SkeletonViz::Poll() {
    if (mPhysicalCamRotation <= mCurrentCamRotation) {
        if (mCurrentCamRotation <= mPhysicalCamRotation) {
            return;
        }
        mCurrentCamRotation -= TheTaskMgr.DeltaUISeconds() * 120.0f;
        if (mCurrentCamRotation < mPhysicalCamRotation) {
            mCurrentCamRotation = mPhysicalCamRotation;
        }
    } else {
        mCurrentCamRotation += TheTaskMgr.DeltaUISeconds() * 120.0f;
        if (mCurrentCamRotation > mPhysicalCamRotation) {
            mCurrentCamRotation = mPhysicalCamRotation;
        }
    }
}

void SkeletonViz::SetCamera(const SkeletonFrame &frame, const Transform &worldXfm, float) {
    if (!mUsePhysicalCam) {
        if (mAxesCoordSys == kCoordCamera || !unk218) {
            UtilDrawAxes(worldXfm, 5.0f / mLineWidthScale, Hmx::Color(1, 1, 1, 1));
        }
        if (unk218 && mCamMesh) {
            mCamMesh->SetWorldPos(worldXfm.v);
            mCamMesh->DrawShowing();
        }
    } else if (mPhysicalCam && unk218) {
        Transform camXfm = worldXfm;
        Hmx::Matrix3 rotMtx;
        rotMtx.Identity();
        RotateAboutZ(rotMtx, mCurrentCamRotation * DEG2RAD, rotMtx);
        camXfm.m = rotMtx;
        mPhysicalCam->SetLocalXfm(camXfm);
        mPhysicalCam->SetFrustum(0.01f, 10.0f, 0.7955211f, 1.0f);
        mPhysicalCam->Select();
    }

    if (unk218) {
        Plane plane;
        plane.Set(
            frame.mFloorClipPlane.x,
            frame.mFloorClipPlane.y,
            frame.mFloorClipPlane.z,
            frame.mFloorClipPlane.w
        );
        UtilDrawPlane(plane, worldXfm.v, Hmx::Color(1, 1, 0, 1), 5, 0.5f, false);
    }
}

void SkeletonViz::DrawPoint3D(
    const Vector3 &vec, float scale, const Hmx::Color &color, float alpha
) {
    if (!mSphereMesh) {
        return;
    }

    Vector3 point;
    Multiply(vec, unk1d4, point);
    if (unk218) {
        Multiply(point, WorldXfm(), point);
    }

    if (mSphereMesh->Mat()) {
        mSphereMesh->Mat()->SetColor(color.red, color.green, color.blue);
    }
    mSphereMesh->SetWorldPos(point);
    mSphereMesh->DrawShowing();
}

void SkeletonViz::DrawJoints(
    const BaseSkeleton &skeleton, Vector3 *camPos, Vector3 *drawPos, bool faded
) {
    float tint = faded ? 0.5f : 1.0f;

    float minZ = 1.0e30f;
    for (int i = 0; i < kNumBones; i++) {
        minZ = std::min(minZ, camPos[BaseSkeleton::sBones[i].joint1].z);
    }

    float maxDepth = camPos[kJointShoulderCenter].z + camPos[kJointHead].z
        + camPos[kJointShoulderLeft].z + minZ;
    float invRange = 1.0f / (minZ - maxDepth);

    for (int i = 0; i < kNumBones; i++) {
        const BoneJoints &bone = BaseSkeleton::sBones[i];
        float c0 =
            std::max(0.0f, std::min(1.0f, (camPos[bone.joint1].z - maxDepth) * invRange));
        float c1 =
            std::max(0.0f, std::min(1.0f, (camPos[bone.joint2].z - maxDepth) * invRange));
        c0 = c0 * 0.8f + 0.2f;
        c1 = c1 * 0.8f + 0.2f;
        Hmx::Color col0(tint * c0, tint * c0, tint * c0, 1.0f);
        Hmx::Color col1(tint * c1, tint * c1, tint * c1, 1.0f);

        mBoneLines[i]->SetPointColor(0, col0, true);
        mBoneLines[i]->SetPointColor(1, col1, true);
        mBoneLines[i]->SetPointPos(0, drawPos[bone.joint1]);
        mBoneLines[i]->SetPointPos(1, drawPos[bone.joint2]);
        float baseWidth = mBoneLines[i]->GetWidth();
        mBoneLines[i]->SetWidth(mLineWidthScale * baseWidth);
        mBoneLines[i]->DrawShowing();
        mBoneLines[i]->SetWidth(baseWidth);
    }

    if (mJointMesh && mJointMat) {
        for (int i = 0; i < kNumJoints; i++) {
            JointConfidence conf = skeleton.JointConf((SkeletonJoint)i);
            Hmx::Color color(1, 0, 0, 1);
            if (conf == kConfidenceInferred) {
                color = Hmx::Color(0.5f, 0.5f, 0.0f, 1.0f);
            } else if (conf == kConfidenceTracked) {
                color = Hmx::Color(0.0f, 0.5f, 0.0f, 1.0f);
            }
            mJointMat->SetColor(color.red, color.green, color.blue);
            mJointMesh->SetWorldPos(drawPos[i]);
            mJointMesh->DrawShowing();
        }
    }
}

void SkeletonViz::Visualize(
    const CameraInput &input,
    const BaseSkeleton &skeleton,
    std::vector<SkeletonCallback *> *callbacks,
    bool faded
) {
    if (!mResource) {
        MILO_ASSERT(TheLoadMgr.EditMode(), 0x72);
        Init();
    }
    MILO_ASSERT(mResource.IsLoaded(), 0x76);

    RndEnvironTracker environTracker(mSkeletonEnv, nullptr);

    unk218 = !input.NatalToWorld(unk1d4);
    if (unk218) {
        unk1d4 = unk194;
    }
    mLineWidthScale = input.DrawScale();

    Transform worldXfm;
    if (unk218) {
        worldXfm = WorldXfm();
    } else {
        worldXfm = unk1d4;
    }

    const SkeletonFrame &cachedFrame = input.CachedFrame();
    RndCam *currentCam = RndCam::Current();
    if (skeleton.IsTracked()) {
        Vector3 camJointPos[kNumJoints];
        Vector3 drawJointPos[kNumJoints];
        for (int i = 0; i < kNumJoints; i++) {
            skeleton.JointPos(kCoordCamera, (SkeletonJoint)i, camJointPos[i]);
            Multiply(camJointPos[i], unk194, drawJointPos[i]);
        }
        SetCamera(cachedFrame, worldXfm, drawJointPos[kJointShoulderCenter].z);
        DrawJoints(skeleton, camJointPos, drawJointPos, faded);

        if (callbacks) {
            FOREACH (it, *callbacks) {
                (*it)->Draw(skeleton, *this);
            }
        }
    } else {
        SetCamera(cachedFrame, worldXfm, 0.0f);
        if (currentCam) {
            currentCam->Select();
        }
    }
}
