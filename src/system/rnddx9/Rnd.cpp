#include "rnddx9\Rnd.h"
#include "Tex.h"
#include "math\Mtx.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj\Bitmap.h"
#include "rndobj/Cam.h"
#include "rndobj\Mat.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\Shader.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Tex.h"
#include "rndobj\Utl.h"
#include "xdk\D3D9.h"
#include "xdk\d3d9i\d3d9.h"
#include "xdk\d3d9i\d3d9caps.h"
#include "xdk\d3d9i\d3d9types.h"

DxRnd TheDxRnd;

void Multiply(const Vector4 &a, const Hmx::Matrix4 &m, Vector4 &out) {
    float x = m.x.x * a.x + m.y.x * a.y + m.z.x * a.z + m.w.x * a.w;
    float w = m.x.w * a.x + m.y.w * a.y + m.z.w * a.z + m.w.w * a.w;
    float z = m.x.z * a.x + m.y.z * a.y + m.z.z * a.z + m.w.z * a.w;
    float y = m.x.y * a.x + m.y.y * a.y + m.z.y * a.z + m.w.y * a.w;
    out.x = x;
    out.w = w;
    out.z = z;
    out.y = y;
}

int D3DFORMAT_BitsPerPixel(D3DFORMAT fmt) {
    switch (fmt) {
    case D3DFMT_LIN_DXT1:
    case D3DFMT_DXT1:
        return 4;
    case D3DFMT_LIN_DXT3:
    case D3DFMT_LIN_DXT5:
    case D3DFMT_DXT3:
    case D3DFMT_DXT5:
    case D3DFMT_L8:
        return 8;
    case D3DFMT_LIN_A1R5G5B5:
    case D3DFMT_LE_LIN_UYVY:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_D16:
    case D3DFMT_LIN_D16:
    case D3DFMT_LIN_R5G6B5:
    case D3DFMT_R5G6B5:
    case D3DFMT_LIN_X1R5G5B5:
        return 16;
    case D3DFMT_LIN_A8R8G8B8:
    case D3DFMT_D24FS8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
    case (D3DFORMAT)0x28287eb2:
    case D3DFMT_LIN_D24S8:
    case D3DFMT_D24S8:
        return 32;
    default:
        MILO_FAIL("Currently unsupported D3DFORMAT: %d", fmt);
        return 0;
    }
}

BEGIN_HANDLERS(DxRnd)
    HANDLE_ACTION(suspend, Suspend())
    HANDLE_SUPERCLASS(Rnd)
END_HANDLERS

void DxRnd::Clear(unsigned int ui, const Hmx::Color &c) {
    float f1;
    if (mReverseZ) {
        f1 = 0;
    } else {
        f1 = 1;
    }
    int mask = 0;
    if (ui & 1) {
        mask = 0xF;
    }
    if (ui & 2) {
        mask |= 0x30;
    }
    D3DDevice_Clear(mD3DDevice, 0, nullptr, mask, MakeColor(c), f1, 0, 0);
}

void DxRnd::DrawRect(
    const Hmx::Rect &rect,
    const Hmx::Color &colorRef,
    RndMat *mat,
    const Hmx::Color *colorPtr1,
    const Hmx::Color *colorPtr2
) {
    DrawRect(rect, mat, kDrawRectShader, colorRef, colorPtr1, colorPtr2);
}

void DxRnd::DrawLine(const Vector3 &v1, const Vector3 &v2, const Hmx::Color &c, bool b4) {
    // Vertex buffer layout: 2 vertices (xyz + color each) + Transform matrix
    // Total: 8 floats + 48 bytes = 96 bytes (24 floats)
    float vertices[24];
    unsigned long colorVal = MakeColor(c);

    // First vertex
    vertices[0] = v1.x;
    vertices[1] = v1.y;
    vertices[2] = v1.z;
    *(unsigned long *)&vertices[3] = colorVal;

    // Second vertex
    vertices[4] = v2.x;
    vertices[5] = v2.y;
    vertices[6] = v2.z;
    *(unsigned long *)&vertices[7] = colorVal;

    // Initialize identity transform in-place (vertices[8..19])
    Transform &xfm = reinterpret_cast<Transform &>(vertices[8]);
    xfm.Reset();

    TheShaderMgr.SetTransform(xfm);
    RndShader::SelectConfig(nullptr, b4 ? kLineShader : kLineNozShader, false);
    D3DDevice_SetFVF(mD3DDevice, 0x42);
    D3DDevice_DrawVerticesUP(mD3DDevice, D3DPT_LINELIST, 2, vertices, 0x10);
}

void DxRnd::MakeDrawTarget() {
    if (mWorldEnded) {
        D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mOffscreenRT);
        D3DDevice_SetDepthStencilSurface(mD3DDevice, mOffscreenDepth);
    } else {
        D3DDevice_SetRenderTarget_External(mD3DDevice, 0, mBackBuffer);
        D3DDevice_SetDepthStencilSurface(mD3DDevice, mWorldDepth);
    }
    NgMat::SetCurrent(nullptr);
}

void DxRnd::SetViewport(const Viewport &v) {
    if (GetGfxMode() == kNewGfx) {
        NgRnd::SetViewport(v);
    }
    D3DVIEWPORT9 dxViewport;
    dxViewport.X = v.X;
    dxViewport.Y = v.Y;
    dxViewport.Width = v.Width;
    dxViewport.Height = v.Height;
    if (mReverseZ) {
        dxViewport.MinZ = 1.0f - v.MinZ;
        dxViewport.MaxZ = 1.0f - v.MaxZ;
    } else {
        dxViewport.MinZ = v.MinZ;
        dxViewport.MaxZ = v.MaxZ;
    }
    D3DDevice_SetViewport(mD3DDevice, &dxViewport);
}

bool DxRnd::Offscreen() const {
    D3DSurface *back = BackBuffer();
    D3DSurface *target = D3DDevice_GetRenderTarget(mD3DDevice, 0);
    bool ret = target != back;
    if (target) {
        D3DResource_Release(target);
    }
    if (back) {
        D3DResource_Release(back);
    }
    return ret;
}

void DxRnd::DrawLargeQuad(
    const LargeQuadRenderData &data, const Transform &tf, RndMat *mat, ShaderType s
) {
    RndMat *next = mat ? dynamic_cast<RndMat *>(mat->NextPass()) : nullptr;
    RndMat *it = mat;
    do {
        RndShader::SelectConfig(it, s, false);
        D3DDevice_SetIndices(mD3DDevice, data.mIndexBuffer);
        D3DDevice_SetStreamSource(mD3DDevice, 0, data.mVertexBuffer, 0, 20, 1);
        D3DDevice_SetFVF(mD3DDevice, 0x102);
        TheShaderMgr.SetVConstant(kVS_WorldTransform, Hmx::Matrix4(tf));
        DxTex *tex = static_cast<DxTex *>(mat->GetDiffuseTex());
        D3DDevice_SetTexture(mD3DDevice, 0x10, tex->Tex(), 0x8000);
        D3DDevice_SetTexture(mD3DDevice, 0, tex->Tex(), 0x80000000);
        D3DDevice_DrawIndexedVertices(
            mD3DDevice, D3DPT_QUADLIST, 0, 0, (data.mHeight - 1) * (data.mWidth - 1) * 4
        );
        it = next;
        next = next ? dynamic_cast<RndMat *>(next->NextPass()) : nullptr;
    } while (it != nullptr);
    D3DDevice_SetIndices(mD3DDevice, nullptr);
    D3DDevice_SetStreamSource(mD3DDevice, 0, nullptr, 0, 0, 1);
    D3DDevice_SetTexture(mD3DDevice, 0x10, nullptr, 0x8000);
}

void DxRnd::CreateLargeQuad(int width, int height, LargeQuadRenderData &data) {
    int w1 = width - 1;
    int h1 = height - 1;
    BeginMemTrackObjectName("DxRnd::CreateLargeQuad");
    D3DIndexBuffer *ib =
        D3DDevice_CreateIndexBuffer(h1 * w1 << 4, 0, D3DFMT_INDEX32, D3DPOOL_DEFAULT);
    DX_ASSERT(ib, 0x2E3);
    EndMemTrackObjectName();
    int *indices = (int *)D3DIndexBuffer_Lock(ib, 0, 0, 0);
    for (unsigned int y = 0; y < (unsigned int)h1; y++) {
        for (unsigned int x = 0; x < (unsigned int)w1; x++) {
            int *quad = indices + (y * w1 + x) * 4;
            quad[0] = y * width + x;
            quad[1] = y * width + x + 1;
            quad[2] = (y + 1) * width + x + 1;
            quad[3] = (y + 1) * width + x;
        }
    }
    D3DIndexBuffer_Unlock(ib);

    BeginMemTrackObjectName("DxRnd::CreateLargeQuad");
    int vbSize = width * height * 0x14;
    D3DVertexBuffer *vb = D3DDevice_CreateVertexBuffer(vbSize, 0, D3DPOOL_DEFAULT);
    DX_ASSERT(vb, 0x2FB);
    EndMemTrackObjectName();
    char *verts = (char *)D3DVertexBuffer_Lock(vb, 0, vbSize, 0);
    float invW = 1.0f / (float)width;
    float invH = 1.0f / (float)height;
    for (unsigned int y = 0; y < (unsigned int)height; y++) {
        for (unsigned int x = 0; x < (unsigned int)width; x++) {
            float *v = (float *)(verts + (y * width + x) * 0x14);
            v[1] = (float)y;
            v[2] = 0.0f;
            v[4] = (float)y * invH;
            v[0] = (float)x;
            v[3] = (float)x * invW;
        }
    }
    D3DVertexBuffer_Unlock(vb);

    data.mIndexBuffer = ib;
    data.mVertexBuffer = vb;
    data.mWidth = width;
    data.mHeight = height;
}

void DxRnd::SetVertShaderTex(RndTex *tex, int sampler) {
    D3DBaseTexture *texPtr;
    if (tex) {
        texPtr = static_cast<DxTex *>(tex)->Tex();
    } else {
        texPtr = nullptr;
    }
    int slot = sampler + 0x10;
    u32 shift = slot + 0x20;
    D3DDevice_SetTexture(
        mD3DDevice,
        slot,
        texPtr,
        0x8000000000000000 >> shift
    );
}

void DxRnd::PreDeviceReset() {
    if (mOcclusionQueryMgr) {
        mOcclusionQueryMgr->ReleaseQueries();
    }
    FOREACH (it, mDxObjects) {
        (*it)->PreDeviceReset();
    }
    ReleaseAutoRelease();
}

void DxRnd::PostDeviceReset() {
    FOREACH (it, mDxObjects) {
        (*it)->PostDeviceReset();
    }
    MakeDrawTarget();
    InitRenderState();
}
void DxRnd::PushClipPlanesInternal(ObjPtrVec<RndTransformable> &planes) {
    int enableMask = 0;
    for (int i = 0; i < unk408; i++) {
        enableMask |= 1 << i;
    }
    for (int i = 0; i < planes.size(); i++) {
        if (i * 0x14 >= 0x78) {
            break;
        }
        RndTransformable *trans = planes[i];
        if (trans) {
            const Transform &xfm = trans->WorldXfm();
            float nx = xfm.m.z.x * -1.0f;
            float ny = xfm.m.z.y * -1.0f;
            float nz = xfm.m.z.z * -1.0f;
            float d = -(nx * xfm.v.x + (nz * xfm.v.z + ny * xfm.v.y));
            Vector4 planeClip(nx, ny, nz, d);
            Vector4 planeObj(nx, ny, nz, d);
            Multiply(planeObj, RndCam::Current()->GetInvViewProjMatrix(), planeObj);
            planeClip = planeObj;
            D3DDevice_SetClipPlane(mD3DDevice, unk408, &planeClip.x);
            enableMask |= 1 << unk408;
            unk408++;
        }
    }
    D3DDevice_SetRenderState_ClipPlaneEnable(TheDxRnd.mD3DDevice, enableMask);
}

void DxRnd::PopClipPlanesInternal(ObjPtrVec<RndTransformable> &planes) {
    for (int i = 0; i < planes.size(); i++) {
        if (i * 0x14 >= 0x78) {
            break;
        }
        if (planes[i]) {
            unk408--;
        }
    }
    int enableMask = 0;
    for (int i = 0; i < unk408; i++) {
        enableMask |= 1 << i;
    }
    D3DDevice_SetRenderState_ClipPlaneEnable(TheDxRnd.mD3DDevice, enableMask);
}

// 77.1%. Two things were investigated here on 2026-08-19; read this before
// touching the MILO_ASSERTs, because one of them is a REFUTED lead and the
// other is a real diagnosis that costs more than it buys.
//
// REFUTED -- "our assert expression text is wrong, find the 34-char one".
//   The lead came from the target calling
//     ??$MakeString@$$BY07$$CBDH$$BY0CD@$$CBD@@...     (expr array = 0x23 = 35)
//   where we call
//     ??$MakeString@$$BY07$$CBDH$$BY0BG@$$CBD@@...     (expr array = 0x16 = 22)
//   That is pure ICF naming noise. Both symbols resolve to 824D1870 in
//   build/373307D9/icf_aliases.map -- every MakeString<char[N], int, char[M]>
//   in the binary folds to one body, and the linker's map happens to name it
//   after a 35-char instantiation. The same diff shows the target "calling"
//   MakeString<CamShotFrame::BlendEaseMode> where we call MakeString<int>
//   (both fold to 82610090), which nobody would read as a real difference.
//   The decisive evidence is the string literal itself: target and base both
//   reference ??_C@_0BG@PPIAGPFI@fmt?5?$CB?$DN?5D3DFMT_UNKNOWN?$AA@ -- _0BG =
//   0x16 = 22 bytes = "fmt != D3DFMT_UNKNOWN" + NUL. The text is CORRECT.
//
// REAL, but not landable as-is -- the missing `cmpwi r31,0xff; bne` guard.
//   The target runs the second assert's fail block unconditionally, so its
//   condition folds at compile time, and to FALSE (the block is emitted, not
//   elided). That cannot happen with `fmt` bound to the masked bitmap order:
//   in the dxt default arm MSVC only knows fmt is none of 0/8/0x10/0x18/0x20,
//   and in the bpp arm it knows fmt == 0 -- neither folds against 0xff. It
//   does happen if `fmt` is the RESULT, still holding its initialiser on both
//   default paths. Rewriting it that way (order/bpp/fmt, `D3DFORMAT fmt =
//   D3DFMT_UNKNOWN`) reproduces the target exactly where it counts: guard
//   gone, prologue back to std r30/r31 + stwu -0x70 with no __savegprlr_29,
//   inline epilogue, and it frees the third callee-saved GPR that only existed
//   to keep the order value live across Debug::Fail.
//   It still scored 3.4% (run_objdiff, worktree plane), because MSVC then
//   cross-jumps the two default arms LATER than the target does: the target
//   shares everything from `bl MakeString` onward, we duplicate ten
//   instructions and only merge inside the assert block. The blocker is one
//   stack slot -- the target parks the line-number temp at 0x54 in BOTH arms,
//   we park it at 0x50 in the dxt arm and 0x54 in the bpp arm, so the tails
//   are not identical. Base grows 340 -> 380 bytes. Reverted.
//   The `return`-per-case shape (as in ../og-dc3-decomp) is worse again, 0.2%:
//   it dissolves the r30 result register the target keeps.
//   Whoever picks this up needs the slot-colouring lever, not another guess at
//   the assert text.
D3DFORMAT DxRnd::D3DFormatForBitmap(const RndBitmap &bitmap) {
    int fmt = bitmap.Order() & 0x38;
    int bpp = bitmap.Bpp();
    D3DFORMAT result = (D3DFORMAT)-1;
    if (fmt != 0) {
        switch (fmt) {
        case 8:
            result = D3DFMT_DXT1;
            break;
        case 0x10:
            result = D3DFMT_DXT3;
            break;
        case 0x18:
            result = D3DFMT_DXT5;
            break;
        case 0x20:
            result = D3DFMT_DXN;
            break;
        default:
            MILO_FAIL("Invalid dxt format: %d", fmt);
            MILO_ASSERT(fmt != D3DFMT_UNKNOWN, 999);
            break;
        }
    } else {
        switch (bpp) {
        case 4:
        case 8:
            result = D3DFMT_A8R8G8B8;
            break;
        case 0x10:
            result = D3DFMT_A1R5G5B5;
            break;
        case 0x18:
            result = D3DFMT_X8R8G8B8;
            break;
        case 0x20:
            result = D3DFMT_A8R8G8B8;
            break;
        default:
            MILO_FAIL("Invalid bpp: %d", bpp);
            MILO_ASSERT(fmt != D3DFMT_UNKNOWN, 999);
            break;
        }
    }
    return result;
}

int DxRnd::BitmapOrderForD3DFormat(D3DFORMAT fmt) {
    switch (fmt) {
    case D3DFMT_DXT1:
    case D3DFMT_LIN_DXT1:
        return 8;
    case D3DFMT_DXT3:
    case D3DFMT_LIN_DXT3:
        return 0x10;
    case D3DFMT_DXT5:
    case D3DFMT_LIN_DXT5:
        return 0x18;
    case D3DFMT_DXN:
    case D3DFMT_LIN_DXN:
        return 0x20;
    default:
        return 0;
    }
}

namespace {
    struct DepthRectVert {
        float sx, sy, sz; // screen-space corner (0x00)
        float pad0, pad1, pad2; // 0x0c
        float nx, ny, nz; // 0x18 (normal)
        float px, py, pz; // 0x24 (corner pos)
    };
    DepthRectVert sDepthRectVerts[4] = {
        {-1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {-1.0f, -1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1.0f, 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {1.0f, -1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    const D3DVERTEXELEMENT9 sDepthRectDecl[] = {
        {0, 0x00, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 0x0C, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 0x18, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        {0, 0x24, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 2},
        D3DDECL_END()
    };
}

void DxRnd::DrawRectDepth(
    const Vector3 &normal,
    const Vector3 (&corners)[4],
    const Vector4 &v4,
    RndMat *mat,
    ShaderType shader
) {
    TheShaderMgr.SetPConstant((PShaderConstant)0x59, v4);
    for (int i = 0; i < 4; i++) {
        sDepthRectVerts[i].nx = normal.x;
        sDepthRectVerts[i].ny = normal.y;
        sDepthRectVerts[i].nz = normal.z;
        sDepthRectVerts[i].px = corners[i].x;
        sDepthRectVerts[i].py = corners[i].y;
        sDepthRectVerts[i].pz = corners[i].z;
    }
    RndShader::SelectConfig(mat, shader, false);
    static D3DVertexDeclaration *sDecl;
    if (!sDecl) {
        sDecl = D3DDevice_CreateVertexDeclaration(sDepthRectDecl);
        DX_ASSERT(sDecl, 0x2C3);
    }
    D3DDevice_SetRenderState_HalfPixelOffset(TheDxRnd.Device(), 1);
    D3DDevice_SetVertexDeclaration(mD3DDevice, sDecl);
    D3DDevice_DrawVerticesUP(mD3DDevice, D3DPT_TRIANGLESTRIP, 4, sDepthRectVerts, sizeof(DepthRectVert));
    D3DDevice_SetRenderState_HalfPixelOffset(TheDxRnd.Device(), 0);
}

void DxRnd::ResetDevice() {
    PreDeviceReset();
    HRESULT res = D3DDevice_Reset(mD3DDevice, &mPresentParams);
    DX_ASSERT_CODE(res, 0xD6);
    PostDeviceReset();
}

long DxRnd::GetDeviceCaps(D3DCAPS9 *cap) {
    D3DDEVTYPE deviceType = mDeviceType;
    return Direct3D_GetDeviceCaps(0, deviceType, cap);
}

void DxRnd::DrawSafeArea(float percent, bool widescreen, const Hmx::Color &color) {
    if (mShrinkToSafe)
        percent = percent * 1.0526316f;

    float realAspect = (float)mHeight / mWidth;
    float targetAspect;
    if (widescreen) {
        targetAspect = 16.f / 9.f;
    } else {
        targetAspect = 4.f / 3.f;
    }

    float v1y = (1.0f - percent) * 0.5f;
    float v1x = v1y + (1.0f - targetAspect * realAspect) * 0.5f;
    float v2y = 1.0f - v1y;
    float v2x = 1.0f - v1x;

    Vector2 vec1;
    vec1.y = v1y;
    vec1.x = v1x;

    Vector2 vec2;
    vec2.x = v2x;
    vec2.y = v2y;

    UtilDrawRect2D(vec1, vec2, color);
}
