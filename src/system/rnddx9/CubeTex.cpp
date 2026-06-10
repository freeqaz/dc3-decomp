#include "rnddx9/CubeTex.h"
#include "Memory.h"
#include "Rnd.h"
#include "rnddx9/Rnd.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Mat_NG.h"
#include "xdk/D3D9.h"
#include "xdk/XGRAPHICS.h"

DxCubeTex::DxCubeTex() : mTex(0) {}
DxCubeTex::~DxCubeTex() { Reset(); }

void DxCubeTex::Select(int x) {
    D3DDevice_SetTexture(TheDxRnd.Device(), x, mTex, 0x8000000000000000 >> (x + 0x20U));
}

void DxCubeTex::Reset() {
    TheDxRnd.AutoRelease(mTex);
    mTex = nullptr;
    NgMat::SetCurrent(nullptr);
}

void DxCubeTex::Sync() {
    PhysMemTypeTracker tracker("D3D(phys):CubeTex");

    int numMips = props.mNumMips + 1;
    DX_ASSERT(mTex = D3DDevice_CreateTexture(
        props.mWidth, props.mWidth, 6, numMips, 0,
        TheDxRnd.D3DFormatForBitmap(mBitmap[kCubeFaceRight]), 0,
        D3DRTYPE_CUBETEXTURE
    ), 0x38);

    XGTEXTURE_DESC desc;
    XGGetTextureDesc(mTex, 0, &desc);

    NgMat::SetCurrent(nullptr);

    for (int face = 0; face < 6; face++) {
        RndBitmap bitmap;

        RndBitmap *pWork = &mBitmap[face];

        if (pWork->Width() == 0 || pWork->Height() == 0) {
            MILO_NOTIFY("%s face %d width or height == 0", PathName(this), face);
        } else {
            RndBitmap *bmp = pWork;
            if (pWork->Palette() != nullptr || pWork->Bpp() == 0x18) {
                bitmap.Create(*pWork, 0x20, pWork->Order(), nullptr);
                bmp = &bitmap;
            }

            for (int mip = 0; mip < numMips; mip++) {
                MILO_ASSERT(bmp, 0x53);
                D3DLOCKED_RECT locked;
                D3DCubeTexture_LockRect(
                    (D3DCubeTexture *)mTex, (D3DCUBEMAP_FACES)face, mip, &locked, nullptr, 0
                );
                XGTileTextureLevel(
                    desc.Width, desc.Height, mip, desc.Format & 0x3f, 0, locked.pBits,
                    nullptr, bmp->Pixels(), bmp->DxtRowBytes(), nullptr
                );
                D3DCubeTexture_UnlockRect(
                    (D3DCubeTexture *)mTex, (D3DCUBEMAP_FACES)face, mip
                );
                bmp = bmp->nextMip();
            }

            mBitmap[face].Reset();
        }

        bitmap.Reset();
    }
}
