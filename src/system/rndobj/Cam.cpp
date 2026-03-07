#include "rndobj/Cam.h"
#include "Rnd.h"
#include "Utl.h"
#include "math/Mtx.h"
#include "math/Rot.h"

#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Draw.h"
#include "rndobj/HiResScreen.h"
#include "rndobj/Trans.h"

float RndCam::sDefaultNearPlane = 1;
float RndCam::sMaxFarNearPlaneRatio = 1000;
static Transform sFlipYZ(Hmx::Matrix3(1, 0, 0, 0, 0, 1, 0, 1, 0), Vector3(0, 0, 0));

RndCam::RndCam()
    : mNearPlane(sDefaultNearPlane), mFarPlane(mNearPlane * sMaxFarNearPlaneRatio),
      mYFov(0.6024178), mAspectRatio(1), mZRange(0.0f, 1.0f),
      mScreenRect(0.0f, 0.0f, 1.0f, 1.0f), mTargetTex(this), mViewProjMatrix(Hmx::Matrix4::ID()),
      mInvViewProjMatrix(Hmx::Matrix4::ID()) {
    UpdateLocal();
}

RndCam::~RndCam() {
    if (sCurrent == this)
        sCurrent = nullptr;
}

BEGIN_HANDLERS(RndCam)
    HANDLE(set_frustum, OnSetFrustum)
    HANDLE(set_z_range, OnSetZRange)
    HANDLE(far_plane, OnFarPlane)
    HANDLE(set_screen_rect, OnSetScreenRect)
    HANDLE(world_to_screen, OnWorldToScreen)
    HANDLE(screen_to_world, OnScreenToWorld)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndCam)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_PROP_SET(near_plane, mNearPlane, SetFrustum(_val.Float(), mFarPlane, mYFov, 1))
    SYNC_PROP_SET(far_plane, mFarPlane, SetFrustum(mNearPlane, _val.Float(), mYFov, 1))
    SYNC_PROP_SET(
        y_fov,
        mYFov * RAD2DEG,
        SetFrustum(mNearPlane, mFarPlane, _val.Float() * DEG2RAD, 1)
    )
    SYNC_PROP(z_range, mZRange)
    SYNC_PROP_MODIFY(screen_rect, mScreenRect, UpdateLocal())
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndCam)
    SAVE_REVS(0xC, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mNearPlane << mFarPlane << mYFov;
    bs << mScreenRect << mZRange;
    bs << mTargetTex;
END_SAVES

BEGIN_COPYS(RndCam)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndTransformable)
    CREATE_COPY(RndCam)
    BEGIN_COPYING_MEMBERS
        if (ty != kCopyFromMax) {
            COPY_MEMBER(mNearPlane)
            COPY_MEMBER(mFarPlane)
            COPY_MEMBER(mYFov)
            COPY_MEMBER(mScreenRect)
            COPY_MEMBER(mZRange)
            COPY_MEMBER(mTargetTex)
        }
    END_COPYING_MEMBERS
    UpdateLocal();
END_COPYS

INIT_REVS(12, 0)

BEGIN_LOADS(RndCam)
    LOAD_REVS(bs)
    ASSERT_REVS(12, 0)
    if (d.rev > 10) {
        Hmx::Object::Load(bs);
    }
    RndTransformable::Load(bs);
    if (d.rev < 10) {
        RndDrawable::DumpLoad(bs);
    }
    if (d.rev == 8) {
        ObjPtrList<Hmx::Object> objList(this, kObjListNoNull);
        int x;
        bs >> x >> objList;
    }
    bs >> mNearPlane;
    bs >> mFarPlane;
    bs >> mYFov;
    if (d.rev < 0xC) {
        mYFov = ConvertFov(mYFov, 0.75f);
    }
    if (d.rev < 2) {
        int x;
        bs >> x;
    }
    bs >> mScreenRect;
    if (d.rev > 0 && d.rev < 3) {
        int x;
        bs >> x;
    }
    if (d.rev > 3) {
        bs >> mZRange;
    }
    if (d.rev > 4) {
        bs >> mTargetTex;
    }
    if (d.rev == 6) {
        int x;
        bs >> x;
    }
    UpdateLocal();
END_LOADS

void RndCam::UpdateLocal() {
    float ratio = (mScreenRect.h / mScreenRect.w) * mAspectRatio;
    if (mTargetTex) {
        ratio *= (float)mTargetTex->Height() / (float)mTargetTex->Width();
    } else {
        ratio *= TheRnd.YRatio();
    }
    mLocalFrustum.Set(mNearPlane, mFarPlane, mYFov, ratio);
    mLocalProjectXfm.m.Zero();
    mLocalProjectXfm.v.Zero();
    mInvLocalProjectXfm.m.Zero();
    mInvLocalProjectXfm.v.Zero();
    if (mYFov == 0) {
        mInvLocalProjectXfm.m.z.x = -ratio;
        mLocalProjectXfm.m.x.x = 1;
        mLocalProjectXfm.v.x = -1.0f / ratio;
        mInvLocalProjectXfm.m.x.x = 1;
    } else {
        float thetan = tanf(mYFov * 0.5f);
        mLocalProjectXfm.m.z.x = 1;
        mInvLocalProjectXfm.v.x = 1;
        mInvLocalProjectXfm.m.x.x = thetan / ratio;
        mInvLocalProjectXfm.m.z.x = -thetan;
        mLocalProjectXfm.m.x.x = ratio / thetan;
        mLocalProjectXfm.v.x = -1.0f / thetan;
    }
    UpdatedWorldXfm();
    mAspect = TheRnd.GetAspect();
}

void RndCam::UpdatedWorldXfm() {
    const Transform &xfm = WorldXfm();
    Invert(xfm, mInvWorldXfm);
    Multiply(mLocalFrustum, xfm, mWorldFrustum);
    Multiply(mInvWorldXfm, mLocalProjectXfm, mWorldProjectXfm);
    Multiply(mInvLocalProjectXfm, xfm, mInvWorldProjectXfm);
}

void RndCam::Select() {
    RndCam *cur = sCurrent;
    if (cur) {
        if (cur->TargetTex() && cur != this) {
            cur->TargetTex()->FinishDrawTarget();
        }
    }
    WorldXfm();
    sCurrent = this;
    if (TheRnd.GetAspect() != mAspect) {
        UpdateLocal();
    }
}

Transform RndCam::GetInvViewXfm() {
    Transform out;
    Multiply(sFlipYZ, WorldXfm(), out);
    return out;
}

void RndCam::GetCamFrustum(Vector3 &origin, Vector3 (&dirs)[4]) {
    const Transform &xfm = WorldXfm();
    origin = xfm.v;
    static Vector2 sCorners[4] = {
        Vector2(0, 0), Vector2(0, 1), Vector2(1, 0), Vector2(1, 1)
    };
    for (int i = 0; (unsigned int)i < 4; i++) {
        ScreenToWorld(sCorners[i], mFarPlane, dirs[i]);
        dirs[i].x -= origin.x;
        dirs[i].y -= origin.y;
        dirs[i].z -= origin.z;
    }
}

void RndCam::SetViewProj(const Hmx::Matrix4 &mtx) {
    Invert(mViewProjMatrix = mtx, mInvViewProjMatrix);
    Transpose(mInvViewProjMatrix, mInvViewProjMatrix);
}

void RndCam::SetTargetTex(RndTex *tex) {
    if (sCurrent == this) {
        if (mTargetTex) {
            mTargetTex->FinishDrawTarget();
        }
    }
    mTargetTex = tex;
    UpdateLocal();
}

void RndCam::Init() {
    REGISTER_OBJ_FACTORY(RndCam);
    if (SystemConfig()) {
        DataArray *cfg = SystemConfig("rnd");
        cfg->FindData("cam_default_near_plane", sDefaultNearPlane, true);
        cfg->FindData("cam_max_far_near_ratio", sMaxFarNearPlaneRatio, true);
    }
    DataRegisterFunc("cam_get_default_near_plane", OnGetDefaultNearPlane);
    DataRegisterFunc("cam_get_max_far_near_ratio", OnGetMaxFarNearPlaneRatio);
}

void RndCam::SetFrustum(float near, float far, float yfov, float f4) {
    if (far - 0.0001f > sMaxFarNearPlaneRatio * near) {
        MILO_NOTIFY_ONCE(
            "%s: %f/%f plane ratio exceeds %d",
            Name(),
            far,
            near,
            (int)sMaxFarNearPlaneRatio
        );
        if (far == mFarPlane) {
            near = far / sMaxFarNearPlaneRatio;
        } else {
            far = sMaxFarNearPlaneRatio * near;
        }
    }
    mNearPlane = near;
    mFarPlane = far;
    mYFov = yfov;
    mAspectRatio = f4;
    UpdateLocal();
}

float RndCam::WorldToScreen(const Vector3 &w, Vector2 &s) const {
    Vector3 projectedVec;
    Multiply(w, mWorldProjectXfm, projectedVec);
    if (projectedVec.z) {
        float scale = 1.0f / projectedVec.z;
        s.x = projectedVec.x * scale;
        s.y = projectedVec.y * scale;
    } else {
        s.x = projectedVec.x;
        s.y = projectedVec.y;
    }
    s.x = (s.x + 1.0f) / 2.0f;
    s.y = (s.y + 1.0f) / 2.0f;
    s.Set(s.x * mScreenRect.w + mScreenRect.x, s.y * mScreenRect.h + mScreenRect.y);
    return projectedVec.z;
}

// Converts screen coordinates to world space at a given depth
void RndCam::ScreenToWorld(const Vector2 &v2, float f, Vector3 &vout) const {
    // Normalize screen coords [0,1] to NDC [-1,1], scale by depth
    float x = (((v2.x - mScreenRect.x) / mScreenRect.w) * 2.0f - 1.0f) * f;
    float y = (((v2.y - mScreenRect.y) / mScreenRect.h) * 2.0f - 1.0f) * f;
    // Assignment order affects codegen - do not reorder
    vout.z = f;
    vout.y = y;
    vout.x = x;
    Multiply(vout, mInvWorldProjectXfm, vout);
}

void RndCam::GetViewProjectXfms(Transform &viewXfm, Hmx::Matrix4 &projMtx) const {
    Multiply(mInvWorldXfm, sFlipYZ, viewXfm);
    projMtx.Zero();
    float nearPlane = mNearPlane;
    float farPlane = mFarPlane;
    float farRatio;
    if (mYFov == 0) {
        projMtx.w.w = 1.0f;
        farRatio = 1.0f / (farPlane - nearPlane);
    } else {
        projMtx.z.w = 1.0f;
        farRatio = farPlane / (farPlane - nearPlane);
    }

    Hmx::Rect hiRect = TheHiResScreen.ScreenRect(this, mScreenRect);

    float cx = mScreenRect.w * 0.5f + mScreenRect.x;
    float cy = mScreenRect.h * 0.5f + mScreenRect.y;

    float left = Max(hiRect.x, 0.0f);
    float bottom = Max(hiRect.y, 0.0f);
    float right = Min(hiRect.x + hiRect.w, 1.0f);
    float top = Min(hiRect.y + hiRect.h, 1.0f);

    float l = (left - cx) * 2.0f;
    float b = (bottom - cy) * 2.0f;
    float r = (right - cx) * 2.0f;
    float t = (top - cy) * 2.0f;

    float width = r - l;
    float height = t - b;

    projMtx.z.z = farRatio;
    projMtx.x.x = (mScreenRect.w * mLocalProjectXfm.m.x.x * 2.0f) / width;
    projMtx.y.y = (-(mScreenRect.h * mLocalProjectXfm.v.x) * 2.0f) / height;
    projMtx.z.y = (t + b) / height;
    projMtx.z.x = -((r + l) / width);
    projMtx.w.z = -(mNearPlane * farRatio);
}

void RndCam::GetDepthRangeValues(Vector4 &v) const {
    float near = mZRange.x;
    float zratio = 1.0f / (mZRange.y - mZRange.x);
    v.Set(mNearPlane, mFarPlane, zratio, zratio * near);
}

void RndCam::GetInfiniteViewProj(Hmx::Matrix4 &m4) const {
    Transform tfa0;
    Hmx::Matrix4 me0;
    GetViewProjectXfms(tfa0, me0);
    me0.z.z = 1;
    me0.w.z = -mNearPlane;
    m4 = tfa0 * me0;
}

DataNode RndCam::OnGetDefaultNearPlane(DataArray *) { return sDefaultNearPlane; }
DataNode RndCam::OnGetMaxFarNearPlaneRatio(DataArray *) { return sMaxFarNearPlaneRatio; }

DataNode RndCam::OnSetFrustum(const DataArray *da) {
    float nearPlane, farPlane, yFov, temp;
    static Symbol near_plane("near_plane");
    static Symbol far_plane("far_plane");
    static Symbol y_fov("y_fov");
    if (!da->FindData(near_plane, nearPlane, false))
        nearPlane = mNearPlane;
    if (!da->FindData(far_plane, farPlane, false))
        farPlane = mFarPlane;
    if (da->FindData(y_fov, yFov, false))
        temp = yFov * DEG2RAD;
    else
        temp = mYFov;
    yFov = temp;
    SetFrustum(nearPlane, farPlane, yFov, 1.0f);
    return 0;
}

DataNode RndCam::OnSetZRange(const DataArray *da) {
    SetZRange(da->Float(2), da->Float(3));
    return 0;
}

DataNode RndCam::OnSetScreenRect(const DataArray *da) {
    Hmx::Rect r(da->Float(2), da->Float(3), da->Float(4), da->Float(5));
    SetScreenRect(r);
    return 0;
}

DataNode RndCam::OnFarPlane(const DataArray *) { return mFarPlane; }

DataNode RndCam::OnWorldToScreen(const DataArray *a) {
    Vector3 w(a->Float(2), a->Float(3), a->Float(4));
    Vector2 s;
    float ret = WorldToScreen(w, s);
    *a->Var(5) = s.x;
    *a->Var(6) = s.y;
    return ret;
}

DataNode RndCam::OnScreenToWorld(const DataArray *a) {
    Vector2 v2(a->Float(2), a->Float(3));
    Vector3 vout;
    ScreenToWorld(v2, a->Float(4), vout);
    *a->Var(5) = vout.x;
    *a->Var(6) = vout.y;
    *a->Var(7) = vout.z;
    return 0;
}
