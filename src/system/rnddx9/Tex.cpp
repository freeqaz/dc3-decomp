#include "rnddx9/Tex.h"
#include "Rnd.h"
#include "Tex.h"
#include "os/Debug.h"
#include "rnddx9/Rnd.h"
#include "rnddx9/TexMgr.h"
#include "rndobj/Mat_NG.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "utl/MemMgr.h"
#include "xdk/d3d9i/d3d9.h"
#include "xdk/d3d9i/d3d9types.h"

std::vector<DxTex *> gAllTextures;

struct CompressLevel {
    D3DSurface *scratchSurface; // 0x0
    char pad[0x2c - 0x4];
    D3DSurface *textureSurface; // 0x2c
    char pad2[0x58 - 0x30];
};

struct CompressDesc {
    D3DTexture *texture; // 0x0
    RndTex::AlphaCompress alpha; // 0x4
    int unk8; // 0x8
    D3DFORMAT format; // 0xc
    void *tiledBuffer; // 0x10
    CompressLevel levels[16]; // 0x14
};

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
