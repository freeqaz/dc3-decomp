#include "char/CharEyes.h"
#include "char/CharInterest.h"
#include "char/CharWeightable.h"
#include "math/Easing.h"
#include "math/Rand.h"
#include "math/Utl.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "utl/BinStream.h"
#include "utl/Std.h"

void NormalizeScale(const Vector3 &param_2, float param_1, Vector3 &param_4) {
    float fVar1;
    float fVar2;
    float fVar3;
    float dVar4;

    fVar2 = param_2[1];
    dVar4 = 0.0f;
    fVar1 = fVar2 * fVar2;
    fVar3 = param_2[0];
    fVar1 = fVar3 * fVar3 + fVar1;
    fVar3 = param_2[2];
    fVar1 = fVar3 * fVar3 + fVar1;
    fVar1 = sqrtf(fVar1);
    if (fVar1 != 0.0f) {
        dVar4 = 1.0f / fVar1;
    }
    fVar1 = dVar4 * param_1;
    fVar2 = param_2[2];
    fVar3 = param_2[1];
    param_4[2] = fVar1 * fVar2;
    param_4[1] = fVar3 * fVar1;
    param_4[0] = param_2[0] * fVar1;
}

CharEyes::CharEyes()
    : mEyes(this), mInterests(this), mFaceServo(this), mCamWeight(this), mTarget(0, 0, 0),
      mDefaultFilterFlags(0), mViewDirection(this), mHeadLookAt(this),
      mMaxExtrapolation(19.5), mMinTargetDist(35), mUpperLidTrackUp(1),
      mUpperLidTrackDown(1), mLowerLidTrackUp(0.75), mLowerLidTrackDown(0.75),
      mLowerLidTrackRotate(false), mInterestFilterFlags(0), mLastFacing(0, 0, 0), mLastLook(0),
      mLastBlinkWeight(0), mBlinkDetect(0), mBlinkActive(0), mForceFocusInterest(this), mCurrentInterest(this), mFocusTimer(-1), mNeedRecalc(0),
      mDartOffset(0, 1, 0), mDartTimer(0), mDartEnabled(0), mDartInterval(-1), mEyeClampCount(-1), mBlinkEnabled(0),
      mBlinkTimer(-1), mBlinkState(0), mUpperBlinkAngle(-1), mLowerBlinkAngle(-1), mEnabled(0), mHeadIKActive(1) {
    mMaxEyeCang = std::cos(0.5235987715423107);
    mEyeStatusOverlay = RndOverlay::Find("eye_status", false);
}

CharEyes::~CharEyes() {}

BEGIN_HANDLERS(CharEyes)
    HANDLE(add_interest, OnAddInterest)
    HANDLE_ACTION(force_blink, ForceBlink())
    HANDLE(toggle_force_focus, OnToggleForceFocus)
    HANDLE(toggle_interest_overlay, OnToggleInterestOverlay)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(CharEyes::EyeDesc)
    SYNC_PROP(eye, o.mEye)
    SYNC_PROP(upper_lid, o.mUpperLid)
    SYNC_PROP(lower_lid, o.mLowerLid)
    SYNC_PROP(upper_lid_blink, o.mUpperLidBlink)
    SYNC_PROP(lower_lid_blink, o.mLowerLidBlink)
END_CUSTOM_PROPSYNC

BEGIN_CUSTOM_PROPSYNC(CharEyes::CharInterestState)
    SYNC_PROP(interest, o.mInterest)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(CharEyes)
    SYNC_PROP(eyes, mEyes)
    SYNC_PROP(view_direction, mViewDirection)
    SYNC_PROP(interests, mInterests)
    SYNC_PROP(face_servo, mFaceServo)
    SYNC_PROP(camera_weight, mCamWeight)
    SYNC_PROP_BITFIELD(default_interest_categories, mDefaultFilterFlags, 0x685)
    SYNC_PROP(head_lookat, mHeadLookAt)
    SYNC_PROP(max_extrapolation, mMaxExtrapolation)
    SYNC_PROP(disable_eye_dart, sDisableEyeDart)
    SYNC_PROP(disable_eye_jitter, sDisableEyeJitter)
    SYNC_PROP(disable_interest_objects, sDisableInterestObjects)
    SYNC_PROP(disable_procedural_blink, sDisableProceduralBlink)
    SYNC_PROP(disable_eye_clamping, sDisableEyeClamping)
    SYNC_PROP_BITFIELD(interest_filter_testing, mInterestFilterFlags, 0x68E)
    SYNC_PROP(min_target_dist, mMinTargetDist)
    SYNC_PROP(ulid_track_up, mUpperLidTrackUp)
    SYNC_PROP(ulid_track_down, mUpperLidTrackDown)
    SYNC_PROP(llid_track_up, mLowerLidTrackUp)
    SYNC_PROP(llid_track_down, mLowerLidTrackDown)
    SYNC_PROP(llid_track_rotate, mLowerLidTrackRotate)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const CharEyes::EyeDesc &desc) {
    bs << desc.mEye;
    bs << desc.mUpperLid;
    bs << desc.mLowerLid;
    bs << desc.mUpperLidBlink;
    bs << desc.mLowerLidBlink;
    return bs;
}

inline BinStream &operator<<(BinStream &bs, const CharEyes::CharInterestState &state) {
    bs << state.mInterest;
    return bs;
}

BinStream &operator>>(BinStreamRev &bsrev, ObjVector<CharEyes::CharInterestState> &vec) {
    BinStream &bs = bsrev.stream;
    int count;
    bs.ReadEndian(&count, 4);
    vec.resize(count);

    CharEyes::CharInterestState *state = vec.begin();
    while (state != vec.end()) {
        state->mInterest.Load(bs, true, 0);
        state++;
    }

    return bs;
}

BEGIN_SAVES(CharEyes)
    SAVE_REVS(18, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mEyes;
    bs << mInterests;
    bs << mFaceServo;
    bs << mCamWeight;
    bs << mDefaultFilterFlags;
    bs << mViewDirection;
    bs << mHeadLookAt;
    bs << mMaxExtrapolation;
    bs << mMinTargetDist;
    bs << mUpperLidTrackUp;
    bs << mUpperLidTrackDown;
    bs << mLowerLidTrackUp;
    bs << mLowerLidTrackDown;
    bs << mLowerLidTrackRotate;
END_SAVES

BEGIN_COPYS(CharEyes)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(CharEyes)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mEyes)
        COPY_MEMBER(mInterests)
        COPY_MEMBER(mFaceServo)
        COPY_MEMBER(mLastFacing)
        COPY_MEMBER(mLastLook)
        COPY_MEMBER(mCamWeight)
        COPY_MEMBER(mDefaultFilterFlags)
        COPY_MEMBER(mViewDirection)
        COPY_MEMBER(mHeadLookAt)
        COPY_MEMBER(mMaxExtrapolation)
        COPY_MEMBER(mMinTargetDist)
        COPY_MEMBER(mUpperLidTrackUp)
        COPY_MEMBER(mUpperLidTrackDown)
        COPY_MEMBER(mLowerLidTrackUp)
        COPY_MEMBER(mLowerLidTrackDown)
        COPY_MEMBER(mLowerLidTrackRotate)
    END_COPYING_MEMBERS
END_COPYS

CharInterest *CharEyes::GetCurrentInterest() {
    if (mCurrentInterest)
        return mCurrentInterest;
    if (mForceFocusInterest)
        return mForceFocusInterest;
    return 0;
}

void CharEyes::ForceBlink() {
    if (mHeadIKActive && !mBlinkEnabled) {
        mBlinkEnabled = true;
        mBlinkTimer = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        mBlinkState++;
    }
}

void CharEyes::SetEnableBlinks(bool b1, bool b2) {
    mHeadIKActive = b1;
    if (!b2 || b1 || !mBlinkEnabled || !mFaceServo)
        return;

    mFaceServo->SetProceduralBlinkWeight(0.0f);
    mBlinkEnabled = false;
    mTarget = mHeadForward;
}

bool CharEyes::SetFocusInterest(CharInterest *interest, int i) {
    if (mCurrentInterest && (unsigned int)mFocusTimer > i)
        return false;

    mCurrentInterest = interest;
    mFocusTimer = i;
    if (interest != mCurrentInterest)
        mNeedRecalc = true;
    if (!mCurrentInterest)
        mFocusTimer = -1;

    return true;
}

void CharEyes::ToggleInterestsDebugOverlay() {
    if (mEyeStatusOverlay)
        mEyeStatusOverlay->SetShowing(!mEyeStatusOverlay->Showing());
}

bool CharEyes::IsHeadIKWeightIncreasing() {
    if (mHeadLookAt) {
        float weight = mHeadLookAt->Weight();
        return (weight > 0 && weight - mDartTimer > 0);
    }
    return false;
}

void CharEyes::ClearAllInterestObjects() { mInterests.clear(); }

float CharEyes::CharInterestState::RefractoryTimeRemaining() {
    if (!mInterest || mRefractoryTime < 0.0)
        return 0.0f;
    else {
        float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime) - mRefractoryTime;
        if (secs < mInterest->RefractoryPeriod())
            return mInterest->RefractoryPeriod() - secs;
        else
            return 0.0f;
    }
}

void CharEyes::ProceduralBlinkUpdate() {
    static DataNode &disableCheat = DataVariable("cheat.disable_procedural_blinks");

    if (sDisableProceduralBlink)
        return;
    if (disableCheat.Int(0))
        return;
    if (!mHeadIKActive && !mBlinkEnabled)
        return;

    mUpperBlinkAngle = mUpperBlinkAngle - TheTaskMgr.DeltaSeconds();
    if (mUpperBlinkAngle < 0.0f) {
        mBlinkState = 0;
        mUpperBlinkAngle = 15.0f;
    }

    if (!mFaceServo)
        return;
    if (!mBlinkEnabled)
        return;

    CharFaceServo *servo = mFaceServo;
    float elapsed = TheTaskMgr.Seconds(TaskMgr::kRealTime) - mBlinkTimer;
    if (elapsed < 0.115f) {
        // Closing phase
        float t = Clamp(0.0f, 1.0f, elapsed * 8.695652f);
        servo->SetProceduralBlinkWeight(EaseInExp(t));
    } else if (elapsed < 0.3f) {
        // Opening phase
        float t = Clamp(0.0f, 1.0f, 1.0f - (elapsed - 0.115f) * 5.405405f);
        servo->SetProceduralBlinkWeight(EaseSigmoid(t, 0.0f, 0.0f));
        mTarget = mHeadForward;
    } else {
        // Blink complete
        servo->SetProceduralBlinkWeight(0.0f);
        mBlinkEnabled = false;
        mTarget = mHeadForward;
    }
}

void CharEyes::DartUpdate() {
    static DataNode &disableCheat = DataVariable("cheat.disable_eye_darts");

    if (sDisableEyeDart)
        return;
    if (disableCheat.Int(0))
        return;

    mDartInterval -= TheTaskMgr.DeltaSeconds();
    if (mDartInterval < 0.0f) {
        if (mDartEnabled) {
            mEyeClampCount--;
            if (mEyeClampCount < 0) {
                mDartEnabled = false;
                mDartInterval = RandomFloat(
                    mData.mMinSecsBetweenSequences, mData.mMaxSecsBetweenSequences
                );
                return;
            }
            goto set_timer;
        }
        if (EyesOnTarget(mData.mOnTargetAngleThresh) && !mBlinkEnabled) {
            mDartEnabled = true;
            mEyeClampCount =
                RandomInt(mData.mMinDartsPerSequence, mData.mMaxDartsPerSequence);
        set_timer:
            mDartInterval =
                RandomFloat(mData.mMinSecsBetweenDarts, mData.mMaxSecsBetweenDarts);
            mCurrentDartOffset = GenerateDartOffset();
        }
    }
}

RndTransformable *CharEyes::GetHead() {
    if (mViewDirection)
        return mViewDirection;
    else if (!mEyes.empty() && mEyes[0].mEye) {
        RndTransformable *src = mEyes[0].mEye->GetSource();
        if (src)
            return src->TransParent();
    }
    return 0;
}

void CharEyes::Enter() {
    mLastFacing.Zero();
    mLastLook = 0;
    mAvDelta = 0;
    mLastCang = 1.0f;
    mLastBlinkWeight = -1.0f;
    mBlinkDetect = 0;
    mDartEnabled = 0;
    mDartInterval = -1.0f;
    mEyeClampCount = -1;
    mBlinkEnabled = 0;
    mBlinkTimer = -1.0f;
    mBlinkState = 0;
    mUpperBlinkAngle = -1.0f;
    mLowerBlinkAngle = -1.0f;
    mBlinkActive = 0;
    mInterestFilterFlags = mDefaultFilterFlags;
    mEnabled = 0;
    mNeedRecalc = 0;
    mDartTimer = 0;
    RndTransformable *head = GetHead();
    if (head) {
        mLastFacing = head->WorldXfm().m.y;
        Normalize(mLastFacing, mLastFacing);
    }
    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
        it->mEye->Enter();
    }
    for (ObjVector<CharInterestState>::iterator it = mInterests.begin();
         it != mInterests.end();
         ++it) {
        it->mRefractoryTime = -1.0f;
    }
    RndPollable::Enter();
}

namespace stlpmtx_std {
    template<>
    _List_iterator<RndPollable*, _Nonconst_traits<RndPollable*>>
    list<RndPollable*, StlNodeAlloc<RndPollable*>>::insert(
        _List_iterator<RndPollable*, _Nonconst_traits<RndPollable*>> __position,
        RndPollable* const& __x)
    {
        _Node_base* __tmp = _M_create_node(__x);
        _Node_base* __n = __position._M_node;
        _Node_base* __p = __n->_M_prev;
        __tmp->_M_next = __n;
        __tmp->_M_prev = __p;
        __p->_M_next = __tmp;
        __n->_M_prev = __tmp;
        return __tmp;
    }
}
