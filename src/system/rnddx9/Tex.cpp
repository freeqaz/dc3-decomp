#include "rnddx9\Tex.h"
#include "Rnd.h"
#include "Tex.h"
#include "os\Debug.h"
#include "rnddx9\Rnd.h"
#include "rnddx9\TexMgr.h"
#include "rndobj\Mat_NG.h"
#include "rndobj\Rnd.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Tex.h"
#include "utl\MemMgr.h"
#include "xdk\d3d9i\d3d9.h"
#include "xdk\d3d9i\d3d9types.h"
#include "xdk\xgraphics\xgraphics.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\Dir.h"
#include "utl\MakeString.h"
#include "utl\TextStream.h"

DataNode DebugPrintAllTextures(DataArray *);

std::vector<DxTex *> gAllTextures;
int gTexDumpCalls;

struct CompressLevel {
    D3DSurface *scratchSurface; // 0x0
    D3DLOCKED_RECT scratchLock; // 0x4
    D3DSURFACE_DESC scratchDesc; // 0xc
    D3DSurface *textureSurface; // 0x2c
    D3DLOCKED_RECT textureLock; // 0x30
    D3DSURFACE_DESC textureDesc; // 0x38
};

struct CompressDesc {
    D3DTexture *texture; // 0x0
    RndTex::AlphaCompress alpha; // 0x4
    int unk8; // 0x8
    D3DFORMAT format; // 0xc
    void *tiledBuffer; // 0x10
    CompressLevel levels[16]; // 0x14
};

// Size of a surface in EDRAM tiles. A tile is 80x16 pixels; the returned value
// is (aligned bytes) / 5120.
extern "C" UINT
XGSurfaceSize(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample) {
    int gpuFormat = Format & 0x3f;
    UINT bytesPerPixel = 4;
    UINT width = Width;
    UINT height = Height;
    if ((int)MultiSample >= 1) {
        height = Height * 2;
    }
    if ((int)MultiSample == 2) {
        width = Width * 2;
    }
    UINT alignedWidth = ((width + 79) / 80) * 80;
    UINT alignedHeight = (height + 15) & ~15;
    if (gpuFormat == 0x15 || gpuFormat == 0x20 || gpuFormat == 0x25) {
        bytesPerPixel = 8;
    }
    return alignedHeight * alignedWidth * bytesPerPixel / 0x1400;
}

DxTex::DxTex()
    : mFormat((D3DFORMAT)-1), mTexture(0), unk84(0), mRenderTarget(0), mDepthRT(0),
      mMovieBufIdx(0), mLockedRect(), unka4(0), unka8(0), unkac(0) {
    gAllTextures.push_back(this);
    for (int i = 0; i < 2; i++) {
        mMovieTextures[i] = 0;
    }
}

DxTex::~DxTex() {
    ResetSurfaces();
    auto it = std::find(gAllTextures.begin(), gAllTextures.end(), this);
    MILO_ASSERT(it != gAllTextures.end(), 0x2D7);
    gAllTextures.erase(it);
}

void DxTex::Compress(AlphaCompress a) {
    void *v = StartCompress(a);
    DoCompress(v);
    FinishCompress(v);
}

void *DxTex::StartCompress(AlphaCompress alpha) {
    MILO_ASSERT(mTexture, 0x15A);
    for (int i = 0; i < 16; i++) {
        TheShaderMgr.SetPConstant((PShaderConstant)i, (RndTex *)nullptr);
    }
    CompressDesc *desc;
    {
        MemTemp tmp;
        desc = (CompressDesc *)MemAlloc(0x594, __FILE__, 0x14B, "CompressDesc");
    }
    desc->alpha = alpha;
    desc->format = ((int)alpha == 2) ? D3DFMT_LIN_DXT5 : D3DFMT_LIN_DXT1;
    desc->unk8 = 0;
    int numLevels = D3DBaseTexture_GetLevelCount(mTexture);
    desc->texture = (D3DTexture *)D3DDevice_CreateTexture(
        mWidth, mHeight, 1, numLevels, 0, desc->format, 0, (D3DRESOURCETYPE)3
    );
    DX_ASSERT(desc->texture, 0x16C);
    MILO_ASSERT(numLevels < 16, 0x16F);
    for (int i = 0; i < numLevels; i++) {
        desc->levels[i].scratchSurface = D3DTexture_GetSurfaceLevel(mTexture, i);
        DX_ASSERT(desc->levels[i].scratchSurface, 0x174);
        D3DSurface_LockRect(desc->levels[i].scratchSurface, &desc->levels[i].scratchLock, nullptr, 0x10);
        D3DSurface_GetDesc(desc->levels[i].scratchSurface, &desc->levels[i].scratchDesc);
        desc->levels[i].textureSurface = D3DTexture_GetSurfaceLevel(desc->texture, i);
        DX_ASSERT(desc->levels[i].textureSurface, 0x179);
        D3DSurface_LockRect(desc->levels[i].textureSurface, &desc->levels[i].textureLock, nullptr, 0);
        D3DSurface_GetDesc(desc->levels[i].textureSurface, &desc->levels[i].textureDesc);
    }
    {
        MemTemp tmp;
        desc->tiledBuffer = MemAlloc(
            desc->levels[0].scratchDesc.Height * (desc->levels[0].scratchDesc.Width * 4),
            __FILE__,
            0x183,
            "compress"
        );
    }
    return desc;
}

void DxTex::DoCompress(void *p) {
    CompressDesc *desc = (CompressDesc *)p;
    int numLevels = D3DBaseTexture_GetLevelCount(mTexture);
    for (int i = 0; i < numLevels; i++) {
        CompressLevel &level = desc->levels[i];
        int rowPitch = level.scratchDesc.Width * 4;
        XGUntileTextureLevel(
            level.scratchDesc.Width,
            level.scratchDesc.Height,
            desc->unk8,
            mFormat & 0x3f,
            1,
            desc->tiledBuffer,
            rowPitch,
            nullptr,
            level.scratchLock.pBits,
            nullptr
        );
        if ((int)desc->alpha == 0) {
            unsigned int *texel = (unsigned int *)desc->tiledBuffer;
            unsigned int *end =
                texel + level.scratchDesc.Width * level.scratchDesc.Height;
            for (; texel < end; texel++) {
                *texel |= 0xff000000;
            }
        }
        XGCompressSurface(
            level.textureLock.pBits,
            level.textureLock.Pitch,
            level.textureDesc.Width,
            level.textureDesc.Height,
            desc->format,
            0,
            desc->tiledBuffer,
            rowPitch,
            (D3DFORMAT)0x18280086,
            0,
            0,
            0.5f
        );
    }
}

void DxTex::FinishCompress(void *p) {
    CompressDesc *desc = (CompressDesc *)p;
    MemFree(desc->tiledBuffer);
    int numLevels = D3DBaseTexture_GetLevelCount(mTexture);
    for (int i = 0; i < numLevels; i++) {
        CompressLevel &level = desc->levels[i];
        D3DSurface_UnlockRect(level.scratchSurface);
        D3DSurface_UnlockRect(level.textureSurface);
        if (level.textureSurface) {
            D3DResource_Release(level.textureSurface);
            level.textureSurface = nullptr;
        }
        if (level.scratchSurface) {
            D3DResource_Release(level.scratchSurface);
            level.scratchSurface = nullptr;
        }
    }
    if (mTexture) {
        D3DResource_Release(mTexture);
        mTexture = nullptr;
    }
    mFormat = desc->format;
    mTexture = desc->texture;
    if (mRenderTarget) {
        D3DResource_Release(mRenderTarget);
        mRenderTarget = nullptr;
    }
    if (mDepthRT) {
        D3DResource_Release(mDepthRT);
        mDepthRT = nullptr;
    }
    mType = kRegular;
    mBpp = ((int)desc->alpha == 2) ? 8 : 4;
    MemFree(desc, __FILE__, 0x14B, "CompressDesc");
}

DataNode DebugPrintAllTextures(DataArray *) {
    TheDebug << MakeString("tex_dump_%d: %s\n", gTexDumpCalls);
    FormatString fmt(
        "ObjName, dir, mTexture, size, width, height, bpp, mips, type, file\n"
    );
    TheDebug << fmt.Str();
    int count = 0;
    for (std::vector<DxTex *>::iterator it = gAllTextures.begin();
         it != gAllTextures.end();
         ++it) {
        DxTex *tex = *it;
        const char *name = tex->Name();
        if (name && *name) {
            const char *dirName = tex->Dir() ? tex->Dir()->Name() : "";
            TheDebug << MakeString(
                "%s, %s, %x, %d, %d, %d, %d, %d, %d, %s\n",
                name,
                dirName,
                tex->Tex(),
                tex->SizeKb() * 1024,
                tex->Width(),
                tex->Height(),
                tex->Bpp(),
                tex->NumMips(),
                tex->GetType(),
                tex->File().c_str()
            );
            count++;
        }
    }
    gTexDumpCalls++;
    return DataNode(count);
}

void DxTex::Init() { DataRegisterFunc("dump_tex", DebugPrintAllTextures); }

void DxTex::LockBitmap(RndBitmap &bm, int flags) {
    if (!mTexture) {
        RndTex::LockBitmap(bm, flags);
        return;
    }
    bool wantRead = (flags & 1) > 0;
    bool wantWrite = (flags & 4) > 0;
    if (!wantRead && !wantWrite) {
        return;
    }
    bool renderTarget = (mType & kRendered) > 0;
    bool frontBuffer = (mType & kFrontBuffer) > 0;
    if ((renderTarget || frontBuffer) && wantRead) {
        if (frontBuffer) {
            unka4 = D3DTexture_GetSurfaceLevel(TheDxRnd.NotFrontBuffer(), 0);
        } else {
            unka4 = GetSurfaceLevel(0);
        }
    } else if ((mType & kBackBuffer) > 0 && wantRead) {
        D3DDevice_Resolve(
            TheDxRnd.Device(), 0, nullptr, mTexture, nullptr, 0, 0, nullptr, 1.0f, 0,
            nullptr
        );
        unka4 = GetSurfaceLevel(0);
    } else if ((mType & kMovie) > 0) {
        unka4 = nullptr;
    } else if ((mType & (kRegular | kScratch)) > 0) {
        unka4 = GetSurfaceLevel(0);
    }
    if (!unka4) {
        return;
    }
    unka8 = flags;
    if (wantRead && !wantWrite) {
        bm.Create(
            mWidth,
            mHeight,
            0,
            D3DFORMAT_BitsPerPixel(mFormat),
            TheDxRnd.BitmapOrderForD3DFormat(mFormat),
            nullptr,
            nullptr,
            nullptr
        );
        D3DSurface_LockRect(unka4, &mLockedRect, nullptr, 0x10);
        XGTEXTURE_DESC desc;
        XGGetTextureDesc((D3DBaseTexture *)unka4, 0, &desc);
        if (desc.Format & 0x100) {
            XGUntileSurface(
                bm.Pixels(),
                bm.DxtRowBytes(),
                nullptr,
                mLockedRect.pBits,
                desc.WidthInBlocks,
                desc.HeightInBlocks,
                nullptr,
                desc.BytesPerBlock
            );
        } else {
            memcpy(bm.Pixels(), mLockedRect.pBits, bm.PixelBytes());
        }
        D3DSurface_UnlockRect(unka4);
        if (unka4) {
            D3DResource_Release(unka4);
            unka4 = nullptr;
        }
    } else if (wantWrite) {
        D3DSurface_LockRect(unka4, &mLockedRect, nullptr, 0);
        bm.Create(
            mWidth,
            mHeight,
            0,
            D3DFORMAT_BitsPerPixel(mFormat),
            TheDxRnd.BitmapOrderForD3DFormat(mFormat),
            nullptr,
            mLockedRect.pBits,
            mLockedRect.pBits
        );
    }
}

void DxTex::UnlockBitmap() {
    if (mTexture) {
        if (unka4) {
            D3DSurface_UnlockRect(unka4);
            if (unka4) {
                D3DResource_Release(unka4);
                unka4 = nullptr;
            }
            if ((unka8 & 0x4) > 0) {
                DX_ASSERT_CODE(D3DXFilterTexture(mTexture, nullptr, -1, -1), 0x618);
            }
        }
        mLockedRect.Pitch = 0;
        mLockedRect.pBits = nullptr;
        unka4 = nullptr;
        unka8 = 0;
    }
}

void DxTex::Select(int x) {
    D3DTexture *tex = mTexture;
    if (!tex) {
        tex = static_cast<DxTex *>(TheRnd.GetNullTexture())->mTexture;
    }
    if (mType & 0x8) {
        if (mType == kFrontBuffer) {
            tex = TheDxRnd.FrontBuffer();
        } else {
            D3DDevice_Resolve(
                TheDxRnd.Device(), 0, nullptr, mTexture, nullptr, 0, 0, nullptr, 1.0f,
                0, nullptr
            );
        }
    }
    D3DDevice_SetTexture(TheDxRnd.Device(), x, tex, (1ULL << 63) >> (unsigned int)(x + 32));
}

void DxTex::FinishDrawTarget() {
    MILO_ASSERT(mType & kRendered, 0x122);
    ResolveMipChain();
    D3DDevice_SetPredication(TheDxRnd.Device(), 0);
    TheDxRnd.SetReverseZ(true);
}

bool DxTex::TexelsLock(void *&p) {
    void *texels = nullptr;
    if (mTexture) {
        UINT baseData;
        XGGetTextureLayout(
            mTexture, &baseData, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
            nullptr, nullptr, 0
        );
        texels = (void *)baseData;
        p = texels;
        return true;
    }
    p = texels;
    return false;
}

void DxTex::MakeDrawTarget() {
    MILO_ASSERT(mType & kRendered, 0xF0);
    if (mTexture) {
        TheDxRnd.Resume();
        D3DDevice_SetPredication(TheDxRnd.Device(), 3);
        D3DDevice_SetRenderTarget_External(TheDxRnd.Device(), 0, mRenderTarget);
        D3DDevice_SetDepthStencilSurface(
            TheDxRnd.Device(), mType == kDepthVolumeMap ? nullptr : mDepthRT
        );
        NgMat::SetCurrent(nullptr);
        TheDxRnd.SetReverseZ(mType != kShadowMap);
    }
}

void DxTex::SetDeviceTex(D3DTexture *tex) {
    mTexture = tex;
    mType = kDeviceTexture;
    if (tex) {
        D3DSURFACE_DESC desc;
        D3DTexture_GetLevelDesc(tex, 0, &desc);
        mNumMips = 0;
        mFormat = desc.Format;
        mWidth = desc.Width;
        mHeight = desc.Height;
        mBpp = D3DFORMAT_BitsPerPixel(desc.Format);
    }
}

D3DSurface *DxTex::GetRT() {
    if (!IsRenderTarget()) {
        return nullptr;
    } else {
        D3DResource_AddRef(mRenderTarget);
        return mRenderTarget;
    }
}

D3DSurface *DxTex::GetDepthRT() { return mDepthRT; }

void DxTex::PreDeviceReset() {
    if (IsBackBuffer() || IsRenderTarget()) {
        ResetSurfaces();
    }
}

void DxTex::PostDeviceReset() {
    if (IsBackBuffer()) {
        SetBitmap(TheRnd.Width(), TheRnd.Height(), TheRnd.Bpp(), mType, false, nullptr);
    }
    if (IsRenderTarget()) {
        SyncBitmap();
    }
}

D3DSurface *DxTex::GetSurfaceLevel(int x) {
    D3DSurface *ret = D3DTexture_GetSurfaceLevel(mTexture, x);
    DX_ASSERT(ret, 0xE6);
    return ret;
}

unsigned int DxTex::TexelsPitch() const {
    D3DLOCKED_RECT rect;
    D3DTexture_LockRect(mTexture, 0, &rect, nullptr, 0);
    D3DTexture_UnlockRect(mTexture, 0);
    return rect.Pitch;
}

D3DSurface *DxTex::GetMovieSurface() {
    if (!(mType & kMovie)) {
        return nullptr;
    } else {
        mTexture = mMovieTextures[mMovieBufIdx];
        return GetSurfaceLevel(0);
    }
}

void DxTex::SwapMovieSurface() {
    MILO_ASSERT((mType & kMovie) > 0, 0x2F5);
    mMovieBufIdx = (mMovieBufIdx + 1) % 2;
    mTexture = mMovieTextures[mMovieBufIdx];
}

void DxTex::ResetSurfaces() {
    // Clean up movie surface double-buffer
    for (int i = 0; i < 2; i++) {
        if (mTexture == mMovieTextures[i]) {
            mTexture = nullptr;
        }
        TheDxRnd.AutoRelease(mMovieTextures[i]);
        mMovieTextures[i] = nullptr;
    }

    // Delete main texture for certain types
    bool _bit0 = (mType & kRendered) != 0;
    if (((_bit0) && mNumMips) || ((mType & kMovie) && (mType & 0x20))
        || (mType & kScratch) || (mType & kRegularLinear)) {
        TheDxRnd.AutoDelete(mTexture);
        mTexture = nullptr;
    }

    // Clear texture pointer for movie/scratch/device types (0x1204 = kMovie | kScratch | kDeviceTexture)
    if (mType & 0x1204) {
        mTexture = nullptr;
    }

    // Release managed texture resource
    if (!TheDxTexMgr.ReleaseRes(unk2c)) {
        TheDxRnd.AutoRelease(mTexture);
    }
    mTexture = nullptr;

    // Clean up render target surfaces
    TheDxRnd.AutoRelease(mRenderTarget);
    mRenderTarget = nullptr;
    TheDxRnd.AutoRelease(mDepthRT);
    mDepthRT = nullptr;
}
