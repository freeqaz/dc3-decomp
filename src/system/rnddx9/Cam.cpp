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

// Inlined at both Select() call sites; the parameter form is what makes MSVC
// read the ScreenRect() temp through the returned sret pointer (see Select).
static inline Vector4 RectToVector4(const Hmx::Rect &r) {
    return Vector4(r.x, r.y, r.w, r.h);
}

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
        // The image RELOADS mTargetTex->mType here (lwz r11, 0x54(r11) at
        // 8261EAF4) rather than reusing the value it kept in r9 for the
        // kShadowMap / kDepthVolumeMap compares -- the mask expression names
        // GetType() again, and MSVC CSEs the two loads inside it into one.
        bool setClear =
            (mTargetTex->GetType() & RndTex::kRendered) && !(mTargetTex->GetType() & 0x20);
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
    if (GetGfxMode() == kNewGfx) {
        SetViewProj(Hmx::operator*(view, proj));
        Transform invView = GetInvViewXfm();
        TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, mViewProjMatrix);
        // w7-bs (2026-09-15) 84.130 -> 98.8 canonical (82.971 -> 98.0 fuzzy),
        // 138 rows: 115 equal / 21 diff_arg / 1 replace / 1 delete.  Two
        // levers, both refuted-as-impossible by earlier notes and both real:
        //
        //  1. `mgr`, a reference bound for THIS call only.  The image holds
        //     TheShaderMgr in r31 and its vtable in r29 across the Matrix4
        //     ctor (8261EBB0/EBB4) and never reloads it; every other call
        //     site in the block reloads the global.  A block-wide reference
        //     moves those four sites the wrong way (w7-bo); one scoped to the
        //     call gives exactly the image's __savegprlr_29 prologue and the
        //     `mr r3, r31` after the ctor.  12 rows (prologue/epilogue + 4).
        //
        //  2. RectToVector4() above.  The image reads the ScreenRect() result
        //     through the POINTER the sret call returns (`mr r11, r3` at
        //     8261EBE4, lfs 0xc/0x8/0x4/0x0 descending) and builds the Vector4
        //     in a temp it passes straight to Set{V,P}Constant.  That is an
        //     inlined function whose `const Hmx::Rect &` parameter is the call
        //     result: a reference bound to the temp in THIS function reads
        //     the slot, a parameter reads r3.  The two Vector4 temps take
        //     0x60 / 0x50 and the shared sret temp 0x70, as in the image --
        //     the SLOT TRIPLE w7-bo filed as MSVC's own order was this.
        //     w7-ap's three refuted spellings all named a local; none passed
        //     the call result to a parameter.  MSVC also emits an out-of-line
        //     COMDAT copy of the helper (36 B, unreferenced -- /OPT:REF drops
        //     it; 15 other TUs already carry `static inline` helpers).
        //
        //  Residual, 23 rows, no lever found:
        //   - the image RELOADS mTargetTex->mType for the setClear mask
        //     (`lwz r11, 0x54(r11)` at 8261EAF4) while keeping the first load
        //     in r9 for both == compares; we CSE all three reads into one.
        //     Spelling the mask through TargetTex(), or `type` through a
        //     local RndTex*, is inert.  1 replace + 1 delete + the r8/r9/r10
        //     relabelling that follows (16 rows).
        //   - TheShaderMgr@h in r30 (image) vs r29 (ours), with the vtable in
        //     the other; both are the third callee-saved GPR the `mgr` lever
        //     introduced.  7 rows.
        {
            RndShaderMgr &mgr = TheShaderMgr;
            mgr.SetVConstant((VShaderConstant)0x10, Hmx::Matrix4(invView));
        }
        TheShaderMgr.SetVConstant(
            (VShaderConstant)0x46, RectToVector4(TheHiResScreen.ScreenRect())
        );
        TheShaderMgr.SetPConstant(
            (PShaderConstant)0x46, RectToVector4(TheHiResScreen.ScreenRect())
        );
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
