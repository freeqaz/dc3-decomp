#include "gesture\SkeletonViz.h"
#include "SkeletonViz.h"
#include "gesture\BaseSkeleton.h"
#include "hamobj\HamCharacter.h"
#include "math/Geo.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "os\File.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\Env.h"
#include "rndobj\Line.h"
#include "rndobj\Mat.h"
#include "rndobj\Utl.h"
#include "rndobj\Poll.h"
#include "rndobj\Trans.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include <algorithm>

// mLineWidthScale defaults to 1.0f, not 0: the image stores f13 --
// `lfs f13, "__real@3f800000"@l(r6)` at 0x824431D8 -- into 0x214(r30) at
// 0x82443268, beside the `stb r4, 0x218(r30)` that sets unk218 to 1.  A zero
// there scales every skeleton bone line to zero width.  Fixing the constant
// took the whole constructor from 80.29 to 100.0 on its own.
SkeletonViz::SkeletonViz()
    : mUsePhysicalCam(0), mPhysicalCamRotation(0), mCurrentCamRotation(0),
      mAxesCoordSys(kCoordCamera), mUtlLine(0), mSkeletonEnv(0), mCamMesh(0),
      mJointMesh(0), mJointMat(0), mPhysicalCam(0), mLineWidthScale(1),
      unk218(true) {
    unk194.Reset();
    Hmx::Matrix3 rot(Vector3(1, 0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0));
    Multiply(rot, unk194.m, unk194.m);
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

// w15-r: file-scope .rdata ahead of INIT_REVS.  The image holds 45.58f (0x423651EC)
// at 0x82031C08, immediately before gRev (0x82031C0C); SetFrustum's 0x3F4BA745 is
// exactly 45.58f * DEG2RAD.  With it, gRev is no longer at offset 0 of this TU's
// .rdata, and MSVC anchors the ASSERT_REVS pair on gAltRev like the image.
const float kPhysicalCamFovDeg = 45.58f;

INIT_REVS(6, 1)

// w15-a (98.45 canonical): the whole residual is the INIT_REVS anchor pick
// (image anchors &gAltRev and reaches gRev as -4; we anchor &gRev) -- see the
// w9-f note in rndobj/Spline.cpp.  Of the 18 image functions that anchor on
// gAltRev, 8 already do so in our build at 100% (CharClip, HamIKEffector,
// HamListRibbon::PreLoad, HamRegulate, CharLookAt, ThreeDSound, InlineHelp,
// RndShockwave), so it IS reproducible from source; the discriminator is
// still unknown.
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
#ifdef HX_NATIVE
    // MILO_ASSERT is print-and-continue natively (MILO_FATAL_FAILS=1 aborts);
    // the image stops at the assert, so do not run on into a null resource.
    if (!mResource.IsLoaded())
        return;
#endif
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
        mUtlLine->SetPointColor(0, color1, true);
        mUtlLine->SetPointColor(1, *color2, true);
    }
    mUtlLine->SetWidth(mLineWidthScale * f);
    mUtlLine->DrawShowing();
    mUtlLine->SetMat(mat);
}

void SkeletonViz::Poll() {
    if (mPhysicalCamRotation < mCurrentCamRotation) {
        mPhysicalCamRotation += TheTaskMgr.DeltaUISeconds() * 120.0f;
        if (mPhysicalCamRotation <= mCurrentCamRotation) {
            return;
        }
    } else {
        if (mPhysicalCamRotation <= mCurrentCamRotation) {
            return;
        }
        mPhysicalCamRotation -= TheTaskMgr.DeltaUISeconds() * 120.0f;
        if (mPhysicalCamRotation >= mCurrentCamRotation) {
            return;
        }
    }
    mPhysicalCamRotation = mCurrentCamRotation;
}

void SkeletonViz::SetCamera(
    const SkeletonFrame &frame, const Transform &worldXfm, float distance
) {
    if (mUsePhysicalCam) {
        if (unk218) {
            Vector3 pos;
            pos.x = 0.0f;
            pos.y = -distance;
            pos.z = 0.0f;
            RotateAboutZ(pos, mPhysicalCamRotation * DEG2RAD, pos);
            pos.y += distance;
            mPhysicalCam->SetLocalPos(pos);
            float tiltRad = frame.TiltAngle();
            float tilt = tiltRad * RAD2DEG;
            mPhysicalCam->SetLocalRot(Vector3(tilt, 0.0f, mPhysicalCamRotation));
            mPhysicalCam->SetFrustum(0.01f, 10.0f, kPhysicalCamFovDeg * DEG2RAD, 1.0f);
        } else {
            Transform xfm;
            xfm.Reset();
            xfm.v = unk1d4.v;
            mPhysicalCam->SetFrustum(0.5f, 1000.0f, mPhysicalCam->YFov(), 1.0f);
            mPhysicalCam->SetLocalXfm(xfm);
        }
        mPhysicalCam->Select();
    } else {
        if (mAxesCoordSys == kCoordCamera || !unk218) {
            UtilDrawAxes(
                worldXfm, 5.0f / mLineWidthScale, Hmx::Color(1.0f, 1.0f, 1.0f, 1.0f)
            );
        }
        if (unk218) {
            mCamMesh->SetWorldPos(worldXfm.v);
            mCamMesh->DrawShowing();
            Vector3 normal;
            Multiply(frame.mFloorNormal, unk1d4.m, normal);
            Add(worldXfm.v, normal, normal);
            // The image stores 0.0f into the blue channel of this colour
            // (`stfs f30, 0x78(r1)` at 0x824408C8, f30 = __real@00000000, and
            // 0x70(r1) is the Hmx::Color passed as r6 to DrawLine at
            // 0x82440878 `addi r6, r1, 0x70`): the floor-normal debug line is
            // YELLOW, the same colour as the floor plane drawn below -- not
            // white. We had (1,1,1,1).
            TheRnd.DrawLine(
                worldXfm.v, normal, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), false
            );
        }
    }

    // RESIDUAL (SetCamera, 95.1 canonical / 95.0 raw): frame delta +0x10.
    // The image shares ONE 16-byte slot at r1+0x50 between the `pos` of the
    // mUsePhysicalCam branch (stores at 0x82440650..0x8244065C, re-read as a
    // 16-byte copy at 0x82440690..0x824406AC) and `plane` here (stores at
    // 0x82440938..0x82440948, `addi r3, r1, 0x50` at 0x8244096C).  We give
    // plane 0x50 and pos 0x60, which pushes every later slot up by 0x10 and
    // costs 30 offset rows plus the 6I/6D schedule cluster inside the inlined
    // SetLocalPos.  NEGATIVES, both measured at exactly 95.1/95.0 (inert):
    //   - wrapping this block's body in an extra `{ }` to match pos's lexical
    //     depth;
    //   - hoisting `Vector3 pos` out to the `if (mUsePhysicalCam)` scope so the
    //     two locals sit at the same depth.
    // Vector3 is 12 bytes and Plane is 16 (math/Vec.h, math/Mtx.h:357), so the
    // packer may simply refuse to merge unequal sizes; needs the permuter.
    //
    // w7-by (still 95.083336): the size theory is wrong -- Vector3 carries a
    // PAD word (Vec.h:140), both are 16 bytes, and the SetLocalRot by-value
    // temp (0x50..0x5f, `ld r4/r5` at 0x824406D8/0x824406EC) already shares
    // 0x50 with `plane` in OUR build too.  The +0x10 is `pos` alone, and what
    // keeps it out of the shared slot is its address reaching the OUT-OF-LINE
    // `RotateAboutZ` (0x82440674): deleting that call (measurement only) puts
    // pos on 0x50 and the frame at 0x140.  Every spelling that keeps the call
    // leaves pos distinct: direct-store init, `Vector3 pos(0,-d,0)`, a
    // whole-object copy init, pos in its own inner `{ }`, a copy temp handed
    // to SetLocalPos, out-of-line SetWorldPos in place of the inlined
    // SetLocalPos, an inline helper returning pos by value, an inline helper
    // owning pos as a named local (95.5 -- reloads mPhysicalCam into r30).
    // The one shape that DOES land pos on 0x50 with frame 0x140 is a by-value
    // `Vector3` parameter of an inlined static helper -- but the callee writes
    // it, so the 16-byte argument copy survives (+16 rows, 91.6; the same with
    // an uninitialised `Vector3()` argument, 87.6), and a reference-taking
    // helper is not inlined at all (62.4).  `plane` itself is not the lever:
    // it is passed to out-of-line Multiply/UtilDrawPlane exactly like pos and
    // shares in both builds.  Whatever the original wrote for pos, it is not
    // a named local whose address escapes.
    //
    // w21-bk (still 95.083336): standalone cl.exe probes of the rule itself --
    // two address-escaping 16-byte locals in SEQUENTIAL scopes never share a
    // slot (if/if, nested if/if, plain struct or with ctor, via an inline
    // helper's named local: frame always +0x10), while the two ARMS of one
    // if/else do share.  So in the image pos and plane cannot both be named
    // escaping locals in sequential scopes.  Measured in the tree: reusing pos
    // for the SetLocalRot argument (`pos.Set(tilt, 0, rot); SetLocalRot(pos)`)
    // lands pos on 0x50 but pushes plane off it (93.6, worse; reverted).  The
    // two fmadds/fadds operand-order rows in the floor-normal block are
    // commutative-only (same association as the image); `normal += v` inert.
    if (unk218) {
        Plane plane = *(const Plane *)&frame.mFloorClipPlane;
        Transform localXfm = unk1d4;
        localXfm.v = worldXfm.v;
        Multiply(plane, localXfm, plane);
        Vector3 planePos = worldXfm.v;
        planePos.y += distance;
        UtilDrawPlane(
            plane, planePos, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), 5, 0.5f, false
        );
    }
}

void SkeletonViz::DrawPoint3D(
    const Vector3 &vec, float scale, const Hmx::Color &color, float alpha
) {
    Vector3 point;
    Multiply(vec, unk1d4, point);
    if (unk218) {
        Multiply(point, WorldXfm(), point);
    }

    float scaled = mLineWidthScale * scale;
    mSphereMesh->Mat()->SetColor(color.red, color.green, color.blue);
    mSphereMesh->Mat()->SetAlpha(alpha);
    mSphereMesh->Mat()->SetCull(kCullNone);

    Transform sphereXfm;
    sphereXfm.Reset();
    sphereXfm.v = point;
    Scale(Vector3(scaled, scaled, scaled), sphereXfm.m, sphereXfm.m);
    mSphereMesh->SetLocalXfm(sphereXfm);
    mSphereMesh->SetSphere(Sphere(Vector3(0, 0, 0), scaled));
    mSphereMesh->DrawShowing();
}

void SkeletonViz::DrawJoints(
    const BaseSkeleton &skeleton, Vector3 *camPos, Vector3 *drawPos, bool faded
) {
    float tint;
    if (faded) {
        tint = 0.5f;
    } else {
        tint = 1.0f;
    }
    Hmx::Color tintColor(tint, tint, tint, 1.0f);

    float depthSpan = (skeleton.BoneLength((SkeletonBone)4, kCoordCamera)
                       + skeleton.BoneLength((SkeletonBone)3, kCoordCamera))
        + skeleton.BoneLength((SkeletonBone)2, kCoordCamera);

    float minZ = 1.0e30f;
    for (int i = 0; i < kNumBones; i++) {
        minZ = Min(minZ, camPos[i].z);
    }

    float maxDepth = minZ + depthSpan;
    float invRange = 1.0f / (minZ - maxDepth);

    Hmx::Color shadedColor;
    for (int i = 0; i < kNumBones; i++) {
        // Endpoint 0 receives the unscaled tint; only endpoint 1 is depth-shaded
        // (image 0x82441228: SetPointColor(0) gets &tintColor at 0x60(r1)).
        // shadedColor.alpha is never initialised (Hmx::Color() is empty) and
        // the image multiplies it in place too.  ONE reused `c`, not c0/c1:
        // two locals flip the second block's fmuls operand order.
        float c = (camPos[BaseSkeleton::sBones[i].joint1].z - maxDepth) * invRange;
        shadedColor.alpha *= tintColor.alpha;
        c = Clamp(0.0f, 1.0f, c);
        c = c * 0.8f + 0.2f;
        shadedColor.red = tintColor.red * c;
        shadedColor.green = tintColor.green * c;
        shadedColor.blue = tintColor.blue * c;
        mBoneLines[i]->SetPointColor(0, tintColor, true);

        c = (camPos[BaseSkeleton::sBones[i].joint2].z - maxDepth) * invRange;
        shadedColor.alpha *= tintColor.alpha;
        c = Clamp(0.0f, 1.0f, c);
        c = c * 0.8f + 0.2f;
        shadedColor.red = tintColor.red * c;
        shadedColor.green = tintColor.green * c;
        shadedColor.blue = tintColor.blue * c;
        mBoneLines[i]->SetPointColor(1, shadedColor, true);

        mBoneLines[i]->SetPointPos(0, drawPos[BaseSkeleton::sBones[i].joint1]);
        mBoneLines[i]->SetPointPos(1, drawPos[BaseSkeleton::sBones[i].joint2]);
        float baseWidth = mBoneLines[i]->GetWidth();
        mBoneLines[i]->SetWidth(mLineWidthScale * baseWidth);
        mBoneLines[i]->DrawShowing();
        mBoneLines[i]->SetWidth(baseWidth);
    }

    // w21-bm: 98.45 -> 100 canonical (one register-only row left: the
    // SetWidth `fmuls f0, f0, f24` comes out with its operands swapped; writing
    // `baseWidth * mLineWidthScale`, or reusing `c` for the width, is inert).
    // Levers: both loops plain and indexed (the image's signed `cmpw` bone
    // latch and the joint loop's `li r30,0 / mr r31,r26` are MSVC's strength
    // reduction), the confidence colour as a flat if/else-if/else, baseScale
    // built straight from the local diagonal with Scale() for scaledScale,
    // `(len4 + len3) + len2` as one parenthesised sum, and one reused `c`.
    Vector3 baseScale(
        mJointMesh->LocalXfm().m.x.x,
        mJointMesh->LocalXfm().m.y.y,
        mJointMesh->LocalXfm().m.z.z
    );
    Vector3 scaledScale;
    Scale(baseScale, mLineWidthScale, scaledScale);
    SetLocalScale(mJointMesh, scaledScale);

    for (int i = 0; i < kNumJoints; i++) {
        JointConfidence conf = skeleton.JointConf((SkeletonJoint)i);
        float red;
        float green;
        if (conf == kConfidenceTracked) {
            red = 0.0f;
            green = tint;
        } else if (conf == kConfidenceInferred) {
            red = tint;
            green = tint;
        } else {
            red = tint;
            green = 0.0f;
        }
        mJointMesh->SetLocalPos(drawPos[i]);
        mJointMat->SetColor(red, green, 0.0f);
        mJointMesh->DrawShowing();
    }

    SetLocalScale(mJointMesh, baseScale);

    int clippingFlags = skeleton.QualityFlags();
    // Red, not white: the image stores f31 (1.0) to .red/.alpha and f30 (0.0)
    // to .green/.blue at 0x80-0x8c(r1) before the "clipped ..." strings.
    Hmx::Color textColor(1.0f, 0.0f, 0.0f, 1.0f);
    Vector2 screenPos(0.1f, 0.1f);
    if (mUsePhysicalCam) {
        const Hmx::Rect &screenRect = mPhysicalCam->GetScreenRect();
        float sy = screenRect.y;
        float sx = screenRect.x;
        screenPos.x = sx;
        screenPos.y = sy;
    }
    if (clippingFlags & 1) {
        const Vector2 &sz =
            TheRnd.DrawStringScreen("clipped right", screenPos, textColor, true);
        screenPos.y = sz.y;
    }
    if (clippingFlags & 2) {
        const Vector2 &sz =
            TheRnd.DrawStringScreen("clipped left", screenPos, textColor, true);
        screenPos.y = sz.y;
    }
    if (clippingFlags & 4) {
        const Vector2 &sz =
            TheRnd.DrawStringScreen("clipped top", screenPos, textColor, true);
        screenPos.y = sz.y;
    }
    if (clippingFlags & 8) {
        const Vector2 &sz =
            TheRnd.DrawStringScreen("clipped bottom", screenPos, textColor, true);
        screenPos.y = sz.y;
    }

    if (mAxesCoordSys != kCoordCamera && unk218) {
        Transform axesXfm, worldXfm;
        skeleton.CameraToPlayerXfm(mAxesCoordSys, axesXfm);
        Multiply(axesXfm, unk1d4, worldXfm);
        Multiply(worldXfm, WorldXfm(), worldXfm);
        Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
        Vector3 tmp = worldXfm.m.y;
        worldXfm.m.y = worldXfm.m.z;
        worldXfm.m.z = tmp;
        UtilDrawAxes(worldXfm, mLineWidthScale * 0.25f, white);
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
#ifdef HX_NATIVE
    // Assert first (the image stops there); MILO_ASSERT is non-fatal natively.
    if (!mResource.IsLoaded())
        return;
#endif

    RndEnvironTracker environTracker(mSkeletonEnv, nullptr);

    unk218 = !input.NatalToWorld(unk1d4);
    if (unk218) {
        unk1d4 = unk194;
    }
    mLineWidthScale = input.DrawScale();

    Transform worldXfm;
        worldXfm = unk218 ? WorldXfm() : unk1d4;

    const SkeletonFrame &cachedFrame = input.CachedFrame();
    RndCam *currentCam = RndCam::Current();
    if (skeleton.IsTracked()) {
        Vector3 camJointPos[kNumJoints];
        Vector3 drawJointPos[kNumJoints];
        for (int i = 0; i < kNumJoints; i++) {
            skeleton.JointPos(kCoordCamera, (SkeletonJoint)i, camJointPos[i]);
            Multiply(camJointPos[i], unk194, drawJointPos[i]);
        }
        // The image loads `lfs f1, 0xc4(r31)`.  drawJointPos is the Multiply
        // output array based at r31+0xc0 with a 16-byte stride (the loop above
        // steps 0x10 and stops at 0x140 = kNumJoints * 16), so 0xc4 is element
        // 0 field +4 -- kJointHipCenter's y, not kJointShoulderCenter's z
        // (which is 0xc0 + 2*16 + 8 = 0xe8, what we used to load).  y is the
        // right axis too: SetCamera's `distance` parameter is a forward
        // distance (`pos.y = -distance; ... pos.y += distance;`), and y is
        // depth in this space, so the old spelling fed it the shoulder's
        // height.
        SetCamera(cachedFrame, worldXfm, drawJointPos[kJointHipCenter].y);
        DrawJoints(skeleton, camJointPos, drawJointPos, faded);

        if (callbacks) {
            FOREACH (it, *callbacks) {
                (*it)->Draw(skeleton, *this);
            }
        }
    } else {
        SetCamera(cachedFrame, worldXfm, 0.0f);
    }
    // restore the caller's camera on every path: SetCamera selected ours
    if (currentCam) {
        currentCam->Select();
    }
}
