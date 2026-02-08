#include "rndobj/Shockwave.h"
#include "obj/Object.h"
#include "rndobj/Poll.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"

RndShockwave *RndShockwave::sSelected;

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

void RndShockwave::Enter() {
    if (mAutoSelect)
        sSelected = this;
}

void RndShockwave::Exit() {
    if (sSelected == this)
        sSelected = nullptr;
}

RndShockwave::RndShockwave()
    : mAutoSelect(0), mRadius(0), mAmplitude(0), mWavelength(10) {}

void RndShockwave::Save(BinStream &bs) {
    bs << 0;
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndTransformable)
    SAVE_SUPERCLASS(RndPollable)
    bs << mAutoSelect;
    bs << mRadius;
    bs << mAmplitude;
    bs << mWavelength;
}


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

BEGIN_HANDLERS(RndShockwave)
    HANDLE_SUPERCLASS(RndPollable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void RndShockwave::PrepareShader(float amplitude_scale) {
    // Set shader constant 0x1E: shockwave center position in world space
    const Vector3 &world_pos = WorldXfm().v;
    Vector4 pos;
    pos.x = world_pos.x;
    pos.y = world_pos.y;
    pos.z = world_pos.z;
    pos.w = 0.0f;
    TheShaderMgr.SetVConstant((VShaderConstant)0x1E, pos);

    // Set shader constant 0x1F: shockwave normal (z-axis of transform)
    const Vector3 &world_normal = WorldXfm().m.z;
    Vector4 normal;
    normal.x = world_normal.x;
    normal.y = world_normal.y;
    normal.z = world_normal.z;
    normal.w = 0.0f;
    TheShaderMgr.SetVConstant((VShaderConstant)0x1F, normal);

    // Set shader constant 0x20: shockwave parameters (radius, amplitude, frequency)
    TheShaderMgr.SetVConstant(
        (VShaderConstant)0x20, Vector4(mRadius, mAmplitude * amplitude_scale, 1.0f / mWavelength, 0.0f)
    );
}
