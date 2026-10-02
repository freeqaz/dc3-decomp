#include "rnddx9\CubeTex.h"
#include "Memory.h"
#include "Rnd.h"
#include "rnddx9\Rnd.h"
#include "rndobj\Bitmap.h"
#include "rndobj\Mat_NG.h"
#include "xdk\D3D9.h"
#include "xdk\XGRAPHICS.h"

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

// 100 modulo register permutation (23 rows): the image binds the shared zero
// to r27, face to r26 and &mBitmap[face] to r25; we bind r26/r25/r27.  A
// permuter sweep (decl reorder / extraction / temp elimination) found nothing.
void DxCubeTex::Sync() {
    PhysMemTypeTracker tracker("D3D(phys):CubeTex");

    D3DFORMAT format = TheDxRnd.D3DFormatForBitmap(mBitmap[kCubeFaceRight]);
    int numMips = props.mNumMips + 1;
    HRESULT hr = IDirect3DDevice9_CreateCubeTexture(
        TheDxRnd.Device(), props.mWidth, numMips, 0, format, 0, &mTex, nullptr
    );
    DX_ASSERT_CODE(hr, 0x38);

    XGTEXTURE_DESC desc;
    XGGetTextureDesc(mTex, 0, &desc);

    for (int face = 0; face < 6; face++) {
        RndBitmap bitmap;

        RndBitmap *pWork = &mBitmap[face];
        RndBitmap *bmp = pWork;

        if (pWork->Width() == 0 || pWork->Height() == 0) {
            MILO_NOTIFY("%s face %d width or height == 0 ", PathName(this), face);
        } else {
            if (pWork->Palette() != nullptr || pWork->Bpp() == 0x18) {
                bitmap.Create(*bmp, 0x20, bmp->Order(), nullptr);
                bmp = &bitmap;
            }

            for (int mip = 0; mip < numMips; mip++) {
                MILO_ASSERT(bmp, 0x53);
                D3DLOCKED_RECT locked;
                D3DCubeTexture_LockRect(
                    (D3DCubeTexture *)mTex, (D3DCUBEMAP_FACES)face, mip, &locked, nullptr, 0
                );
                DWORD gpuFormat = desc.Format & 0x3f;
                XGTileTextureLevel(
                    desc.Width, desc.Height, mip, gpuFormat, 0, locked.pBits,
                    nullptr, bmp->Pixels(), bmp->DxtRowBytes(), nullptr
                );
                D3DCubeTexture_UnlockRect(
                    (D3DCubeTexture *)mTex, (D3DCUBEMAP_FACES)face, mip
                );
                bmp = bmp->nextMip();
            }

            mBitmap[face].Reset();
        }
    }
    NgMat::SetCurrent(nullptr);
}
