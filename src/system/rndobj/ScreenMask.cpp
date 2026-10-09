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
    // w13-a (97.8 -> 100 modulo register permutation): inside the branch
    // the image re-reads the texture through cam->TargetTex() for each
    // dimension (82712A68 `clrrwi r10, r11, 0` is that re-read pointer); with
    // both reads off the accessor, both ints are stored before either is
    // converted (slots 0x60 and 0x50), which is the image's 0xd0 frame.
    if ((int)targetTex) {
        width = (float)cam->TargetTex()->Width();
        height = (float)cam->TargetTex()->Height();
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
    // The reassignment sits INSIDE the branch, after the test re-reads
    // Current(): same code (the two sCurrent reads CSE), but the sCurrent temp
    // drops one ref, so its colour priority falls from 29 to 23 and ties the
    // TheRnd temp, which wins on tie key (112 vs 111) and takes r30 as in the
    // image.  Assigning before the test gave sCurrent r30 and TheRnd r29.
    if (!mUseCamRect && !RndCam::Current()->TargetTex()) {
        cam = RndCam::Current();
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
