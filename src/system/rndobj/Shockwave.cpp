#include "rndobj/Shockwave.h"
#include "obj/Object.h"
#include "rndobj/Poll.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"

RndShockwave *RndShockwave::sSelected = nullptr;
static const float sWavelength = 10;

RndShockwave::RndShockwave()
    : mAutoSelect(0), mRadius(0), mAmplitude(0), mWavelength(sWavelength) {}

BEGIN_HANDLERS(RndShockwave)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndShockwave)
    SYNC_PROP_SET(
        selected, sSelected == this, sSelected = _val.Int() != 0 ? this : nullptr
    )
    SYNC_PROP(auto_select, mAutoSelect)
    SYNC_PROP(radius, mRadius)
    SYNC_PROP(amplitude, mAmplitude)
    SYNC_PROP(wavelength, mWavelength)
    SYNC_SUPERCLASS(RndPollable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndShockwave)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    SAVE_SUPERCLASS(RndPollable)
    bs << mAutoSelect;
    bs << mRadius;
    bs << mAmplitude;
    bs << mWavelength;
END_SAVES

BEGIN_COPYS(RndShockwave)
    if (this != o) {
        COPY_SUPERCLASS(Hmx::Object)
        COPY_SUPERCLASS(RndTransformable)
        COPY_SUPERCLASS(RndPollable)
        CREATE_COPY(RndShockwave)
        BEGIN_COPYING_MEMBERS
            COPY_MEMBER(mAutoSelect)
            COPY_MEMBER(mRadius)
            COPY_MEMBER(mAmplitude)
            COPY_MEMBER(mWavelength)
        END_COPYING_MEMBERS
    }
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(RndShockwave)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndTransformable)
    LOAD_SUPERCLASS(RndPollable)
    d >> mAutoSelect;
    d >> mRadius;
    d >> mAmplitude;
    d >> mWavelength;
END_LOADS

void RndShockwave::Enter() {
    if (mAutoSelect)
        sSelected = this;
}

void RndShockwave::Exit() {
    if (sSelected == this)
        sSelected = nullptr;
}

void RndShockwave::PrepareShader(float amplitude_scale) {
    const Vector3 &v = WorldXfm().v;
    TheShaderMgr.SetVConstant(kVS_ShockwavePos, Vector4(v.x, v.y, v.z, 0));
    const Vector3 &mz = WorldXfm().m.z;
    TheShaderMgr.SetVConstant(kVS_ShockwaveNormal, Vector4(mz.x, mz.y, mz.z, 0));
    TheShaderMgr.SetVConstant(
        kVS_ShockwaveParams, Vector4(mRadius, mAmplitude * amplitude_scale, 1.0f / mWavelength, 0.0f)
    );
}
