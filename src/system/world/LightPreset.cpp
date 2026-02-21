#include "world/LightPreset.h"
#include "LightPreset.h"
#include "SpotlightDrawer.h"
#include "math/Mtx.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Anim.h"
#include "rndobj/Env.h"
#include "rndobj/PostProc.h"
#include "utl/BinStream.h"
#include "utl/Loader.h"
#include <algorithm>

// Explicit template instantiation
namespace stlpmtx_std {
    template class vector<ObjPtrVec<Spotlight, ObjectDir>::Node, StlNodeAlloc<ObjPtrVec<Spotlight, ObjectDir>::Node>>;
}

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
    : mIntensity(0), mColor(0), unk8(3), mTarget(owner) {
    unk20.Reset();
    unk30.Zero();
}

void LightPreset::SpotlightEntry::Save(BinStream &bs) const {
    Hmx::Color color(mColor);
    bs << mIntensity;
    bs << unk20;
    bs << color;
    bs << mTarget;
    bs << (bool)(unk8 & 1);
}

void LightPreset::SpotlightEntry::Load(BinStreamRev &d) {
    float intensity;
    d >> intensity;
    mIntensity = intensity;
    d >> unk20;
    Hmx::Color color;
    d >> color;
    color.alpha = 1;
    mColor = color.Pack();
    if (!mTarget.Load(d.stream, false, nullptr)) {
        unk8 &= ~2;
    }
    if (d.rev < 0x13) {
        Symbol s;
        d >> s;
    }
    if (d.rev > 1) {
        bool b;
        d >> b;
        if (b) {
            unk8 |= kEnabled;
        } else {
            unk8 &= ~kEnabled;
        }
        if (d.rev < 9) {
            int x;
            d >> x;
        }
    }
    if (mTarget || !(unk8 & 2)) {
        unk20.Set(0, 0, 0, 0);
    }
}

bool LightPreset::SpotlightEntry::operator!=(const LightPreset::SpotlightEntry &e) const {
    return e.mIntensity != mIntensity || e.unk8 != unk8 || e.mTarget != mTarget
        || (unsigned int)e.mColor != mColor || e.unk20 != unk20;
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
    SYNC_PROP_SET(flare_enabled, o.unk8 & LightPreset::SpotlightEntry::kEnabled, ) {
        static Symbol _s("rotation");
        if (sym == _s) {
            MakeRotMatrix(o.unk20, o.unk30);
            if (PropSync(o.unk30, _val, _prop, _i + 1, _op))
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
        entry.unk20 =
            entry.mTarget ? Hmx::Quat(0, 0, 0, 0) : Hmx::Quat(spot->mWorldXfm.m);
    }
    if (mask != 0 && spot->IsFlareEnabled()) {
        entry.unk8 |= SpotlightEntry::kEnabled;
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
