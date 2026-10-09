#include "char\CharServoBone.h"
#include "char\CharBoneDir.h"
#include "char\CharBonesMeshes.h"
#include "char\CharClipDriver.h"
#include "char\CharDriver.h"
#include "char\CharPollable.h"
#include "char\CharUtl.h"
#include "char\Character.h"
#include "math\Mtx.h"
#include "math\Rot.h"
#include "math\Trig.h"
#include "math\Utl.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "utl\Symbol.h"

void RotateAboutZ(const Vector3 &v, float f, Vector3 &res) {
    float c = Cosine(f);
    float s = Sine(f);
    res.Set(v.x * c - v.y * s, v.x * s + v.y * c, v.z);
}

CharServoBone::CharServoBone()
    : mPelvis(0), mFacingRotDelta(0), mFacingPosDelta(0), mFacingRot(0), mFacingPos(0),
      mMoveSelf(false), mDeltaChanged(false), mRegulate(this) {}

CharServoBone::~CharServoBone() {}

BEGIN_PROPSYNCS(CharServoBone)
    SYNC_PROP_SET(clip_type, mClipType, SetClipType(_val.Sym()))
    SYNC_PROP_SET(move_self, mMoveSelf, SetMoveSelf(_val.Int()))
    SYNC_PROP(delta_changed, mDeltaChanged)
    SYNC_PROP(regulate, mRegulate)
    SYNC_SUPERCLASS(CharBonesMeshes)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(CharServoBone)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mClipType;
END_SAVES

BEGIN_COPYS(CharServoBone)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(CharServoBone)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMoveSelf)
        SetClipType(c->mClipType);
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(CharServoBone)
    LOAD_REVS(bs)
    ASSERT_REVS(2, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    Symbol s;
    if (d.rev > 1)
        bs >> s;
    SetClipType(s);
END_LOADS

void CharServoBone::Poll() {
    if (!mMeshes.empty()) {
        PoseMeshes();
#ifdef HX_NATIVE
        {
            extern void Dc3KneeLog(const char *);
            char evt[192];
            snprintf(evt, sizeof(evt), "ServoPose-POST %s", PathName(this));
            Dc3KneeLog(evt);
        }
#endif
        Character *me = Character::Current();
        if (mFacingPosDelta) {
            if (!mMoveSelf) {
                if (mDeltaChanged) {
                    Transform tf48 = me->LocalXfm();
                    MoveToDeltaFacing(tf48);
                    Transform tf78;
                    Multiply(mPelvis->LocalXfm(), tf48, tf78);
                    MoveToFacing(mPelvis->DirtyLocalXfm());
                    Transform tfa8;
                    FastInvert(mPelvis->DirtyLocalXfm(), tfa8);
                    Multiply(tfa8, tf78, me->DirtyLocalXfm());
                } else {
                    MoveToFacing(mPelvis->DirtyLocalXfm());
                }
                CharBoneDir *dir = CharBoneDir::FindResourceFromClipType(mClipType);
                for (ObjDirItr<CharBone> it(dir, false); it != nullptr; ++it) {
                    if (it->BakeOutAsTopLevel()) {
                        String str(it->Name());
                        if (str.find(".cb") != String::npos) {
                            str = str.substr(0, str.length() - 3);
                        }
                        RndTransformable *boneTrans =
                            CharUtlFindBoneTrans(str.c_str(), Dir());
                        if (mDeltaChanged) {
                            MoveToDeltaFacing(boneTrans->DirtyLocalXfm());
                            MoveToFacing(boneTrans->DirtyLocalXfm());
                        } else {
                            MoveToFacing(boneTrans->DirtyLocalXfm());
                        }
                    }
                }
            } else {
                if (mDeltaChanged) {
                    Transform tfd8(mPelvis->LocalXfm());
                    MoveToFacing(tfd8);
                    Multiply(tfd8, me->LocalXfm(), tfd8);
                    Transform tf108;
                    FastInvert(mPelvis->LocalXfm(), tf108);
                    Multiply(tf108, tfd8, me->DirtyLocalXfm());
                } else {
                    MoveToDeltaFacing(me->DirtyLocalXfm());
                }
                RegulateInternal(me);
            }
            mDeltaChanged = false;
        }
        ZeroDeltas();
    }
}

void CharServoBone::ReallocateInternal() {
    CharBonesMeshes::ReallocateInternal();
    mFacingRotDelta = 0;
    mFacingPosDelta = (Vector3 *)FindPtr("bone_facing_delta.pos");
    if (mFacingPosDelta) {
        mFacingPos = (Vector3 *)FindPtr("bone_facing.pos");
        mPelvis = CharUtlFindBoneTrans("bone_pelvis", Dir());
        if (!mFacingPos) {
            MILO_NOTIFY("CharServoBone: no Facing Pos in ReallocateInternal()");
        }
        if (!mPelvis) {
            MILO_NOTIFY("CharServoBone: no pelvis bone in this dir.");
        }
        mFacingRot = (float *)FindPtr("bone_facing.rotz");
        mFacingRotDelta = (float *)FindPtr("bone_facing_delta.rotz");
    }
}

void CharServoBone::Enter() {
    ZeroDeltas();
    SetRegulateWaypoint(nullptr);
    mDeltaChanged = false;
    mMoveSelf = mFacingPosDelta;
}

void CharServoBone::ZeroDeltas() {
    if (mFacingPosDelta)
        mFacingPosDelta->Zero();
    if (!mFacingRotDelta)
        return;
    *mFacingRotDelta = 0.0f;
}

void CharServoBone::SetClipType(Symbol sym) {
    if (sym != mClipType) {
        mClipType = sym;
        ClearBones();
        CharBoneDir::StuffBones(*this, mClipType);
    }
}

void CharServoBone::SetMoveSelf(bool b) {
    if (mMoveSelf == b)
        return;

    mMoveSelf = b;
    mDeltaChanged = true;
}

void CharServoBone::RegulateInternal(Character *me) {
    if (mRegulate) {
        CharClipDriver *driver = me->Driver()->Before(me->Driver()->Last());
        CharClipDriver *next = driver && driver->mRampIn > 0 ? driver->Next() : nullptr;
        if (next) {
            DoRegulate(me, mRegulate, next, driver->mRampIn, Max(2.0f, driver->mRampIn * (1.0f / 1.5f)));
        }
        mRegulate->Constrain(me->DirtyLocalXfm());
    }
}

void CharServoBone::DoRegulate(
    Character *me, Waypoint *waypoint, CharClipDriver *driver, float f3, float f4
) {
    Transform &myxfm = me->DirtyLocalXfm();
    ClipPredict pred(driver->GetClip(), myxfm.v, GetZAngle(myxfm.m));
    pred.Predict(driver->mBeat, driver->mBeat + f3);
    Vector3 pos(pred.mPos);
    float ang = pred.mAng;
    float deltaBeat = TheTaskMgr.DeltaBeat() / f4;
    Vector3 shapedDelta;
    waypoint->ShapeDelta(pos, shapedDelta);
    ScaleAddEq(myxfm.v, shapedDelta, deltaBeat);
    float shapeDelta = waypoint->ShapeDelta(ang);
    RotateAboutZ(myxfm.m, shapeDelta * deltaBeat, myxfm.m);
}

// w25-oa: 52/54. The two rows left are the y and z fadds operand order of
// `tf.v += v18` (c2's commutative sort key, C2RS-BRIDGE 8.7). Keys: for x, the
// load M[operator+= this, variable sid 4] is 0x18008, above v18.x (V11, 0x10160),
// so the load goes first, which matches. For y, v18.y (V5, 0x100a0) is above
// M[base temp 248] (0x10008); for z, v18.z (V7, 0x100e0) is above M[base temp
// 252] (0x10008). So the value goes first in y and z, and the image has the
// load first in both. Needs base temps 248 and 252 to be != 0 mod 4 (key >=
// 0x14008), i.e. a shift of 1-3 temps before them. Nothing reachable: the bases
// stay at 248/252 under 25 code-neutral spellings (refs/pointers to tf.v, delta
// or matrix, Transform alias, scopes, explicit Set() expansion of Multiply,
// named components or partial sums, a copy of v18, the early-return if). They
// move only with code-changing ones: a Vector3 copy of the delta gives 241/245
// and the predicted y/z flip, but adds 9 instructions.
// Also changes the code (w25-gc): Add(tf.v, v18, tf.v) or Add(v18, ...),
// per-component += or explicit sums (they drop the image's dead
// `addi r11, r4, 48`, so the image does call operator+=), and an in-place
// Multiply on a copy.
void CharServoBone::MoveToDeltaFacing(Transform &tf) {
    Vector3 v18;
    Multiply(*mFacingPosDelta, tf.m, v18);
    tf.v += v18;
    if (mFacingRotDelta) {
        RotateAboutZ(tf.m, *mFacingRotDelta, tf.m);
        Normalize(tf.m, tf.m);
    }
}

void CharServoBone::MoveToFacing(Transform &tf) {
    if (mFacingRot) {
        RotateAboutZ(tf.m, *mFacingRot, tf.m);
        RotateAboutZ(tf.v, *mFacingRot, tf.v);
        Normalize(tf.m, tf.m);
    }
    tf.v += *mFacingPos;
#ifdef HX_NATIVE
    // DIAG (DC3_IK_DIAG): the facing channel positions the char on stage; its Z
    // should be ~0 (floor plane). If facingPos.z is large-negative on dancers it
    // is the pelvis-drop root cause (the ~-10u sink). Compare main.milo dancers
    // vs the clean iconman.
    if (getenv("DC3_IK_DIAG")) {
        static int sFacingLog = 0;
        const char *p = PathName(this);
        if (sFacingLog < 18 && p && mFacingPos) {
            sFacingLog++;
            fprintf(stderr,
                "DC3_IK_DIAG Facing[%d] self=%s facingPos=(%.3f,%.3f,%.3f) "
                "facingRot=%.3f pelvisLocalAfter=(%.2f,%.2f,%.2f)\n",
                sFacingLog, p, mFacingPos->x, mFacingPos->y, mFacingPos->z,
                mFacingRot ? *mFacingRot : -999.0f, tf.v.x, tf.v.y, tf.v.z);
        }
    }
#endif
}

void CharServoBone::PollDeps(
    std::list<Hmx::Object *> &, std::list<Hmx::Object *> &change
) {
    // ICF-folded with CharFaceServo::PollDeps at 0x82371970:
    //   mr r4,r5 ; addi r3,r3,8 ; b CharBonesMeshes::StuffMeshes
    StuffMeshes(change);
}

BEGIN_HANDLERS(CharServoBone)
    HANDLE_SUPERCLASS(CharPollable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS
