#include "char/CharTransDraw.h"
#include "obj/Object.h"
#include "rndobj/Draw.h"

CharTransDraw::CharTransDraw() : mChars(this), unk54(false) {}

CharTransDraw::~CharTransDraw() {
    for (ObjPtrList<Character>::iterator it = mChars.begin(); it != NULL; ++it) {
        if (it != NULL) {
            Character *c = *it;
            *(u32 *)((u32)c + 0x294) = 3;
        }
    }
}

BEGIN_PROPSYNCS(CharTransDraw)
    SYNC_PROP(chars, mChars)
    SYNC_PROP(force_draw, unk54)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharTransDraw)
    SAVE_REVS(2, 1)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mChars;
    bs << unk54;
END_SAVES

BEGIN_COPYS(CharTransDraw)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(CharTransDraw)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mChars)
        COPY_MEMBER(unk54)
    END_COPYING_MEMBERS
END_COPYS

void CharTransDraw::Load(BinStream &bs) {
    LOAD_REVS(bs)
    ASSERT_REVS(2, 1)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    bs >> mChars;
    if (d.altRev > 0)
        bs >> unk54;

    // Iterate through characters and set flag
    for (auto it = mChars.begin(); it != mChars.end(); ++it) {
        auto c = *it;
        if (c != 0) {
            *(u32 *)((u32)c + 0x294) = 1;
        }
    }
}

void CharTransDraw::DrawShowing() {
    int mode2 = 2;
    int mode1 = 1;
    ObjPtrList<Character>::iterator it = mChars.begin();
    ObjPtrList<Character>::iterator end = mChars.end();

    while (it != end) {
        Character *c = *it;
        bool showing = c->Showing();

        if (showing) {
            c->Draw();
        } else if (unk54) {
            c->SetShowing(true);
            c->Draw();
            c->SetShowing(false);
        }

        ++it;
    }
}

BEGIN_HANDLERS(CharTransDraw)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
