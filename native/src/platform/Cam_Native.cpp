// DC3 Native Port — RndCam::UpdateLocal() implementation
// Overrides the weak stub in engine_stubs_generated.cpp.
// Based on RB3 reference; builds local frustum and projection transforms
// from near/far/fov/aspect so that frustum culling works correctly.

#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"
#include <cmath>

extern Rnd& TheRnd;

void RndCam::UpdateLocal() {
    float ratio = (mScreenRect.h / mScreenRect.w) * mAspectRatio;
    if (mTargetTex) {
        ratio *= (float)mTargetTex->Height() / (float)mTargetTex->Width();
    } else {
        ratio *= TheRnd.YRatio();
    }

    // Build local frustum (6 planes in camera-local space)
    mLocalFrustum.Set(mNearPlane, mFarPlane, mYFov, ratio);

    // Build local projection transform
    mLocalProjectXfm.m.Zero();
    mLocalProjectXfm.v.Zero();
    mInvLocalProjectXfm.m.Zero();
    mInvLocalProjectXfm.v.Zero();

    if (!mYFov) {
        // Orthographic
        mLocalProjectXfm.m.x.x = 1;
        mInvLocalProjectXfm.m.x.x = 1;
        mInvLocalProjectXfm.m.y.z = -ratio;
        mLocalProjectXfm.m.z.y = -1.0f / ratio;
    } else {
        // Perspective
        float thetan = tanf(mYFov * 0.5f);
        mLocalProjectXfm.m.y.z = 1;
        mInvLocalProjectXfm.m.z.y = 1;
        mLocalProjectXfm.m.x.x = ratio / thetan;
        mLocalProjectXfm.m.z.y = -1.0f / thetan;
        mInvLocalProjectXfm.m.x.x = thetan / ratio;
        mInvLocalProjectXfm.m.y.z = -thetan;
    }

    // Update world-space transforms from local + world xfm
    UpdatedWorldXfm();
    mAspect = TheRnd.GetAspect();
}

void RndCam::GetViewProjectXfms(Transform& view, Hmx::Matrix4& proj) const {
    view = mInvWorldXfm;
    // Build projection matrix from local project transform
    // The Matrix4 is the 3x3 rotation + homogeneous w
    proj = Hmx::Matrix4::ID();
    proj.x.x = mLocalProjectXfm.m.x.x;
    proj.x.y = mLocalProjectXfm.m.x.y;
    proj.x.z = mLocalProjectXfm.m.x.z;
    proj.y.x = mLocalProjectXfm.m.y.x;
    proj.y.y = mLocalProjectXfm.m.y.y;
    proj.y.z = mLocalProjectXfm.m.y.z;
    proj.z.x = mLocalProjectXfm.m.z.x;
    proj.z.y = mLocalProjectXfm.m.z.y;
    proj.z.z = mLocalProjectXfm.m.z.z;
    proj.w.x = mLocalProjectXfm.v.x;
    proj.w.y = mLocalProjectXfm.v.y;
    proj.w.z = mLocalProjectXfm.v.z;
}
