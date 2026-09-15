#include "char\CharEyes.h"
#include "char\CharInterest.h"
#include "char\CharLookAt.h"
#include "char\CharWeightable.h"
#include "decomp.h"
#include "math/Easing.h"
#include "math/Rand.h"
#include "math\Rot.h"
#include "math\Utl.h"
#include "math\Vec.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Cam.h"
#include "rndobj\Graph.h"
#include "rndobj\Rnd.h"
#include "rndobj\Trans.h"
#include "ui\PanelDir.h"
#include "utl/BinStream.h"
#include "utl\Std.h"
#include "utl\Symbol.h"
#include "world\Dir.h"
#include <cmath>

void NormalizeScale(const Vector3 &, float, Vector3 &);

bool CharEyes::sDisableEyeDart;
bool CharEyes::sDisableEyeJitter;
bool CharEyes::sDisableInterestObjects;
bool CharEyes::sDisableProceduralBlink;
bool CharEyes::sDisableEyeClamping;
// CharLookAt::sDisableJitter is defined in CharLookAt.cpp

INIT_REVS(18, 0)

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
float pow(float base, float exp) { return std::pow(base, exp); }
#endif

CharEyes::CharEyes()
    : mEyes(this), mInterests(this), mFaceServo(this), mCamWeight(this), mTarget(0, 0, 0),
      mDefaultFilterFlags(0), mViewDirection(this), mHeadLookAt(this),
      mMaxExtrapolation(19.5), mMinTargetDist(35), mUpperLidTrackUp(1),
      mUpperLidTrackDown(1), mLowerLidTrackUp(0.75), mLowerLidTrackDown(0.75),
      mLowerLidTrackRotate(false), mInterestFilterFlags(0), mLastFacing(0, 0, 0),
      mLastLook(0), mLastBlinkWeight(0),
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
    mLastLook = 0;
    // The image stores a third float zero, to 0xcc, that this function never
    // wrote. Realigning the header's (uniformly 0x28-stale) offset comments
    // against the store set puts mAvDelta there, and the `stateReset:` block in
    // NextLook() resets it on exactly this adjacency: `mLastLook = 0.0f;
    // mAvDelta = 0.0f;`. Without it, Enter() leaked the previous take's
    // angular-velocity accumulator into a freshly entered character.
    mAvDelta = 0.0f;
    // MSVC/Xenon preserves source order WITHIN each store stream (the integer
    // stream and the float stream) and only interleaves the two streams.
    // Reading the target's two streams separately gives the image's statement
    // order directly: among the floats it writes 0xc0 (mLastCang, 1.0f) BEFORE
    // 0xd0 (mLastBlinkWeight, -1.0f), and among the integers it writes 0xd5
    // (mBlinkActive) LAST, immediately before the mInterestFilterFlags copy.
    // RB3's CharEyes::Enter confirms both: `mLastCang = 1.0f; mLastBlinkWeight
    // = -1.0f;` and the lone byte store (there `mTargetTooClose`) sitting right
    // above `mInterestFilterFlags = mDefaultFilterFlags;`.
    //
    // The float half LANDS (87.3 -> 87.4 canonical, and it also fixes the
    // lis-order rows at idx 8/9: with 1.0f referenced first the image's
    // `lis r9, __real@3f800000` comes first, as ours now does).
    //
    // NEGATIVE RESULT (w7-ap, 2026-09-14): the integer half does NOT, in any
    // placement. mBlinkActive after mBlinkCount = 85.7; after mLowerBlinkAngle
    // = 85.7; both tried on top of the float swap, and an earlier lane measured
    // 85.8 for the former without it. Every placement away from the current one
    // costs an extra unmatched row and re-scrambles the schedule of the whole
    // block. Either the image reaches 0xd5 from a statement we do not have, or
    // this last int store is pure store scheduling. Residual: 4 `stb`/`stw`
    // offset rows (idx 19/21/23/25/27/29) that rotate the six integer stores by
    // one position, plus the idx 11/13 `lfs 0.0` scheduling pair.
    mLastCang = 1.0f;
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
    mEnabled = false;
    mNeedRecalc = false;
    RndTransformable *head = GetHead();
    if (head) {
        mLastFacing = head->WorldXfm().m.y;
        Normalize(mLastFacing, mLastFacing);
    }
    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
#ifdef HX_NATIVE
        if (!it->mEye) continue;
#endif
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
    if (GetHead()) {
        RndGraph *oneframe = RndGraph::GetOneFrame();
        RndTransformable *trans = nullptr;
        for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
            trans = it->mEye->GetSource();
            if (trans) {
                const Transform &tf1 = trans->WorldXfm();
                const Transform &tf2 = trans->WorldXfm();
                Vector3 v100;
                ScaleAdd(tf2.v, tf1.m.y, 3, v100);
                if (it->mEye->mDisableRoll)
                    oneframe->AddLine(
                        trans->WorldXfm().v, v100, Hmx::Color(1.0f, 0.0f, 0.0f), true
                    );
                else
                    oneframe->AddLine(
                        trans->WorldXfm().v, v100, Hmx::Color(0.0f, 1.0f, 0.0f), true
                    );
            }
        }
        Vector3 v10c(GetHead()->WorldXfm().v);
        if (trans) {
            bool fcmp = mLastCang
                >= (mCurrentInterest ? mCurrentInterest->MaxViewAngleCos() : mMaxEyeCang);
            if (mDartEnabled) {
                oneframe->AddSphere(mTarget, mData.mMaxRadius, Hmx::Color(0.9f, 0.9f, 0.9f));
                Vector3 v118;
                Add(mTarget, *(Vector3 *)&mCurrentDartOffsetX, v118);
                EnforceMinimumTargetDistance(v10c, v118, v118);
                oneframe->AddSphere(v118, 0.5f, Hmx::Color(0.0f, 0.0f, 1.0f));
                oneframe->AddLine(
                    trans->WorldXfm().v,
                    v118,
                    fcmp ? Hmx::Color(0.2f, 0.2f, 1.0f) : Hmx::Color(1, 0, 0),
                    true
                );
            } else {
                oneframe->AddLine(
                    trans->WorldXfm().v,
                    mTarget,
                    fcmp ? Hmx::Color(1, 1, 1) : Hmx::Color(1, 0, 0),
                    true
                );
            }
            if (mBlinkEnabled) {
                oneframe->AddString3D(
                    "p blink!", trans->WorldXfm().v, Hmx::Color(1, 1, 1)
                );
            }
        }

        if (mFocusInterest) {
            if (mFocusInterest != mCurrentInterest) {
                const char *nametouse = mCurrentInterest ? mCurrentInterest->Name() : "GENERATED";
                oneframe->AddString3D(
                    MakeString("focus = '%s' (looking at %s)", mFocusInterest->Name(), nametouse),
                    v10c,
                    Hmx::Color(1, 0, 0)
                );
            } else {
                oneframe->AddString3D(
                    MakeString("focus = '%s'", mFocusInterest->Name()), v10c, Hmx::Color(0, 1, 0)
                );
            }
        } else {
            if (mCurrentInterest) {
                oneframe->AddString3D(
                    MakeString("interest = '%s'", mCurrentInterest->Name()),
                    v10c,
                    Hmx::Color(0, 1, 0)
                );
            }
        }

        if (mInterests.size() != 0) {
            const Transform &headXfm = GetHead()->WorldXfm();
            Vector3 headMY = headXfm.m.y;
            Normalize(headMY, headMY);
            Vector3 va0 = headXfm.v;
            for (ObjVector<CharInterestState>::iterator it = mInterests.begin();
                 it != mInterests.end();
                 ++it) {
                bool b7 = it->mInterest->IsMatchingFilterFlags(mInterestFilterFlags)
                    || ((mInterestFilterFlags == mDefaultFilterFlags)
                        && !it->mInterest->CategoryFlags());
                if (mCurrentInterest == it->mInterest) {
                    oneframe->AddSphere(
                        it->mInterest->WorldXfm().v, 2, Hmx::Color(0, 1, 0)
                    );
                    Vector2 v2;
                    if (RndCam::Current()->WorldToScreen(it->mInterest->WorldXfm().v, v2)
                        > 0) {
                        v2.x *= TheRnd.Width();
                        v2.y *= TheRnd.Height();
                        v2.y += 15.0;
                        v2.x -= 30.0;
                        oneframe->AddString(
                            MakeString("%s", it->mInterest->Name()),
                            v2,
                            Hmx::Color(1, 1, 1)
                        );
                    }
                } else {
                    if (it->mInterest->IsWithinViewCone(va0, mDartOffset)
                        && it->mInterest->IsWithinViewCone(va0, headMY)) {
                        oneframe->AddSphere(
                            it->mInterest->WorldXfm().v,
                            2,
                            b7 ? Hmx::Color(1, 1, 0) : Hmx::Color(1, 0.64705884f, 0)
                        );
                    } else {
                        oneframe->AddSphere(
                            it->mInterest->WorldXfm().v,
                            2,
                            b7 ? Hmx::Color(1, 0, 0)
                               : Hmx::Color(0.6901961f, 0.1882353f, 0.3764706f)
                        );
                    }
                }
                if (it->IsInRefractoryPeriod()) {
                    oneframe->AddString3D(
                        MakeString("r=%f", it->RefractoryTimeRemaining()),
                        it->mInterest->WorldXfm().v,
                        Hmx::Color(1, 1, 1)
                    );
                }
            }
        }
    }
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
        *mEyeStatusOverlay << "t(" << mLastLook << ") ";
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
    // AT LIMIT at 99.286. The only 7 mismatched rows in this function are inside
    // the two MakeString calls this macro expands to, and all 7 are the CSE
    // anchor pick: the target anchors the pair on gAltRev and reaches gRev as
    // `subi r7, r29, 0x4`, we anchor on gRev and reach gAltRev as `+ 4`. Not
    // source-reachable without editing the shared ASSERT_REVS macro in
    // obj/Object.h, which 645 sibling Loads share and 613 of which are at 100%
    // with this exact spelling. Refuted in full at
    // docs/decomp/patterns/relocation-names-are-unmetered.md:708.
    ASSERT_REVS(18, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    if (d.rev > 5) {
        LOAD_SUPERCLASS(CharWeightable)
    }
    if (d.rev > 4) {
        d >> mEyes;
    } else {
        ObjPtrList<CharLookAt> lookats(this);
        d >> lookats;
        mEyes.resize(lookats.size());
        int idx = 0;
        for (ObjPtrList<CharLookAt>::iterator it = lookats.begin(); it != lookats.end();
             ++it) {
            mEyes[idx].mEye = *it;
            mEyes[idx].mUpperLid = nullptr;
            mEyes[idx].mLowerLid = nullptr;
            mEyes[idx].mUpperLidBlink = nullptr;
            mEyes[idx].mLowerLidBlink = nullptr;
            ++idx;
        }
    }
    if (d.rev > 2 && d.rev < 5) {
        ObjPtr<RndTransformable> trans(this);
        d >> trans;
    }
    mInterests.clear();
    if (d.rev > 3 && d.rev <= 8) {
        ObjPtr<RndTransformable> trans(this);
        int count;
        d >> count;
        for (int i = 0; i < count; i++) {
            d >> trans;
            int x;
            d >> x;
        }
    } else if (d.rev > 8) {
        d >> mInterests;
    }
    if (d.rev > 4) {
        d >> mFaceServo;
    } else {
        mFaceServo = nullptr;
    }
    if (d.rev > 7) {
        d >> mCamWeight;
    }
    if (d.rev > 9) {
        d >> mDefaultFilterFlags;
    }
    if (d.rev > 10) {
        d >> mViewDirection;
    }
    if (d.rev > 11) {
        d >> mHeadLookAt;
    }
    if (d.rev > 12) {
        d >> mMaxExtrapolation;
    }
    if (d.rev > 13) {
        d >> mMinTargetDist;
    }
    if (d.rev > 14) {
        d >> mUpperLidTrackUp >> mUpperLidTrackDown >> mLowerLidTrackUp;
        if (d.rev < 17) {
            int x, y;
            d >> x >> mLowerLidTrackDown >> y;
        } else {
            d >> mLowerLidTrackDown;
        }
    }
    if (d.rev > 17) {
        d >> mLowerLidTrackRotate;
    }
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

    bool changed = interest != mFocusInterest;
    mFocusInterest = interest;
    mFocusTimer = i;
    if (changed)
        mNeedRecalc = true;
    if (!mFocusInterest)
        mFocusTimer = -1;

    return true;
}

CharInterest *CharEyes::GetCurrentInterest() {
    if (mFocusInterest)
        return mFocusInterest;
    if (mCurrentInterest)
        return mCurrentInterest;
    return 0;
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

void CharEyes::ListPollChildren(std::list<RndPollable *> &plist) const {
    for (ObjVector<EyeDesc>::const_iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
        plist.push_back((*it).mEye);
    }
}

void CharEyes::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    for (ObjVector<CharInterestState>::iterator it = mInterests.begin();
         it != mInterests.end();
         ++it) {
        ObjectDir *dir = it->mInterest->Dir();
        if (dir == Dir()) {
            changedBy.push_back(it->mInterest);
        }
    }
    if (!mEyes.empty()) {
        changedBy.push_back(GetHead());
        change.push_back(GetTarget());
    }
    if (mHeadLookAt)
        changedBy.push_back(mHeadLookAt);
    if (mFaceServo)
        changedBy.push_back(mFaceServo);
}

void CharEyes::DartUpdate() {
    static DataNode &dartCheat = DataVariable("cheat.disable_eye_darts");
    if (sDisableEyeDart || dartCheat.Int(NULL) != 0)
        return;
    mDartInterval -= TheTaskMgr.DeltaSeconds();
    if (mDartEnabled) {
        if (mDartInterval < 0) {
            mEyeClampCount--;
            if (mEyeClampCount < 0) {
                mDartEnabled = false;
                mDartInterval = RandomFloat(
                    mData.mMinSecsBetweenSequences,
                    mData.mMaxSecsBetweenSequences
                );
            } else {
                mDartInterval = RandomFloat(
                    mData.mMinSecsBetweenDarts,
                    mData.mMaxSecsBetweenDarts
                );
                *(Vector3 *)&mCurrentDartOffsetX = GenerateDartOffset();
            }
        }
    } else if (mDartInterval < 0 && EyesOnTarget(mData.mOnTargetAngleThresh)
               && !mBlinkEnabled) {
        mDartEnabled = true;
        mEyeClampCount = RandomInt(
            mData.mMinDartsPerSequence,
            mData.mMaxDartsPerSequence
        );
        mDartInterval = RandomFloat(
            mData.mMinSecsBetweenDarts,
            mData.mMaxSecsBetweenDarts
        );
        *(Vector3 *)&mCurrentDartOffsetX = GenerateDartOffset();
    }
}

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
        RndTransformable *src = it->mEye->GetSource();
        if (src) {
            Vector3 diff;
            Subtract(mTarget, src->WorldXfm().v, diff);
            Vector3 fwd(src->WorldXfm().m.y);
            Vector3 diff2d(diff);
            diff2d.z = 0;
            fwd.z = 0;
            float dot = Dot(fwd, diff2d);
            float angle = std::acos(Clamp<float>(-1, 1, dot / (Length(fwd) * Length(diff2d))));
            if (angle * 57.29578f > f) {
                return false;
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
    mBlinkActive = false;
    float minDist;
    if (mCurrentInterest && mCurrentInterest->mOverridesMinTargetDist)
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
        const Vector3 &v = GetHead()->WorldXfm().v;
        Vector3 diff(mTarget.x - v.x, mTarget.y - v.y, mTarget.z - v.z);
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

// The target does NOT loop over the vectors (RB3's shape): because EyeDesc::mEye and
// CharInterestState::mInterest sit at offset 0 of their elements, an incoming ObjRef*
// IS an element address, so the image recovers the element by pointer arithmetic.
// The recovered element pointer starts life as end() and is only overwritten when the
// offset is in range and element-aligned; the != end() test is the single join point
// all the failure branches fall into (target 8237D218 / 8237D2A4), which is why it
// cannot be nested inside the range checks.
//
// LEVER (new, 2026-09-14): the image reads begin() TWICE and end() TWICE off the same
// vector with no intervening store, and MSVC will happily CSE the second load of each
// pair away -- which is what kept this at 91.4 with two `delete` rows. Spelling the two
// reads of one member through TWO DIFFERENT LVALUES -- the member itself (`mEyes`) for
// one and a bound reference (`_ref0`) for the other -- defeats the CSE and reproduces
// both loads. Which of the pair gets the reference decides the base register MSVC picks,
// and it is not free: end() wants the REFERENCE first and the member at the test (the
// other way round leaves a base+displacement row at idx 24/59), begin() wants the
// member first and the reference at the offset computation. 91.40 -> 100.00.
bool CharEyes::Replace(ObjRef *ref, Hmx::Object *obj) {
    auto& _ref0 = mEyes;
    EyeDesc *desc = _ref0.end();
    EyeDesc *eyeBegin = mEyes.begin();
    int eyeCount = (int)((char *)desc - (char *)eyeBegin) / (int)sizeof(EyeDesc);
    if (eyeCount != 0) {
        int eyeOff = (int)((char *)ref - (char *)(EyeDesc *)_ref0.begin());
        if (eyeOff >= 0) {
            int eyeTotal = eyeCount * (int)sizeof(EyeDesc);
            if ((unsigned)eyeOff < (unsigned)eyeTotal) {
                int eyeRounded = (eyeOff / (int)sizeof(EyeDesc)) * (int)sizeof(EyeDesc);
                if (eyeRounded == eyeOff)
                    desc = (EyeDesc *)((char *)eyeBegin + eyeRounded);
            }
        }
        if (desc != mEyes.end()) {
            if (!desc->mEye.SetObj(obj))
                _ref0.erase(ObjVector<EyeDesc>::iterator(desc));
            return true;
        }
    }
    auto& _ref1 = mInterests;
    CharInterestState *state = _ref1.end();
    CharInterestState *stateBegin = mInterests.begin();
    int stateCount =
        (int)((char *)state - (char *)stateBegin) / (int)sizeof(CharInterestState);
    if (stateCount != 0) {
        int stateOff = (int)((char *)ref - (char *)(CharInterestState *)_ref1.begin());
        if (stateOff >= 0) {
            int stateTotal = stateCount * (int)sizeof(CharInterestState);
            if ((unsigned)stateOff < (unsigned)stateTotal) {
                int stateRounded = (stateOff / (int)sizeof(CharInterestState))
                    * (int)sizeof(CharInterestState);
                if (stateRounded == stateOff)
                    state = (CharInterestState *)((char *)stateBegin + stateRounded);
            }
        }
        if (state != mInterests.end()) {
            if (!state->mInterest.SetObj(obj))
                _ref1.erase(ObjVector<CharInterestState>::iterator(state));
            return true;
        }
    }
    return CharWeightable::Replace(ref, obj);
}

void CharEyes::NextLook() {
    auto& _ref0 = mTarget;
    Vector3 oldTarget = _ref0;

    RndTransformable *head = GetHead();
    const Transform &headXfm = head->WorldXfm();

    Vector3 facingDir(headXfm.m.y);
    Normalize(facingDir, facingDir);

    if (mFocusInterest) {
        _ref0 = mFocusInterest->WorldXfm().v;
        mCurrentInterest = mFocusInterest;
        const CharEyeDartRuleset *dartOverride = mCurrentInterest->GetDartRulesetOverride();
        if (dartOverride) {
            mData = dartOverride->mData;
        } else {
            mData.ClearToDefaults();
        }
    } else {
        const Vector3 &lastFacing = mLastFacing;
        // NEGATIVE RESULT -- the one unexplained STRUCTURE left in this function.
        // The image makes a SECOND 16-byte Vector3 local here, a copy of
        // facingDir: `addi r10, r31, 0x70` (facingDir) / four `lwz` off r10 /
        // `addi r11, r31, 0x60` / four `stw` off r11, interleaved with the three
        // `facingDir - mLastFacing` fsubs below (CharEyes.s, the ten
        // instructions at diff idx 91-105).  It then reads the extrapolated
        // facing's y and z back out of THAT copy after the tan() call --
        // `lfs f0, 0x64(r31)` / `lfs f13, 0x68(r31)` -- while taking x from the
        // register f27 that still holds facingDir.x.  That reload is why the
        // image only saves f26-f31 where we save f24-f31: we keep all three
        // facingDir components pinned in callee-saved FPRs across tan() and
        // RandomFloat() instead.
        //
        // Spelling it as `Vector3 newFacing = facingDir;` and reading the three
        // components out of `newFacing` is BYTE-FOR-BYTE INERT: MSVC copy-
        // propagates the local-to-local copy away (510 instructions, 91.0
        // canonical, identical row set), even though facingDir's address has
        // already escaped into Normalize().  Whatever the image is copying from
        // is reached through a pointer MSVC cannot see through -- note that both
        // sides of that copy use computed address registers, where the other two
        // Vector3 copies in this function (oldTarget at 0x90, facingDir at 0x70)
        // use a computed address only for the destination.
        //
        // w7-ay addendum (2026-09-14, at 96.7 canonical): `memcpy(&newFacing,
        // &facingDir, sizeof)` with y/z read from the copy is ALSO inert (same
        // 76/4/1/13 rows), and so is the plain copy re-tested on top of the
        // levers below.  The residual is exactly this copy (`addi r11, r31,
        // 0x60` at 0x823799FC, reloads at 0x82379A6C/0x82379A74), the f26-vs-
        // f24 save (`bl __savefpr_26` at 0x8237989C) and the frame it implies
        // (`stwu r1, -0x130(r1)` at 0x823798A4 vs our -0x140), plus register
        // naming.  What moved 91.0 -> 96.7 was spelling, not structure:
        // `Set(...)` for the two projected targets (loads all three sources
        // before any store, as the image does), `mData = dartOverride->mData`
        // instead of memcpy (no pre-hoisted `addi &mData` above the null test),
        // the direct-member tail copies `mHeadForward = mTarget; mTarget =
        // oldTarget` (a reference there made the copy word-serialised), a named
        // `headPos` reference so the loop loads go through the pointer register,
        // and a Transform reference for Dir()'s WorldXfm().
        //
        // w7-bn (96.7 -> 99.6): FOUND.  The copy is `newFacing`, and what keeps
        // MSVC from propagating it away is a MEMBER operator on it that returns
        // `*this` -- `newFacing += Vector3(dx, dy, dz)`.  With that spelling the
        // 16-byte copy appears at 0x823799F8..0x82379A30, the `&mLastFacing` home
        // store sinks to 0x82379A18, y/z are reloaded from the copy at
        // 0x82379A6C/0x82379A74, and the prologue drops to `bl __savefpr_26` /
        // `stwu r1, -0x130(r1)` exactly.  The same copy is ELIMINATED (96.7,
        // full ninja each) by every spelling that reads or writes the fields
        // directly: `newFacing.x += dx` (93.5), `newFacing.Set(newFacing.x + dx,
        // dy + newFacing.y, dz + newFacing.z)`, `Add(Vector3(dx, dy, dz),
        // newFacing, newFacing)`.  A modified copy for the DELTA instead
        // (`Vector3 extrap = facingDir; extrap -= lastFacing; extrap *= 45.0f`)
        // keeps a copy but puts the fsubs on it and the post-tan reloads on
        // facingDir, the mirror image of the target (95.5).
        //
        // Residual at 99.6: eight commutative operand orders (`fmuls f30, f30,
        // f0` vs ours `f30, f0, f30` at 0x82379A60/64, the post-tan y/z fadds,
        // and the projection/Set rows at 0x82379AB0..0x82379AD0) that source
        // operand order does NOT drive -- `dy = scale * dy` vs `dy = dy * scale`
        // is inert on them -- plus the tail's oldDir load order (the image loads
        // oldTarget x/y/z before headXfm.v at 0x82379F4C; `Subtract(oldTarget,
        // headXfm.v, oldDir)` homes a reference instead, 98.0) and the
        // `mTarget.z < dirXfm.v.z` load order (`dirXfm.v.z > mTarget.z` flips
        // the branch to ble, 98.0).
        Vector3 newFacing = facingDir;
        float dz = (facingDir.z - lastFacing.z) * 45.0f;
        float dx = (facingDir.x - lastFacing.x) * 45.0f;
        float dy = (facingDir.y - lastFacing.y) * 45.0f;

        float extrapMag = std::sqrt(dy * dy + (dx * dx + dz * dz));
        float maxExtrap = std::tan(mMaxExtrapolation * 0.017453292f);

        if (extrapMag > maxExtrap) {
            float scale = maxExtrap / extrapMag;
            dx = scale * dx;
            dy = dy * scale;
            dz = dz * scale;
        }

        newFacing += Vector3(dx, dy, dz);

        float dist = RandomFloat(20.0f, 100.0f);
        dist *= 12.0f;

        float projX = dist * newFacing.x;
        float projY = newFacing.y * dist;
        float projZ = newFacing.z * dist;

        _ref0.Set(headXfm.v.x + projX, projY + headXfm.v.y, headXfm.v.z + projZ);
        const Vector3 &headPos = headXfm.v;

        auto _tmp0 = Dir();
        RndTransformable *dirTrans = dynamic_cast<RndTransformable *>(_tmp0);
        if (dirTrans) {
            const Transform &dirXfm = dirTrans->WorldXfm();
            if (mTarget.z < dirXfm.v.z) {
                float scale = (dirXfm.v.z - headXfm.v.z) / (mTarget.z - headXfm.v.z);
                float sx = projX * scale;
                float sy = projY * scale;
                float sz = projZ * scale;
                _ref0.Set(headPos.x + sx, sy + headPos.y, headPos.z + sz);
            }
        }

        static DataNode &interestCheat = DataVariable("cheat.disable_interest_objects");

        if (mInterests.size() > 0 && !sDisableInterestObjects) {
            if (interestCheat.Int(0) == 0) {
                float maxDistSq = -1.0f;
                float bestScore = maxDistSq;
                for (ObjVector<CharInterestState>::iterator it = mInterests.begin();
                     it != mInterests.end();
                     ++it) {
                    const Vector3 &intPos = it->mInterest->WorldXfm().v;
                    float fy = intPos.y - headPos.y;
                    float fx = intPos.x - headPos.x;
                    float fz = intPos.z - headPos.z;
                    float distSq = (fz * fz + (fx * fx + fy * fy));
                    if (distSq > maxDistSq)
                        maxDistSq = distSq;
                }

                if (maxDistSq > 0.0f) {
                    CharInterestState *bestState = 0;
                    Vector3 targetDir;
                    Subtract(_ref0, headPos, targetDir);
                    Normalize(targetDir, targetDir);

                    float inverseDist = 1.0f / maxDistSq;

                    for (ObjVector<CharInterestState>::iterator it = mInterests.begin();
                         it != mInterests.end();
                         ++it) {
                        if (it->mInterest != mCurrentInterest) {
                            if (!it->IsInRefractoryPeriod()) {
                                float score = it->mInterest->ComputeScore(
                                    headXfm.m.y,
                                    headPos,
                                    targetDir,
                                    inverseDist,
                                    mInterestFilterFlags,
                                    mDefaultFilterFlags == mInterestFilterFlags
                                );
                                if (score >= 0.0f && score > bestScore) {
                                    bestScore = score;
                                    bestState = &*it;
                                }
                            }
                        }
                    }

                    if (bestState) {
                        _ref0 = bestState->mInterest->WorldXfm().v;
                        mCurrentInterest = bestState->mInterest;
                        const CharEyeDartRuleset *dartOverride =
                            mCurrentInterest->GetDartRulesetOverride();
                        if (dartOverride) {
                            mData = dartOverride->mData;
                        } else {
                            mData.ClearToDefaults();
                        }
                        bestState->mRefractoryTime =
                            TheTaskMgr.Seconds(TaskMgr::kRealTime);
                    } else {
                        mCurrentInterest = 0;
                        mData.ClearToDefaults();
                    }

                    mDartOffset = targetDir;
                    goto stateReset;
                }
            }
        }

        mCurrentInterest = 0;
        mData.ClearToDefaults();
    }

stateReset:
    mLastLook = 0.0f;
    mAvDelta = 0.0f;
    mEnabled = false;
    mNeedRecalc = false;
    mLastCang = 1e30f;
    mDartEnabled = false;
    mDartInterval = 0.2f;
    mEyeClampCount = -1;

    static DataNode &blinkCheat = DataVariable("cheat.disable_procedural_blinks");

    if (!sDisableProceduralBlink && !blinkCheat.NotNull() && !mBlinkEnabled && mFaceServo
        && mBlinkCount < 25
        && TheTaskMgr.Seconds(TaskMgr::kRealTime) - mLowerBlinkAngle > 0.6f
        && mLastBlinkWeight < 0.5f) {
        Vector3 oldDir(
            oldTarget.x - headXfm.v.x,
            oldTarget.y - headXfm.v.y,
            oldTarget.z - headXfm.v.z
        );
        Normalize(oldDir, oldDir);

        Vector3 newDir(
            _ref0.x - headXfm.v.x,
            _ref0.y - headXfm.v.y,
            _ref0.z - headXfm.v.z
        );
        Normalize(newDir, newDir);

        auto _tmp1 = Dot(newDir, oldDir);
        if (_tmp1 < 0.984808f) {
            ForceBlink();
            mHeadForward = mTarget;
            mTarget = oldTarget;
        }
    }
}

// RESIDUAL at 98.35 canonical (w7-at, 2026-09-14). ~100 of the 111 diff_arg rows
// are ONE frame-slot permutation, not 100 causes. Both sides allocate the same eight
// 16-byte slots at 0x50..0xc0 and the same sharing groups; only the order differs:
//   target  0x50 srcPos  0x60 lidPos  0x70 Symbol-temp  0x80 upperDir
//           0x90 upperBlinkPos  0xa0 sourcePos  0xb0 lowerBlinkPos  0xc0 lowerDir
//   ours    0x50 srcPos  0x60 Symbol-temp  0x70 upperDir  0x80 lidPos
//           0x90 lowerDir  0xa0 upperBlinkPos  0xb0 sourcePos  0xc0 lowerBlinkPos
// i.e. exactly two moves: lidPos up two places, lowerDir to the top. The relative
// order of the (lowerBlinkPos, sourcePos, upperBlinkPos) trio ALREADY matches. The
// slots are shared with later variables (newLowerPos / origDir / newDir / the debug
// Color temps), so this is a graph-colouring result, not a declaration-order one.
//
// NEGATIVE RESULTS, all measured in this worktree, none of which moved canonical:
//  - `Vector3 lidPos;` as a bare declaration ahead of srcPos, assigned in place:
//    98.3 -> 98.3 (and two extra rows at idx 644-650). Confirms the brief's "a bare
//    declaration claims its slot at first STORE" -- it does not claim it earlier.
//  - flipping the fmuls operand order in BOTH lid-rotate arms
//    (`negEyeRot * (cond ? up : down)`): byte-identical output, MSVC canonicalises it.
//  - RB3's per-arm spelling (`if (eyeRot >= 0) angle = -eyeRot * up; else ...`):
//    98.3 -> 98.0, one extra insert/delete pair and a second FPR swap pair. The
//    current scoped-negation spelling is the better of the two.
// Remaining non-slot rows: idx 100/102 (our `fneg` lands before the `fcmpu`, the
// image's between the `fcmpu` and the `blt`, with f0/f13 roles swapped), idx 179-209
// (we hoist `lbz 0xbd(rN)` -- RndTransformable's WorldXfm dirty flag -- ~10
// instructions earlier than the image in both blink-position blocks), and idx 627-650
// (a 16-byte Transform copy whose three word loads/stores are scheduled in a rotated
// order; the "wrong field" story in the resolved-offsets block is the base-register
// defect noted in CLAUDE.md, not a finding).
void CharEyes::LidTrackAndClampingUpdate(EyeDesc &desc, float blinkWeight) {
    if (DataVariable("no_lids").Int(0))
        return;
    if (!mFaceServo)
        return;
    if (!mFaceServo->mClips)
        return;
    if (!mFaceServo->mBaseClip)
        return;

    RndTransformable *source = desc.mEye->GetSource();
    if (!source)
        return;

    RndTransformable *lowerLid = desc.mLowerLid;
    RndTransformable *upperLid = desc.mUpperLid;

    float dist = -1.0f;
    float maxDot = dist;
    if (lowerLid) {
        Vector3 srcPos = source->WorldXfm().v;
        Vector3 lidPos = lowerLid->WorldXfm().v;
        float dx = lidPos.x - srcPos.x;
        float dy = lidPos.y - srcPos.y;
        float dz = lidPos.z - srcPos.z;
        dist = std::sqrt((dy * dy + (dx * dx + dz * dz)));
    }

    float eyeRot = (1.0f - blinkWeight) * source->LocalXfm().m.y.x;

    if (upperLid) {
        // The negation is scoped to each block that needs it: the target emits
        // one `fneg` here and a second inside the lower-lid rotate arm, rather
        // than one hoisted copy at function scope.
        float negEyeRot = -eyeRot;
        float angle =
            (0.0f <= eyeRot ? mUpperLidTrackUp : mUpperLidTrackDown) * negEyeRot;
        bool isNaN = (angle != angle);
        if (!isNaN) {
            Transform &xfm = upperLid->DirtyLocalXfm();
            RotateAboutZ(xfm.m, angle, xfm.m);
        }
    }

    if (lowerLid) {
        if (mLowerLidTrackRotate) {
            float negEyeRot = -eyeRot;
            float angle =
                (eyeRot >= 0.0f ? mLowerLidTrackUp : mLowerLidTrackDown) * negEyeRot;
            bool isNaN = (angle != angle);
            if (!isNaN) {
                Transform &xfm = lowerLid->DirtyLocalXfm();
                RotateAboutZ(xfm.m, angle, xfm.m);
            }
        } else {
            float offset =
                (eyeRot >= 0.0f ? mLowerLidTrackUp : mLowerLidTrackDown) * eyeRot;
            lowerLid->DirtyLocalXfm().v.x += offset;
        }
    }

    RndTransformable *lowerBlink = desc.mLowerLidBlink;
    RndTransformable *upperBlink = desc.mUpperLidBlink;
    if (lowerBlink && upperBlink) {
        Vector3 sourcePos = source->WorldXfm().v;
        Vector3 upperBlinkPos = upperBlink->WorldXfm().v;
        Vector3 lowerBlinkPos = lowerBlink->WorldXfm().v;

        Vector3 upperDir(
            upperBlinkPos.x - sourcePos.x,
            upperBlinkPos.y - sourcePos.y,
            upperBlinkPos.z - sourcePos.z
        );
        Normalize(upperDir, upperDir);

        Vector3 lowerDir(
            lowerBlinkPos.x - sourcePos.x,
            lowerBlinkPos.y - sourcePos.y,
            lowerBlinkPos.z - sourcePos.z
        );
        Normalize(lowerDir, lowerDir);

        Vector3 cross;
        Cross(lowerDir, upperDir, cross);

        const Transform &srcXfm = source->WorldXfm();
        // The target tests the dot product POSITIVE (`fcmpu; bgt`) and keeps the
        // result as "the lids are crossed", so every use below is the plain
        // variable rather than a negation.  Spelling this the other way round
        // (`<= 0.0f` plus `!lidsOK` at each use) inverts four branches.
        bool notLidsOK =
            cross.x * srcXfm.m.x.x + cross.y * srcXfm.m.x.y + cross.z * srcXfm.m.x.z
            > 0.0f;

        if (!sDisableEyeClamping) {
            DataNode &clampCheat = DataVariable("eyes.disable_clamping");
            if (!clampCheat.Int(0) && notLidsOK) {
                float midX =
                    (upperBlinkPos.x - lowerBlinkPos.x) * 0.5f + lowerBlinkPos.x;
                float midY =
                    (upperBlinkPos.y - lowerBlinkPos.y) * 0.5f + lowerBlinkPos.y;
                float midZ =
                    (upperBlinkPos.z - lowerBlinkPos.z) * 0.5f + lowerBlinkPos.z;

                float clampOffX = midX - lowerBlinkPos.x;
                float clampOffY = midY - lowerBlinkPos.y;
                float clampOffZ = midZ - lowerBlinkPos.z;

                Vector3 newLowerPos = lowerLid->WorldXfm().v;
                newLowerPos.x += clampOffX;
                newLowerPos.y += clampOffY;
                newLowerPos.z += clampOffZ;
                lowerLid->SetWorldPos(newLowerPos);

                const Vector3 &ulidPos = upperLid->WorldXfm().v;
                Vector3 origDir(
                    upperBlinkPos.x - ulidPos.x,
                    upperBlinkPos.y - ulidPos.y,
                    upperBlinkPos.z - ulidPos.z
                );
                Normalize(origDir, origDir);

                const Vector3 &ulidPos2 = upperLid->WorldXfm().v;
                Vector3 newDir(
                    midX - ulidPos2.x,
                    midY - ulidPos2.y,
                    midZ - ulidPos2.z
                );
                Normalize(newDir, newDir);

                float dot = Dot(newDir, origDir);
                maxDot = maxDot - dot >= 0.0f ? maxDot : dot;
                float clamped = maxDot - 1.0f >= 0.0f ? 1.0f : maxDot;
                float angle = std::acos(clamped);

                bool isNaN = (angle != angle);
                if (!isNaN) {
                    Transform &xfm = upperLid->DirtyLocalXfm();
                    RotateAboutZ(xfm.m, -angle, xfm.m);
                }
            }
        }

        DataNode &drawCheat = DataVariable("eyes.debug_clamping");
        if (drawCheat.Int(0)) {
            RndGraph *graph = RndGraph::GetOneFrame();

            // The target does NOT null-check `graph`: it calls GetOneFrame() and
            // uses the result immediately.  The colour is a TERNARY of two
            // unnamed temporaries (the image builds each arm into its own stack
            // slot and lands `addi r6, r1, <slot>` in both arms), not an
            // if/else around two whole calls.
            graph->AddSphere(
                upperBlinkPos,
                0.05f,
                notLidsOK ? Hmx::Color(1.0f, 0.0f, 0.0f, 1.0f)
                          : Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f)
            );
            graph->AddSphere(
                lowerBlinkPos,
                0.05f,
                notLidsOK ? Hmx::Color(1.0f, 0.0f, 0.0f, 1.0f)
                          : Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f)
            );
            graph->AddSphere(sourcePos, 0.05f, Hmx::Color(0.0f, 0.0f, 1.0f, 1.0f));

            // Two separate cyan temporaries, not one named local: the target
            // stores (0,1,1,1) into two different stack slots.
            graph->AddLine(
                sourcePos, upperBlinkPos, Hmx::Color(0.0f, 1.0f, 1.0f, 1.0f), false
            );
            graph->AddLine(
                sourcePos, lowerBlinkPos, Hmx::Color(0.0f, 1.0f, 1.0f, 1.0f), false
            );

            Normalize(cross, cross);
            Vector3 normalEnd(
                cross.x + sourcePos.x, cross.y + sourcePos.y, cross.z + sourcePos.z
            );
            graph->AddLine(
                sourcePos,
                normalEnd,
                notLidsOK ? Hmx::Color(1.0f, 0.0f, 0.0f, 1.0f)
                          : Hmx::Color(0.0f, 1.0f, 0.0f, 1.0f),
                false
            );

            const Transform &srcXfm2 = source->WorldXfm();
            Vector3 facingEnd(
                srcXfm2.m.x.x + sourcePos.x,
                srcXfm2.m.x.y + sourcePos.y,
                srcXfm2.m.x.z + sourcePos.z
            );
            graph->AddLine(
                sourcePos, facingEnd, Hmx::Color(1.0f, 1.0f, 0.0f, 1.0f), false
            );

            if (notLidsOK) {
                Vector3 mid2(
                    (upperBlinkPos.x - lowerBlinkPos.x) * 0.5f + lowerBlinkPos.x,
                    (upperBlinkPos.y - lowerBlinkPos.y) * 0.5f + lowerBlinkPos.y,
                    (upperBlinkPos.z - lowerBlinkPos.z) * 0.5f + lowerBlinkPos.z
                );
                graph->AddSphere(
                    mid2, 0.03f, Hmx::Color(1.0f, 0.0f, 1.0f, 1.0f)
                );
            }
        }
    }

    if (!DataVariable("eyes.disable_llidnorm").Int(0) && !mLowerLidTrackRotate && 0.0f < dist) {
        Vector3 srcPos = source->WorldXfm().v;
        Vector3 lidPos = lowerLid->WorldXfm().v;
        Vector3 dir(
            lidPos.x - srcPos.x, lidPos.y - srcPos.y, lidPos.z - srcPos.z
        );
        Normalize(dir, dir);
        Vector3 clampedPos(
            dir.x * dist + srcPos.x, dir.y * dist + srcPos.y, dir.z * dist + srcPos.z
        );
        lowerLid->SetWorldPos(clampedPos);
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
        auto blinkWeight = EaseInExp(t);
        mFaceServo->SetProceduralBlinkWeight(blinkWeight);
    } else if (elapsed < 0.3f) {
        // Opening phase
        float t = Clamp(0.0f, 1.0f, 1.0f - (elapsed - 0.115f) * 5.4054055f);
        auto blinkWeight = EaseSigmoid(t, 0.0f, 0.0f);
        mFaceServo->SetProceduralBlinkWeight(blinkWeight);
        mTarget = mHeadForward;
    } else {
        // Blink complete
        mFaceServo->SetProceduralBlinkWeight(0.0f);
        mBlinkEnabled = false;
        mTarget = mHeadForward;
    }
}

// RESIDUAL at 98.0 canonical / 97.1 raw (w7-ax, 2026-09-14).  53 of the 63
// remaining rows are one register permutation the canonical ruler forgives.
// The four real rows, each verified against build/373307D9/asm/system/char/CharEyes.s:
//  - We CSE the `1.0f` of `Clamp(-1.0f, 1.0f, cang)` into the callee-saved f31
//    and reuse it as the `: 1.0f` default of minLookTime; the image keeps only
//    the literal-pool ANCHOR in a callee-saved GPR (`lis r30,
//    "__real@3f800000"@ha` at 0x8237AB74) and issues a SECOND
//    `lfs f30, "__real@3f800000"@l(r30)` at 0x8237ABFC for the default.
//    Because our else-arm is then empty, MSVC fuses the minLookTime and
//    maxLookTime diamonds into one, costing the image's `b .L_8237AC00`
//    (0x8237ABF8), that `lfs`, and the second `cmpwi cr6, r10, 0x0` / `beq cr6`
//    pair at 0x8237AC00/0x8237AC04 -- 4 rows.
//  - `addi r24, r3, 0x8` (diff idx 8): we pin &mEyes in a callee-saved GPR for
//    the whole function; the image re-derives it, as `r25 + 0x30` off the
//    CharEyes* at the two interior sites and as `r31 + 0x8` at the final loop,
//    and pays one extra `addi r11, r25, 0x30` (0x8237AE38) before re-reading
//    begin() for mEyes[0].
//  - The cam fallback chain: the image's first null test is SIGNED
//    (`cmpwi cr6, r30, 0x0` at 0x8237AE90) and its second uses cr6; we emit
//    `cmplwi cr6` and then `cmplwi r30, 0x0` on cr0.  Control flow is identical.
// NEGATIVE RESULTS, all measured in this worktree, none of which moved canonical:
//  - `float cang = Clamp(-1.0f, 1.0f, Dot(facingDir, targetDir));` as one
//    statement: byte-inert.
//  - minLookTime as an if/else statement pair instead of a ternary: inert.
//  - `mEyes.begin()->mEye` instead of `mEyes[0].mEye` (both sites): inert.
//  - `RndCam *cam = TheWorld ? TheWorld->Cam() : 0;` instead of the zero-init +
//    `if (TheWorld)` block: 98.0 -> 97.2 (three extra inserts, one extra
//    delete, and it moves the `lwz 0x0(r11)` reload onto the wrong register).
void CharEyes::Poll() {
    if (mEyes.empty())
        return;

    RndTransformable *head = GetHead();
    if (!head)
        return;

    float dt = TheTaskMgr.DeltaSeconds();
    if (dt < 0.0f) {
        Enter();
        return;
    }

    float camWeight = 0.0f;
    if (mCamWeight) {
        camWeight = mCamWeight->Weight();
    }

    mLastLook += TheTaskMgr.DeltaSeconds();

    float blinkWeight;
    if (mFaceServo) {
        blinkWeight = mFaceServo->BlinkWeightLeft();
    } else {
        blinkWeight = 0.0f;
    }

    bool blinkDetected = false;
    if (blinkWeight < 0.3f) {
        mBlinkDetect = true;
    } else {
        if (mBlinkDetect && mLastBlinkWeight > 0.8f && blinkWeight < mLastBlinkWeight) {
            mBlinkDetect = false;
            mBlinkCount++;
            blinkDetected = true;
            mLowerBlinkAngle = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        }
    }
    mLastBlinkWeight = blinkWeight;

    const Transform &headXfm = head->WorldXfm();
    const Vector3 &headPos = headXfm.v;

    Vector3 targetDir;
    Subtract(mTarget, headPos, targetDir);
    Normalize(targetDir, targetDir);

    Vector3 facingDir(headXfm.m.y);
    Normalize(facingDir, facingDir);

    float cang = Dot(facingDir, targetDir);
    cang = Clamp(-1.0f, 1.0f, cang);

    if (mLastCang != 1e+30f) {
        TheTaskMgr.Seconds(TaskMgr::kRealTime);
        mAvDelta = (cang - mLastCang - mAvDelta) * 0.1f + mAvDelta;

        float minLookTime = mCurrentInterest ? mCurrentInterest->mMinLookTime : 1.0f;
        float maxLookTime = mCurrentInterest ? mCurrentInterest->mMaxLookTime : 3.0f;
        float viewAngleCos =
            mCurrentInterest ? mCurrentInterest->mMaxViewAngleCos : mMaxEyeCang;

        bool canSeeTarget = cang >= viewAngleCos;

        if (mLastLook <= maxLookTime && !mNeedRecalc
            && (mFocusInterest == 0 || mCurrentInterest == (CharInterest *)mFocusInterest
                || ((mLastLook <= 0.4f
                     || !mFocusInterest->IsWithinViewCone(headPos, facingDir))
                    && !IsHeadIKWeightIncreasing()))
            && (!mEnabled || mLastLook <= 0.25f)) {
            if (mLastLook <= minLookTime)
                goto storeState;
            if (!blinkDetected) {
                if (canSeeTarget) {
                    bool anyEyeClamped;
                    auto eyesEnd = mEyes.end();
                    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != eyesEnd;
                         ++it) {
                        if (it->mEye && it->mEye->mDisableRoll) {
                            anyEyeClamped = true;
                            goto haveClamped;
                        }
                    }
                    anyEyeClamped = false;
                haveClamped:
                    if (!anyEyeClamped)
                        goto storeState;
                }
                if (mAvDelta >= 0.0f)
                    goto storeState;
            }
        }

        if (camWeight == 0.0f) {
            NextLook();
        }
    }

storeState:
    mLastCang = cang;
    mLastFacing = facingDir;

    float headLookWeight;
    if (mHeadLookAt) {
        headLookWeight = mHeadLookAt->Weight();
    } else {
        headLookWeight = 0.0f;
    }
    mDartTimer = headLookWeight;

    DartUpdate();

    if (mCurrentInterest) {
        if (!mBlinkEnabled) {
            mTarget = mCurrentInterest->WorldXfm().v;
        } else {
            mHeadForward = mCurrentInterest->WorldXfm().v;
        }
        EnforceMinimumTargetDistance(headPos, mTarget, mTarget);
    }

    RndTransformable *eyeTarget;
    if (!mEyes.empty() && mEyes[0].mEye) {
        eyeTarget = mEyes[0].mEye->mTarget;
    } else {
        eyeTarget = 0;
    }

    if (eyeTarget) {
        float weight = Weight();
        const Vector3 *interpTarget;
        Transform xfm;
        Vector3 localTarget;
        float interpWeight;
        if (camWeight > 0.0f) {
            RndCam *cam = 0;
            if (TheWorld)
                cam = TheWorld->Cam();
            if (!cam)
                cam = RndCam::Current();
            if (!cam)
                cam = TheRnd.GetDefaultCam();
            if (!cam)
                goto skipInterp;
            xfm = eyeTarget->WorldXfm();
            interpTarget = &cam->WorldXfm().v;
            interpWeight = camWeight;
        } else {
            localTarget = mTarget;
            if (mDartEnabled) {
                localTarget.x += mCurrentDartOffsetX;
                localTarget.y += mCurrentDartOffsetY;
                localTarget.z += mCurrentDartOffsetZ;
                EnforceMinimumTargetDistance(headPos, localTarget, localTarget);
            }
            xfm = eyeTarget->WorldXfm();
            interpTarget = &localTarget;
            interpWeight = weight;
        }
        Interp(xfm.v, *interpTarget, interpWeight, xfm.v);
        eyeTarget->SetWorldXfm(xfm);
    }
skipInterp:

    ProceduralBlinkUpdate();

    CharLookAt::sDisableJitter = sDisableEyeJitter;
    for (ObjVector<EyeDesc>::iterator it = mEyes.begin(); it != mEyes.end(); ++it) {
#ifdef HX_NATIVE
        if (!it->mEye) continue;
#endif
        it->mEye->Poll();
        LidTrackAndClampingUpdate(*it, blinkWeight);
    }
    CharLookAt::sDisableJitter = false;

    UpdateOverlay();
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

// NormalizeScale body guarded: defining it here causes IPA — the compiler
// sees NormalizeScale doesn't touch r4/r6, so EnforceMinimumTargetDistance
// skips callee-saved GPR saves (r29-r31) that the target uses. Result:
// structurally incompatible prologue (91.3% -> 57.8%). AT_LIMIT.
// Native build: NormalizeScale is provided inline in src/system/math/Vec.h.
