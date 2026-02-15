#include "char/CharCuff.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
#include "rndobj/Rnd.h"
#include "math/Trig.h"
#include <cmath>

CharCuff::CharCuff() : mOpenEnd(0), mIgnore(this), mBone(this), mEccentricity(1) {
    mShape[0].offset = -2.9;
    mShape[0].radius = 1.9;
    mShape[1].offset = 0;
    mShape[1].radius = 2.6;
    mShape[2].offset = 2.0;
    mShape[2].radius = 3.5;
    mOuterRadius = 3.1;
}

CharCuff::~CharCuff() {}

BEGIN_HANDLERS(CharCuff)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(CharCuff)
    SYNC_PROP(offset0, mShape[0].offset)
    SYNC_PROP(radius0, mShape[0].radius)
    SYNC_PROP(offset1, mShape[1].offset)
    SYNC_PROP(radius1, mShape[1].radius)
    SYNC_PROP(offset2, mShape[2].offset)
    SYNC_PROP(radius2, mShape[2].radius)
    SYNC_PROP(outer_radius, mOuterRadius)
    SYNC_PROP(open_end, mOpenEnd)
    SYNC_PROP(bone, mBone)
    SYNC_PROP(eccentricity, mEccentricity)
    SYNC_PROP(category, mCategory)
    SYNC_PROP(ignore, mIgnore)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharCuff)
    SAVE_REVS(8, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    for (int i = 0; i < 3; i++) {
        bs << mShape[i].radius;
        bs << mShape[i].offset;
    }
    bs << mOuterRadius;
    bs << mOpenEnd;
    bs << mBone;
    bs << mEccentricity;
    bs << mCategory;
    bs << mIgnore;
END_SAVES

BEGIN_COPYS(CharCuff)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndTransformable)
    CREATE_COPY(CharCuff)
    BEGIN_COPYING_MEMBERS
        memcpy(mShape, c->mShape, sizeof(mShape));
        COPY_MEMBER(mOuterRadius)
        COPY_MEMBER(mOpenEnd)
        COPY_MEMBER(mBone)
        COPY_MEMBER(mEccentricity)
        COPY_MEMBER(mCategory)
        COPY_MEMBER(mIgnore)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(8, 0)

BEGIN_LOADS(CharCuff)
    LOAD_REVS(bs)
    ASSERT_REVS(8, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndTransformable)
    for (int i = 0; i < 3; i++) {
        bs >> mShape[i].radius >> mShape[i].offset;
    }
    if (d.rev > 1)
        bs >> mOuterRadius;
    else
        mOuterRadius = mShape[1].radius + 0.5f;
    if (d.rev > 2)
        bs >> mOpenEnd;
    else
        mOpenEnd = false;
    if (d.rev > 3)
        bs >> mBone;
    else
        mBone = TransParent();
    if (d.rev > 4)
        bs >> mEccentricity;
    else
        mEccentricity = 1.0f;
    if (d.rev > 5)
        bs >> mCategory;
    else
        mCategory = Symbol("");
    if (d.rev > 7)
        bs >> mIgnore;
    if (d.rev < 7)
        MILO_NOTIFY("%s old CharCuff, must convert, see James", PathName(this));
END_LOADS

float CharCuff::Eccentricity(const Vector2 &v) const {
    float f1 = v.y * v.y;
    float f2 = v.x * v.x;
    return std::sqrt((f1 + f2) / (f1 * (1.0f / (mEccentricity * mEccentricity)) + f2));
}

void CharCuff::Highlight() {
    Hmx::Color white(1, 1, 1, 1);
    const float kTwoPi = 6.2831855f;
    const float kInv32 = 1.0f / 32.0f;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 32; j++) {
            float toSine = (kInv32 * (kTwoPi * j));
            Vector3 va8(Sine(toSine), Cosine(toSine), mShape[i].offset);
            Vector3 vb4(Sine(toSine), Cosine(toSine), mShape[i + 1].offset);
            (Vector2 &)va8 *= mShape[i].radius * Eccentricity((Vector2 &)va8);
            (Vector2 &)vb4 *= mShape[i + 1].radius * Eccentricity((Vector2 &)vb4);
            Vector3 vc0;
            Multiply(va8, WorldXfm(), vc0);
            Vector3 vcc;
            Multiply(vb4, WorldXfm(), vcc);
            TheRnd.DrawLine(vc0, vcc, white, false);
            if (i < 2) {
                float toSinePlus1 = (kInv32 * (kTwoPi * (j + 1)));
                va8.Set(Sine(toSinePlus1), Cosine(toSinePlus1), mShape[i].offset);
                (Vector2 &)va8 *= mShape[i].radius * Eccentricity((Vector2 &)va8);
                Multiply(va8, WorldXfm(), vcc);
                TheRnd.DrawLine(vc0, vcc, white, false);
            }
            if (i == 1) {
                Vector3 vd8(Sine(toSine), Cosine(toSine), mShape[i].offset);
                (Vector2 &)vd8 *= mOuterRadius;
                Multiply(vd8, WorldXfm(), vc0);
                float toSinePlus1 = (j + 1) * kTwoPi * kInv32;
                vb4.Set(Sine(toSinePlus1), Cosine(toSinePlus1), mShape[i].offset);
                (Vector2 &)vb4 *= mOuterRadius;
                Multiply(vb4, WorldXfm(), vcc);
                TheRnd.DrawLine(vc0, vcc, white, false);
            }
        }
    }
}
