#include "rndobj/ScreenMask.h"
#include "math/Geo.h"
#include "rndobj/Draw.h"
#include "utl/BinStream.h"

bool Hmx::Rect::operator==(const Hmx::Rect &other) const {
    return x == other.x && y == other.y && w == other.w && h == other.h;
}

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
