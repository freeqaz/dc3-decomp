#include "rndobj\Lit.h"
#include "Lit.h"
#include "obj/Object.h"
#include "rndobj\Trans.h"
#include "utl/BinStream.h"

void RndLight::SetShadowOverride(ObjPtrList<RndDrawable> *l) { mShadowOverride = l; }

void RndLight::SetPackedColor(int packed, float scalar) {
    Hmx::Color col;
    col.Unpack(packed);
    Multiply(col, scalar, col);
    SetColor(col);
}

const char *RndLight::TypeToStr(Type t) {
    const char *lightTypes[] = { "Point", "Directional", "Projected", "ShadowRef" };
    MILO_ASSERT(t < DIM(lightTypes), 0x17A);
    return lightTypes[t];
}

void RndLight::Save(BinStream &bs) {
    bs << 0x10;
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    bs << (Vector4 &)mColor << mRange << mType;
    bs << mFalloffStart;
    bs << mAnimateColorFromPreset;
    bs << mAnimatePositionFromPreset;
    bs << mTopRadius << mBotRadius;
    bs << mTexture;
    bs << mColorOwner;
    bs << mTextureXfm;
    bs << mCubeTexture;
    bs << mShadowObjects;
    bs << mProjectedBlend;
    bs << mAnimateRangeFromPreset;
}

BEGIN_COPYS(RndLight)
    CREATE_COPY_AS(RndLight, l)
    MILO_ASSERT(l, 0xC4);
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndTransformable)
    COPY_MEMBER_FROM(l, mColor)
    COPY_MEMBER_FROM(l, mType)
    COPY_MEMBER_FROM(l, mAnimateColorFromPreset)
    COPY_MEMBER_FROM(l, mAnimatePositionFromPreset)
    COPY_MEMBER_FROM(l, mAnimateRangeFromPreset)
    if (ty != kCopyFromMax)
        COPY_MEMBER_FROM(l, mRange)
    COPY_MEMBER_FROM(l, mFalloffStart)
    COPY_MEMBER_FROM(l, mTopRadius)
    COPY_MEMBER_FROM(l, mBotRadius)
    COPY_MEMBER_FROM(l, mTexture)
    COPY_MEMBER_FROM(l, mCubeTexture)
    COPY_MEMBER_FROM(l, mShadowOverride)
    COPY_MEMBER_FROM(l, mShadowObjects)
    // NOT mTextureXfm. The image goes straight from the mShadowObjects
    // ObjPtrList::operator= at 0x826BD890 to `lwz r11, 0x17c(r30)` (the
    // mProjectedBlend load) at 0x826BD894 -- no memcpy, no _blkmov, and no
    // reference to any source offset in 0x130..0x16f anywhere in the whole
    // function. RndLight::Copy simply does not copy the texture transform,
    // even though Save/Load both do. Adding it emitted an extra
    // `memcpy(this-0x50, l+0x134, 0x40)`.
    COPY_MEMBER_FROM(l, mProjectedBlend)
    if (ty == kCopyShallow || (ty == kCopyFromMax && l->mColorOwner != l)) {
        COPY_MEMBER_FROM(l, mColorOwner)
    } else {
        mColorOwner = this;
        COPY_MEMBER_FROM(l, mColor)
    }
END_COPYS

bool RndLight::Replace(ObjRef *ref, Hmx::Object *obj) {
    if (&mColorOwner == ref) {
        RndLight *lit = NULL;
        if (mColorOwner != this) {
            lit = dynamic_cast<RndLight *>(obj);
        }
        if (lit) {
            mColorOwner = lit->mColorOwner;
        } else {
            mColorOwner = this;
        }
        return true;
    }
    return RndTransformable::Replace(ref, obj);
}

// Built as a Matrix3 plus a Vector3 and handed to Transform's two-argument
// constructor: retail's static initialiser runs that ctor with `this` ==
// &sBias, so the rotation arrives as one 0x30 memcpy out of a Matrix3 local
// and the translation as a four-word copy out of a separate Vector3 local.
// A `Transform bias;` built field-wise would be NRVO'd straight into the
// static and lose both copies.
static Transform MakeShadowBias() {
    Hmx::Matrix3 m;
    m.x.Set(0.5f, 0.0f, 0.0f);
    m.y.Set(0.0f, 0.5f, 0.0f);
    m.z.Set(0.5f, 0.5f, 1.0f);
    Vector3 v(0.0f, 0.0f, 0.0f);
    return Transform(m, v);
}

Transform RndLight::Projection() {
    Transform result;
    if (mRange == 0.0f) {
        result.Reset();
        return result;
    } else {
        Vector3 xRow = WorldXfm().m.x;

        const Transform &wz = WorldXfm();
        float nzx = -wz.m.z.x;
        float nzy = -wz.m.z.y;
        float nzz = -wz.m.z.z;

        Vector3 yRow = WorldXfm().m.y;

        Vector3 pos = WorldXfm().v;

        // NEGATIVE RESULT (w7-ao, 2026-09-14): the residual here is the frame
        // offset of the xRow/yRow/pos 16-byte Vector3 slots, not regalloc, and
        // declaration ORDER does not move it. Both directions measured (a bare
        // `Vector3 pos;` declared first, then assigned where it is now; and
        // swapping the `yRow`/`pos` statements): `pos` stays at 0xa0 in every
        // variant and the diff is byte-identical. The lever that works in
        // SuperFormatString -- init order deciding callee-saved assignment --
        // is inert on MSVC's 16-byte-aligned Vector3 slots, which it allocates
        // by first USE, not by declaration.
        float topR = mTopRadius;
        float slope = (mBotRadius - topR) / mRange;

        result.m.x.y = nzx;
        float _fpr0 = yRow.y;
        float _fpr1 = yRow.z;
        float _fpr2 = yRow.x;
        float _fpr3 = pos.z;
        float _fpr4 = pos.y;
        float _fpr5 = pos.x;
        // The image reuses the three scaled column entries it has just stored
        // (f10/f8/f7) for v.z, and accumulates them in y, z, x order:
        //   fmuls f0, pos.y, yzCol ; fmadds pos.z, zzCol ; fmadds pos.x, xzCol
        //   fsubs f0, topR, f0
        float yzCol = _fpr0 * slope;
        float zzCol = _fpr1 * slope;
        float xzCol = _fpr2 * slope;
        result.m.y.z = yzCol;
        result.m.z.z = zzCol;
        result.m.x.z = xzCol;

        result.v.x = -((_fpr3 * xRow.z + (_fpr4 * xRow.y + _fpr5 * xRow.x)));
        result.v.y = -((_fpr5 * nzx + (_fpr4 * nzy + _fpr3 * nzz)));
        result.v.z = topR - (_fpr4 * yzCol + _fpr3 * zzCol + _fpr5 * xzCol);

        result.m.x.x = xRow.x;
        result.m.y.x = xRow.y;
        result.m.z.x = xRow.z;
        result.m.y.y = nzy;
        result.m.z.y = nzz;

        Multiply(result, mTextureXfm, result);

        static const Transform sBias = MakeShadowBias();
        Multiply(result, sBias, result);
    }
    return result;
}

BEGIN_HANDLERS(RndLight)
    HANDLE_ACTION(set_showing, SetShowing(_msg->Int(2)))
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

RndLight::RndLight()
    : mColor(1, 1, 1), mColorOwner(this, this), mRange(1000.0f), mFalloffStart(0),
      mType(kPoint), mAnimateColorFromPreset(1), mAnimatePositionFromPreset(1),
      mAnimateRangeFromPreset(1), mShowing(1), mTexture(this), mCubeTexture(this),
      mShadowOverride(nullptr), mShadowObjects(this, kObjListNoNull), mTopRadius(0),
      mBotRadius(30.0f), mProjectedBlend(0) {
    mTextureXfm.Reset();
}

int RndLight::PackedColor() const {
    Hmx::Color col;
    Multiply(GetColor(), 1.0f / Intensity(), col);
    return col.Pack();
}

float RndLight::Intensity() const {
    Hmx::Color col(GetColor());
    return Max(1.0f, Max(col.red, col.green, col.blue));
}

BEGIN_PROPSYNCS(RndLight)
    SYNC_PROP(animate_color_from_preset, mAnimateColorFromPreset)
    SYNC_PROP(animate_position_from_preset, mAnimatePositionFromPreset)
    SYNC_PROP(animate_range_from_preset, mAnimateRangeFromPreset)
    SYNC_PROP_SET(light_type, mType, SetLightType((Type)_val.Int()))
    SYNC_PROP_SET(range, mRange, SetRange(_val.Float()))
    SYNC_PROP_SET(falloff_start, mFalloffStart, SetFalloffStart(_val.Float()))
    SYNC_PROP_SET(color, PackedColor(), SetPackedColor(_val.Int(), Intensity()))
    SYNC_PROP_SET(intensity, Intensity(), SetPackedColor(PackedColor(), _val.Float()))
    SYNC_PROP_SET(topradius, mTopRadius, SetTopRadius(_val.Float()))
    SYNC_PROP_SET(botradius, mBotRadius, SetBotRadius(_val.Float()))
    SYNC_PROP(color_owner, mColorOwner)
    SYNC_PROP(texture, mTexture)
    SYNC_PROP(cube_texture, mCubeTexture)
    SYNC_PROP(texture_xfm, mTextureXfm)
    SYNC_PROP_SET(projected_blend, mProjectedBlend, SetProjectedBlend(_val.Int()))
    SYNC_PROP(shadow_objects, mShadowObjects)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

INIT_REVS(0x10, 0)

BEGIN_LOADS(RndLight)
    LOAD_REVS(bs)
    ASSERT_REVS(0x10, 0)
    if (d.rev > 3)
        LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndTransformable)
    bs >> mColor;
    if (d.rev < 2) {
        Hmx::Color col1, col2;
        bs >> col1 >> col2;
    }
    if (d.rev < 3) {
        int i, j;
        bs >> i >> j;
    }
    bs >> mRange;
    if (d.rev < 3) {
        int i, j, k;
        bs >> i >> j >> k;
    }
    if (d.rev > 0) {
        int count;
        bs >> count;
        if (d.rev < 0xE) {
            if (count > 1)
                count--;
        }
        mType = (Type)count;
    }
    if (d.rev > 0xB) {
        bs >> mFalloffStart;
    }
    if (d.rev > 4 && d.rev < 5) {
        bool tmp;
        d >> tmp;
        mAnimateColorFromPreset = tmp;
        mAnimatePositionFromPreset = tmp;
    } else if (d.rev > 5) {
        d >> mAnimateColorFromPreset;
        d >> mAnimatePositionFromPreset;
    }
    if (d.rev > 6) {
        bs >> mTopRadius >> mBotRadius;
        if (d.rev < 0xE) {
            int i, j;
            bs >> i >> j;
        }
    }
    if (d.rev > 7) {
        bs >> mTexture;
        if (d.rev == 9) {
            ObjPtrList<RndDrawable> drawList(this);
            bs >> drawList;
        } else if (d.rev == 8) {
            ObjPtr<RndDrawable> drawPtr(this);
            bs >> drawPtr;
        }
    }
    if (d.rev > 10) {
        bs >> mColorOwner;
        if (!mColorOwner)
            mColorOwner = this;
    }
    if (d.rev > 0xC)
        bs >> mTextureXfm;
    if (d.rev > 0xD) {
        bs >> mCubeTexture;
    }
    if (d.rev > 0xE) {
        bs >> mShadowObjects;
        bs >> mProjectedBlend;
    }
    if (d.rev > 0xF)
        d >> mAnimateRangeFromPreset;
    else
        mAnimateRangeFromPreset = mAnimateColorFromPreset;
END_LOADS
