#include "gesture\LiveCameraInput.h"
#include "DrawUtl.h"
#include "gesture\CameraInput.h"
#include "gesture\Skeleton.h"
#include "gesture\SkeletonUpdate.h"
#include "gesture\SpeechMgr.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rnddx9\Tex.h"
#include "rndobj\Bitmap.h"
#include "rndobj\Mat.h"
#include "rndobj\Tex.h"
#include "math\Utl.h"
#include "utl\MemTrack.h"
#include "utl\Std.h"
#include "xdk\nui\nuiapi.h"
#include "xdk\nui\nuiaudio.h"
#include "xdk\nui\nuidetroit.h"
#include "xdk\win_types.h"
#include "xdk\XGRAPHICS.h"
#include "xdk\xapilibi\handleapi.h"
#include "xdk\xapilibi\winerror.h"
#include "utl\FilePath.h"
#include "utl\FileStream.h"
#include "utl/Loader.h"
#include "Memory.h"

class DxRnd {
public:
    void ReleaseAutoRelease();
};
extern DxRnd TheDxRnd;

float gTempPortraitOffset = 0.125f;

namespace {
    bool gDebugDepth;
    bool GetExposureRegion(NUI_CAMERA_AE_ROI &);
    long GetColorCameraProperty(NUI_CAMERA_PROPERTY);
    /** Convert one YCbCr sample to RGB565 -- the same body DrawUtl.cpp carries.
     *
     *  This TU used to only *declare* it. On the PPC build that was harmless
     *  (the linker map shows ?YUVtoRGB@?A0x8e584365@@ contributed by
     *  gesture:LiveCameraInput.obj and ICF-folded with gesture:DrawUtl.obj's
     *  copy, i.e. the original was one header definition included by both TUs).
     *  On the native port it was a live bug: clang emitted an *undefined*
     *  reference to an internal-linkage symbol, and the weak asm-label stub
     *  `_stub_yuvtorgb` in native/src/engine_stubs_generated.cpp satisfied it,
     *  so every Kinect colour texel came out 0 -- the camera feed was solid
     *  black in UpdateFromColorBuffer and UpdateFromColorBufferClip.
     *
     *  `inline` is load-bearing for the same reason it is in DrawUtl.cpp: as an
     *  inline COMDAT the body is deferred and the call sites stay conservative.
     */
    inline unsigned short YUVtoRGB(int y, int u, int v) {
        int r = y + ((91881 * v) >> 16);
        int g = y + ((-46802 * v - 22553 * u) >> 16);
        int b = y + ((116130 * u) >> 16);
        r = r > 255 ? 255 : (r < 0 ? 0 : r);
        g = g > 255 ? 255 : (g < 0 ? 0 : g);
        b = b > 255 ? 255 : (b < 0 ? 0 : b);
        return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }

    void SetColorCameraProperty(NUI_CAMERA_PROPERTY prop, long value) {
        HRESULT hr = NuiCameraSetProperty(NUI_CAMERA_TYPE_COLOR, prop, value);
        if (FAILED(hr)) {
            MILO_LOG(
                "NuiCameraSetProperty failed.  Property 0x%x, error (0x%x)\n",
                prop,
                hr
            );
        }
    }

    void LoadDebugDepthBuffer(RndTex *&outTex) {
        outTex = nullptr;
        FileLoader loader(
            FilePath("../../system/run/gesture/dev_depth.data"),
            "../../system/run/gesture/dev_depth.data", kLoadFront, 0, false, true,
            nullptr, nullptr
        );
        TheLoadMgr.PollUntilLoaded(&loader, nullptr);
        int sz;
        char *buffer = loader.GetBuffer(&sz);
        if (sz != 0) {
            int mWidth = 320, mHeight = 240, mBpp = 16;
            MILO_ASSERT(sz == mWidth * mHeight * mBpp / 8, 0x5a);
            DxTex *tex = Hmx::Object::New<DxTex>();
            D3DTexture *d3dTex = new D3DTexture;
            int texSize =
                XGSetTextureHeader(
                    mWidth, mHeight, 1, 4, (D3DFORMAT)0x1a220058, 0, 0, -1, 0, d3dTex,
                    nullptr, nullptr
                );
            int allocSize = (texSize + 0xFFF) & 0xFFFFF000;
            void *ptr = PhysicalAllocTracked(
                allocSize, 4, "LiveCameraInput.cpp", 0x66, "Tex(phys)"
            );
            MILO_ASSERT(ptr, 0x67);
            XGOffsetResourceAddress(d3dTex, ptr);
            tex->SetDeviceTex(d3dTex);
            void *texels;
            tex->TexelsLock(texels);
            memcpy(texels, buffer, sz);
            tex->TexelsUnlock();
            outTex = tex;
        }
        MemFree(buffer, "unknown", 0, "unknown");
    }
}

LiveCameraInput *LiveCameraInput::sInstance;
int g_ColorPollCnt;
int g_ColorNoFrameDataCnt;
int side;
int g_colorBufferUpdate1;
int g_colorBufferUpdate2;
int g_colorBufferUpdate3;
int g_colorBufferUpdate4;
bool g_startMetering;

void CamTexClip::StoreTextureClip(RndTex *tex, float clipLeft, float clipTop, float, float) {
    const float scaleX = 132.0f / 640.0f;
    const float scaleY = 160.0f / 480.0f;
    const float scaleZ = 1.0f;
    const float minX = 66.0f / 640.0f;
    const float maxX = 574.0f / 640.0f;
    const float minY = 80.0f / 480.0f;
    const float maxY = 400.0f / 480.0f;

    // RESIDUAL (w8-r, 99.919, 296 B, 10 of 74 rows) -- pure FPR colouring in
    // the two trailing row scales, and the arithmetic is identical on both
    // sides.  `m.y *= scaleY` loads 0x14 into f13 on both sides and then the
    // image takes 0x10 into f12 / 0x18 into f0 where we take 0x18 into f12 /
    // 0x10 into f0; `m.z *= scaleZ` (folded to a load/store triple because
    // scaleZ is 1.0f) is the same story on 0x20/0x24/0x28.  The STORE order is
    // ascending on both sides -- only which FPR carries which component
    // differs, so there is no component swap here.
    float adjustedTop = gTempPortraitOffset * scaleY + clipTop;
    mTex = tex;
    mXfm = Transform::IDXfm();
    mXfm.m.x *= scaleX;
    float clampedX = Clamp(minX, maxX, clipLeft);
    float clampedY = Clamp(minY, maxY, adjustedTop);
    mXfm.v.x = clampedX;
    mXfm.v.y = clampedY;
    // w17-c: Scale(), not `*=`, for the last two rows (99.919 -> 100 on a
    // non-PCH probe; every per-component `*=` ordering was worse).  The
    // three-argument Scale reads all three components before it writes,
    // which is the load colouring described above.
    Scale(mXfm.m.y, scaleY, mXfm.m.y);
    Scale(mXfm.m.z, scaleZ, mXfm.m.z);
}

#pragma region TextureStore

void LiveCameraInput::TextureStore::StoreTexture(RndTex *tex) {
    if (mTex) {
        RELEASE(mTex);
    }
    if (tex) {
        mTex = Hmx::Object::New<RndTex>();
        RndBitmap bitmap;
        tex->LockBitmap(bitmap, 1);
        mTex->SetBitmap(bitmap, nullptr, true, RndTex::kRenderedNoZ);
        tex->UnlockBitmap();
    } else {
        mTex = nullptr;
    }
}

void LiveCameraInput::TextureStore::StoreColorBuffer(LiveCameraInput *cam) {
    if (mTex) {
        if (mTex->Width() == 640 && mTex->Height() == 480)
            goto update;
        RELEASE(mTex);
    }
    mTex = Hmx::Object::New<RndTex>();
    mTex->SetBitmap(640, 480, 16, RndTex::kScratch, false, nullptr);
    MILO_ASSERT(mTex, 0x53A);
    MILO_ASSERT(mTex->Bpp() == 16, 0x53B);
    MILO_ASSERT(mTex->GetType() == RndTex::kScratch, 0x53C);
update:
    UpdateFromColorBuffer(cam);
}

void LiveCameraInput::TextureStore::StoreDepthBuffer(LiveCameraInput *cam) {
    if (mTex) {
        if (mTex->Width() == 640 && mTex->Height() == 480)
            goto update;
        RELEASE(mTex);
    }
    mTex = Hmx::Object::New<RndTex>();
    mTex->SetBitmap(640, 480, 16, RndTex::kScratch, false, nullptr);
    MILO_ASSERT(mTex, 0x602);
    MILO_ASSERT(mTex->Bpp() == 16, 0x603);
    MILO_ASSERT(mTex->GetType() == RndTex::kScratch, 0x604);
update:
    UpdateFromDepthBuffer(cam);
}

void LiveCameraInput::TextureStore::StoreColorBufferClip(
    LiveCameraInput *cam, float clipLeft, float clipTop, float clipWidth, float clipHeight
) {
    if (mTex) {
        if (mTex->Width() == clipWidth && mTex->Height() == clipHeight)
            goto update;
        RELEASE(mTex);
    }
    {
        int w = (1 - (int)(clipWidth * -640.0f)) & 0xfffe;
        int h = (1 - (int)(clipHeight * -480.0f)) & 0xfffe;
        if (w > 0x280)
            w = 0x280;
        if (h > 0x1e0)
            h = 0x1e0;
        mTex = Hmx::Object::New<RndTex>();
        mTex->SetBitmap(w, h, 16, RndTex::kScratch, false, nullptr);
        MILO_ASSERT(mTex, 0x595);
        MILO_ASSERT(mTex->Bpp() == 16, 0x596);
        MILO_ASSERT(mTex->GetType() == RndTex::kScratch, 0x597);
    }
update:
    UpdateFromColorBufferClip(cam, clipLeft, clipTop);
}

void LiveCameraInput::TextureStore::StoreDepthBufferClip(
    LiveCameraInput *cam, float clipLeft, float clipTop, float clipWidth, float clipHeight
) {
    if (mTex) {
        if (mTex->Width() == clipWidth && mTex->Height() == clipHeight)
            goto update;
        RELEASE(mTex);
    }
    {
        int w = (1 - (int)(clipWidth * -640.0f)) & 0xfffe;
        int h = (1 - (int)(clipHeight * -480.0f)) & 0xfffe;
        if (w > 0x280)
            w = 0x280;
        if (h > 0x1e0)
            h = 0x1e0;
        mTex = Hmx::Object::New<RndTex>();
        mTex->SetBitmap(w, h, 16, RndTex::kScratch, false, nullptr);
        MILO_ASSERT(mTex, 0x660);
        MILO_ASSERT(mTex->Bpp() == 16, 0x661);
        MILO_ASSERT(mTex->GetType() == RndTex::kScratch, 0x662);
    }
update:
    UpdateFromDepthBufferClip(cam, clipLeft, clipTop);
}

void LiveCameraInput::TextureStore::UpdateFromColorBuffer(LiveCameraInput *cam) {
    void *texels = nullptr;
    mTex->TexelsLock(texels);
    // A real unsigned short* rather than an integer address: the target's inner
    // loop is `sth 0(dst)` + `sthu 2(dst)` + `addi dst, 2`, which is what MSVC
    // emits for two `*dst++ = ...` stores, and it also keeps the native LP64
    // build from truncating the texel pointer to 32 bits.
    unsigned short *destPtr = (unsigned short *)texels;
    g_colorBufferUpdate1++;
    void *bufferData = cam->StreamBufferData(kBufferColor);
    if (bufferData) {
        LockedRect lockedRect;
        cam->LockStream(bufferData, lockedRect);
        g_colorBufferUpdate2++;
        unsigned int *srcPtr = (unsigned int *)((uintptr_t)lockedRect.mBits - 4);
        // w20-c: both strides named as ints BEFORE the loop, the spelling
        // DrawUtl's UpdateBufferTex uses for the same blit.  Same values as the
        // old in-loop `(int)((pitch >> 1) - 640)` form (the unsigned
        // subtraction is converted to int before it steps the pointer, so the
        // LP64 native build still steps backwards when the pitch is narrow),
        // but it flips the image's r22/r23 (src/dest stride) and r10/r11
        // colouring: fuzzy 99.054 -> all 74 rows equal.  The one remaining
        // relocation row is D3DCubeTexture_UnlockRect vs D3DTexture_UnlockRect,
        // a proven ICF fold at 0x82B9BEC0 (icf_aliases.map).
        int dstExtraStride = mTex->TexelsPitch() / 2 - 640;
        int srcExtraStride = lockedRect.mPitch / 4 - 320;
        for (int row = 0; row < 480; row++) {
            for (int col = 0; col < 320; col++) {
                srcPtr++;
                unsigned int pixel = *srcPtr;
                int cr = (pixel >> 24) - 0x80;
                int cb = (pixel >> 8 & 0xff) - 0x80;
                *destPtr++ = YUVtoRGB(pixel >> 16 & 0xff, cr, cb);
                *destPtr++ = YUVtoRGB(pixel & 0xff, cr, cb);
            }
            destPtr += dstExtraStride;
            srcPtr += srcExtraStride;
        }
        D3DTexture_UnlockRect((D3DTexture *)bufferData, 0);
    }
}

// playerIdx is an unsigned int, not an unsigned short: as a short it
// outranked colour in the colour allocator (pri 18 vs 17), so it took r11 and
// colour r10, the reverse of the image. As an int its priority drops to 11,
// colour is coloured first and takes r11.
void LiveCameraInput::TextureStore::UpdateFromDepthBuffer(LiveCameraInput *cam) {
    void *texels = nullptr;
    mTex->TexelsLock(texels);
    uintptr_t destBase = (uintptr_t)texels;
    void *bufferData = cam->StreamBufferData(kBufferDepth);
    if (bufferData) {
        LockedRect lockedRect;
        cam->LockStream(bufferData, lockedRect);
        uintptr_t srcBase = (uintptr_t)lockedRect.mBits;
        unsigned int rowIdx = 0;
        do {
            unsigned int x = 0;
            unsigned short *destRow = (unsigned short *)(destBase - 2);
            do {
                unsigned short depthPixel =
                    *(unsigned short *)(((int)x / 2) * 2 + srcBase);
                // The `playerIdx <= 7` guard IS in the image -- `cmplwi cr6,
                // r10, 0x7` / `bgt cr6` at 0x82432B8C -- so it is not the
                // tautology it looks like; REMOVING it costs 93.940 -> 92.723.
                unsigned short color = 0;
                unsigned int playerIdx = depthPixel & 7;
                if (playerIdx <= 7) {
                switch (playerIdx) {
                case 0:
                    color = 0;
                    break;
                case 1:
                    color = 0xf800;
                    break;
                case 2:
                    color = 0x7e0;
                    break;
                case 3:
                    color = 0x1f;
                    break;
                case 4:
                    color = 0xf81f;
                    break;
                case 5:
                    color = 0x7ff;
                    break;
                case 6:
                    color = 0xffe0;
                    break;
                case 7:
                    color = 0xffff;
                    break;
                }
                }
                x++;
                destRow++;
                *destRow = color;
            } while ((int)x < 640);
            unsigned int pitch = mTex->TexelsPitch();
            destBase += (pitch & 0xfffffffe);
            if ((rowIdx & 1) != 0) {
                srcBase += (lockedRect.mPitch & 0xfffffffe);
            }
            rowIdx++;
        } while ((int)rowIdx < 480);
        D3DTexture_UnlockRect((D3DTexture *)bufferData, 0);
    }
}

/** SURVEYED w7-aj, 88.5% canonical (up from 87.1), 596 B, target
 *  0x82431060-0x824312B4.  The one source-reachable lever found was statement
 *  ORDER of the `g_colorBufferUpdate3++` counter: retail materialises the
 *  counter's `lis` AFTER both `& 0xfffe` / `% N` clip computations
 *  (target 0x824310C4), not before, so the increment belongs below them.
 *  Worth +1.4.
 *
 *  The residual is register allocation plus one scheduling cluster, and three
 *  variants were measured NON-improving -- do not re-derive:
 *    (1) commutative swaps on `texWidth + startX - 1 >= 640` and
 *        `texHeight + startY - 1 >= 480` (target indices 16/31): exactly
 *        neutral, MSVC normalises both operand orders.
 *    (2) declaring `srcPitch` before `destWidth`: exactly neutral at 88.5.
 *    (3) declaring `clippedY` before `clippedX` to flip the r7/r6 assignment
 *        at target indices 40/41: canonical unchanged at 88.5 but raw drops
 *        and it manufactures two new (0x1e0,0x280) OFFSET_SWAP rows -- strictly
 *        worse, reverted.
 *
 *  w7-by (88.50336 -> 97.31544 canonical): the "scheduling cluster at target
 *  indices 86-115" was the LOOP SHAPE, not the scheduler.  The image's
 *  preheader values -- `slwi r23, r10, 1` (destStride * 2), `slwi r22, r11, 2`
 *  ((srcPitch - 320) * 4) and `subi r28, r30, 0x4` at 0x82430A28..0x82430A34,
 *  all placed AFTER the `ble` on mTex->Height() -- are loop-invariant code
 *  motion of expressions written inside the loop: `*srcPtr++` (which MSVC
 *  strength-reduces to `lwzu r31, 0x4(r28)` from a pre-decremented base),
 *  `destPtr += destStride * 2` and `srcPtr += srcStride` with the strides
 *  named unscaled.  Written pre-scaled before the loop they are computed
 *  before the guard, which dragged 40+ register assignments with them.
 *  Residual (12 rows): the two commutative `add`s at 0x824308C8/0x82430904
 *  and the r6/r7 assignment of the two masked clip values (0x82430924) --
 *  clippedY-first, rounding startX/startY in place, and swapping the add
 *  operands were all re-measured in this shape and are inert or worse.
 *
 *  w21-bk (97.315 -> 100, all 149 rows equal; raw <100 only for the ICF
 *  UnlockRect name below): two spellings, found with a real-TU cl.exe probe.
 *  (1) Read mTex->Width()/Height() AFTER the startX/startY clamp-to-zero, not
 *  before: that is the image's `add r8, r10, r9` (texWidth first) at both
 *  edge tests.  (2) Round startX/startY IN PLACE instead of into new
 *  clippedX/clippedY locals: that gives the image's r7=X / r6=Y masks and the
 *  counter `lis` after both `+1`s.  (The in-place rounding was measured inert
 *  in the earlier shape; it needs (1) first.)  Same values throughout.
 *
 *  Not a bug: the final `D3DCubeTexture_UnlockRect` vs our
 *  `D3DTexture_UnlockRect` (index 153) are the SAME address 0x82B9BEC0 in
 *  build/373307D9/icf_aliases.map -- a proven ICF fold. */
void LiveCameraInput::TextureStore::UpdateFromColorBufferClip(
    LiveCameraInput *cam, float clipLeft, float clipTop
) {
    int startX = (int)(clipLeft * 640.0f);
    if (startX < 0)
        startX = 0;
    int texWidth = mTex->Width();
    if (texWidth + startX - 1 >= 640) {
        startX = 640 - texWidth;
    }
    int startY = (int)(clipTop * 480.0f);
    if (startY < 0)
        startY = 0;
    int texHeight = mTex->Height();
    if (texHeight + startY - 1 >= 480) {
        startY = 480 - texHeight;
    }
    // startX/startY now become the even-aligned clip origin (w21-bk: in place).
    startX = ((startX + 1) & 0xfffe) % 640;
    startY = ((startY + 1) & 0xfffe) % 480;
    g_colorBufferUpdate3++;
    if (!g_startMetering) {
        // The image materialises 0 FIRST (0x82430970 `li r10, 0x0`, then
        // 0x82430974 `li r9, 0x1`), i.e. the zeroed counter is the first
        // statement even though its `stw` is emitted second.
        g_ColorNoFrameDataCnt = 0;
        g_startMetering = true;
    }
    void *texels = nullptr;
    mTex->TexelsLock(texels);
    // uintptr_t: see the note in UpdateFromColorBuffer. Same width as
    // `unsigned int` on PPC, 64-bit on the native LP64 build.
    uintptr_t destPtr = (uintptr_t)texels;
    void *bufferData = cam->StreamBufferData(kBufferColor);
    if (bufferData) {
        LockedRect lockedRect;
        cam->LockStream(bufferData, lockedRect);
        g_colorBufferUpdate4++;
        // mBits gets its OWN statement so its load lands before the
        // TexelsPitch() vcall: the image reads both LockedRect fields
        // (0x824309C8 `lwz r10, 0x5c(r1)`, 0x824309DC `lwz r11, 0x58(r1)`)
        // and computes srcOffset (0x824309FC `add r30, r11, r10`) BEFORE the
        // `bctrl` at 0x82430A00.  Left inside the srcOffset expression, mBits
        // sinks past the call and drags the add with it.
        int destWidth = mTex->Width();
        unsigned int srcPitch = lockedRect.mPitch >> 2;
        uintptr_t srcBits = (uintptr_t)lockedRect.mBits;
        unsigned int *srcPtr = (unsigned int *)(srcPitch * startY * 4 + srcBits);
        unsigned int destPitch = mTex->TexelsPitch();
        int destStride = (int)((destPitch >> 1) - destWidth);
        int srcStride = (int)(srcPitch - 320);
        for (int row = 0; row < mTex->Height(); row++) {
            for (int col = 0; col < 320; col++) {
                unsigned int pixel = *srcPtr++;
                if (col >= startX / 2 && col < (mTex->Width() + startX) / 2) {
                    int cr = (pixel >> 24) - 0x80;
                    int cb = (pixel >> 8 & 0xff) - 0x80;
                    *(unsigned short *)destPtr = YUVtoRGB(pixel >> 16 & 0xff, cr, cb);
                    destPtr += 2;
                    *(unsigned short *)destPtr = YUVtoRGB(pixel & 0xff, cr, cb);
                    destPtr += 2;
                }
            }
            destPtr += destStride * 2;
            srcPtr += srcStride;  // unsigned subtraction, converted to int (see UpdateFromColorBuffer)
        }
        D3DTexture_UnlockRect((D3DTexture *)bufferData, 0);
    }
}

// RESIDUAL (w8-r, 98.182, 440 B, 22 of 111 rows) -- ONE callee-saved
// colouring decision and nothing else.  The image puts clippedX in r27
// (`subf r27, r11, r10`, the %640 remainder) and srcPitch in r28
// (`srwi r28, r8, 1`); we colour them the other way round, and every later row
// -- r8<->r9 and r10<->r11 in the inner pixel loop, the `mullw`, the `lhzx`,
// the `sthu` -- inherits the swap.  The one non-rename row is the `mr r11,
// r27` copy of clippedX, which the image emits BEFORE the `add`/`cmpw` pair
// that bounds the row and we emit four instructions later.  No address,
// constant or value differs on either side.
// w17-c: the callee-saved colouring above came from the hand-rotated
// `if (n) do {} while` loops and the pre-decremented destRow.  Written as two
// plain for loops (MSVC rotates them and forms the same sthu itself) the
// r27/r28 swap and the misplaced `mr r11, r27` are gone; what is left on a
// non-PCH probe is one volatile r8/r9 swap in the inner pixel loop.
// depthPixel is an unsigned int holding the zero-extended halfword, not an
// unsigned short (same values: it is only masked and shifted).  As a short the
// pixel was a short-lived temp (pri 8, tie 77) that lost to destRow (pri 8,
// tie 97), so destRow took r9 and the pixel r8, the reverse of the image.  As
// an int it is a named candidate at pri 13, coloured first, and takes r9.
// Storing in each arm instead of via `color`: 94.3.
void LiveCameraInput::TextureStore::UpdateFromDepthBufferClip(
    LiveCameraInput *cam, float clipLeft, float clipTop
) {
    void *texels = nullptr;
    int clippedX = (1 - (int)(clipLeft * -640.0f)) & 0xfffe;
    clippedX = clippedX % 640;
    mTex->TexelsLock(texels);
    uintptr_t destBase = (uintptr_t)texels;
    void *bufferData = cam->StreamBufferData(kBufferDepth);
    if (bufferData) {
        LockedRect lockedRect;
        cam->LockStream(bufferData, lockedRect);
        unsigned int srcPitch = lockedRect.mPitch >> 1;
        int clippedY = (1 - (int)(clipTop * -480.0f)) & 0xfffe;
        clippedY = clippedY % 480;
        uintptr_t srcBase = (clippedY / 2) * srcPitch * 2 + (uintptr_t)lockedRect.mBits;
        for (int rowIdx = 0; rowIdx < mTex->Height(); rowIdx++) {
            unsigned short *destRow = (unsigned short *)destBase;
            for (int x = clippedX; x < mTex->Width() + clippedX; x++) {
                    unsigned short color = 0;
                    unsigned int depthPixel = *(unsigned short *)((x / 2) * 2 + srcBase);
                    if (depthPixel & 3) {
                        int depth = 0x1f - ((depthPixel >> 10) & 0x1f);
                        color = (((depth << 5) | depth) << 6) | depth;
                    }
                    *destRow++ = color;
            }
            unsigned int pitch = mTex->TexelsPitch();
            destBase += (pitch & 0xfffffffe);
            if ((rowIdx & 1) != 0) {
                srcBase += srcPitch * 2;
            }
        }
        D3DTexture_UnlockRect((D3DTexture *)bufferData, 0);
    }
}

#pragma endregion
#pragma region LiveCameraInput

// w21-bd (98.908 -> 100, all 380 rows equal, name_check too).  Three levers:
//  * CamTexClip has an inline default ctor (mTex = nullptr), so mTexClips is
//    built by MSVC's member-init phase (the image's unfolded
//    `addi r10, r30, 0x1224` / `subi r10, r10, 0x4` CTR loop) and mSpeechMgr's
//    single store follows it from the init list; the body loop and the body
//    `mSpeechMgr = nullptr` (the duplicate store w7-aa measured) are gone.
//  * mNumSnapshots = 0 written LAST before clear(): MSVC emits the last of the
//    four member stores first, giving the image's 1200,14a8,14ac,14b0.
//  * mFrames[] cleared by an inner 2-trip loop: MSVC hands CTR to the inner
//    loop and unrolls it, leaving the outer stream loop on the image's GPR
//    down-counter (li 4 / subic. / bne, +0xc-biased pointer).  Not an
//    artifact after all (w20-p).  Same pattern: FreestyleMoveRecorder::
//    ClearFrameScores.
// Behaviour identical to before on every path.
LiveCameraInput::LiveCameraInput()
    : mConnected(true), mColorPolled(0), mDepthPolled(0), mColorReceived(0), mDepthReceived(0), mSpeechMgr(0) {
    // w7-aa's duplicate-mSpeechMgr analysis is superseded by the CamTexClip ctor (w21-bd).
    mColorStreamTex = 0;
    mDepthStreamTex = 0;
    mDebugDepthTex = 0;
    mNumSnapshots = 0;
    mSnapshotBatches.clear();
    mNumSnapshots = 0;
    SkeletonUpdate::Init();
    // BEHAVIOURAL FIX (w8-r): SpeechMgr is constructed from the "speech"
    // SUB-ARRAY, not from the whole "kinect" array.  The image keeps
    // FindArray("speech")'s result in r25 (`mr r25, r3` at 0x82432E78) and
    // that is the register it passes to SpeechMgr::SpeechMgr -- `mr r4, r25`
    // at 0x82432FA8, the instruction before the
    // `bl ??0SpeechMgr@@QAA@PBVDataArray@@@Z` at 0x82432FAC.  kinectArr lives
    // in r23 (`mr. r23, r3` at 0x82432E44) and is never passed there.  LiveCameraInput::Init corroborates it: the image
    // feeds SpeechMgr::InitGrammars the same speech array.
    //
    // `speechArr` is also declared OUTSIDE the `if` and deliberately left
    // uninitialised, because the image reads it back on the !kinectArr path:
    // 0x82432EBC is `lwz r25, 0x54(r31)`, and 0x54(r31) is the slot the
    // "speech" Symbol temp occupies (`addi r3, r31, 0x54` at 0x82432E54),
    // i.e. the stack packer shares it with speechArr's home.  The read is harmless in the
    // shipped game -- b17 can only be true when kinectArr is non-null -- but
    // it is what buys the `b` over the reload at target indices 110/111, and
    // w7-bp's reading of those two rows as "the image spills and reloads a
    // value across that join" is hereby RETRACTED: it is an uninitialised
    // local, the same shape as LiveCameraInput::Init.
    DataArray *speechArr;
#ifdef HX_NATIVE
    speechArr = nullptr;
#endif
    DataArray *kinectArr = SystemConfig()->FindArray("kinect", false);
    bool b17 = false;
    if (kinectArr) {
        speechArr = kinectArr->FindArray("speech");
        b17 = speechArr->FindArray("enabled")->Int(1);
    }
    // w7-bp/w7-bs/w20-p read this loop's GPR down-counter (vs our mtctr/bdnz)
    // as backend-only; it was the inner mFrames loop below (w21-bd).
    for (int i = 0; i < kBufferNum; i++) {
        Buffer &cur = mStreams[i];
        cur.mHandle = nullptr;
        for (int j = 0; j < 2; j++) {
            cur.mFrames[j] = nullptr;
        }
        cur.mWriteIdx = 0;
        cur.mReadIdx = 1;
        cur.mMat = nullptr;
    }
    int initFlags = 0x4049;
    if (!UsingCD()) {
        initFlags = 0x40004049;
    }
    BeginMemTrackObjectName("NuiInitialize");
    HRESULT initRes = NuiInitialize(initFlags, -1);
    EndMemTrackObjectName();
    if (initRes == E_NUI_DATABASE_NOT_FOUND) {
        MILO_NOTIFY(
            "Could not find NUI database.  Do you have Map DVD Drive enabled in Visual Studio?"
        );
    }
    MILO_ASSERT_FMT(SUCCEEDED(initRes), "NuiInitialize failed (0x%x)", initRes);
    if (b17) {
        mSpeechMgr = new SpeechMgr(speechArr);
    }
    mAudioInitialized = 0;
    if (SUCCEEDED(NuiAudioCreate(5, NuiAudioErrorCallback, 1, &mAudioHandle, nullptr))) {
        NuiAudioRegisterCallbacks(&mAudioHandle, 1, NuiAudioDataCallback);
        mAudioInitialized = 1;
    }
    bool i6 = kinectArr->FindArray("title_tracked_skeletons")->Int(1);
    HANDLE new_skeleton_event = SkeletonUpdate::NewSkeletonEvent();
    MILO_ASSERT(new_skeleton_event, 0x14D);
    MILO_ASSERT_FMT(
        SUCCEEDED(NuiSkeletonTrackingEnable(new_skeleton_event, i6 ? 2 : 0)),
        "NuiSkeletonTrackingEnable failed"
    );
    BeginMemTrackObjectName("NuiImageStreamOpen:color");
    MILO_ASSERT_FMT(
        SUCCEEDED(NuiImageStreamOpen(
            NUI_IMAGE_TYPE_COLOR_YUV,
            NUI_IMAGE_RESOLUTION_640x480,
            0,
            2,
            nullptr,
            &mStreams[kBufferColor].mHandle
        )),
        "NuiImageStreamOpen color failed"
    );
    EndMemTrackObjectName();
    mStreams[kBufferColor].mMat = CreateCameraBufferMat(640, 480, RndTex::kScratch);
    BeginMemTrackObjectName("NuiImageStreamOpen:depth");
    MILO_ASSERT_FMT(
        SUCCEEDED(NuiImageStreamOpen(
            NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE,
            NUI_IMAGE_RESOLUTION_320x240,
            0,
            2,
            0,
            &mStreams[kBufferDepth].mHandle
        )),
        "NuiImageStreamOpen depth failed"
    );
    EndMemTrackObjectName();
    mStreams[kBufferDepth].mMat = CreateCameraBufferMat(320, 240, RndTex::kScratch);
    mStreams[kBufferPlayer].mMat = CreateCameraBufferMat(320, 240, RndTex::kScratch);
    mStreams[kBufferPlayerColor].mMat =
        CreateCameraBufferMat(640, 480, RndTex::kScratch);
    RELEASE(mColorStreamTex);
    mColorStreamTex = Hmx::Object::New<DxTex>();
    RELEASE(mDepthStreamTex);
    mDepthStreamTex = Hmx::Object::New<DxTex>();
    DataArray *maxArr = kinectArr->FindArray("camera")->FindArray("max_snapshots", false);
    if (maxArr) {
        mMaxSnapshots = maxArr->Int(1);
    } else {
        MILO_NOTIFY("Could not find max_snapshots in SystemConfig");
        mMaxSnapshots = 1;
    }
    mSnapshotBatches.reserve(6);
    SetColorCameraProperty(NUI_CAMERA_PROPERTY_AE_AWB_MODE, 1);
}

LiveCameraInput::~LiveCameraInput() {
    SkeletonUpdate::Terminate();
    for (int i = 0; i < 4; i++) {
        RndMat *curMat = mStreams[i].mMat;
        RndTex *diffuseTex = curMat ? curMat->GetDiffuseTex() : nullptr;
        delete diffuseTex;
        delete curMat;
        if (mStreams[i].mHandle) {
            CloseHandle(mStreams[i].mHandle);
        }
    }
    ClearSnapshots();
    if (mAudioInitialized) {
        NuiAudioUnregisterCallbacks(&mAudioHandle, NuiAudioDataCallback);
        NuiAudioRelease(&mAudioHandle);
    }
    delete mSpeechMgr;
    NuiShutdown();
}

void LiveCameraInput::PollTracking() {
    mColorPolled = false;
    mDepthPolled = false;
    mColorReceived = false;
    mDepthReceived = false;
    PollNewStream(kBufferColor);
    CameraInput::PollTracking();
}

DataNode OnCameraDumpUnique(DataArray *);
DataNode OnCameraDebugDepth(DataArray *);

void LiveCameraInput::PreInit() {
    if (!sInstance) {
        sInstance = new LiveCameraInput();
        SkeletonUpdate::CreateInstance();
        TheDebug.AddExitCallback(LiveCameraInput::Terminate);
        DataRegisterFunc("camera_dump", OnCameraDumpUnique);
        DataRegisterFunc("camera_debug_depth", OnCameraDebugDepth);
        LoadDebugDepthBuffer(sInstance->mDebugDepthTex);
    }
}

void LiveCameraInput::Init() {
    PreInit();
    if (sInstance) {
        // `speechArr` is deliberately left uninitialised on the no-"kinect"
        // path: the image reads it back from its home slot there.  At
        // 0x8243390C the `FindArray("kinect")==0` arm is `lwz r31, 0x54(r1)`
        // -- 0x54(r1) is the slot the *"speech" Symbol temp* occupies
        // (`addi r3, r1, 0x54` at 0x824338AC), i.e. the stack packer shares it
        // with speechArr's home and the image passes whatever is there to
        // InitGrammars.  The old spelling assigned `cfg = speechArr` inside the
        // block and passed `cfg`, which gives the memory phi no reason to
        // exist.  Reproduced for the match; nulled on native so the port does
        // not dereference a garbage DataArray.
        DataArray *speechArr;
#ifdef HX_NATIVE
        speechArr = nullptr;
#endif
        DataArray *cfg = SystemConfig()->FindArray("kinect", false);
        if (cfg) {
            speechArr = cfg->FindArray("speech");
            speechArr->FindInt("enabled");
        }
        if (sInstance->mSpeechMgr) {
            sInstance->mSpeechMgr->InitGrammars(speechArr);
        }
    }
}

void LiveCameraInput::Terminate() {
    if (!sInstance) {
        MILO_ASSERT(sInstance, 0xE2);
    }
    RELEASE(sInstance);
}

RndMat *LiveCameraInput::GetSnapshot(int idx) const {
    if ((idx < 0 || idx >= mNumSnapshots) && mNumSnapshots > 0) {
        MILO_LOG("Snapshot index %d out of bounds [0-%d].", idx, mNumSnapshots);
    } else if (idx < mSnapshots.size()) {
        return mSnapshots[idx];
    }
    return nullptr;
}

int LiveCameraInput::GetSnapshotBatchStartingIndex(int idx) const {
    return idx >= mSnapshotBatches.size() ? mNumSnapshots : mSnapshotBatches[idx];
}

RndTex *LiveCameraInput::GetStoredTexture(int idx) const {
    if (idx >= 0 && idx < mTextureStore.size()) {
        return mTextureStore[idx].mTex;
    } else {
        // lol did they forget the other %d
        MILO_LOG(
            "LiveCameraInput::GetStoredTexture: index %d out of bounds [max=%d]\n",
            mTextureStore.size() - 1
        );
        return nullptr;
    }
}

void LiveCameraInput::InitSnapshots(int numSnapshots) {
    ClearSnapshots();
    if (numSnapshots > 0) {
        MILO_ASSERT(numSnapshots <= mMaxSnapshots, 0x193);
        if (mSnapshots.size() != numSnapshots) {
            mSnapshots.resize(numSnapshots);
            for (int i = 0; i < numSnapshots; i++) {
                mSnapshots[i] = CreateCameraBufferMat(640, 480, RndTex::kRendered);
            }
        }
    }
}

void LiveCameraInput::ClearSnapshots() {
    for (unsigned int i = 0; i < mSnapshots.size(); i++) {
        RndMat *mat = mSnapshots[i];
        RndTex *diffuseTex = mat ? mat->GetDiffuseTex() : nullptr;
        if (diffuseTex) {
            delete diffuseTex;
        }
        if (mat) {
            delete mat;
        }
    }
    // clear() -- the inlined erase(begin(), end()) reads both ends off the
    // vector's own this (r31), where a spelled-out erase reloads end() via
    // the outer this.
    mSnapshots.clear();
    mNumSnapshots = 0;
    // Bind mSnapshotBatches to a reference so its address is materialized once,
    // matching the target's register selection for the erase() call.
    std::vector<int> &batches = mSnapshotBatches;
    batches.erase(batches.begin(), batches.end());
    TheDxRnd.ReleaseAutoRelease();
}

void LiveCameraInput::InitTextureStore(int max) {
    mMaxTextures = max;
    ClearTextureStore();
}

void LiveCameraInput::ClearTextureStore() {
    unsigned int i = 0;
    while (i < mTextureStore.size()) {
        TextureStore *data = mTextureStore.begin();
        if (data[i].mTex) {
            delete data[i].mTex;
            data[i].mTex = null;
        }
        data[i].mTex = null;
        i++;
    }
    mTextureStore.clear();
    mTextureStore.resize(mMaxTextures);
    mNumStoredTextures = 0;
}

void LiveCameraInput::StartSnapshotBatch() { mSnapshotBatches.push_back(mNumSnapshots); }

void LiveCameraInput::StoreTextureClipAt(
    float f1, float f2, float f3, float f4, int idx1, int idx2
) {
    if (idx1 >= 0 && idx1 < mTextureStore.size()) {
        mTexClips[idx2].StoreTextureClip(mTextureStore[idx1].mTex, f1, f2, f3, f4);
    } else {
        MILO_LOG(
            "LiveCameraInput::StoreColorBufferClip: index %d out of bounds [max=%d]\n",
            idx1,
            mTextureStore.size() - 1
        );
    }
}

void LiveCameraInput::ResetSnapshots() {
    mNumSnapshots = 0;
    mSnapshotBatches.clear();
}

void LiveCameraInput::SetNewFrame(const SkeletonFrame *frame) {
    mNewFrame = frame;
    mCachedFrame = *frame;
}

int LiveCameraInput::NumSnapshots() const { return mNumSnapshots; }
int LiveCameraInput::NumStoredTextures() const { return mNumStoredTextures; }

void LiveCameraInput::PollNewStream(BufferType buf) {
    MILO_ASSERT(kBufferColor == buf || kBufferDepth == buf, 0x227);
    MILO_ASSERT_RANGE(buf, 0, DIM(mStreams), 0x22C);
    Buffer &curBuf = mStreams[buf];
    if (curBuf.mHandle) {
        HRESULT hr =
            NuiImageStreamGetNextFrame(curBuf.mHandle, 0, &curBuf.mFrames[curBuf.mWriteIdx]);
        if (buf == kBufferColor) {
            g_ColorPollCnt++;
            mColorPolled = true;
        } else {
            mDepthPolled = true;
        }
        if (SUCCEEDED(hr)) {
            if (buf == kBufferColor) {
                mColorReceived = true;
            } else {
                mDepthReceived = true;
            }
            mConnected = true;
            mColorStreamTex->SetDeviceTex(nullptr);
            mDepthStreamTex->SetDeviceTex(nullptr);
            if (curBuf.mFrames[curBuf.mReadIdx]) {
                HRESULT hr =
                    NuiImageStreamReleaseFrame(curBuf.mHandle, curBuf.mFrames[curBuf.mReadIdx]);
                MILO_ASSERT(SUCCEEDED(hr), 0x24E);
                curBuf.mFrames[curBuf.mReadIdx] = 0;
            }
            curBuf.mWriteIdx = curBuf.mWriteIdx - 1U & 1;
            curBuf.mReadIdx = curBuf.mReadIdx - 1U & 1;
        } else if (hr == E_NUI_DEVICE_NOT_CONNECTED) {
            mConnected = false;
        } else if (hr == (HRESULT)0x83010001 && buf == kBufferColor) {
            g_ColorNoFrameDataCnt++;
        }
    }
}

// BEHAVIOURAL FIX (w8-r): the previous body indexed mStreams[type] and read
// mFrames[i3] with a *constant* i3.  The image (0x8242F7F0..0x8242F824) does
// neither.  It remaps the buffer type to a STREAM index -- kBufferPlayer(2)
// reads the depth stream, kBufferPlayerColor(3) reads the colour stream, i.e.
// `idx = type==2 ? 1 : (type==3 ? 0 : type)`, the `type != 3` arm lowered as
// the subfic/subfe bool mask at 0x8242F804-0x8242F80C and `and r11, r11, r31`
// -- and then reads mFrames[mReadIdx], the same double-buffer read index
// PollNewStream maintains.  The old spelling read mStreams[2]/mStreams[3],
// which PollNewStream never fills (it asserts type is colour or depth), so
// StreamBufferData(kBufferPlayer) always returned null; and for colour/depth
// it pinned mFrames[0] instead of following mReadIdx, so it returned the
// wrong half of the double buffer on every other frame.
void *LiveCameraInput::StreamBufferData(BufferType type) const {
    MILO_ASSERT(type < kBufferNum, 0x1FC);
    // The player buffers have no stream of their own: kBufferPlayer reads the
    // DEPTH stream (player index lives in the depth frame) and
    // kBufferPlayerColor the COLOR stream.  The image selects the stream as
    //   type == 2 ? 1 : (subfic/subfe mask of type != 3) & type
    // and then reads that stream's frame at mReadIdx (`lwz r11, 0x1458(r11)`,
    // Buffer+0x10).  We indexed mStreams[type] -- whose player entries are
    // never filled -- with a 0/1 derived from the type, so the player
    // buffers always came back null and the colour/depth ones ignored
    // mReadIdx (the double-buffer slot PollNewStream just released).
    BufferType stream;
    if (type == kBufferPlayer) {
        stream = kBufferDepth;
    } else {
        stream = type == kBufferPlayerColor ? kBufferColor : type;
    }
    const Buffer &buf = mStreams[stream];
    const NUI_IMAGE_FRAME *frame = buf.mFrames[buf.mReadIdx];
    if (frame) {
        return frame->pFrameTexture;
    } else {
        return nullptr;
    }
}

RndMat *LiveCameraInput::DisplayMat(BufferType type) const {
    MILO_ASSERT(type < kBufferNum, 0x20F);
    return mStreams[type].mMat;
}

RndTex *LiveCameraInput::DisplayTex(BufferType type) const {
    RndMat *mat = DisplayMat(type);
    if (mat) {
        return mat->GetDiffuseTex();
    } else {
        return nullptr;
    }
}

void LiveCameraInput::DumpProperties() {
    MILO_LOG("NUI CAMERA PROPERTIES ****************************************\n");
    union {
        LONG lValue;
        FLOAT fValue;
    };
    NuiCameraGetProperty(NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTY_AE_AWB_MODE, &lValue);
    MILO_LOG("%s:\n", "NUI_CAMERA_PROPERTY_AE_AWB_MODE");
    if (lValue == NUI_CAMERA_PROPERTY_AE_AWB_MODE_STANDARD) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_AWB_MODE_STANDARD");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_AWB_MODE_FACEBASED) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_AWB_MODE_FACEBASED");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_AWB_MODE_OFF) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_AWB_MODE_OFF");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_AWB_MODE_DEFAULT) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_AWB_MODE_DEFAULT");
    }
    NuiCameraGetProperty(
        NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTY_FRAME_RATE_MAX, &lValue
    );
    MILO_LOG("%s:\n", "NUI_CAMERA_PROPERTY_FRAME_RATE_MAX");
    if (lValue == NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_30FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_30FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_15FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_15FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_10FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_FRAME_RATE_MAX_10FPS");
    }
    NuiCameraGetProperty(
        NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN, &lValue
    );
    MILO_LOG("%s:\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN");
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_30FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_30FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_15FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_15FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_10FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_10FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_LOWEST) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN_LOWEST");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MODE_MIN_15FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MODE_MIN_15FPS");
    }
    if (lValue == NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MODE_MIN_10FPS) {
        MILO_LOG("      %s\n", "NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MODE_MIN_10FPS");
    }
    NuiCameraGetPropertyF(
        NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTYF_EXPOSURE_TIME, &fValue
    );
    MILO_LOG("%s:\t\t%.4f\n", "NUI_CAMERA_PROPERTYF_EXPOSURE_TIME", fValue);
    NuiCameraGetPropertyF(
        NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTYF_AE_EXPOSURE_COMPENSATION, &fValue
    );
    MILO_LOG("%s:\t\t%.4f\n", "NUI_CAMERA_PROPERTYF_AE_EXPOSURE_COMPENSATION", fValue);
    NuiCameraGetPropertyF(NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTYF_COLOR_GAIN, &fValue);
    MILO_LOG("%s:\t\t%.4f\n", "NUI_CAMERA_PROPERTYF_COLOR_GAIN", fValue);
    NuiCameraGetPropertyF(
        NUI_CAMERA_TYPE_COLOR, NUI_CAMERA_PROPERTYF_AE_FACEBASED_MAX_GAIN, &fValue
    );
    MILO_LOG("%s:\t\t%.4f\n", "NUI_CAMERA_PROPERTYF_AE_FACEBASED_MAX_GAIN", fValue);
    NUI_CAMERA_AE_ROI region;
    HRESULT hr = NuiCameraGetExposureRegionOfInterest(NUI_CAMERA_TYPE_COLOR, &region);
    if (hr != ERROR_SUCCESS) {
        MILO_LOG("NuiCameraGetExposureRegionOfInterest returned bad result.\n");
    } else {
        MILO_LOG(
            "Region of Interest: left(%.3f) top(%.3f) width(%.3f) height(%.3f)\n",
            region.Left,
            region.Top,
            region.Width,
            region.Height
        );
    }
    MILO_LOG("**************************************************************\n");
}

void LiveCameraInput::IncrementSnapshotCount() {
    if (mNumSnapshots < mSnapshots.size()) {
        mNumSnapshots++;
    } else {
        MILO_NOTIFY("Max snapshots already taken.");
    }
}

int LiveCameraInput::NumSnapshotBatches() const { return mSnapshotBatches.size(); }

void LiveCameraInput::NuiAudioErrorCallback(HRESULT hr) {
    if (hr != ERROR_SUCCESS) {
        MILO_NOTIFY("NuiAudioErrorCallback reached (0x%x)", hr);
    }
}

// SpeechMgr::mVoiceDirection — use public getter/setter instead of raw byte offset

void LiveCameraInput::NuiAudioDataCallback(NUIAUDIO_RESULTS *results) {
    LiveCameraInput *inst = sInstance;
    if (!inst)
        return;
    SpeechMgr *mgr = inst->mSpeechMgr;
    if (!mgr)
        return;
    if (!mgr->Recognizing())
        return;

    float confidence = results->Confidence;
    float beamAngle = results->BeamAngle;
    if (confidence > 0.2f) {
        inst->mBeamAngle = beamAngle;
        inst->mBeamConfidence = confidence;
        side = (int)(beamAngle / Abs(beamAngle)) + side;
        if (side > 10) {
            side = 10;
        } else if (side < -10) {
            side = -10;
        }
    } else if (side != 0) {
        int absVal = side < 0 ? -side : side;
        side = side - side / absVal;
    }

    // One call per arm. The compiler tail-merges the two arms into a single
    // `lwz r11, 0x1444(r8)` / `stw r10, 0x44(r11)` join, as in the image. The
    // direction constant is then a short-lived temp assigned at that join
    // (the reload takes r11, the value r10). A named `direction` local
    // instead becomes a colour candidate (pri 4, tie 63) that takes r11 first.
    if (side == 10) {
        inst->mSpeechMgr->SetVoiceDirection(0);
    } else if (side == -10) {
        inst->mSpeechMgr->SetVoiceDirection(1);
    }
}

bool LiveCameraInput::SetAutoexposure(bool enable) {
    SetColorCameraProperty(NUI_CAMERA_PROPERTY_AE_AWB_MODE, enable ? 1 : 0);
    return enable;
}

bool LiveCameraInput::GetAutoexposure() const {
    long val = GetColorCameraProperty(NUI_CAMERA_PROPERTY_AE_AWB_MODE);
    return val == 1;
}

bool LiveCameraInput::SetTweakedAutoexposure(bool enable) {
    SetColorCameraProperty(NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN, enable ? 2 : 0);
    NUI_CAMERA_AE_ROI region;
    region.Left = 0.0f;
    region.Top = 0.0f;
    region.Width = 1.0f;
    region.Height = 1.0f;
    if (enable && !GetExposureRegion(region)) {
        MILO_NOTIFY("Could not find ae_region in SystemConfig");
    }
    HRESULT hr = NuiCameraSetExposureRegionOfInterest(NUI_CAMERA_TYPE_COLOR, &region);
    if (!SUCCEEDED(hr)) {
        TheDebug << MakeString(
            "Autoexposure region not set. NuiCameraSetExposureRegionOfInterest failed (0x%x)\n",
            hr
        );
    }
    return enable;
}

bool LiveCameraInput::GetTweakedAutoexposure() const {
    bool frameRateOk = GetColorCameraProperty(NUI_CAMERA_PROPERTY_AE_FRAME_RATE_MIN) == 2;
    NUI_CAMERA_AE_ROI currentRegion;
    unsigned int hr = NuiCameraGetExposureRegionOfInterest(NUI_CAMERA_TYPE_COLOR, &currentRegion);
    if (!SUCCEEDED((HRESULT)hr)) {
        MILO_FAIL("NuiCameraGetExposureRegionOfInterest failed (0x%x)", hr);
    }
    NUI_CAMERA_AE_ROI configRegion;
    if (!GetExposureRegion(configRegion)) {
        return false;
    }
    // ONE && chain, not five `if (!x) return false;` statements.  The image
    // gives the GetExposureRegion test its own `li r3, 0x0` + branch to the
    // epilogue (0x82430330), then funnels every later failure into a SINGLE
    // `li r11, 0x0` at 0x824303E4 while the success path materialises
    // `li r11, 0x1` at 0x824303E0 -- the short-circuit shape.  Separate early
    // returns give each test its own zero block and drop the final 0/1
    // normalisation (`clrlwi. ; li 1 ; bne ; li 0 ; clrlwi r3`).
    return frameRateOk && NearlyEqual(currentRegion.Left, configRegion.Left)
        && NearlyEqual(currentRegion.Top, configRegion.Top)
        && NearlyEqual(currentRegion.Width, configRegion.Width)
        && NearlyEqual(currentRegion.Height, configRegion.Height);
}

#define NUI_CAMERA_AE_ROI_MINIMUM_WIDTH 0.15f
#define NUI_CAMERA_AE_ROI_MINIMUM_HEIGHT 0.15f

void LiveCameraInput::SetExposureRegion(float left, float top, float width, float height) {
    MILO_ASSERT(width >= NUI_CAMERA_AE_ROI_MINIMUM_WIDTH, 0x41f);
    MILO_ASSERT(height >= NUI_CAMERA_AE_ROI_MINIMUM_HEIGHT, 0x420);
    MILO_ASSERT(left >= 0 && top >= 0 && width >= 0 && height >= 0, 0x421);
    MILO_ASSERT(left + width <= 1.0f, 0x422);
    MILO_ASSERT(top + height <= 1.0f, 0x423);
    NUI_CAMERA_AE_ROI region;
    region.Left = left;
    region.Top = top;
    region.Width = width;
    region.Height = height;
    HRESULT hr = NuiCameraSetExposureRegionOfInterest(NUI_CAMERA_TYPE_COLOR, &region);
    if (!SUCCEEDED(hr)) {
        MILO_NOTIFY(
            "Autoexposure region not set. NuiCameraSetExposureRegionOfInterest failed (0x%x)",
            hr
        );
    }
}

void LiveCameraInput::SetTrackedSkeletons(int id1, int id2) const {
    DWORD ids[2];
    ids[0] = id1;
    ids[1] = id2;
    NuiSkeletonSetTrackedSkeletons(ids);
}

int LiveCameraInput::StoreTexture(RndTex *tex) {
    if (mNumStoredTextures >= mMaxTextures) {
        MILO_ASSERT(mNumStoredTextures==mTextureStore.size(), 799);
        MILO_LOG(
            "LiveCameraInput::AddTextureToStore: No room available. Max textures=\n",
            mMaxTextures
        );
        return 0;
    } else {
        for (int i = 0; i < mTextureStore.size(); i++) {
            if (!mTextureStore[i].mTex) {
                mTextureStore[i].mTex = tex;
                return i;
            }
        }
        mTextureStore[mNumStoredTextures++].StoreTexture(tex);
        return mNumStoredTextures - 1;
    }
}

void LiveCameraInput::StoreTextureAt(RndTex *tex, int idx) {
    if (idx >= 0 && idx < mMaxTextures) {
        mTextureStore[idx].StoreTexture(tex);
    } else {
        // i think they forgot the second %d
        MILO_LOG(
            "LiveCameraInput::StoreTextureAt: index %d out of bounds [max=%d]\n",
            mTextureStore.size() - 1
        );
    }
}

void LiveCameraInput::ApplyTextureClip(RndMat *mat, int idx) const {
    if (idx < 0 || idx >= 8) {
        MILO_LOG(
            "LiveCameraInput::GetStoredTexture: index %d out of bounds [max=%d]\n", 7
        );
    }
    const CamTexClip *clip = &mTexClips[idx];
    mat->SetTexGen(kTexGenXfmOrigin);
    mat->SetTexXfm(clip->mXfm);
    mat->SetDiffuseTex(clip->mTex);
}

void LiveCameraInput::StoreColorBuffer(int idx) {
    if (idx >= 0 && idx < mTextureStore.size()) {
        mTextureStore[idx].StoreColorBuffer(this);
    } else {
        MILO_LOG(
            "LiveCameraInput::StoreColorBuffer: index %d out of bounds [max=%d]\n",
            idx,
            mTextureStore.size() - 1
        );
    }
}

void LiveCameraInput::StoreColorBufferClip(
    float f1, float f2, float f3, float f4, int idx
) {
    if (idx >= 0 && idx < mTextureStore.size()) {
        mTextureStore[idx].StoreColorBufferClip(this, f1, f2, f3, f4);
    } else {
        MILO_LOG(
            "LiveCameraInput::StoreColorBufferClip: index %d out of bounds [max=%d]\n",
            idx,
            mTextureStore.size() - 1
        );
    }
}

void LiveCameraInput::StoreDepthBuffer(int idx) {
    if (idx >= 0 && idx < mTextureStore.size()) {
        mTextureStore[idx].StoreDepthBuffer(this);
    } else {
        MILO_LOG(
            "LiveCameraInput::StoreDepthBufferAt: index %d out of bounds [max=%d]\n",
            idx,
            mTextureStore.size() - 1
        );
    }
}

void LiveCameraInput::StoreDepthBufferClip(
    float f1, float f2, float f3, float f4, int idx
) {
    if (idx >= 0 && idx < mTextureStore.size()) {
        mTextureStore[idx].StoreDepthBufferClip(this, f1, f2, f3, f4);
    } else {
        MILO_LOG(
            "LiveCameraInput::StoreDepthBufferClip: index %d out of bounds [max=%d]\n",
            idx,
            mTextureStore.size() - 1
        );
    }
}

RndTex *LiveCameraInput::GetStreamTex(BufferType type) const {
    MILO_ASSERT(type == kBufferColor || type == kBufferDepth, 0x1EA);
    void *bufferData = StreamBufferData(type);
    DxTex *tex;
    if (type == kBufferColor) {
        tex = mColorStreamTex;
    } else {
        tex = mDepthStreamTex;
    }
    tex->SetDeviceTex((D3DTexture *)bufferData);
    RndTex *result = tex;
    if (type == kBufferDepth) {
        if (mDebugDepthTex != nullptr && (bufferData == nullptr || gDebugDepth != 0)) {
            result = mDebugDepthTex;
        }
    }
    return result;
}

#pragma endregion
#pragma region AnonymousNamespace

namespace {

long GetColorCameraProperty(NUI_CAMERA_PROPERTY prop) {
#ifdef HX_NATIVE
    long result = 0;
#else
    long result; // not zeroed in the target; NuiCameraGetProperty writes it
    HRESULT hr = NuiCameraGetProperty(NUI_CAMERA_TYPE_COLOR, prop, &result);
    if (!SUCCEEDED(hr)) {
        TheDebug << MakeString(
            "NuiCameraGetProperty failed.  Property 0x%x, error (0x%x)\n", prop, hr
        );
    }
#endif
    return result;
}

bool GetExposureRegion(NUI_CAMERA_AE_ROI &region) {
    static Symbol kinect("kinect");
    static Symbol camera("camera");
    static Symbol ae_region("ae_region");
    DataArray *arr = SystemConfig(kinect, camera)->FindArray(ae_region, false);
    if (arr) {
        region.Left = arr->Float(1);
        region.Top = arr->Float(2);
        region.Width = arr->Float(3);
        region.Height = arr->Float(4);
    }
    return arr != nullptr;
}

} // namespace

// LockStream / UnlockStream are NOT platform-specific: they are the recovered
// PPC bodies (100% match, 25 and 6 instructions) and they only call through the
// D3D texture-lock entry points, which the native build shims. They used to sit
// inside the `#else` arm below, which is why `ldd -r dc3-native` reported
// LiveCameraInput::LockStream / UnlockStream as undefined -- the eight call
// sites in UpdateFromColorBuffer / UpdateFromDepthBuffer / the two Clip
// variants / UpdateBufferTex all pointed at a JUMP_SLOT relocated to 0.
void LiveCameraInput::LockStream(const void *buf, LockedRect &rect) {
    D3DLOCKED_RECT d3dRect;
    if (buf) {
        D3DLineTexture_LockRect((D3DLineTexture *)buf, 0, &d3dRect, nullptr, 0x10);
        rect.mBits = d3dRect.pBits;
    } else {
        d3dRect.Pitch = 0;
        rect.mBits = nullptr;
    }
    rect.mPitch = d3dRect.Pitch;
}

void LiveCameraInput::UnlockStream(const void *buf) {
    if (buf != nullptr) {
        D3DLineTexture_UnlockRect((D3DLineTexture *)buf, 0);
    }
}

#ifdef HX_NATIVE
DataNode OnCameraDumpUnique(DataArray *) { return DataNode(0); }
DataNode OnCameraDebugDepth(DataArray *) { return DataNode(0); }
#else

DataNode OnCameraDebugDepth(DataArray *) {
    gDebugDepth = !gDebugDepth;
    return DataNode(0);
}

void CameraDump(const char *filename) {
    LiveCameraInput *cam = LiveCameraInput::sInstance;
    if (!cam->mDepthPolled) {
        cam->PollNewStream(LiveCameraInput::kBufferDepth);
    }
    RndTex *tex = cam->GetStreamTex(LiveCameraInput::kBufferDepth);
    int texSize = tex->Width() * tex->Height() * tex->Bpp() / 8;
    void *buf = MemAlloc(texSize, "unknown", 0, "unknown", 0);
    // `= nullptr`, not a bare declaration: the image writes 0 into the slot
    // before the TexelsLock call (`li r11, 0x0` / `stw r11, 0x50(r31)` at
    // 0x82434964..0x8243496C, reloaded as memcpy's r4 at 0x8243497C).  That
    // took 94.737 from 6 charged rows to 5.  RESIDUAL (w8-r, 94.737): the
    // image hoists tex's vtable load ABOVE the `mr r29, r3` / home store
    // (`lwz r10, 0x0(r27)` is the instruction after `li r11, 0x0`) where we
    // emit it two instructions after, and it reloads `lwz r4, 0x50(r31)` after
    // `mr r3, r29` where we load it before -- a two-instruction hoist, no
    // value or address differs.
    // w24-c2: closed (94.737 -> 100) by scoping texels to a BLOCK around the
    // lock/copy -- declaration scope, not order, moved both hoists.
    {
        void *texels = nullptr;
        tex->TexelsLock(texels);
        memcpy(buf, texels, texSize);
    }
    tex->TexelsUnlock();
    FileStream fs(filename, FileStream::kWrite, true);
    if (fs.Fail()) {
        MILO_NOTIFY("Screenshot failed; could not open destination file (%s).", filename);
    } else {
        fs.Write(buf, texSize);
    }
}

void CameraDumpUnique(const char *name) {
    String uniqueName = UniqueFilename(name, "bmp");
    CameraDump(uniqueName.c_str());
}

DataNode OnCameraDumpUnique(DataArray *) {
    CameraDumpUnique("camera");
    return DataNode(0);
}

#endif

#pragma endregion
