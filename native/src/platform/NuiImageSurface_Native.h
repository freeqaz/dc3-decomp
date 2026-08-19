#pragma once
#ifdef HX_NATIVE

// Native stand-in for the D3D texture handle that a NUI image frame carries.
//
// On the Xbox build, NUI_IMAGE_FRAME::pFrameTexture is a real D3D9 texture and
// LiveCameraInput::LockStream reaches its bits through D3DLineTexture_LockRect.
// The native port has neither D3D9 nor NUI, so there is nothing to lock -- but
// LockStream is a recovered decomp body that we compile, and its call sites
// (TextureStore::UpdateFromColorBuffer and friends) are the Kinect colour decode
// path.
//
// This gives that path a defined native contract: any pointer handed to
// D3DLineTexture_LockRect is treated as a NuiImageSurface if and only if it
// carries the magic, in which case the lock hands back its pitch and bits.
// Anything else -- including the null that the desktop path actually produces --
// yields Pitch = 0 / pBits = null. That last part matters on its own: the
// previous behaviour was a weak return-0 stub, which left LockStream's
// D3DLOCKED_RECT *uninitialised*, so rect.mBits came back as whatever was on
// the stack.
//
// Nothing in the shipping game constructs one of these. It exists so a
// synthetic frame can be injected at the layer the hardware would have filled.

struct NuiImageSurface {
    static const unsigned int kMagic = 0x4E554953u; // 'NUIS'

    unsigned int mMagic;
    unsigned int mPitch; // bytes per row
    void *mBits;

    NuiImageSurface() : mMagic(kMagic), mPitch(0), mBits(0) {}
    NuiImageSurface(void *bits, unsigned int pitch)
        : mMagic(kMagic), mPitch(pitch), mBits(bits) {}
};

#endif
