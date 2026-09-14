#include "rnddx9\Cam.h"
#include "math\Mtx.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Stats_NG.h"
#include "rndobj\Tex.h"
#include "rnddx9\Rnd.h"
#include "xdk\d3d9i\d3d9.h"

Vector3 Hmx::Matrix4::Col3(int col) const {
    return Vector3(x[col], y[col], z[col]);
}

DxCam::DxCam() {}

void DxCam::Select() {
    TheNgStats->mCams++;
    RndCam::Select();
    if (mTargetTex != nullptr) {
        mTargetTex->MakeDrawTarget();
    } else {
        TheDxRnd.MakeDrawTarget();
    }
    Transform view;
    Hmx::Matrix4 proj;
    GetViewProjectXfms(view, proj);
    SetViewport();
    auto _tmp1 = GetGfxMode();
    if (mTargetTex != nullptr) {
        RndTex::Type type = mTargetTex->GetType();
        bool isShadowMap = false;
        float depth = 1.0f;
        if (type == RndTex::kShadowMap) {
            isShadowMap = true;
        } else {
            depth = 0.0f;
        }
        UINT clearColor = 0;
        UINT clearFlags = 0;
        bool setClear = (type & RndTex::kRendered) && !(type & 0x20);
        if (setClear) {
            clearFlags = 0x30;
        }
        if (!isShadowMap) {
            clearFlags |= 0xf;
        }
        if (type == RndTex::kDepthVolumeMap) {
            clearColor = 0xFF000000;
        }
        auto _tmp0 = TheDxRnd.Device();
        D3DDevice_Clear(
            _tmp0, 0, nullptr, clearFlags, clearColor, depth, 0, 0
        );
    }
    if (_tmp1 == kNewGfx) {
        Hmx::Matrix4 viewProj = Hmx::operator*(view, proj);
        SetViewProj(viewProj);
        Transform invView = GetInvViewXfm();
        TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, mViewProjMatrix);
        Hmx::Matrix4 invViewMtx(invView);
        TheShaderMgr.SetVConstant((VShaderConstant)0x10, invViewMtx);
        Hmx::Rect tmp = TheHiResScreen.ScreenRect();
        Hmx::Rect rect;
        rect.x = tmp.x;
        rect.y = tmp.y;
        rect.w = tmp.w;
        rect.h = tmp.h;
        TheShaderMgr.SetVConstant((VShaderConstant)0x46, (const Vector4 &)rect);
        Hmx::Rect tmp2 = TheHiResScreen.ScreenRect();
        Hmx::Rect rect2;
        rect2.x = tmp2.x;
        rect2.y = tmp2.y;
        rect2.w = tmp2.w;
        rect2.h = tmp2.h;
        TheShaderMgr.SetPConstant((PShaderConstant)0x46, (const Vector4 &)rect2);
    }
}

void DxCam::SetViewport() {
    int width, height;
    if (mTargetTex != nullptr) {
        width = mTargetTex->Width();
        height = mTargetTex->Height();
    } else {
        width = TheDxRnd.Width();
        height = TheDxRnd.Height();
    }
    Hmx::Rect r;
    // Both arms leave the far corner in x2/y2 and the near corner in r.x/r.y;
    // the conversion to a WIDTH and HEIGHT happens once, after the join.  That
    // applies to the tiled path too: CurrentTileRect fills r with a min/max
    // pair, and the image reloads 0x68/0x6c straight after the call and runs
    // them through the same two fsubs.
    float x2, y2;
    if (TheHiResScreen.IsActive()) {
        Hmx::Rect tileRect;
        TheHiResScreen.CurrentTileRect(mScreenRect, r, tileRect);
        // Loaded high word first: the image reads 0x6c before 0x68.
        y2 = r.h;
        x2 = r.w;
    } else {
        float x = mScreenRect.x;
        float y = mScreenRect.y;
        x2 = mScreenRect.w + x;
        y2 = mScreenRect.h + y;
        // The clamps run entirely in registers -- r.x and r.y are stored only
        // once, after the Min, not between the Max and the Min.
        x = Max(0.0f, x);
        y = Max(0.0f, y);
        x2 = Max(0.0f, x2);
        y2 = Max(0.0f, y2);
        // Min(value, 1.0f), not Min(1.0f, value): the image forms `value - 1.0`
        // and selects the CONSTANT on the non-negative side, which is the
        // argument order reversed from the Max clamps above.
        r.x = Min(x, 1.0f);
        r.y = Min(y, 1.0f);
        x2 = Min(x2, 1.0f);
        y2 = Min(y2, 1.0f);
    }
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    MILO_ASSERT((r.x >= 0.f) && (r.x <= 1.f), 0x43);
    MILO_ASSERT((r.y >= 0.f) && (r.y <= 1.f), 0x44);
    MILO_ASSERT((r.w >= 0.f) && (r.w <= 1.f), 0x45);
    MILO_ASSERT((r.h >= 0.f) && (r.h <= 1.f), 0x46);
    NgRnd::Viewport vp;
    vp.X = (unsigned int)((float)width * r.x);
    vp.Y = (unsigned int)((float)height * r.y);
    vp.Width = (unsigned int)((float)width * r.w);
    vp.Height = (unsigned int)((float)height * r.h);
    vp.MinZ = mZRange.x;
    vp.MaxZ = mZRange.y;
    TheNgRnd.SetViewport(vp);
}

unsigned int DxCam::ProjectZ(float z) {
    float f = ((z - mNearPlane) / z)
        * (mFarPlane / (mFarPlane - mNearPlane))
        * (mZRange.y - mZRange.x) + mZRange.x;
    if (TheDxRnd.ReverseZ()) {
        f = 1.0f - f;
    }
    return (unsigned int)(f * 16777215.0f);
}
