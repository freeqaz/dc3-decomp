#include "rndobj\Wind.h"
#include "math/Rand.h"
#include "math\Utl.h"
#include "obj/Object.h"
#include "utl/BinStream.h"
#include "math/Rand.h"
#include "math\Utl.h"
#include "math\Mtx.h"

extern float gUnitsPerMeter;
static Rand *sRand = nullptr;
static float sWhiteField[0x400] = { 0 };
static float sWindField[0x401] = { 0 };
Vector3 sOffset(0.0f, 0.3384f, 0.66843998f);

void SetWind(int start, int end, float startVal, float endVal, float amplitude) {
    sWindField[start] = startVal;
    if (end - start >= 2) {
        float half = 0.5f;
        float decay = 1.0f / sqrtf(2.0f);
        do {
            int mid = (start + end) / 2;
            float midVal =
                sRand->Gaussian() * amplitude + (startVal + endVal) * half;
            amplitude *= decay;
            SetWind(start, mid, startVal, midVal, amplitude);
            startVal = midVal;
            start = mid;
            sWindField[mid] = midVal;
        } while (end - start >= 2);
    }
}

float RndWind::GetWind(float x) {
    float f = Mod(x, 1.0f) * 1024.0f;
    int i = (int)f;
    return sWindField[i] + (sWindField[i + 1] - sWindField[i]) * (f - (float)(int)f);
}

float RndWind::GetWhiteNoise(float x) {
    float f = Mod(x, 1023.0f);
    int i = (int)f;
    return sWhiteField[i] + (sWhiteField[i + 1] - sWhiteField[i]) * (f - (float)(int)f);
}

void RndWind::SelfGetWind(const Vector3 &pos, float time, Vector3 &result) {
    result.x = GetWind(mTimeRate.x * time + mSpaceRate.x * pos.x + sOffset.x) * mRandom.x
        + mPrevailing.x;
    result.y = GetWind(mTimeRate.y * time + mSpaceRate.y * pos.y + sOffset.y) * mRandom.y
        + mPrevailing.y;
    result.z = GetWind(mSpaceRate.z * pos.z + mTimeRate.z * time + sOffset.z) * mRandom.z
        + mPrevailing.z;

    RndTransformable *trans = mTrans.Ptr();
    if ((int)trans) {
        const Transform &xfm = trans->WorldXfm();
        if (mAboutZ) {
            // w7-bt (89.6 -> 96.3 canonical): the three 16-byte rows at
            // 0x50/0x60/0x70 are one Hmx::Matrix3, not three locals.  m.z is
            // the axis (the 4-word copy at 0x8266FDB4..FDCC), m.x the
            // projection, m.y the cross.  Normalize takes &m.y (0x60) out of
            // line at 0x8266FE34, so the m.x stores at 0x8266FDFC/FE04/FE0C
            // are the aggregate staying live across the call, and the axis
            // is reloaded from 0x70/0x74/0x78 afterwards instead of being
            // parked in f29..f31 (the w7-ab residual: 3 callee-saved FPRs
            // where the image saves 1).  A separate `proj` local, whatever
            // its spelling, has its stores elided: an unnamed `Vector3(...)`
            // temp bound to Cross's const-ref param and a TU-local
            // by-value-return helper were both byte-identical at 89.6.
            // The image's final rows are NOT `Multiply(result, m, result)`
            // with m.x = Cross(m.y, m.z) (92.7, and a `Vector3 mx` cross
            // plus explicit rows is 94.6): they are the explicit
            // right-associated rows below (result.y = rx*mx.y + (rz*z.y +
            // ry*c.y) at 0x8266FE98, etc.), with the row-x cross terms
            // spelled inline.
            // RESIDUAL (w7-bt, 96.3 canonical): fmuls/fmsubs operand order
            // inside the first cross (0x8266FE10..FE30) and fnmadds at
            // 0x8266FDF4 -- spelling the cross as explicit `m.y.Set(...)` in
            // the image's operand order is byte-identical to Vec.h's Cross,
            // so the order is the allocator's, not the source's -- and the
            // scheduler's load order after the Normalize call (image loads
            // c.z/z.y/c.y/z.x/ry first, we load c.y/z.x/c.z/z.y/rz).
            Hmx::Matrix3 m;
            m.z = xfm.m.z;
            Vector3 diff(pos.x - xfm.v.x, pos.y - xfm.v.y, pos.z - xfm.v.z);
            float dot = -(diff.x * m.z.x + diff.y * m.z.y + diff.z * m.z.z);
            ScaleAdd(diff, m.z, dot, m.x);
            Cross(m.z, m.x, m.y);
            Normalize(m.y, m.y);
            float ry = result.y;
            float rz = result.z;
            float rx = result.x;
            result.y = rx * (m.y.z * m.z.x - m.z.z * m.y.x)
                + (rz * m.z.y + ry * m.y.y);
            result.z = rz * m.z.z
                + (rx * (m.z.y * m.y.x - m.y.y * m.z.x) + ry * m.y.z);
            result.x = rz * m.z.x
                + (ry * m.y.x + rx * (m.y.y * m.z.z - m.y.z * m.z.y));
        } else {
            Multiply(result, xfm.m, result);
        }
    }

    float len = sqrtf(result.x * result.x + result.y * result.y + result.z * result.z);
    float limit;
    if (len > 0.0f) {
        if (len > mMaxSpeed) {
            limit = mMaxSpeed;
        } else if (len < mMinSpeed) {
            limit = mMinSpeed;
        } else {
            goto done;
        }
        float scale = limit / len;
        result.x *= scale;
        result.y *= scale;
        result.z *= scale;
    }
done:;
}

RndWind::RndWind()
    : mPrevailing(0.0f, 0.0f, 0.0f), mRandom(0.0f, 0.0f, 0.0f), mTimeLoop(100.0f),
      mSpaceLoop(gUnitsPerMeter * 10.0f), mTrans(this), mAboutZ(false), mMaxSpeed(1e30f),
      mMinSpeed(0.0f), mWindOwner(this, this) {
    SyncLoops();
}

RndWind::~RndWind() {}

bool RndWind::Replace(ObjRef *from, Hmx::Object *to) {
    if (&mWindOwner == from) {
        RndWind *wind;
        if (mWindOwner == this || !(wind = dynamic_cast<RndWind *>(to))) {
            mWindOwner.SetObjConcrete(this);
        } else {
            mWindOwner.SetObjConcrete(wind->mWindOwner.Ptr());
        }
        return true;
    } else {
        return Hmx::Object::Replace(from, to);
    }
}


BEGIN_HANDLERS(RndWind)
    HANDLE_SUPERCLASS(Hmx::Object)
    HANDLE_ACTION(set_defaults, SetDefaults())
    HANDLE_ACTION(set_zero, Zero())
END_HANDLERS

BEGIN_PROPSYNCS(RndWind)
    SYNC_PROP(prevailing, mPrevailing)
    SYNC_PROP(random, mRandom)
    SYNC_PROP(max_speed, mMaxSpeed)
    SYNC_PROP(min_speed, mMinSpeed)
    SYNC_PROP_SET(wind_owner, mWindOwner.Ptr(), SetWindOwner(_val.Obj<RndWind>()))
    SYNC_PROP_MODIFY(time_loop, mTimeLoop, SyncLoops())
    SYNC_PROP_MODIFY(space_loop, mSpaceLoop, SyncLoops())
    SYNC_PROP(trans, mTrans)
    SYNC_PROP(about_z, mAboutZ)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndWind)
    SAVE_REVS(4, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mPrevailing;
    bs << mRandom;
    bs << mTimeLoop;
    bs << mSpaceLoop;
    bs << mWindOwner;
    bs << mTrans;
    bs << mAboutZ;
    bs << mMinSpeed;
    bs << mMaxSpeed;
END_SAVES

BEGIN_COPYS(RndWind)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(RndWind)
    BEGIN_COPYING_MEMBERS
        if (ty == kCopyShallow) {
            mWindOwner = c->mWindOwner.Ptr();
        } else {
            mWindOwner = this;
            mWindOwner = c->mWindOwner.Ptr();
            COPY_MEMBER(mPrevailing)
            COPY_MEMBER(mRandom)
            COPY_MEMBER(mTimeLoop)
            COPY_MEMBER(mSpaceLoop)
            COPY_MEMBER(mTrans)
            COPY_MEMBER(mAboutZ)
            COPY_MEMBER(mMinSpeed)
            COPY_MEMBER(mMaxSpeed)
            SyncLoops();
        }
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(4, 0)

BEGIN_LOADS(RndWind)
    LOAD_REVS(bs)
    ASSERT_REVS(4, 0)
    LOAD_SUPERCLASS(RndHighlightable)
    d >> mPrevailing;
    d >> mRandom;
    d >> mTimeLoop;
    d >> mSpaceLoop;
    if (d.rev > 1) {
        d >> mWindOwner;
        SetWindOwner(mWindOwner);
    }
    if (d.rev > 2) {
        d >> mTrans;
        d >> mAboutZ;
    }
    if (d.rev > 3) {
        d >> mMinSpeed;
        d >> mMaxSpeed;
    }
    SyncLoops();
END_LOADS

void RndWind::SyncLoops() {
    float f1;
    f1 = (mTimeLoop == 0.0f) ? 0.0f : (1.0f / mTimeLoop);
    mTimeRate.Set(f1, f1 * 0.773437f, f1 * 1.38484f);
    f1 = (mSpaceLoop == 0.0f) ? 0.0f : (1.0f / mSpaceLoop);
    mSpaceRate.Set(f1, f1 * 0.773437f, f1 * 1.38484f);
}

void RndWind::Zero() {
    mRandom.Set(0.0f, 0.0f, 0.0f);
    mPrevailing.Set(0.0f, 0.0f, 0.0f);
}

void RndWind::SetDefaults() {
    mPrevailing.Set(0.0f, 0.0f, 0.0f);
    mRandom.Set(17.0f, 17.0f, 0.0f);
    mTimeLoop = 100.0f;
    mSpaceLoop = gUnitsPerMeter * 10;
}

void RndWind::SetWindOwner(RndWind *wind) { mWindOwner = wind ? wind : this; }

void RndWind::Init() {
    REGISTER_OBJ_FACTORY(RndWind)
    sRand = new Rand(0x7FEF8A);
    SetWind(0, 0x400, 0.0f, 0.0f, 0.5f);
    sWindField[0x400] = sWindField[0];
    for (int i = 0; i < 0x400; i++) {
        sWhiteField[i] = RandomFloat(0.0f, 1.0f);
    }
    delete sRand;
    sRand = 0;
}
