#include "DrawUtl.h"

#include "SkeletonViz.h"
#include "gesture\SkeletonUpdate.h"
#include "rndobj\Utl.h"

Vector3 DrawUtlVec3(0.5f, 0.05f, 0.4f);

Hmx::Rect DrawUtlRect;

namespace {
    /** Convert one YCbCr sample to RGB565. The Kinect colour stream hands us
     *  full-range Y with 128-biased U/V, so the standard 16.16 fixed-point
     *  BT.601 matrix applies directly and every channel is clamped to 8 bits
     *  before it is packed.
     *
     *  `inline` is load-bearing, not cosmetic. Without it MSVC emits this body
     *  eagerly, learns that it clobbers no volatile register, and lets
     *  UpdateBufferTex park u/v/dst in r4/r5/r6 across both calls -- which the
     *  target does not do. As an inline COMDAT the body is deferred, the call
     *  sites become conservative, and UpdateBufferTex goes 92.6% -> 96.7%.
     *  The linker map agrees: the same symbol appears under two different
     *  anonymous-namespace hashes (gesture:LiveCameraInput.obj and
     *  gesture:DrawUtl.obj) ICF-folded to one address, i.e. it was a header
     *  definition included by both translation units. */
    inline unsigned short YUVtoRGB(int y, int u, int v) {
        int r = y + ((91881 * v) >> 16);
        int g = y + ((-46802 * v - 22553 * u) >> 16);
        int b = y + ((116130 * u) >> 16);
        r = r > 255 ? 255 : (r < 0 ? 0 : r);
        g = g > 255 ? 255 : (g < 0 ? 0 : g);
        b = b > 255 ? 255 : (b < 0 ? 0 : b);
        return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    /** Blit the 320x240 Kinect depth stream into an RGB565 scratch texture as a
     *  repeating 1m ramp. Each depth texel is 13 bits of millimetres in the high
     *  bits and a 3-bit player index in the low bits; the ramp restarts every
     *  metre and the player index knocks out colour channels so the six tracked
     *  players come out in distinct hues. */
    void CopyDepth(
        int dstExtraStride,
        int srcExtraStride,
        unsigned short *__restrict dst,
        const unsigned short *__restrict src
    ) {
        for (int y = 0; y < 240; y++) {
            for (int x = 0; x < 320; x++) {
                unsigned short texel = *src;
                int depth = texel >> 3;
                // Brightness lerps 0.6 -> 1.0 across each metre. The span must be
                // spelled `1.0f - 0.6f`: the target's constant is 0x3ECCCCCC, the
                // float value of that subtraction, not 0.4f (0x3ECCCCCD).
                float meters = (float)(depth % 1000) * 0.001f;
                float ramp = meters * (1.0f - 0.6f) + 0.6f;
                int r = depth ? (int)(ramp * 64.0f) : 0;
                int g = depth ? (int)(ramp * 128.0f) : 0;
                int b = depth ? (int)(ramp * 64.0f) : 0;
                int player = texel & 7;
                if (player == 1 || player == 4 || player == 5) {
                    r = 0;
                }
                if (player == 2 || player == 4 || player == 6) {
                    g = 0;
                }
                if (player == 3 || player == 5 || player == 6) {
                    b = 0;
                }
                *dst++ = (unsigned short)(((r << 6 | g) << 5) | b);
                src++;
            }
            src += srcExtraStride;
            dst += dstExtraStride;
        }
    }

    /** Blit the 320x240 player-index plane into an RGB565 scratch texture using a
     *  fixed palette: background stays black, the active skeleton is drawn white
     *  and every other tracked player gets a flat tint. */
    void CopyPlayerMask(
        int srcExtraStride,
        unsigned short *__restrict dst,
        const unsigned short *__restrict src,
        int player
    ) {
        unsigned int colors[8];
        colors[0] = 0;
        colors[1] = (unsigned short)(player == 0 ? -1 : 0x3000);
        colors[2] = (unsigned short)(player == 1 ? -1 : 0x3000);
        colors[3] = 0x216b;
        colors[4] = 0x216b;
        colors[5] = 0x216b;
        colors[6] = 0x216b;
        colors[7] = 0x216b;
        for (int y = 0; y < 240; y++) {
            for (int x = 0; x < 320; x++) {
                *dst++ = (unsigned short)colors[*src++ & 7];
            }
            src += srcExtraStride;
        }
    }

    void ScreenSpace(Hmx::Rect &rect) {
        float vx = DrawUtlVec3.x;
        float vy = DrawUtlVec3.y;
        float vz = DrawUtlVec3.z;
        rect.x = vx;
        rect.y = vy;
        rect.w = vz;
        float scale = (float)TheRnd.Width() / (float)TheRnd.Height();
        if (scale > 1.3333334f) {
            float rectW = 1.3333334f / scale;
            float diff = vz - rectW;
            rect.w = diff >= 0.0f ? rectW : vz;
        }
        rect.h = (scale * 0.75f) * rect.w;
    }

    void PixelSpace(Hmx::Rect &rect) {
        int width = TheRnd.Width();
        int height = TheRnd.Height();
        Hmx::Rect temp;
        ScreenSpace(temp);
        rect.x = width * temp.x;
        rect.w = width * temp.w;
        rect.y = height * temp.y;
        rect.h = height * temp.h;
    }
}

void TerminateDrawUtl() {
    if (TheSkeletonViz) {
        delete TheSkeletonViz;
    }
    TheSkeletonViz = nullptr;
}

#ifndef HX_NATIVE
bool ToggleDrawSkeletons() {
    MILO_ASSERT(TheSkeletonViz, 0xe2);
    TheSkeletonViz->SetShowing(!TheSkeletonViz->Showing());
    return TheSkeletonViz->Showing();
}
#endif

RndMat *CreateCameraBufferMat(int width, int height, RndTex::Type type) {
    auto tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(width, height, 16, type, false, nullptr);
    auto newMat = Hmx::Object::New<RndMat>();
    newMat->SetUseEnv(false);
    newMat->SetPreLit(true);
    newMat->SetBlend(BaseMaterial::kBlendSrc);
    newMat->SetZMode(kZModeDisable);
    newMat->SetDiffuseTex(tex);
    CreateAndSetMetaMat(newMat);
    return newMat;
}

void DrawBufferMat(RndMat *mat, Hmx::Rect &rect) {
    Hmx::Color white(1.0f, 1.0f, 1.0f, 1.0f);
    TheRnd.DrawRect(rect, white, mat, nullptr, nullptr);
}

void DrawSnapshot(const GestureMgr &gm, int index) {
    MILO_ASSERT(index >= 0 && index < gm.GetLiveCameraInput()->NumSnapshots(), 0xfb);
    auto cam = gm.GetLiveCameraInput();
    auto snap = cam->GetSnapshot(index);
    DrawBufferMat(snap, DrawUtlRect);
}

void InitDrawUtl(const GestureMgr &gm) {
    TheSkeletonViz = Hmx::Object::New<SkeletonViz>();
    TheSkeletonViz->Init();
    TheSkeletonViz->SetUsePhysicalCam(true);
    TheSkeletonViz->SetAxesCoordSys(kCoordCamera);
    TheSkeletonViz->SetShowing(false);
    Hmx::Rect temp;
    PixelSpace(temp);
    DrawUtlRect = temp;
}

void SetDrawSpace(float x, float y, float z) {
    DrawUtlVec3.Set(x, y, z);
}

void DrawGestureMgr(GestureMgr &gm, LiveCameraInput::BufferType bufferType, float) {
    TheRnd.EndWorld();

    if (bufferType != LiveCameraInput::kBufferNum && bufferType != LiveCameraInput::kBufferPlayer
        && bufferType != LiveCameraInput::kBufferDepth) {
        LiveCameraInput *cam = gm.GetLiveCameraInput();
        if (UpdateBufferTex(cam, cam->DisplayTex(bufferType), bufferType, &gm)) {
            DrawBufferMat(cam->DisplayMat(bufferType), DrawUtlRect);
        }
    }

    if (TheSkeletonViz->Showing()) {
        Hmx::Rect screenRect;
        ScreenSpace(screenRect);
        TheSkeletonViz->SetPhysicalCamScreenRect(screenRect);

        SkeletonUpdateHandle handle = SkeletonUpdate::InstanceHandle();
        CameraInput *cameraInput = handle.GetCameraInput();
        for (int i = 0; i < 6; i++) {
            TheSkeletonViz->Visualize(
                *cameraInput, gm.GetSkeleton(i), &handle.Callbacks(), false
            );
        }
        gm.DrawSkeletonKinectData();
    }
}

bool UpdateBufferTex(LiveCameraInput *cam, RndTex *tex, LiveCameraInput::BufferType bufType, GestureMgr *gm) {
    START_AUTO_TIMER("draw_natal_buffer");
    MILO_ASSERT(bufType < LiveCameraInput::kBufferNum, 0x12b);
    if (cam == nullptr) {
        return false;
    }
    MILO_ASSERT(tex, 0x130);
    MILO_ASSERT(tex->Bpp() == 16, 0x131);
    MILO_ASSERT(tex->GetType() == RndTex::kScratch, 0x132);

    int width = tex->Width();
    int height = tex->Height();
    void *texelsPtr = nullptr;
    tex->TexelsLock(texelsPtr);
    unsigned short *texels = (unsigned short *)texelsPtr;

    if (bufType == LiveCameraInput::kBufferColor) {
        void *bufStream = cam->StreamBufferData(LiveCameraInput::kBufferColor);
        if (bufStream != nullptr) {
            LiveCameraInput::LockedRect lockedRect;
            cam->LockStream(bufStream, lockedRect);
            const unsigned int *src = (const unsigned int *)lockedRect.mBits;
            int dstExtraStride = tex->TexelsPitch() / 2 - 640;
            int srcExtraStride = lockedRect.mPitch / 4 - 320;
            unsigned short *dst = texels;
            for (int y = 0; y < 480; y++) {
                for (int x = 0; x < 320; x++) {
                    // UYVY: one 32-bit word carries U, Y0, V, Y1 -- two pixels.
                    unsigned int packed = *src++;
                    int u = (packed >> 24) - 128;
                    int v = ((packed >> 8) & 0xff) - 128;
                    int y0 = (packed >> 16) & 0xff;
                    int y1 = packed & 0xff;
                    *dst++ = YUVtoRGB(y0, u, v);
                    *dst++ = YUVtoRGB(y1, u, v);
                }
                dst += dstExtraStride;
                src += srcExtraStride;
            }
            cam->UnlockStream(bufStream);
        }
    } else if (bufType == LiveCameraInput::kBufferPlayerColor) {
        void *colorStream = cam->StreamBufferData(LiveCameraInput::kBufferColor);
        void *depthStream = cam->StreamBufferData(LiveCameraInput::kBufferDepth);
        LiveCameraInput::LockedRect colorRect;
        cam->LockStream(colorStream, colorRect);
        LiveCameraInput::LockedRect depthRect;
        cam->LockStream(depthStream, depthRect);
        const unsigned int *colorRow = (const unsigned int *)colorRect.mBits;
        const unsigned short *depthRow = (const unsigned short *)depthRect.mBits;
        unsigned short *dstRow = texels;
        for (int y = 0; y < 480; y++) {
            for (int x = 0; x < 640; x++) {
                if (depthRow[x / 2] & 3) {
                    // 0xAARRGGBB -> RGB565.
                    unsigned int packed = colorRow[x];
                    dstRow[x] = (unsigned short
                    )(((packed >> 8) & 0xf800) | ((packed >> 5) & 0x7e0)
                      | ((packed >> 3) & 0x1f));
                } else {
                    dstRow[x] = 0;
                }
            }
            dstRow += tex->TexelsPitch() / 2;
            colorRow += colorRect.mPitch / 4;
            if (y & 1) {
                depthRow += depthRect.mPitch / 2;
            }
        }
        cam->UnlockStream(colorStream);
        cam->UnlockStream(depthStream);
    } else {
        MILO_ASSERT(gm, 0x191);
        void *bufStream = cam->StreamBufferData(bufType);
        LiveCameraInput::LockedRect lockedRect;
        cam->LockStream(bufStream, lockedRect);
        const unsigned short *src = (const unsigned short *)lockedRect.mBits;
        MILO_ASSERT(width == 320 && height == 240, 0x19a);
        if (src) {
            int srcExtraStride = lockedRect.mPitch / 2 - 320;
            int dstExtraStride = tex->TexelsPitch() / 2 - 320;
            if (bufType == LiveCameraInput::kBufferDepth) {
                CopyDepth(dstExtraStride, srcExtraStride, texels, src);
            } else {
                MILO_ASSERT(LiveCameraInput::kBufferPlayer == bufType, 0x1a5);
                CopyPlayerMask(
                    srcExtraStride, texels, src, gm->GetActiveSkeletonIndex()
                );
            }
        }
        cam->UnlockStream(bufStream);
    }

    tex->TexelsUnlock();
    return true;
}
