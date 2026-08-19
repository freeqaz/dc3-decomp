#ifdef HX_NATIVE

#include "NuiImageSurface_Native.h"

#include "xdk/d3d9i/d3d9types.h"

// The two entry points LiveCameraInput::LockStream / UnlockStream call.
//
// They became undefined the moment LockStream/UnlockStream stopped being hidden
// behind #ifndef HX_NATIVE. Before that they were referenced by nothing, so the
// stub generator never produced even a weak body for them -- meaning a call
// would have gone to a PLT slot with no definition anywhere and the process
// would die on the first lock (verified: LD_BIND_NOW=1 makes every such symbol
// fatal at startup).
//
// D3DLineTexture is only ever forward-declared in the native build, so the
// pointer is opaque here; reinterpret through the NuiImageSurface contract.

struct D3DLineTexture;

extern "C" {

VOID D3DLineTexture_LockRect(
    struct D3DLineTexture *pTexture,
    UINT /*Level*/,
    D3DLOCKED_RECT *pLockedRect,
    const tagRECT * /*pRect*/,
    DWORD /*Flags*/
) {
    if (!pLockedRect)
        return;
    pLockedRect->Pitch = 0;
    pLockedRect->pBits = 0;
    if (!pTexture)
        return;
    const NuiImageSurface *surf = reinterpret_cast<const NuiImageSurface *>(pTexture);
    if (surf->mMagic != NuiImageSurface::kMagic)
        return;
    pLockedRect->Pitch = (INT)surf->mPitch;
    pLockedRect->pBits = surf->mBits;
}

VOID D3DLineTexture_UnlockRect(struct D3DLineTexture * /*pTexture*/, UINT /*Level*/) {
    // Nothing to release: a NuiImageSurface points at caller-owned memory.
}

} // extern "C"

#endif
