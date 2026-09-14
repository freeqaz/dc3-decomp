#include "rndobj\ScreenMask.h"
#include "math/Geo.h"
#include "os\Debug.h"
#include "rndobj/Cam.h"
#include "rndobj\Draw.h"
#include "rndobj\HiResScreen.h"
#include "rndobj\Rnd.h"
#include "utl/BinStream.h"

void RndScreenMask::Save(BinStream &bs) {
    bs << 2;
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mMat << mColor << mRect << mUseCamRect;
}

BEGIN_HANDLERS(RndScreenMask)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_COPYS(RndScreenMask)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(RndScreenMask)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMat)
        COPY_MEMBER(mColor)
        COPY_MEMBER(mRect)
        COPY_MEMBER(mUseCamRect)
    END_COPYING_MEMBERS
END_COPYS

BEGIN_PROPSYNCS(RndScreenMask)
    SYNC_PROP(mat, mMat)
    SYNC_PROP(color, mColor)
    SYNC_PROP(alpha, mColor.alpha)
    SYNC_PROP(screen_rect, mRect)
    SYNC_PROP(use_cam_rect, mUseCamRect)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

RndScreenMask::RndScreenMask()
    : mMat(this), mColor(1, 1, 1, 1), mRect(0, 0, 1, 1), mUseCamRect(false) {}

INIT_REVS(2, 0)

BEGIN_LOADS(RndScreenMask)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    d >> mMat;
    d >> mColor;
    if (d.rev > 0) {
        d >> mRect;
    }
    if (d.rev > 1) {
        d >> mUseCamRect;
    }
END_LOADS

void RndScreenMask::DrawShowing() {
    if (TheRnd.DrawMode() != Rnd::kDrawNormal)
        return;

    float width = (float)TheRnd.Width();
    float height = (float)TheRnd.Height();
    RndCam *cam = RndCam::Current();
    RndTex *targetTex = cam->TargetTex();
    // Residual in the targetTex branch (~5 rows) is a frame-size delta, not a
    // source shape: the image keeps a SECOND int->float scratch slot here
    //   build/373307D9/asm/system/rndobj/ScreenMask.s @82712A68
    //     clrrwi r10, r11, 0          ; we address off r11 directly
    //     lwa  r9, 0x5c(r10) / std r9, 0x60(r1)    ; height -> slot 0x60
    //     lwa r10, 0x58(r10) / std r10, 0x50(r1)   ; width  -> slot 0x50
    //     lfd f0, 0x50 / fcfid / frsp f31          ; width
    //     lfd f13, 0x60 / fcfid / frsp f30         ; height
    // i.e. both ints are stored before either is converted, so 0x50 cannot be
    // reused and the frame is 0xd0 instead of our 0xc0.  We store/convert/
    // store/convert through 0x50 twice.  Swapping the two assignments to
    // `height` then `width` DOES move the first lwa to 0x5c and kills the
    // OFFSET_SWAP, but it flips the conversion order too (new frsp f31<->f30
    // swaps) and the row count goes 54 -> 64, raw 97.0 -> 96.8.  Refuted.
    if ((int)targetTex) {
        width = (float)targetTex->Width();
        height = (float)targetTex->Height();
    }

    if (!mUseCamRect && (int)targetTex) {
        Hmx::Rect defaultRect(0.0f, 0.0f, 1.0f, 1.0f);
        if (!(cam->GetScreenRect() == defaultRect)) {
            MILO_NOTIFY_ONCE(
                "%s: Overriding camera screen_rect not supported with render texture",
                Name()
            );
        }
    }

    // The image re-loads ?sCurrent@RndCam@@1PAV1@A here, after the
    // MILO_NOTIFY_ONCE block (`lwz r29, ?sCurrent@RndCam@@1PAV1@A@l(r27)`) --
    // but it re-loads it INTO THE SAME VARIABLE, and then uses that variable
    // for the Select() at the bottom of the branch too:
    //   build/373307D9/asm/system/rndobj/ScreenMask.s
    //     lwz r29, ?sCurrent@...@l(r27)   ; re-read
    //     lwz r11, 0x2f4(r29)             ; cam->TargetTex()
    //     ...
    //     lwz r11, 0x0(r29) / lwz r11, 0x4(r11) / mr r3, r29   ; cam->Select()
    // Writing `RndCam::Current()->Select()` at the bottom made MSVC re-read the
    // global a third time (TheRnd.DrawRect intervenes and may write it), which
    // is 3 rows the image does not have.  A fresh `RndCam *curCam` local closes
    // those but costs a register; reassigning `cam` scores the same and keeps
    // the image's one-variable shape.  (Spelling the TEST as `cam->TargetTex()`
    // without the re-read scores 97.44505 -- refuted earlier.)
    cam = RndCam::Current();
    if (!mUseCamRect && !cam->TargetTex()) {
        TheRnd.GetDefaultCam()->Select();
        Hmx::Rect hiRes = TheHiResScreen.InvScreenRect();
        Hmx::Rect drawRect;
        drawRect.x = (mRect.x * hiRes.w + hiRes.x) * width;
        drawRect.y = (mRect.y * hiRes.h + hiRes.y) * height;
        drawRect.w = (mRect.w * hiRes.w) * width;
        drawRect.h = (mRect.h * hiRes.h) * height;
        TheRnd.DrawRect(drawRect, mColor, mMat, nullptr, nullptr);
        cam->Select();
    } else {
        Hmx::Rect hiRes = TheHiResScreen.InvScreenRect();
        Hmx::Rect drawRect;
        drawRect.x = (mRect.x * hiRes.w + hiRes.x) * width;
        drawRect.y = (mRect.y * hiRes.h + hiRes.y) * height;
        drawRect.w = (mRect.w * hiRes.w) * width;
        drawRect.h = (mRect.h * hiRes.h) * height;
        TheRnd.DrawRect(drawRect, mColor, mMat, nullptr, nullptr);
    }
}
