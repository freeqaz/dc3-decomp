#include "char/CharEyes.h"
#include "char/CharInterest.h"
#include "char/CharWeightable.h"
#include "decomp.h"
#include "math/Easing.h"
#include "math/Rand.h"
#include "math/Utl.h"
#include "math/Vec.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Graph.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include <cmath>

void NormalizeScale(const Vector3 &, float, Vector3 &);

INIT_REVS(18, 0)

float pow(float base, float exp) { return std::pow(base, exp); }

CharEyes::CharEyes()
    : mEyes(this), mInterests(this), mFaceServo(this), mCamWeight(this), mTarget(0, 0, 0),
      mDefaultFilterFlags(0), mViewDirection(this), mHeadLookAt(this),
      mMaxExtrapolation(19.5), mMinTargetDist(35), mUpperLidTrackUp(1),
      mUpperLidTrackDown(1), mLowerLidTrackUp(0.75), mLowerLidTrackDown(0.75),
      mLowerLidTrackRotate(false), mInterestFilterFlags(0), mLastFacing(0, 0, 0),
      mLastCang(0), mLastLook(0), mMaxEyeCang(0), mAvDelta(0), mLastBlinkWeight(0),
      mBlinkDetect(0), mBlinkActive(0), mCurrentInterest(this), mFocusInterest(this),
      mFocusTimer(-1), mNeedRecalc(0), mDartOffset(0, 1, 0), mDartTimer(0),
      mDartEnabled(0), mDartInterval(-1), mEyeClampCount(-1),
      mBlinkEnabled(0), mBlinkTimer(-1), mBlinkCount(0),
      mUpperBlinkAngle(-1), mLowerBlinkAngle(-1), mEnabled(0), mHeadIKActive(1) {
    mMaxEyeCang = std::cos(0.5235987715423107);
    mEyeStatusOverlay = RndOverlay::Find("eye_status", false);
}

CharEyes::~CharEyes() {}

void CharEyes::Enter() {
    mLastFacing.Zero();
    mLastCang = 1.0f;
    mLastLook = 0;
    mLastBlinkWeight = -1.0f;
    mBlinkDetect = false;
    mBlinkActive = false;
    mDartEnabled = false;
    mDartInterval = -1.0f;
    mEyeClampCount = -1;
    mBlinkEnabled = false;
    mBlinkTimer = -1.0f;
    mBlinkCount = 0;
    mUpperBlinkAngle = -1.0f;
    mLowerBlinkAngle = -1.0f;
    mInterestFilterFlags = mDefaultFilterFlags;
    mDartTimer = 0.0f;
    mNeedRecalc = false;
    mEnabled = false;
    mNeedRecalc = false;
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

void CharEyes::Exit() {
    mFocusInterest = 0;
    mFocusTimer = -1;
    mInterests.clear();
    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
        it->mEye->Exit();
    }
    RndPollable::Exit();
}

void CharEyes::Highlight() {
#ifdef MILO_DEBUG
    if (GetHead()) {
        RndGraph *oneframe = RndGraph::GetOneFrame();
        RndTransformable *trans = 0;
        for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
            CharLookAt *eye = it->mEye;
            if (eye) {
                trans = eye->GetSource();
                if (trans) {
                    const Transform &tf = trans->WorldXfm();
                    Vector3 v100(
                        tf.m.y.x * 3.0f + tf.v.x,
                        tf.m.y.y * 3.0f + tf.v.y,
                        tf.m.y.z * 3.0f + tf.v.z
                    );
                    if (eye->mDisableRoll)
                        oneframe->AddLine(
                            trans->WorldXfm().v, v100, Hmx::Color(1.0f, 0.0f, 0.0f), true
                        );
                    else
                        oneframe->AddLine(
                            trans->WorldXfm().v, v100, Hmx::Color(0.0f, 1.0f, 0.0f), true
                        );
                }
            }
        }
        Vector3 headPos(GetHead()->WorldXfm().v);
        if (trans) {
            float f1 = mCurrentInterest ? mCurrentInterest->mMaxViewAngleCos : mMaxEyeCang;
            float f2 = mLastBlinkWeight;
            if (mDartEnabled) {
                oneframe->AddSphere(
                    mTarget, mData.mMaxRadius, Hmx::Color(0.9f, 0.9f, 0.9f)
                );
                Vector3 dartTarget(
                    mTarget.x + mCurrentDartOffsetX,
                    mTarget.y + mCurrentDartOffsetY,
                    mTarget.z + mCurrentDartOffsetZ
                );
                EnforceMinimumTargetDistance(headPos, dartTarget, dartTarget);
                oneframe->AddSphere(dartTarget, 0.5f, Hmx::Color(0.0f, 0.0f, 1.0f));
                oneframe->AddLine(
                    trans->WorldXfm().v,
                    dartTarget,
                    f2 < f1 ? Hmx::Color(1.0f, 0.0f, 0.0f) : Hmx::Color(0.2f, 0.2f, 1.0f),
                    true
                );
            } else {
                oneframe->AddLine(
                    trans->WorldXfm().v,
                    mTarget,
                    f2 < f1 ? Hmx::Color(1.0f, 0.0f, 0.0f) : Hmx::Color(1.0f, 1.0f, 1.0f),
                    true
                );
            }
            if (mBlinkEnabled) {
                oneframe->AddString3D(
                    "p blink!", trans->WorldXfm().v, Hmx::Color(1.0f, 1.0f, 1.0f)
                );
            }
        }
        if (mFocusInterest) {
            if (mFocusInterest != mCurrentInterest) {
                const char *nametouse = mCurrentInterest ? mCurrentInterest->Name() : "GENERATED";
                oneframe->AddString3D(
                    MakeString("focus = '%s' (looking at %s)", mFocusInterest->Name(), nametouse),
                    headPos,
                    Hmx::Color(1.0f, 0.0f, 0.0f)
                );
            } else {
                oneframe->AddString3D(
                    MakeString("focus = '%s'", mFocusInterest->Name()),
                    headPos,
                    Hmx::Color(0.0f, 1.0f, 0.0f)
                );
            }
        } else {
            if (mCurrentInterest) {
                oneframe->AddString3D(
                    MakeString("interest = '%s'", mCurrentInterest->Name()),
                    headPos,
                    Hmx::Color(0.0f, 1.0f, 0.0f)
                );
            }
        }
    }
#endif
}

DECOMP_FORCEACTIVE(CharEyes, "%s", "r=%f")

void CharEyes::UpdateOverlay() {
    if (mEyeStatusOverlay && mEyeStatusOverlay->Showing()) {
        *mEyeStatusOverlay << Dir()->Name() << ": ";
        if (mCurrentInterest) {
            if (mFocusInterest) {
                if (streq(mCurrentInterest->Name(), mFocusInterest->Name())) {
                    *mEyeStatusOverlay << "Look(FOC) ";
                    goto done_look;
                }
            }
            *mEyeStatusOverlay << "Look(" << mCurrentInterest->Name() << ") ";
        } else
            *mEyeStatusOverlay << "Look(GEN) ";
    done_look:
        if (mFocusInterest) {
            const Transform &headxfm = GetHead()->WorldXfm();
            Vector3 fwd(headxfm.m.y);
            Normalize(fwd, fwd);
            const char *str = mFocusInterest->IsWithinViewCone(headxfm.v, fwd) ? "t" : "f";
            *mEyeStatusOverlay << "Foc(" << mFocusInterest->Name() << " p(" << mFocusTimer << ") v(" << str << ")) ";
        } else
            *mEyeStatusOverlay << "Foc(NA) ";
        *mEyeStatusOverlay << "t(" << mLastCang << ") ";
        Vector3 headPos(GetHead()->WorldXfm().v);
        Vector3 diff;
        Vector3 target(mTarget);
        RndTransformable *tgt = GetTarget();
        if (tgt)
            target = tgt->WorldXfm().v;
        Subtract(target, headPos, diff);
        float len = Length(diff);
        *mEyeStatusOverlay << "Dist(" << len << ") ";
        if (mBlinkEnabled)
            *mEyeStatusOverlay << "P Blink! ";
        if (mDartEnabled)
            *mEyeStatusOverlay << "Dart! ";
        if (mBlinkActive)
            *mEyeStatusOverlay << "Close! ";
        *mEyeStatusOverlay << "\n";
    }
}

DECOMP_FORCEACTIVE(
    CharEyes,
    "no_lids",
    "eyes.disable_clamping",
    "eyes.debug_clamping",
    "eyes.disable_llidnorm",
    "cheat.disable_eye_darts",
    "cheat.disable_procedural_blinks",
    "cheat.disable_interest_objects",
    "ObjPtr_p.h",
    "f.Owner()",
    ""
)

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

BinStreamRev &operator>>(BinStreamRev &bs, CharEyes::EyeDesc &desc) {
    bs >> desc.mEye;
    bs >> desc.mUpperLid;
    if (bs.rev > 6)
        bs >> desc.mLowerLid;
    if (bs.rev > 0xF) {
        bs >> desc.mUpperLidBlink;
        bs >> desc.mLowerLidBlink;
    }
    return bs;
}

BinStream &operator>>(BinStream &bs, CharEyes::EyeDesc &desc) {
    bs >> desc.mEye;
    bs >> desc.mUpperLid;
    if (gRev > 6)
        bs >> desc.mLowerLid;
    if (gRev > 0xF) {
        bs >> desc.mUpperLidBlink;
        bs >> desc.mLowerLidBlink;
    }
    return bs;
}

BinStreamRev &operator>>(BinStreamRev &bs, CharEyes::CharInterestState &state) {
    bs >> state.mInterest;
    return bs;
}

BinStream &operator>>(BinStream &bs, CharEyes::CharInterestState &state) {
    bs >> state.mInterest;
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

BEGIN_LOADS(CharEyes)
    LOAD_REVS(bs)
    ASSERT_REVS(0x12, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (gRev > 5)
        LOAD_SUPERCLASS(CharWeightable)
    if (gRev > 4)
        d >> mEyes;
    else {
        ObjPtrList<CharLookAt> pList(this, kObjListNoNull);
        d.stream >> pList;
        mEyes.resize(pList.size());
        int idx = 0;
        for (ObjPtrList<CharLookAt>::iterator it = pList.begin(); it != pList.end();
             ++it) {
            mEyes[idx].mEye = *it;
            mEyes[idx].mUpperLid = 0;
            mEyes[idx].mLowerLid = 0;
            mEyes[idx].mLowerLidBlink = 0;
            mEyes[idx].mUpperLidBlink = 0;
            idx++;
        }
    }
    if (gRev == 3 || gRev == 4) {
        ObjPtr<RndTransformable> tPtr(this);
        d.stream >> tPtr;
    }
    mInterests.clear();
    if (gRev >= 4 && gRev <= 8) {
        ObjPtr<RndTransformable> tPtr(this);
        int cnt;
        d.stream >> cnt;
        for (int i = 0; i < cnt; i++) {
            d.stream >> tPtr;
            int x;
            d.stream >> x;
        }
    } else if (gRev > 8)
        d >> mInterests;
    if (gRev > 4)
        d.stream >> mFaceServo;
    else
        mFaceServo = 0;
    if (gRev > 7)
        d.stream >> mCamWeight;
    if (gRev > 9)
        d.stream >> mDefaultFilterFlags;
    if (gRev > 10)
        d.stream >> mViewDirection;
    if (gRev > 0xB)
        d.stream >> mHeadLookAt;
    if (gRev > 0xC)
        d.stream >> mMaxExtrapolation;
    if (gRev > 0xD)
        d.stream >> mMinTargetDist;
    if (gRev > 0xE) {
        d.stream >> mUpperLidTrackUp;
        d.stream >> mUpperLidTrackDown;
        d.stream >> mLowerLidTrackUp;
        if (gRev < 0x11) {
            int x, y;
            d.stream >> x;
            d.stream >> mLowerLidTrackDown;
            d.stream >> y;
        } else
            d.stream >> mLowerLidTrackDown;
    }
    if (gRev > 0x11)
        d >> mLowerLidTrackRotate;
END_LOADS

BEGIN_COPYS(CharEyes)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(CharEyes)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mEyes)
        COPY_MEMBER(mInterests)
        COPY_MEMBER(mFaceServo)
        COPY_MEMBER(mLastFacing)
        COPY_MEMBER(mLastCang)
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

void CharEyes::ForceBlink() {
    if (mHeadIKActive && !mBlinkEnabled) {
        mBlinkEnabled = true;
        mBlinkTimer = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        mBlinkCount++;
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
    if (mFocusInterest && mFocusTimer > i)
        return false;

    mFocusInterest = interest;
    mFocusTimer = i;
    if (interest != mFocusInterest)
        mNeedRecalc = true;
    if (!mFocusInterest)
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

RndTransformable *CharEyes::GetTarget() {
    if (mEyes.empty() || !mEyes[0].mEye)
        return nullptr;
    else {
        return mEyes[0].mEye->mTarget;
    }
}

void CharEyes::ClearAllInterestObjects() { mInterests.clear(); }

bool CharEyes::CharInterestState::IsInRefractoryPeriod() {
    if (!mInterest || mRefractoryTime < 0)
        return false;
    else {
        float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime) - mRefractoryTime;
        if (secs < mInterest->mRefractoryPeriod)
            return true;
        else
            return false;
    }
}

float CharEyes::CharInterestState::RefractoryTimeRemaining() {
    if (!mInterest || mRefractoryTime < 0)
        return 0.0f;
    else {
        float secs = TheTaskMgr.Seconds(TaskMgr::kRealTime) - mRefractoryTime;
        if (secs < mInterest->mRefractoryPeriod)
            return mInterest->mRefractoryPeriod - secs;
        else
            return 0.0f;
    }
}

void CharEyes::AddInterestObject(CharInterest *interest) {
    if (interest) {
        CharInterestState state(this);
        state.mInterest = interest;
        mInterests.push_back(state);
    }
}

bool CharEyes::EyesOnTarget(float f) {
    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
        CharLookAt *eye = it->mEye;
        if (eye) {
            RndTransformable *src = eye->GetSource();
            if (src) {
                Vector3 diff;
                Subtract(mTarget, src->WorldXfm().v, diff);
                Vector3 fwd(src->WorldXfm().m.y);
                Vector3 diff2d(diff);
                diff2d.z = 0;
                fwd.z = 0;
                float dot = Dot(fwd, diff2d);
                float angle = std::acos(Clamp<float>(-1, 1, dot / (Length(fwd) * Length(diff2d))));
                if (angle * 57.295776f > f) {
                    return false;
                }
            }
        }
    }
    return true;
}

void CharEyes::EnforceMinimumTargetDistance(
    const Vector3 &v1, const Vector3 &v2, Vector3 &vout
) {
    Vector3 diff;
    Subtract(v2, v1, diff);
    float vlen = Length(diff);
    bool overrideMin = false;
    mBlinkActive = false;
    if (mCurrentInterest && mCurrentInterest->mOverridesMinTargetDist)
        overrideMin = true;
    float minDist;
    if (overrideMin)
        minDist = mCurrentInterest->mMinTargetDistOverride;
    else
        minDist = mMinTargetDist;
    if (vlen < minDist) {
        Vector3 scaled;
        NormalizeScale(diff, minDist, scaled);
        Add(v1, scaled, vout);
        mBlinkActive = true;
    }
}

Vector3 CharEyes::GenerateDartOffset() {
    Vector3 vout;
    float start = mData.mMinRadius;
    float end = mData.mMaxRadius;
    if (mData.mScaleWithDistance && mData.mReferenceDistance > 0.1f) {
        Vector3 diff;
        Subtract(mTarget, GetHead()->WorldXfm().v, diff);
        float len = Length(diff);
        start *= len / mData.mReferenceDistance;
        end *= len / mData.mReferenceDistance;
    }
    float mult = RandomFloat(0, 1) > 0.5f ? 1.0f : -1.0f;
    vout[0] = RandomFloat(start, end) * mult;
    mult = RandomFloat(0, 1) > 0.5f ? 1.0f : -1.0f;
    vout[1] = RandomFloat(start, end) * mult;
    mult = RandomFloat(0, 1) > 0.5f ? 1.0f : -1.0f;
    vout[2] = RandomFloat(start, end) * mult;
    return vout;
}

bool CharEyes::Replace(ObjRef *ref, Hmx::Object *obj) {
    int eyeSize = sizeof(EyeDesc);
    int eyeCount = mEyes.size();
    int eyeOffset = (char *)ref - (char *)mEyes.begin();
    if ((unsigned)eyeOffset < (unsigned)(eyeSize * eyeCount)) {
        int eyeIdx = eyeOffset / eyeSize;
        if (eyeOffset == eyeIdx * eyeSize) {
            EyeDesc &desc = mEyes[eyeIdx];
            if (!desc.mEye.SetObj(obj))
                mEyes.erase(mEyes.begin() + eyeIdx);
            return true;
        }
    }
    int stateSize = sizeof(CharInterestState);
    int stateCount = mInterests.size();
    int stateOffset = (char *)ref - (char *)mInterests.begin();
    if ((unsigned)stateOffset < (unsigned)(stateSize * stateCount)) {
        int stateIdx = stateOffset / stateSize;
        if (stateOffset == stateIdx * stateSize) {
            CharInterestState &state = mInterests[stateIdx];
            if (!state.mInterest.SetObj(obj))
                mInterests.erase(mInterests.begin() + stateIdx);
            return true;
        }
    }
    return CharWeightable::Replace(ref, obj);
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
        mBlinkCount = 0;
        mUpperBlinkAngle = 15.0f;
    }

    if (!mFaceServo)
        return;
    if (!mBlinkEnabled)
        return;

    float elapsed = TheTaskMgr.Seconds(TaskMgr::kRealTime) - mBlinkTimer;
    if (elapsed < 0.115f) {
        // Closing phase
        float t = Clamp(0.0f, 1.0f, elapsed * 8.695652f);
        mFaceServo->SetProceduralBlinkWeight(EaseInExp(t));
    } else if (elapsed < 0.3f) {
        // Opening phase
        float t = Clamp(0.0f, 1.0f, 1.0f - (elapsed - 0.115f) * 5.405405f);
        mFaceServo->SetProceduralBlinkWeight(EaseSigmoid(t, 0.0f, 0.0f));
        mTarget = mHeadForward;
    } else {
        // Blink complete
        mFaceServo->SetProceduralBlinkWeight(0.0f);
        mBlinkEnabled = false;
        mTarget = mHeadForward;
    }
}

DataNode CharEyes::OnToggleForceFocus(DataArray *da) {
    if (mFocusInterest)
        SetFocusInterest(0, 0);
    else
        SetFocusInterest(mCurrentInterest, 0);
    return 0;
}

DataNode CharEyes::OnToggleInterestOverlay(DataArray *da) {
    ToggleInterestsDebugOverlay();
    return 0;
}

DataNode CharEyes::OnAddInterest(DataArray *arr) {
    mInterests.push_back(CharInterestState(arr->Obj<CharInterest>(1)));
    return 0;
}
