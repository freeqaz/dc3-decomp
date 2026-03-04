#include "world/LightPreset.h"
#include "LightPreset.h"
#include "SpotlightDrawer.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Color.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Anim.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/PostProc.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include <algorithm>
#include <cfloat>

#ifndef HX_NATIVE
// Explicit template instantiation
namespace stlpmtx_std {
    template class vector<ObjPtrVec<Spotlight, ObjectDir>::Node, StlNodeAlloc<ObjPtrVec<Spotlight, ObjectDir>::Node>>;
}
#endif

LightPreset *gEditPreset;
std::deque<std::pair<LightPreset::KeyframeCmd, float> > LightPreset::sManualEvents;

static bool sLoading;
class AutoLoading {
public:
    AutoLoading() { sLoading = true; }
    ~AutoLoading() { sLoading = false; }
};

#pragma region EnvironmentEntry

LightPreset::EnvironmentEntry::EnvironmentEntry()
    : mFogEnable(0), mFogStart(0), mFogEnd(0) {
    mAmbientColor.Zero();
    mFogColor.Zero();
}

void LightPreset::EnvironmentEntry::Save(BinStream &bs) const {
    bs << mAmbientColor;
    bs << mFogEnable;
    bs << mFogStart;
    bs << mFogEnd;
    bs << mFogColor;
}

void LightPreset::EnvironmentEntry::Load(BinStream &bs) {
    bs >> mAmbientColor;
    bs >> mFogEnable;
    bs >> mFogStart;
    bs >> mFogEnd;
    bs >> mFogColor;
}

bool LightPreset::EnvironmentEntry::operator!=(const LightPreset::EnvironmentEntry &e
) const {
    if (mFogEnable != e.mFogEnable)
        return true;
    else if (mFogStart != e.mFogStart)
        return true;
    else if (mFogEnd != e.mFogEnd)
        return true;
    else if (mAmbientColor != e.mAmbientColor)
        return true;
    else
        return mFogColor != e.mFogColor;
}

BinStream &operator<<(BinStream &bs, const LightPreset::EnvironmentEntry &e) {
    e.Save(bs);
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, LightPreset::EnvironmentEntry &e) {
    e.Load(d.stream);
    return d;
}

#pragma endregion
#pragma region EnvLightEntry

LightPreset::EnvLightEntry::EnvLightEntry() : mRange(0), mLightType(RndLight::kPoint) {
    mOrientation.Reset();
    mPosition.Zero();
    mColor.Zero();
    mRotation.Zero();
}

void LightPreset::EnvLightEntry::Save(BinStream &bs) const {
    bs << mOrientation;
    bs << mPosition;
    bs << mColor;
    bs << mRange;
    bs << mLightType;
}

void LightPreset::EnvLightEntry::Load(BinStream &bs) {
    bs >> mOrientation;
    bs >> mPosition;
    bs >> mColor;
    mColor.alpha = 1;
    bs >> mRange;
    bs >> (int &)mLightType;
}

bool LightPreset::EnvLightEntry::operator!=(const LightPreset::EnvLightEntry &e) const {
    if (mRange != e.mRange)
        return true;
    else if ((unsigned int)mLightType != e.mLightType)
        return true;
    else if (mOrientation != e.mOrientation)
        return true;
    else if (mPosition != e.mPosition)
        return true;
    else
        return mColor != e.mColor;
}

BinStream &operator<<(BinStream &bs, const LightPreset::EnvLightEntry &e) {
    e.Save(bs);
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, LightPreset::EnvLightEntry &e) {
    e.Load(d.stream);
    return d;
}

#pragma endregion
#pragma region SpotlightEntry

LightPreset::SpotlightEntry::SpotlightEntry(Hmx::Object *owner)
    : mIntensity(0), mColor(0), mFlags(3), mTarget(owner) {
    mRotation.Reset();
    mRotationMatrix.Zero();
}

void LightPreset::SpotlightEntry::Save(BinStream &bs) const {
    Hmx::Color color(mColor);
    bs << mIntensity;
    bs << mRotation;
    bs << color;
    bs << mTarget;
    bs << (bool)(mFlags & 1);
}

void LightPreset::SpotlightEntry::Load(BinStreamRev &d) {
    float intensity;
    d >> intensity;
    mIntensity = intensity;
    d >> mRotation;
    Hmx::Color color;
    d >> color;
    color.alpha = 1;
    mColor = color.Pack();
    if (!mTarget.Load(d.stream, false, nullptr)) {
        mFlags &= ~2;
    }
    if (d.rev < 0x13) {
        Symbol s;
        d >> s;
    }
    if (d.rev > 1) {
        bool b;
        d >> b;
        if (b) {
            mFlags |= kEnabled;
        } else {
            mFlags &= ~kEnabled;
        }
        if (d.rev < 9) {
            int x;
            d >> x;
        }
    }
    if (mTarget || !(mFlags & 2)) {
        mRotation.Set(0, 0, 0, 0);
    }
}

bool LightPreset::SpotlightEntry::operator!=(const LightPreset::SpotlightEntry &e) const {
    return e.mIntensity != mIntensity || e.mFlags != mFlags || e.mTarget != mTarget
        || (unsigned int)e.mColor != mColor || e.mRotation != mRotation;
}

BinStream &operator<<(BinStream &bs, const LightPreset::SpotlightEntry &e) {
    e.Save(bs);
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, LightPreset::SpotlightEntry &e) {
    e.Load(d);
    return d;
}

#pragma endregion
#pragma region SpotlightDrawerEntry

LightPreset::SpotlightDrawerEntry::SpotlightDrawerEntry()
    : mTotalIntensity(0), mBaseIntensity(0), mSmokeIntensity(0), mLightInfluence(0) {}

void LightPreset::SpotlightDrawerEntry::Save(BinStream &bs) const {
    bs << mBaseIntensity;
    bs << mSmokeIntensity;
    bs << mTotalIntensity;
    bs << mLightInfluence;
}

void LightPreset::SpotlightDrawerEntry::Load(BinStreamRev &d) {
    d >> mBaseIntensity;
    d >> mSmokeIntensity;
    d >> mTotalIntensity;
    if (d.rev > 0xF) {
        d >> mLightInfluence;
    } else {
        mLightInfluence = 1;
    }
}

bool LightPreset::SpotlightDrawerEntry::operator!=(
    const LightPreset::SpotlightDrawerEntry &e
) const {
    if (mBaseIntensity != e.mBaseIntensity)
        return true;
    else if (mSmokeIntensity != e.mSmokeIntensity)
        return true;
    else if (mLightInfluence != e.mLightInfluence)
        return true;
    else if (mTotalIntensity != e.mTotalIntensity)
        return true;
    else
        return false;
}

BinStream &operator<<(BinStream &bs, const LightPreset::SpotlightDrawerEntry &e) {
    e.Save(bs);
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &d, LightPreset::SpotlightDrawerEntry &e) {
    e.Load(d);
    return d;
}

#pragma endregion
#pragma region Keyframe

LightPreset::Keyframe::Keyframe(Hmx::Object *owner)
    : mSpotlightEntries(owner), mTriggers(owner), mDuration(0), mFadeOutTime(0),
      unka8(-1) {
    LightPreset *preset = dynamic_cast<LightPreset *>(owner);
    MILO_ASSERT(preset, 0x56F);

    mSpotlightEntries.resize(preset->mSpotlights.size());
    mEnvironmentEntries.resize(preset->mEnvironments.size());
    mLightEntries.resize(preset->mLights.size());
    mSpotlightDrawerEntries.resize(preset->mSpotlightDrawers.size());
    if (!sLoading)
        preset->SetKeyframe(*this);
}

void LightPreset::Keyframe::Save(BinStream &bs) const {
    bs << mDuration;
    bs << mFadeOutTime;
    bs << mSpotlightEntries;
    bs << mEnvironmentEntries;
    bs << mLightEntries;
    bs << mDescription;
    bs << mSpotlightDrawerEntries;
    bs << mTriggers;
}

void LightPreset::Keyframe::Load(BinStreamRev &d) {
    MILO_ASSERT(d.rev != 14, 0x5A3);
    d >> mDuration;
    d >> mFadeOutTime;
    d >> mSpotlightEntries;
    d >> mEnvironmentEntries;
    d >> mLightEntries;
    if (d.rev > 5) {
        d >> mDescription;
    }
    if (d.rev > 9) {
        d >> mSpotlightDrawerEntries;
    }
    if (d.rev > 0x11 && d.rev < 0x16) {
        ObjPtr<RndPostProc> pp(mSpotlightEntries.Owner());
        d >> pp;
    }
    if (d.rev > 0x13) {
        d >> mTriggers;
    }
    if (d.rev > 0xB && d.rev < 0x16) {
        LegacyLoadStageKit(d.stream);
    }
}

void LightPreset::Keyframe::LegacyLoadStageKit(BinStream &bs) {
    for (int i = 0; i < 9; i++) {
        int x;
        bs >> x;
    }
}

void LightPreset::Keyframe::LegacyLoadP9(BinStreamRev &d) {
    MILO_ASSERT(d.rev == 14, 0x596);
    d >> mDescription;
    d >> mSpotlightEntries;
    d >> mEnvironmentEntries;
    d >> mLightEntries;
    d >> mSpotlightDrawerEntries;
    LegacyLoadStageKit(d.stream);
}

BinStream &operator<<(BinStream &bs, const LightPreset::Keyframe &k) {
    k.Save(bs);
    return bs;
}

#pragma region LightPreset

LightPreset::LightPreset()
    : mKeyframes(this), mSpotlights(this, (EraseMode)0, kObjListOwnerControl),
      mEnvironments(this, (EraseMode)0, kObjListOwnerControl),
      mLights(this, (EraseMode)0, kObjListOwnerControl),
      mSpotlightDrawers(this, (EraseMode)0, kObjListOwnerControl), mLooping(0),
      mPlatformOnly(kPlatformNone), mSelectTriggers(this), mManual(0),
      mSpotlightState(this), mLastKeyframe(0), mLastBlend(-1), mStartBeat(0),
      mManualFrameStart(0), mManualFrame(0), mLastManualFrame(-1), mManualFadeTime(0),
      mEndFrame(0), mLocked(0), mHue(0) {}

LightPreset::~LightPreset() { Clear(); }

BEGIN_HANDLERS(LightPreset)
    HANDLE(set_keyframe, OnSetKeyframe)
    HANDLE(view_keyframe, OnViewKeyframe)
    HANDLE_ACTION(next, OnKeyframeCmd(kPresetKeyframeNext))
    HANDLE_ACTION(prev, OnKeyframeCmd(kPresetKeyframePrev))
    HANDLE_ACTION(first, OnKeyframeCmd(kPresetKeyframeFirst))
    HANDLE_ACTION(reset_events, ResetEvents())
    HANDLE_SUPERCLASS(RndAnimatable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void LightPreset::ResetEvents() { sManualEvents.clear(); }

template <class T>
__forceinline const char *GetObjName(const ObjPtrVec<T> &vec, int idx) {
    if (idx >= vec.size())
        return "<obj index out of bounds>";
    else if (!vec[idx])
        return "<obj not found>";
    else
        return vec[idx]->Name();
}

const char *GetName(LightPreset *preset, int idx, LightPreset::PresetObject obj) {
    switch (obj) {
    case LightPreset::kPresetSpotlight:
        return GetObjName(preset->mSpotlights, idx);
    case LightPreset::kPresetSpotlightDrawer:
        return GetObjName(preset->mSpotlightDrawers, idx);
    case LightPreset::kPresetEnv:
        return GetObjName(preset->mEnvironments, idx);
    case LightPreset::kPresetLight:
        return GetObjName(preset->mLights, idx);
    default:
        return "<invalid preset object>";
    }
}

BEGIN_CUSTOM_PROPSYNC(LightPreset::EnvironmentEntry)
    SYNC_PROP_SET(
        environment, GetName(gEditPreset, _prop->Int(_i - 1), LightPreset::kPresetEnv),
    )
    SYNC_PROP(ambient_color, o.mAmbientColor)
    SYNC_PROP_SET(fog_enable, o.mFogEnable, )
    SYNC_PROP_SET(fog_start, o.mFogStart, )
    SYNC_PROP_SET(fog_end, o.mFogEnd, )
    SYNC_PROP(fog_color, o.mFogColor)
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(LightPreset::EnvLightEntry)
    SYNC_PROP_SET(
        light, GetName(gEditPreset, _prop->Int(_i - 1), LightPreset::kPresetLight),
    )
    SYNC_PROP(position, o.mPosition)
    SYNC_PROP_SET(color, o.mColor.Pack(), )
    SYNC_PROP_SET(range, o.mRange, )
    SYNC_PROP_SET(type, RndLight::TypeToStr(o.mLightType), ) {
        static Symbol _s("rotation");
        if (sym == _s) {
            MakeRotMatrix(o.mOrientation, o.mRotation);
            if (PropSync(o.mRotation, _val, _prop, _i + 1, _op))
                return true;
            else
                return false;
        }
    }
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(LightPreset::SpotlightEntry)
    SYNC_PROP_SET(
        spotlight,
        GetName(gEditPreset, _prop->Int(_i - 1), LightPreset::kPresetSpotlight),
    )
    SYNC_PROP_SET(intensity, o.mIntensity, )
    SYNC_PROP_SET(color, o.mColor, )
    SYNC_PROP(target, o.mTarget)
    SYNC_PROP_SET(flare_enabled, o.mFlags & LightPreset::SpotlightEntry::kEnabled, ) {
        static Symbol _s("rotation");
        if (sym == _s) {
            MakeRotMatrix(o.mRotation, o.mRotationMatrix);
            if (PropSync(o.mRotationMatrix, _val, _prop, _i + 1, _op))
                return true;
            else
                return false;
        }
    }
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(LightPreset::SpotlightDrawerEntry)
    SYNC_PROP_SET(
        spotlight_drawer,
        GetName(gEditPreset, _prop->Int(_i - 1), LightPreset::kPresetSpotlightDrawer),
    )
    SYNC_PROP_SET(total, o.mTotalIntensity, )
    SYNC_PROP_SET(base_intensity, o.mBaseIntensity, )
    SYNC_PROP_SET(smoke_intensity, o.mSmokeIntensity, )
    SYNC_PROP_SET(light_influence, o.mLightInfluence, )
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(LightPreset::Keyframe)
    SYNC_PROP(description, o.mDescription)
    SYNC_PROP(duration, o.mDuration)
    SYNC_PROP(fade_out, o.mFadeOutTime)
    SYNC_PROP(spotlight_entries, o.mSpotlightEntries)
    SYNC_PROP(spotlight_drawer_entries, o.mSpotlightDrawerEntries)
    SYNC_PROP(environment_entries, o.mEnvironmentEntries)
    SYNC_PROP(light_entries, o.mLightEntries)
    SYNC_PROP(triggers, o.mTriggers)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(LightPreset)
    gEditPreset = this;
    SYNC_PROP_MODIFY(keyframes, mKeyframes, CacheFrames())
    SYNC_PROP(looping, mLooping)
    SYNC_PROP(category, mCategory)
    SYNC_PROP(select_triggers, mSelectTriggers)
    SYNC_PROP(manual, mManual)
    SYNC_PROP(locked, mLocked)
    SYNC_PROP(platform_only, (int &)mPlatformOnly)
    SYNC_PROP(hue, mHue)
    SYNC_SUPERCLASS(RndAnimatable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(LightPreset)
    SAVE_REVS(0x16, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndAnimatable)
    bs << mKeyframes;
    bs << mSpotlights;
    bs << mEnvironments;
    bs << mLights;
    bs << mLooping;
    bs << mCategory;
    bs << mSelectTriggers;
    bs << mManual;
    bs << mLocked;
    bs << mPlatformOnly;
    bs << mSpotlightDrawers;
END_SAVES

BEGIN_COPYS(LightPreset)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndAnimatable)
    CREATE_COPY(LightPreset)
    BEGIN_COPYING_MEMBERS
        Clear();
        COPY_MEMBER(mKeyframes)
        COPY_MEMBER(mSpotlights)
        COPY_MEMBER(mEnvironments)
        COPY_MEMBER(mLights)
        COPY_MEMBER(mSpotlightDrawers)
        mSpotlightState.resize(mSpotlights.size());
        mEnvironmentState.resize(mEnvironments.size());
        mLightState.resize(mLights.size());
        COPY_MEMBER(mLooping)
        COPY_MEMBER(mCategory)
        COPY_MEMBER(mSelectTriggers)
        COPY_MEMBER(mManual)
        COPY_MEMBER(mLocked)
        COPY_MEMBER(mPlatformOnly)
        CacheFrames();
    END_COPYING_MEMBERS
END_COPYS

void LightPreset::StartAnim() {
    mManualFrame = 0;
    mLastManualFrame = -1;
    mManualFrameStart = 0;
    mManualFadeTime = 0;
    mStartBeat = TheTaskMgr.Beat();
    mLastKeyframe = 0;
    mLastBlend = -1.0f;
    static Message start_anim_msg("start_anim_msg");
    Handle(start_anim_msg, false);
    FOREACH (it, mSelectTriggers) {
        (*it)->Trigger();
    }
}

void LightPreset::SetFrame(float frame, float blend) { SetFrameEx(frame, blend, false); }

int LightPreset::GetCurrentKeyframe() const {
    if (mManual)
        return mManualFrame;
    else if (mKeyframes.empty())
        return -1;
    else {
        int i;
        int ret;
        float f;
        GetKey(GetFrame(), i, ret, f);
        return ret;
    }
}

bool LightPreset::PlatformOk() const {
    if (TheLoadMgr.EditMode() || !mPlatformOnly
        || TheLoadMgr.GetPlatform() == kPlatformNone) {
        return true;
    } else {
        Platform plat = TheLoadMgr.GetPlatform();
        if (TheLoadMgr.GetPlatform() == kPlatformPC) {
            plat = kPlatformXBox;
        }
        return plat == mPlatformOnly;
    }
}

int LightPreset::NextManualFrame(LightPreset::KeyframeCmd cmd) const {
    int frame;
    if (cmd == kPresetKeyframeFirst) {
        frame = 0;
    } else {
        frame = mManualFrame + (cmd == kPresetKeyframeNext ? 1 : -1);
    }
    if (mLooping) {
        return frame % mKeyframes.size();
    } else {
        return Max<int>(0, Min<int>(frame, mKeyframes.size() - 1));
    }
}

void LightPreset::AdvanceManual(LightPreset::KeyframeCmd cmd) {
    MILO_ASSERT(mManual, 0x2c0);
    if (cmd != kPresetKeyframeFirst || mManualFrame) {
        mManualFrameStart = GetFrame();
        mLastManualFrame = mManualFrame;
        mManualFrame = NextManualFrame(cmd);
    }
}

void LightPreset::FillLightPresetData(RndLight *light, LightPreset::EnvLightEntry &entry) {
    entry.mColor = light->GetColor();
    entry.mOrientation = Hmx::Quat(light->WorldXfm().m);
    entry.mPosition = light->WorldXfm().v;
    entry.mRange = light->Range();
    entry.mLightType = light->GetType();
}

void LightPreset::RemoveLight(int idx) {
    for (uint i = 0; i != mKeyframes.size(); i++) {
        Keyframe &cur = mKeyframes[i];
        cur.mLightEntries.erase(cur.mLightEntries.begin() + idx);
    }
    mLightState.erase(mLightState.begin() + idx);
    mLights.erase(mLights.begin() + idx);
}

void LightPreset::RemoveSpotlightDrawer(int idx) {
    for (uint i = 0; i != mKeyframes.size(); i++) {
        Keyframe &cur = mKeyframes[i];
        cur.mSpotlightDrawerEntries.erase(cur.mSpotlightDrawerEntries.begin() + idx);
    }
    mSpotlightDrawerState.erase(mSpotlightDrawerState.begin() + idx);
    mSpotlightDrawers.erase(mSpotlightDrawers.begin() + idx);
}

void LightPreset::ApplyState(const LightPreset::Keyframe &k) {
    mSpotlightState = k.mSpotlightEntries;
    mEnvironmentState = k.mEnvironmentEntries;
    mLightState = k.mLightEntries;
    mSpotlightDrawerState = k.mSpotlightDrawerEntries;
}

void LightPreset::RemoveSpotlight(int idx) {
    for (uint i = 0; i != mKeyframes.size(); i++) {
        Keyframe &cur = mKeyframes[i];
        cur.mSpotlightEntries.erase(cur.mSpotlightEntries.begin() + idx);
    }
    mSpotlightState.erase(mSpotlightState.begin() + idx);
    mSpotlights.erase(mSpotlights.begin() + idx);
}

void LightPreset::RemoveEnvironment(int idx) {
    for (uint i = 0; i != mKeyframes.size(); i++) {
        Keyframe &cur = mKeyframes[i];
        cur.mEnvironmentEntries.erase(cur.mEnvironmentEntries.begin() + idx);
    }
    mEnvironmentState.erase(mEnvironmentState.begin() + idx);
    mEnvironments.erase(mEnvironments.begin() + idx);
}

void LightPreset::AddLight(RndLight *lit) {
    mLights.push_back(lit);
    EnvLightEntry e;
    FillLightPresetData(lit, e);
    for (uint i = 0; i != mKeyframes.size(); i++) {
        mKeyframes[i].mLightEntries.push_back(e);
        MILO_ASSERT(mKeyframes[i].mLightEntries.size() == mLights.size(), 0x41a);
    }
    mLightState.push_back(e);
}

void LightPreset::Clear() {
    mKeyframes.clear();
    mSpotlights.clear();
    mEnvironments.clear();
    mSpotlightDrawers.clear();
    mLights.clear();
}

void LightPreset::OnKeyframeCmd(LightPreset::KeyframeCmd cmd) {
    sManualEvents.push_back(std::make_pair(cmd, TheTaskMgr.Beat() + 4.0f));
}

void LightPreset::AddEnvironment(RndEnviron *env) {
    mEnvironments.push_back(env);
    EnvironmentEntry e;
    FillEnvPresetData(env, e);
    for (int i = 0; i != mKeyframes.size(); i++) {
        mKeyframes[i].mEnvironmentEntries.push_back(e);
        MILO_ASSERT(mKeyframes[i].mEnvironmentEntries.size() == mEnvironments.size(), 0x40A);
    }
    mEnvironmentState.push_back(e);
}

void LightPreset::FillSpotlightDrawerPresetData(
    SpotlightDrawer *sd, LightPreset::SpotlightDrawerEntry &e
) {
    e.mBaseIntensity = sd->Params().mBaseIntensity;
    e.mSmokeIntensity = sd->Params().mSmokeIntensity;
    e.mLightInfluence = sd->Params().mLightingInfluence;
    e.mTotalIntensity = sd->Params().mIntensity;
}

void LightPreset::AddSpotlightDrawer(SpotlightDrawer *sd) {
    mSpotlightDrawers.push_back(sd);
    SpotlightDrawerEntry e;
    FillSpotlightDrawerPresetData(sd, e);
    for (int i = 0; i != mKeyframes.size(); i++) {
        mKeyframes[i].mSpotlightDrawerEntries.push_back(e);
        MILO_ASSERT(mKeyframes[i].mSpotlightDrawerEntries.size() == mSpotlightDrawers.size(), 0x42A);
    }
    mSpotlightDrawerState.push_back(e);
}

void LightPreset::AddSpotlight(Spotlight *s, bool b) {
    mSpotlights.push_back(s);
    SpotlightEntry e(this);
    FillSpotPresetData(s, e, -1);
    if (b) {
        e.mIntensity = 0;
        e.mColor = 0;
    }
    for (int i = 0; i != mKeyframes.size(); i++) {
        mKeyframes[i].mSpotlightEntries.push_back(e);
        MILO_ASSERT(mKeyframes[i].mSpotlightEntries.size() == mSpotlights.size(), 0x3FA);
    }
    mSpotlightState.push_back(e);
}

void LightPreset::SetSpotlight(Spotlight *s, int data) {
    uint idx;
    for (idx = 0; idx != mSpotlights.size(); idx++) {
        if (mSpotlights[idx] == s)
            break;
    }
    if (idx == mSpotlights.size())
        AddSpotlight(s, false);
    for (uint i = 0; i != mKeyframes.size(); i++) {
        FillSpotPresetData(s, mKeyframes[i].mSpotlightEntries[idx], data);
    }
}

void LightPreset::FillSpotPresetData(
    Spotlight *spot, LightPreset::SpotlightEntry &entry, int mask
) {
    if (mask & 1) {
        entry.mIntensity = spot->Intensity();
        entry.mColor = spot->Color().Pack();
    }
    if (mask & 2) {
        entry.mTarget = spot->GetTarget();
        entry.mRotation =
            entry.mTarget ? Hmx::Quat(0, 0, 0, 0) : Hmx::Quat(spot->mWorldXfm.m);
    }
    if (mask != 0 && spot->IsFlareEnabled()) {
        entry.mFlags |= SpotlightEntry::kEnabled;
    }
}

DataNode LightPreset::OnViewKeyframe(DataArray *da) {
    ApplyState(mKeyframes[da->Int(2)]);
    Animate(1.0f);
    return 0;
}

void LightPreset::SyncKeyframeTargets() {
    for (ObjDirItr<Spotlight> it(Dir(), true); it; ++it) {
        Spotlight *key = it;
        ObjPtrVec<Spotlight, ObjectDir>::const_iterator found = mSpotlights.find(key);
        if (found == mSpotlights.end()) {
            AddSpotlight(key, false);
        }
    }
    for (ObjDirItr<RndEnviron> it(Dir(), true); it; ++it) {
        RndEnviron *key = it;
        ObjPtrVec<RndEnviron, ObjectDir>::const_iterator found = mEnvironments.find(key);
        if (found == mEnvironments.end()) {
            AddEnvironment(key);
        }
        FOREACH (lit, key->mLightsReal) {
            RndLight *lkey = *lit;
            ObjPtrVec<RndLight, ObjectDir>::const_iterator lfound = mLights.find(lkey);
            if (lfound == mLights.end()) {
                AddLight(lkey);
            }
        }
        FOREACH (lit, key->mLightsApprox) {
            RndLight *lkey = *lit;
            ObjPtrVec<RndLight, ObjectDir>::const_iterator lfound = mLights.find(lkey);
            if (lfound == mLights.end()) {
                AddLight(lkey);
            }
        }
    }
    for (ObjDirItr<SpotlightDrawer> it(Dir(), true); it; ++it) {
        SpotlightDrawer *key = it;
        ObjPtrVec<SpotlightDrawer, ObjectDir>::const_iterator found = mSpotlightDrawers.find(key);
        if (found == mSpotlightDrawers.end()) {
            AddSpotlightDrawer(key);
        }
    }
    CacheFrames();
}

#pragma region Entry Animate Methods

void LightPreset::EnvironmentEntry::Animate(const EnvironmentEntry &other, float t) {
    Interp(mAmbientColor, other.mAmbientColor, t, mAmbientColor);
    Interp(mFogStart, other.mFogStart, t, mFogStart);
    Interp(mFogEnd, other.mFogEnd, t, mFogEnd);
    Interp(mFogColor, other.mFogColor, t, mFogColor);
    Interp(mFogEnable, other.mFogEnable, t, mFogEnable);
}

void LightPreset::EnvLightEntry::Animate(const EnvLightEntry &other, float t) {
    Interp(mColor, other.mColor, t, mColor);
    Interp(mPosition, other.mPosition, t, mPosition);
    Interp(mRange, other.mRange, t, mRange);
    Interp(mOrientation, other.mOrientation, t, mOrientation);
}

void LightPreset::SpotlightEntry::Animate(
    Spotlight *spot, const SpotlightEntry &other, float t
) {
    Interp(mIntensity, other.mIntensity, t, mIntensity);
    Hmx::Color c1, c2, result;
    c1.Unpack(mColor);
    c2.Unpack(other.mColor);
    Interp(c1, c2, t, result);
    mColor = result.Pack();
    if (mRotation != Hmx::Quat(0, 0, 0, 0) && other.mRotation != Hmx::Quat(0, 0, 0, 0)) {
        Interp(mRotation, other.mRotation, t, mRotation);
    }
}

void LightPreset::SpotlightEntry::CalculateDirection(
    Spotlight *spot, Hmx::Quat &q
) const {
    if (mTarget) {
        Hmx::Matrix3 m;
        spot->CalculateDirection(mTarget, m);
        q = Hmx::Quat(m);
    } else {
        q.Reset();
    }
}

#pragma endregion

#pragma region Stubbed LightPreset Methods

void LightPreset::CacheFrames() {
    float f = 0;
    for (uint i = 0; i != mKeyframes.size(); i++) {
        Keyframe &kf = mKeyframes[i];
        kf.unka8 = f;
        f += kf.mDuration + kf.mFadeOutTime;
        kf.mSpotlightChanges.clear();
        kf.mSpotlightChanges.resize(kf.mSpotlightEntries.size());
        kf.mEnvironmentChanges.clear();
        kf.mEnvironmentChanges.resize(kf.mEnvironmentEntries.size());
        kf.mLightChanges.clear();
        kf.mLightChanges.resize(kf.mLightEntries.size());
        kf.mSpotlightDrawerChanges.clear();
        kf.mSpotlightDrawerChanges.resize(kf.mSpotlightDrawerEntries.size());
        if (mLooping || i != 0) {
            Keyframe &prev = mKeyframes[i == 0 ? mKeyframes.size() - 1 : i - 1];
            for (uint j = 0; j != kf.mSpotlightEntries.size(); j++) {
                if (kf.mSpotlightEntries[j] != prev.mSpotlightEntries[j]) {
                    kf.mSpotlightChanges[j] = true;
                }
            }
            for (uint j = 0; j != kf.mEnvironmentEntries.size(); j++) {
                if (kf.mEnvironmentEntries[j] != prev.mEnvironmentEntries[j]) {
                    kf.mEnvironmentChanges[j] = true;
                }
            }
            for (uint j = 0; j != kf.mLightEntries.size(); j++) {
                if (kf.mLightEntries[j] != prev.mLightEntries[j]) {
                    kf.mLightChanges[j] = true;
                }
            }
            for (uint j = 0; j != kf.mSpotlightDrawerEntries.size(); j++) {
                if (kf.mSpotlightDrawerEntries[j] != prev.mSpotlightDrawerEntries[j]) {
                    kf.mSpotlightDrawerChanges[j] = true;
                }
            }
        }
    }
    mEndFrame = f;
}

void LightPreset::GetKey(float frame, int &prevIdx, int &curIdx, float &blend) const {
    if (frame <= 0.0f || mEndFrame <= 0.0f) {
        prevIdx = -1;
        curIdx = 0;
        blend = 1.0f;
        return;
    }
    float theframe = frame;
    if (mLooping) {
        theframe = std::fmod(frame, mEndFrame);
        if (frame >= mKeyframes.back().unka8) {
            if (mKeyframes.back().mFadeOutTime <= 0.0f) {
                prevIdx = -1;
                curIdx = mKeyframes.size() - 1;
                blend = 1.0f;
                return;
            }
            float framedur = mKeyframes.back().unka8 + mKeyframes.back().mDuration;
            if (theframe > framedur) {
                prevIdx = mKeyframes.size() - 1;
                curIdx = 0;
                blend = (theframe - framedur) / mKeyframes.back().mFadeOutTime;
                return;
            }
            prevIdx = -1;
            curIdx = mKeyframes.size() - 1;
            blend = 1.0f;
            return;
        }
    } else if (frame >= mKeyframes.back().unka8) {
        prevIdx = -1;
        curIdx = mKeyframes.size() - 1;
        blend = 1.0f;
        return;
    }

    // Binary search for the keyframe
    int lo = 0;
    int hi = mKeyframes.size() - 1;
    while (lo + 1 < hi) {
        int mid = (lo + hi) >> 1;
        if (theframe < mKeyframes[mid].unka8)
            hi = mid;
        else
            lo = mid;
    }

    float dur = mKeyframes[lo].unka8 + mKeyframes[lo].mDuration;
    if (theframe > dur) {
        prevIdx = lo;
        curIdx = hi;
        blend = (theframe - dur) / mKeyframes[lo].mFadeOutTime;
    } else {
        prevIdx = -1;
        curIdx = lo;
        blend = 1.0f;
    }
}

void LightPreset::AnimateState(
    const LightPreset::Keyframe &kPrev,
    const LightPreset::Keyframe &kCur,
    float t
) {
    if (t < 1.1920929E-7f)
        return;
    for (uint i = 0; i != mSpotlightState.size(); i++) {
        if (kCur.mSpotlightChanges[i]) {
            mSpotlightState[i].Animate(mSpotlights[i], kPrev.mSpotlightEntries[i], t);
        }
    }
    for (uint i = 0; i != mEnvironmentState.size(); i++) {
        if (kCur.mEnvironmentChanges[i]) {
            mEnvironmentState[i].Animate(kPrev.mEnvironmentEntries[i], t);
        }
    }
    for (uint i = 0; i != mLightState.size(); i++) {
        if (kCur.mLightChanges[i]) {
            mLightState[i].Animate(kPrev.mLightEntries[i], t);
        }
    }
    for (uint i = 0; i != mSpotlightDrawerState.size(); i++) {
        if (kCur.mSpotlightDrawerChanges[i]) {
            SpotlightDrawerEntry &state = mSpotlightDrawerState[i];
            const SpotlightDrawerEntry &prev = kPrev.mSpotlightDrawerEntries[i];
            Interp(state.mTotalIntensity, prev.mTotalIntensity, t, state.mTotalIntensity);
            Interp(state.mBaseIntensity, prev.mBaseIntensity, t, state.mBaseIntensity);
            Interp(state.mSmokeIntensity, prev.mSmokeIntensity, t, state.mSmokeIntensity);
            Interp(state.mLightInfluence, prev.mLightInfluence, t, state.mLightInfluence);
        }
    }
}

void LightPreset::SetFrameEx(float frame, float blend, bool b) {
    RndAnimatable::SetFrame(blend, frame);
    if ((unsigned int)frame == 0 && TheLoadMgr.EditMode()) {
        SyncNewSpotlights();
    }
    if (!mKeyframes.empty()) {
        Keyframe *kfPrev = nullptr;
        float f = 1.0f;
        Keyframe *kfCur;
        if (mManual) {
            kfCur = &mKeyframes[mManualFrame];
            while (!sManualEvents.empty() && sManualEvents.front().second <= mStartBeat) {
                sManualEvents.pop_front();
            }
            if (!sManualEvents.empty()) {
                float fadeTime = kfCur->mFadeOutTime;
                float eventBeat = sManualEvents.front().second;
                float beat = TheTaskMgr.Beat();
                if (eventBeat - fadeTime / 480.0f <= beat) {
                    AdvanceManual(sManualEvents.front().first);
                    beat = TheTaskMgr.Beat();
                    if (eventBeat > beat) {
                        beat = TheTaskMgr.Beat();
                        mManualFadeTime = (eventBeat - beat) * 480.0f;
                    } else {
                        mManualFadeTime = 0;
                    }
                    sManualEvents.pop_front();
                    kfCur = &mKeyframes[mManualFrame];
                }
            }
            if (mLastManualFrame != -1) {
                kfPrev = &mKeyframes[mLastManualFrame];
                if (mManualFadeTime >= 1) {
                    f = Min((frame - mManualFrameStart) / mManualFadeTime, 1.0f);
                    f = Max(0.0f, f);
                } else {
                    f = 0;
                }
            }
        } else {
            int iPrev, iCur;
            GetKey(frame, iPrev, iCur, f);
            kfCur = &mKeyframes[iCur];
            if (iPrev != -1)
                kfPrev = &mKeyframes[iPrev];
        }

        bool same = false;
        Keyframe *last = mLastKeyframe;
        if (kfCur == last && mLastBlend == f)
            same = true;
        if (!same) {
            ApplyState(*kfCur);
            if (kfPrev) {
                AnimateState(*kfPrev, *kfCur, 1.0f - f);
            }
            mLastKeyframe = kfCur;
            mLastBlend = f;
        }
        if (!same || !b) {
            Animate(blend);
        }
        if (kfCur != last) {
            FOREACH (it, mLastKeyframe->mTriggers) {
                (*it)->Trigger();
            }
        }
    }
}

void LightPreset::SetKeyframe(Keyframe &k) {
    for (uint i = 0; i != k.mSpotlightEntries.size(); i++) {
        FillSpotPresetData(mSpotlights[i], k.mSpotlightEntries[i], -1);
    }
    for (uint i = 0; i != k.mEnvironmentEntries.size(); i++) {
        FillEnvPresetData(mEnvironments[i], k.mEnvironmentEntries[i]);
    }
    for (uint i = 0; i != k.mLightEntries.size(); i++) {
        FillLightPresetData(mLights[i], k.mLightEntries[i]);
    }
    for (uint i = 0; i != k.mSpotlightDrawerEntries.size(); i++) {
        FillSpotlightDrawerPresetData(mSpotlightDrawers[i], k.mSpotlightDrawerEntries[i]);
    }
}

DataNode LightPreset::OnSetKeyframe(DataArray *da) {
    int idx = da->Int(2);
    SetKeyframe(mKeyframes[idx]);
    CacheFrames();
    return 0;
}

void LightPreset::FillEnvPresetData(RndEnviron *env, EnvironmentEntry &e) {
    e.mAmbientColor = env->AmbientColor();
    e.mFogColor = env->FogColor();
    e.mFogEnable = env->FogEnable();
#ifdef HX_NATIVE
    e.mFogStart = env->FogStart();
    e.mFogEnd = env->FogEnd();
#endif
}

void LightPreset::TranslateColor(const Hmx::Color &col, Hmx::Color &res) {
    if (mHue)
        mHue->TranslateColor(col, res);
    else
        res = col;
}

void LightPreset::AnimateLightFromPreset(
    RndLight *light, const EnvLightEntry &entry, float f
) {
    if (light->Showing()) {
        Hmx::Color c;
        TranslateColor(entry.mColor, c);
        Interp(light->GetColor(), c, f, c);
        light->SetColor(c);
    }
    if (light->AnimateRangeFromPreset()) {
        float range;
        Interp(light->Range(), entry.mRange, f, range);
        light->SetRange(range);
    }
    if (light->AnimatePosFromPreset()) {
        Hmx::Matrix3 m;
        MakeRotMatrix(entry.mOrientation, m);
        Transform tf;
        Interp(light->WorldXfm().v, entry.mPosition, f, tf.v);
        Interp(light->WorldXfm().m, m, f, tf.m);
        light->SetWorldXfm(tf);
    }
}

static void AnimateEnvFromPreset(
    LightPreset *preset, RndEnviron *env, const LightPreset::EnvironmentEntry &entry, float f
) {
    Hmx::Color c;
    preset->TranslateColor(entry.mAmbientColor, c);
    Interp(env->AmbientColor(), c, f, c);
    env->SetAmbientColor(c);
#ifdef HX_NATIVE
    float fogStart, fogEnd;
    if (entry.mFogEnable) {
        Hmx::Color fc;
        preset->TranslateColor(entry.mFogColor, fc);
        Interp(env->FogColor(), fc, f, fc);
        env->SetFogColor(fc);
        Interp(env->FogStart(), entry.mFogStart, f, fogStart);
        Interp(env->FogEnd(), entry.mFogEnd, f, fogEnd);
    } else {
        float far = RndCam::Current() ? RndCam::Current()->FarPlane() : FLT_MAX;
        Interp(env->FogStart(), far, f, fogStart);
        Interp(env->FogEnd(), far, f, fogEnd);
    }
    env->SetFogRange(fogStart, fogEnd);
    if (f == 1.0f)
        env->SetFogEnable(entry.mFogEnable);
#endif
}

static void AnimateSpotFromPreset(
    LightPreset *preset, Spotlight *spot, const LightPreset::SpotlightEntry &entry, float f
) {
    if (spot->AnimateColorFromPreset()) {
        Hmx::Color spotColor = spot->Color();
        float intensity = spot->Intensity();
        Hmx::Color col;
        col.Unpack(entry.mColor);
        preset->TranslateColor(col, col);
        Interp(spotColor, col, f, spotColor);
        Interp(intensity, entry.mIntensity, f, intensity);
        spot->SetColorIntensity(spotColor, intensity);
        if (spot->GetFlare() && f == 1.0f) {
            spot->SetFlareEnabled(entry.mFlags & LightPreset::SpotlightEntry::kEnabled);
        }
    }
}

static void AnimateSpotlightDrawerFromPreset(
    SpotlightDrawer *sd, const LightPreset::SpotlightDrawerEntry &e, float f
) {
    SpotDrawParams &p = const_cast<SpotDrawParams &>(sd->Params());
    float val;
    Interp(p.mBaseIntensity, e.mBaseIntensity, f, val);
    p.mBaseIntensity = val;
    Interp(p.mSmokeIntensity, e.mSmokeIntensity, f, val);
    p.mSmokeIntensity = val;
    Interp(p.mLightingInfluence, e.mLightInfluence, f, val);
    p.mLightingInfluence = val;
    Interp(p.mIntensity, e.mTotalIntensity, f, val);
    p.mIntensity = val;
}

static float ComputeSpotBlend(int i, float f) {
    int min = Min<int>((int)(f * 5.0f), 4);
    if (i % 5 < min)
        return 1.0f;
    else if (i % 5 > min)
        return 0.0f;
    else
        return Min(Max((f - i / 5.0f) * 5.0f, 0.0f), 1.0f);
}

void LightPreset::Animate(float f) {
    if (f < 1.1920929E-7f)
        return;
    for (uint i = 0; i != mSpotlights.size(); i++) {
        if (mSpotlights[i] && mSpotlights[i]->GetAnimateFromPreset()) {
            float blend = ComputeSpotBlend(i, f);
            if (blend >= 1.1920929E-7f) {
                AnimateSpotFromPreset(this, mSpotlights[i], mSpotlightState[i], blend);
            }
        }
    }
    for (uint i = 0; i != mEnvironments.size(); i++) {
        if (mEnvironments[i] && mEnvironments[i]->GetAnimateFromPreset()) {
            AnimateEnvFromPreset(this, mEnvironments[i], mEnvironmentState[i], f);
        }
    }
    for (uint i = 0; i != mLights.size(); i++) {
        if (mLights[i] && mLights[i]->GetAnimateFromPreset()) {
            AnimateLightFromPreset(mLights[i], mLightState[i], f);
        }
    }
    for (uint i = 0; i != mSpotlightDrawers.size(); i++) {
        if (mSpotlightDrawers[i]) {
            AnimateSpotlightDrawerFromPreset(mSpotlightDrawers[i], mSpotlightDrawerState[i], f);
        }
    }
}

void LightPreset::SyncNewSpotlights() {
    for (ObjDirItr<Spotlight> it(Dir(), true); it; ++it) {
        Spotlight *key = it;
        ObjPtrVec<Spotlight, ObjectDir>::const_iterator found = mSpotlights.find(key);
        if (found == mSpotlights.end()) {
            AddSpotlight(key, true);
        }
    }
}

INIT_REVS(0x16, 0)

BEGIN_LOADS(LightPreset)
    AutoLoading al;
    Clear();
    LOAD_REVS(bs)
    ASSERT_REVS(0x16, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    auto& _ref0 = mKeyframes;
    if (d.rev != 0xE) {
        LOAD_SUPERCLASS(RndAnimatable)
        // Load keyframes manually since there's no BinStream >> Keyframe operator
        {
            unsigned int count;
            bs >> count;
            _ref0.resize(count);
            BinStreamRev rev(bs, d.rev);
            for (uint i = 0; i < count; i++) {
                _ref0[i].Load(rev);
            }
        }
    } else {
        _ref0.resize(1);
        BinStreamRev rev(bs, 0xE);
        _ref0[0].LegacyLoadP9(rev);
    }
    bs >> mSpotlights;
    bs >> mEnvironments;
    bs >> mLights;
    if (d.rev != 0xE)
        bs >> mLooping;
    bs >> mCategory;
    String str(mCategory.Str());
    str.ToLower();
    mCategory = Symbol(str.c_str());
    if (d.rev < 0x15) {
        ObjPtr<EventTrigger> trigPtr(this, 0);
        bs >> trigPtr;
        if (trigPtr)
            mSelectTriggers.push_back(trigPtr);
    } else {
        bs >> mSelectTriggers;
    }
    if (d.rev != 0xE) {
        bs >> mManual;
    }
    bs >> mLocked;
    if (d.rev > 0xC)
        bs >> (int &)mPlatformOnly;
    if (d.rev > 9) {
        bs >> mSpotlightDrawers;
    }
    mSpotlightState.resize(mSpotlights.size());
    mEnvironmentState.resize(mEnvironments.size());
    mLightState.resize(mLights.size());
    mSpotlightDrawerState.resize(mSpotlightDrawers.size());
    for (int i = 0; i != (int)mSpotlights.size(); i++) {
        if (!mSpotlights[i] || !mSpotlights[i]->GetAnimateFromPreset()) {
            RemoveSpotlight(i);
            i--;
        }
    }
    for (int i = 0; i != (int)mEnvironments.size(); i++) {
        if (!mEnvironments[i] || !mEnvironments[i]->GetAnimateFromPreset()) {
            RemoveEnvironment(i);
            i--;
        }
    }
    for (int i = 0; i != (int)mLights.size(); i++) {
        if (!mLights[i] || !mLights[i]->GetAnimateFromPreset()) {
            RemoveLight(i);
            i--;
        }
    }
    for (int i = 0; i != (int)mSpotlightDrawers.size(); i++) {
        if (!mSpotlightDrawers[i]) {
            RemoveSpotlightDrawer(i);
            i--;
        }
    }
    SyncNewSpotlights();
    CacheFrames();
END_LOADS

bool LightPreset::Replace(ObjRef *from, Hmx::Object *to) {
    if (!to) {
        Hmx::Object *old = from->GetObj();
        for (int i = 0; i < (int)mSpotlights.size(); i++) {
            if (mSpotlights[i] == old) {
                RemoveSpotlight(i);
                CacheFrames();
                return true;
            }
        }
        for (int i = 0; i < (int)mEnvironments.size(); i++) {
            if (mEnvironments[i] == old) {
                RemoveEnvironment(i);
                CacheFrames();
                return true;
            }
        }
        for (int i = 0; i < (int)mLights.size(); i++) {
            if (mLights[i] == old) {
                RemoveLight(i);
                CacheFrames();
                return true;
            }
        }
        for (int i = 0; i < (int)mSpotlightDrawers.size(); i++) {
            if (mSpotlightDrawers[i] == old) {
                RemoveSpotlightDrawer(i);
                CacheFrames();
                return true;
            }
        }
    }
    return RndAnimatable::Replace(from, to);
}

#pragma endregion
