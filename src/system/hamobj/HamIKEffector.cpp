#include "hamobj/HamIKEffector.h"
#include "HamIKEffector.h"
#include "char/CharPollable.h"
#include "char/CharUtl.h"
#include "char/CharWeightable.h"
#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Poll.h"
#include "rndobj/Trans.h"
#include "utl/BinStream.h"
#include "utl/Str.h"

#ifdef HX_NATIVE
#include <vector>
#endif


#ifdef HX_NATIVE
// ---------------------------------------------------------------------------
// CharLocalIKScope — run the matched ankle/pelvis IK in CHARACTER-LOCAL space.
//
// The matched ankle clamp (HamIKEffector::Poll, below) forms
//     q.v = neutral + eff          (empty constraints => totalWeight == 0)
// then back-transforms  finalXfm = (effW . fingerW^-1) . q  to the ankle bone.
// That identity is only well-behaved when `neutral`/`eff` are SMALL,
// character-local bone worlds: then  neutral + eff ~= eff  and the
// back-transform collapses to the clean planted effector world. The residual
// error of the back-transform is proportional to the character's distance from
// the world origin (verified: iconman at the origin lands the foot cleanly;
// venue dancers placed at world X ~= +/-37 by Character::Teleport /
// HamRegulate::Poll get q.v.x ~= 110 (doubled) and the ankle world Z explodes
// to ~60-348, which the render-time WorldXfm_Force recompute then discards ->
// the foot drops to the sunk anim pose).
//
// On Xbox the per-character IK runs character-local (Xbox ankle clamp ground
// truth neutralZ=0.017, effZ=0.882 — same scale as native's origin-rooted
// iconman, NOT native's venue dancer); the venue placement is composited AFTER
// the IK, at render. Native bakes the venue offset into the IK-read bone worlds
// (the bones are children of the venue-placed character root) BEFORE the IK
// polls, which is the divergence.
//
// This scope replicates Xbox's flow WITHOUT touching the matched IK math: on
// construction it re-roots the character to the world origin (saves the root's
// world, sets it to identity, and dirties the bone subtree so every bone the
// matched body reads recomputes character-local). On destruction it restores
// the venue placement and composites it back onto exactly the bones the body
// recomputed (clean) — touched-by-IK and merely-read bones alike map by
// W_venue = W_charlocal . R, which sends untouched bones back to their original
// venue world and IK-written bones to the IK result placed at the venue. Bones
// never read during the body stay dirty and recompute venue-from-local on the
// next access. HamIKEffector is a friend of RndTransformable, so the world /
// dirty fields are reachable directly.
//
// No-op when the character is already at the origin (e.g. iconman) — that path
// already matches Xbox.
class CharLocalIKScope {
public:
    CharLocalIKScope(Character *character) : mRoot(character), mActive(false) {
        // FORENSIC-NEUTRALIZED: the prior agent's re-root-to-origin workaround
        // is disabled so the consecutive-frame chain trace below observes the
        // RAW native bug (no state mutation). Compiles, runs as a pure no-op.
        return;
        if (!mRoot)
            return;
        const Transform &rootWorld = mRoot->WorldXfm();
        // Cheap "is this character venue-placed?" test: any non-trivial root
        // world translation. Origin-rooted characters (iconman) skip the
        // re-root entirely and run exactly as before.
        if (fabsf(rootWorld.v.x) < 0.01f && fabsf(rootWorld.v.y) < 0.01f
            && fabsf(rootWorld.v.z) < 0.01f)
            return;
        mActive = true;
        mSavedRoot = rootWorld;

        // Snapshot the bone subtree so we can re-place it afterwards.
        Collect(mRoot);

        // Re-root to the world origin: the matched body now reads
        // character-local bone worlds.
        mRoot->mWorldXfm.Reset();
        mRoot->mDirty = false;
        for (std::list<RndTransformable *>::iterator it = mRoot->mChildren.begin();
             it != mRoot->mChildren.end(); ++it) {
            (*it)->SetDirty_Force();
        }
    }

    ~CharLocalIKScope() {
        if (!mActive)
            return;
        // Restore the venue placement on the root...
        mRoot->mWorldXfm = mSavedRoot;
        mRoot->mDirty = false;
        // ...then composite it back onto every bone the body recomputed
        // (clean) while we were rooted at the origin. Touched and untouched
        // recomputed bones both map by W_venue = W_charlocal . R. Bones that
        // stayed dirty (never read) recompute venue-from-local on next access.
        for (size_t i = 0; i < mBones.size(); i++) {
            RndTransformable *t = mBones[i];
            if (t->mDirty)
                continue;
            Transform venue;
            Multiply(t->mWorldXfm, mSavedRoot, venue);
            t->mWorldXfm = venue;
        }
    }

private:
    void Collect(RndTransformable *t) {
        for (std::list<RndTransformable *>::iterator it = t->mChildren.begin();
             it != t->mChildren.end(); ++it) {
            mBones.push_back(*it);
            Collect(*it);
        }
    }

    RndTransformable *mRoot;
    Transform mSavedRoot;
    std::vector<RndTransformable *> mBones;
    bool mActive;
};
#endif


HamIKEffector::HamIKEffector()
    : mSkeleton(this), mEffector(this), mFinger(this), mGround(this), mMore(this),
      mOther(this), mElbow(this), mConstraints(this), mCharacter(this) {}

HamIKEffector::~HamIKEffector() {}

BEGIN_HANDLERS(HamIKEffector)
    HANDLE_SUPERCLASS(CharWeightable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_CUSTOM_PROPSYNC(HamIKEffector::Constraint)
    SYNC_PROP(target, o.mTarget)
    SYNC_PROP(weight, o.mWeight)
END_CUSTOM_PROPSYNC

BEGIN_PROPSYNCS(HamIKEffector)
    SYNC_PROP(skeleton, mSkeleton)
    SYNC_PROP(effector, mEffector)
    SYNC_PROP(finger, mFinger)
    SYNC_PROP(ground, mGround)
    SYNC_PROP(more, mMore)
    SYNC_PROP(other, mOther)
    SYNC_PROP(elbow, mElbow)
    SYNC_PROP(constraints, mConstraints)
    SYNC_SUPERCLASS(CharWeightable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BinStream &operator<<(BinStream &bs, const HamIKEffector::Constraint &c) {
    bs << c.mTarget;
    bs << c.mWeight;
    return bs;
}

BEGIN_SAVES(HamIKEffector)
    SAVE_REVS(7, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(CharWeightable)
    bs << mEffector;
    bs << mMore;
    bs << mElbow;
    bs << mConstraints;
    bs << mGround;
    bs << mOther;
    bs << mFinger;
    bs << mSkeleton;
END_SAVES

BEGIN_COPYS(HamIKEffector)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(CharWeightable)
    CREATE_COPY(HamIKEffector)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mEffector)
        COPY_MEMBER(mFinger)
        COPY_MEMBER(mSkeleton)
        COPY_MEMBER(mMore)
        COPY_MEMBER(mOther)
        COPY_MEMBER(mElbow)
        COPY_MEMBER(mConstraints)
        COPY_MEMBER(mGround)
    END_COPYING_MEMBERS
END_COPYS

BinStreamRev &operator>>(BinStreamRev &d, HamIKEffector::Constraint &c) {
    d >> c.mTarget;
    if (d.rev < 6) {
        Symbol s;
        d >> s;
    }
    if (d.rev > 2) {
        d >> c.mWeight;
    }
    return d;
}

// idk what the significance of these are
const float kConstraintConsts[3] = { 0.50508249f, -0.0023923444f, 7.4688797f };

INIT_REVS(7, 0)

BEGIN_LOADS(HamIKEffector)
    LOAD_REVS(bs)
    ASSERT_REVS(7, 0)
    LOAD_SUPERCLASS(CharPollable)
    LOAD_SUPERCLASS(CharWeightable)
    d >> mEffector;
    d >> mMore;
    if (d.rev > 1) {
        d >> mElbow;
    }
    if (d.rev < 1) {
        int x;
        d >> x;
    }
    d >> mConstraints;
    if (d.rev > 3) {
        d >> mGround;
    }
    if (d.rev > 4) {
        d >> mOther;
    }
    if (d.rev > 5) {
        d >> mFinger;
    }
    if (d.rev > 6) {
        d >> mSkeleton;
    }
END_LOADS

void HamIKEffector::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mCharacter = dynamic_cast<Character *>(dir);
}

void HamIKEffector::ListPollChildren(std::list<RndPollable *> &polls) const {
    RndPollable *poll = mMore ? mMore->mSkeleton.Ptr() : nullptr;
    polls.push_back(poll);
    polls.push_back(mOther);
}

void HamIKEffector::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mSkeleton);
    change.push_back(mEffector);
    changedBy.push_back(mEffector);
    change.push_back(mFinger);
    changedBy.push_back(mFinger);
    FOREACH (it, mConstraints) {
        changedBy.push_back(it->mTarget);
    }
    if (mMore) {
        FOREACH (it, mMore->mConstraints) {
            changedBy.push_back(it->mTarget);
        }
    }
    EffectorType t = GetType();
    if (t == kEffectorTypeAnkle || t == kEffectorTypeHand) {
        RndTransformable *parent = mEffector->TransParent();
        if (parent) {
            change.push_back(parent);
            changedBy.push_back(parent);
            parent = parent->TransParent();
            if (parent) {
                change.push_back(parent);
                changedBy.push_back(parent);
            }
        }
    }
}

HamIKEffector::EffectorType HamIKEffector::GetType() {
    if (!mEffector) {
        MILO_NOTIFY_ONCE("%s trying to get type with NULL effector", PathName(this));
        return kEffectorTypeNone;
    } else if (strneq(mEffector->Name(), "bone_pelvis", 11)) {
        return kEffectorTypePelvis;
    } else if (strneq(mEffector->Name(), "bone_L-ankle", 12)
               || strneq(mEffector->Name(), "bone_R-ankle", 12)) {
        return kEffectorTypeAnkle;
    } else if (strneq(mEffector->Name(), "bone_L-hand", 11)
               || strneq(mEffector->Name(), "bone_R-hand", 11)) {
        return kEffectorTypeHand;
    } else if (strneq(mEffector->Name(), "bone_L-foreArm", 11)
               || strneq(mEffector->Name(), "bone_R-foreArm", 11)) {
        return kEffectorTypeForearm;
    } else if (strneq(mEffector->Name(), "bone_head", 9)) {
        return kEffectorTypeHead;
    } else
        return kEffectorTypeNone;
}

void HamIKEffector::IKElbow(const Vector3 &v) {
    RndTransformable *parent = mEffector->TransParent();
    if (parent) {
        RndTransformable *grandparent = parent->TransParent();
        if (grandparent) {
            Transform xfm = grandparent->WorldXfm();
            QuatXfm q100;
            Hmx::Matrix3 me0;
            Transform tfb0;
            ComputeHandPullAndQuat(q100, tfb0, xfm, v);
            MakeRotMatrix(q100.q, me0);
            Multiply(me0, xfm.m, xfm.m);
            xfm.v += q100.v;
            grandparent->SetWorldXfm(xfm);
            Transform tf70;
            Multiply(tfb0, xfm, tf70);
            parent->SetWorldXfm(tf70);
        }
    }
}

float HamIKEffector::ApplyConstraints(
    QuatXfm &quatXfm, const Transform &xfm, HamIKEffector *effector
) {
    float f11 = 0;
    for (int i = 0; i < mConstraints.size(); i++) {
        Constraint &curConstraint = mConstraints[i];
        if (curConstraint.mTarget) {
            if (curConstraint.mWeight <= 0) {
                const Transform &world = curConstraint.mTarget->WorldXfm();
                quatXfm.v = world.v;
                quatXfm.q.Set(world.m);
                return 1;
            }
            Transform tf140;
            mSkeleton->NeutralWorldXfm(curConstraint.mTarget, tf140);
            Normalize(tf140.m, tf140.m);
            Transform tfc0;
            Transpose(tf140, tfc0);
            Transform tf180;
            Multiply(xfm, tfc0, tf180);
            float lensq = LengthSquared(tf180.v);
            float f7 = Max(lensq, 0.001f);
            f7 = lensq * -0.0023923444f + (kConstraintConsts[2] / f7)
                + kConstraintConsts[0];
            f7 = Max(f7, 0.0f);
            f7 *= curConstraint.mWeight;
            f11 += f7;
            Transform tf100 = curConstraint.mTarget->WorldXfm();
            Normalize(tf100.m, tf100.m);
            Multiply(tf180, tf100, tf180);
            QuatXfm newQuatXfm(tf180);
            ScaleAdd(quatXfm.v, newQuatXfm.v, f7, quatXfm.v);
            ScaleAddEq(quatXfm.q, newQuatXfm.q, f7);
        }
    }
    if (mMore) {
        f11 += mMore->ApplyConstraints(quatXfm, xfm, effector);
    }
    return f11;
}

float HamIKEffector::ApplyPosConstraints(
    Vector3 &v1, const Vector3 &v2, HamIKEffector *effector
) {
    float f8 = 0;
    for (int i = 0; i < mConstraints.size(); i++) {
        Constraint &curConstraint = mConstraints[i];
        if (curConstraint.mTarget) {
            Transform tf100;
            mSkeleton->NeutralWorldXfm(curConstraint.mTarget, tf100);
            Normalize(tf100.m, tf100.m);
            Transform tfc0;
            Transpose(tf100, tfc0);
            Vector3 v110;
            Multiply(v2, tfc0, v110);
            float lensq = LengthSquared(v110);
            Multiply(v110, curConstraint.mTarget->WorldXfm(), v110);
            float f4 = Max(lensq, 0.001f);
            float f9 = lensq * -0.0023923444f + (kConstraintConsts[2] / f4)
                + kConstraintConsts[0];
            f9 = Max(f9, 0.0f);
            f9 *= curConstraint.mWeight;
            ScaleAdd(v1, v110, f9, v1);
            f8 += f9;
        }
    }
    if (mMore) {
        f8 += mMore->ApplyPosConstraints(v1, v2, effector);
    }
    return f8;
}

float HamIKEffector::GetGroundHeight(RndTransformable *t) {
    HamIKEffector *it = this;
    while (true) {
        RndTransformable *ground = it->mGround;
        if ((int)ground != 0) {
            return ground->WorldXfm().v.z;
        }
        it = it->mMore;
        if ((int)it == 0)
            break;
    }
    return t->WorldXfm().v.z;
}

void HamIKEffector::Poll() {
#ifdef HX_NATIVE
    // Log the poll ORDER to determine if pelvis runs before or after ankle.
    // This is the key question: if pelvis runs AFTER ankle, its SetWorldXfm
    // cascades dirty through the leg chain, overwriting ankle IK corrections.
    {
        static int sPollOrderCount = 0;
        if (sPollOrderCount < 30) {
            sPollOrderCount++;
            EffectorType tDbg = mEffector ? GetType() : kEffectorTypeNone;
            const char* typeNames[] = {"none","pelvis","ankle","hand","forearm","head"};
            fprintf(stderr, "DC3_IK_DIAG PollOrder[%d]: %s type=%s\n",
                    sPollOrderCount, PathName(this),
                    (tDbg >= 0 && tDbg <= 5) ? typeNames[tDbg] : "?");
        }
    }
    // Run the matched IK in CHARACTER-LOCAL space (re-rooted to the world
    // origin), then composite the venue placement back on at render — exactly
    // as Xbox does. See CharLocalIKScope above. No-op for origin-rooted
    // characters (iconman), which already match. This replicates the Xbox flow
    // WITHOUT touching the byte-matched IK math below.
    CharLocalIKScope ikScope(mCharacter.Ptr());
#endif
    if (mSkeleton) {
        EffectorType t = GetType();
        if (t != kEffectorTypeForearm) {
            float weight = Weight();
            if (mEffector && weight != 0.0f) {
                ObjPtr<RndTransformable> &fingerRef =
                    (int)mFinger.Ptr() != 0 ? mFinger : mEffector;
                RndTransformable *finger = fingerRef.Ptr();

                Transform neutral;
                mSkeleton->NeutralWorldXfm(finger, neutral);
                Normalize(neutral.m, neutral.m);
                Transform finalXfm;
                QuatXfm neutralQ(neutral);
                QuatXfm q;
                q.v.x = 0.0f;
                q.v.y = 0.0f;
                q.v.z = 0.0f;
                q.q.x = 0.0f;
                q.q.y = 0.0f;
                q.q.z = 0.0f;
                q.q.w = 0.0f;
                float totalWeight = ApplyConstraints(q, neutral, this);
#ifdef HX_NATIVE
                // ===================================================================
                // FORENSIC FULL-CHAIN TRACE (player0 bone_L-ankle.ikf)
                // One consolidated line per frame, capturing the entire IK chain:
                // inputs -> blend -> back-transform -> SetWorldXfm output -> result.
                // Capture vars are filled as the chain executes (below) and the
                // line is emitted after the final SetWorldXfm.
                // ===================================================================
                bool fcTrace = false;
                {
                    const char *fp = PathName(this);
                    extern int HamDirector_NativeSetFrameCount();
                    static int sFCCount = 0;
                    bool fcMain = fp && strstr(fp, "main.milo") && !strstr(fp, "backup");
                    bool fcLA = fp && strstr(fp, "bone_L-ankle.ikf");
                    if (sFCCount < 40 && fcMain && fcLA && t == kEffectorTypeAnkle
                        && HamDirector_NativeSetFrameCount() > 800) {
                        sFCCount++;
                        fcTrace = true;
                    }
                }
                // chain capture slots
                float fcNeutralX = neutralQ.v.x, fcNeutralY = neutralQ.v.y, fcNeutralZ = neutralQ.v.z;
                float fcEffX = 0, fcEffY = 0, fcEffZ = 0;
                float fcFingerX = 0, fcFingerY = 0, fcFingerZ = 0;
                float fcClampFactor = -999, fcQvX = 0, fcQvY = 0, fcQvZ = 0;
                float fcEffWX = 0, fcEffWY = 0, fcEffWZ = 0;
                float fcFingerWX = 0, fcFingerWY = 0, fcFingerWZ = 0;
                float fcInvX = 0, fcInvY = 0, fcInvZ = 0;
                float fcFinalX = 0, fcFinalY = 0, fcFinalZ = 0;
                float fcTotalWeightIn = totalWeight;  // raw ApplyConstraints result (input)
                {
                    // Capture only main.milo (the actual player character),
                    // not backup.milo. Filter by ankle Z below 1.5 (gameplay pose).
                    static int sTotalWeightLog = 0;
                    const char *path = PathName(this);
                    bool isMain = path && strstr(path, "main.milo") != nullptr
                                  && strstr(path, "backup") == nullptr;
                    if (sTotalWeightLog < 3
                        && t == kEffectorTypeAnkle
                        && isMain
                        && mEffector->WorldXfm().v.z < 1.5f) {
                        // One-shot: dump TypeProps state to see if constraints
                        // live there even when mConstraints is empty.
                        fprintf(stderr,
                            "DC3_IK_DIAG TypePropsDump: type=%s typeProps=%p "
                            "constraintCount=%d\n",
                            Type().Str() ? Type().Str() : "null",
                            (void*)mTypeProps,
                            (int)mConstraints.size());
                        if (mTypeProps) {
                            DataNode *n = mTypeProps->KeyValue(
                                Symbol("constraints"), false);
                            fprintf(stderr,
                                "DC3_IK_DIAG TypePropsConstraints: node=%p type=%d\n",
                                (void*)n, n ? (int)n->Type() : -1);
                        }
                    }
                    extern int HamDirector_NativeSetFrameCount();
                    if (sTotalWeightLog < 60
                        && t == kEffectorTypeAnkle
                        && isMain
                        && HamDirector_NativeSetFrameCount() > 3000
                        && strstr(path, "bone_L-ankle.ikf")) {
                        sTotalWeightLog++;
                        const Transform &fingW = finger->WorldXfm();
                        const Transform &effW = mEffector->WorldXfm();
                        fprintf(stderr,
                            "DC3_IK_DIAG IkSnap[%d] f=%d: effPath=%s "
                            "fingerW.v=(%.2f,%.2f,%.2f) effW.v=(%.2f,%.2f,%.2f) "
                            "neutral.v=(%.2f,%.2f,%.2f) "
                            "fingerDirty=%d effDirty=%d "
                            "totalWeight=%.3f constraintCount=%d\n",
                            sTotalWeightLog,
                            HamDirector_NativeSetFrameCount(),
                            PathName(this),
                            fingW.v.x, fingW.v.y, fingW.v.z,
                            effW.v.x, effW.v.y, effW.v.z,
                            neutral.v.x, neutral.v.y, neutral.v.z,
                            (int)finger->Dirty(), (int)mEffector->Dirty(),
                            totalWeight,
                            (int)mConstraints.size());
                    }
                }
#endif

                if (t == kEffectorTypeHand && mElbow != nullptr) {
                    DoFancyElbow(q, totalWeight);
                } else {
                    if (weight != 1.0f) {
                        MILO_ASSERT(weight == 1, 0x135);
                    }

                    if (totalWeight < 1.0f) {
                        if (totalWeight == 0.0f && t == kEffectorTypeNone)
                            goto done;

                        QuatXfm effQ(finger->WorldXfm());
#ifdef HX_NATIVE
                        if (fcTrace) {
                            fcEffX = effQ.v.x; fcEffY = effQ.v.y; fcEffZ = effQ.v.z;
                            const Transform &fcfw = finger->WorldXfm();
                            fcFingerX = fcfw.v.x; fcFingerY = fcfw.v.y; fcFingerZ = fcfw.v.z;
                            const Transform &fcew = mEffector->WorldXfm();
                            fcEffWX = fcew.v.x; fcEffWY = fcew.v.y; fcEffWZ = fcew.v.z;
                            fcFingerWX = fcfw.v.x; fcFingerWY = fcfw.v.y; fcFingerWZ = fcfw.v.z;
                        }
#endif
                        if (t == kEffectorTypeAnkle || t == kEffectorTypePelvis) {
                            Character *character = mCharacter.Ptr();
                            RndTransformable *ground =
                                character ? (RndTransformable *)character : nullptr;
                            float groundHeight = GetGroundHeight(ground);

                            if (t == kEffectorTypePelvis) {
                                RndTransformable *knee =
                                    CharUtlFindBoneTrans("bone_L-knee", Dir());
                                RndTransformable *ankle =
                                    CharUtlFindBoneTrans("bone_L-ankle", Dir());
                                if (knee != nullptr && ankle != nullptr) {
                                    Vector3 localPos;
                                    mSkeleton->NeutralLocalPos(ankle, localPos);
                                    float ankleLen = localPos.x;
                                    mSkeleton->NeutralLocalPos(knee, localPos);
                                    float kneeLen = localPos.x;
                                    float totalLen = kneeLen + ankleLen;
                                    float worldHeight =
                                        ankle->mLocalXfm.v.x + knee->mLocalXfm.v.x;
                                    float ratio = worldHeight / totalLen;
                                    float lowerBound = kneeLen * 0.3f + ankleLen;
                                    float upperBound = kneeLen * 0.8f + ankleLen;
                                    float blend =
                                        (effQ.v.z - groundHeight - lowerBound)
                                        / (upperBound - lowerBound);
                                    blend = Max(0.0f, blend);
                                    blend = Min(blend, 1.0f);
                                    effQ.v.z =
                                        (((ratio - 1.0f) * blend) + 1.0f)
                                        * (effQ.v.z - groundHeight)
                                        + groundHeight;
                                }
                            } else if (t == kEffectorTypeAnkle) {
                                Vector3 savedPos = effQ.v;
                                float clampFactor =
                                    (neutralQ.v.z - groundHeight - 5.0f) * 0.09090909f;
                                clampFactor = Max(0.0f, clampFactor);
                                clampFactor = Min(clampFactor, 1.0f);
#ifdef HX_NATIVE
                                if (fcTrace) fcClampFactor = clampFactor;
                                {
                                    // Clamp-internals diagnostic: the ankle
                                    // plant lever. neutralZ should be the
                                    // un-dropped (~+4) planted ankle; if it has
                                    // collapsed onto effZ (the sunk live ankle),
                                    // Interp returns the sunk pose regardless.
                                    static int sClampLog = 0;
                                    const char *p = PathName(this);
                                    bool isMain = p && strstr(p, "main.milo")
                                                  && !strstr(p, "backup");
                                    if (sClampLog < 24 && isMain) {
                                        sClampLog++;
                                        fprintf(stderr,
                                            "DC3_IK_DIAG AnkleClamp[%d]: eff=%s "
                                            "neutralZ=%.3f effZ=%.3f groundH=%.3f "
                                            "clampFactor=%.4f neutralV=(%.2f,%.2f,%.2f) "
                                            "effV=(%.2f,%.2f,%.2f)\n",
                                            sClampLog, p,
                                            neutralQ.v.z, effQ.v.z, groundHeight,
                                            clampFactor,
                                            neutralQ.v.x, neutralQ.v.y, neutralQ.v.z,
                                            effQ.v.x, effQ.v.y, effQ.v.z);
                                    }

                                    // SAME-INSTANT CHAIN TRACE: frame-correlated.
                                    // Restrict to ONE effector (bone_L-ankle.ikf)
                                    // so consecutive samples track the same chain
                                    // across frames. Logs frame#, the eff source
                                    // (finger == spot_L-toe.trans), and the live
                                    // L-leg chain via Dir(). Resolves whether the
                                    // finger / toe is planted (+0.1) or sunk
                                    // (-4) at the exact clamp instant, and how it
                                    // tracks frame-to-frame.
                                    extern int HamDirector_NativeSetFrameCount();
                                    static int sChainLog = 0;
                                    bool isLeftAnkle = p && strstr(p, "bone_L-ankle.ikf");
                                    if (sChainLog < 60 && isMain && isLeftAnkle
                                        && HamDirector_NativeSetFrameCount() > 3000) {
                                        sChainLog++;
                                        ObjectDir *d = Dir();
                                        const char *names[] = {
                                            "bone_L-toe.mesh",
                                            "spot_L-toe.trans",
                                            "bone_L-ankle.mesh",
                                            "bone_L-knee.mesh",
                                            "bone_L-thigh.mesh",
                                            "bone_pelvis.mesh",
                                        };
                                        fprintf(stderr,
                                            "DC3_IK_DIAG ChainZ[%d] f=%d eff=%s:",
                                            sChainLog,
                                            HamDirector_NativeSetFrameCount(), p);
                                        for (int ci = 0; ci < 6; ci++) {
                                            RndTransformable *bt =
                                                d ? d->Find<RndTransformable>(
                                                        names[ci], false)
                                                  : nullptr;
                                            if (bt) {
                                                const Transform &w = bt->WorldXfm();
                                                fprintf(stderr, " %s.z=%.2f(d=%d)",
                                                    names[ci], w.v.z,
                                                    (int)bt->Dirty());
                                            } else {
                                                fprintf(stderr, " %s=<null>",
                                                    names[ci]);
                                            }
                                        }
                                        // finger == the eff source for the clamp
                                        const Transform &fw = finger->WorldXfm();
                                        fprintf(stderr,
                                            " | FINGER=%s effZ_used=%.3f"
                                            " fingerW.z=%.3f neutralZ=%.3f"
                                            " clampF=%.3f\n",
                                            finger->Name(), effQ.v.z,
                                            fw.v.z, neutralQ.v.z, clampFactor);
                                    }
                                }
#endif
                                Interp(neutralQ.v, effQ.v, clampFactor, q.v);
                                Interp(neutralQ.q, effQ.q, clampFactor, q.q);
                                if (effQ.v.z < groundHeight) {
                                    effQ.v.z = groundHeight;
                                }
                                effQ.v.x = savedPos.x;
                                effQ.v.y = savedPos.y;
                            }
                        }

                        float remaining = 1.0f - totalWeight;
                        q.v.x += remaining * effQ.v.x;
                        q.v.y += remaining * effQ.v.y;
                        q.v.z += remaining * effQ.v.z;
                        ScaleAddEq(q.q, effQ.q, remaining);
                        totalWeight += remaining;
                    }

                    float invWeight = 1.0f / totalWeight;
                    q.v.x *= invWeight;
                    q.v.y *= invWeight;
                    q.v.z *= invWeight;
                    Normalize(q.q, q.q);

#ifdef HX_NATIVE
                    if (fcTrace) { fcQvX = q.v.x; fcQvY = q.v.y; fcQvZ = q.v.z; }
#endif
                    finalXfm.v = q.v;
                    MakeRotMatrix(q.q, finalXfm.m);

#ifdef HX_NATIVE
                    Transform dbgFinalBefore = finalXfm;
                    bool dbgBackXform = false;
                    Transform dbgInv;
                    bool dbgFingerIsChild = false;
#endif
                    if (finger != mEffector.Ptr()) {
                        Transform inv;
                        if (finger->TransParent() == mEffector.Ptr()) {
                            FastInvert(finger->LocalXfm(), inv);
#ifdef HX_NATIVE
                            dbgFingerIsChild = true;
#endif
                        } else {
                            FastInvert(finger->WorldXfm(), inv);
                            Multiply(mEffector->WorldXfm(), inv, inv);
                        }
                        Multiply(inv, finalXfm, finalXfm);
#ifdef HX_NATIVE
                        dbgBackXform = true;
                        dbgInv = inv;
                        if (fcTrace) { fcInvX = inv.v.x; fcInvY = inv.v.y; fcInvZ = inv.v.z; }
#endif
                    }
#ifdef HX_NATIVE
                    if (fcTrace) {
                        fcFinalX = finalXfm.v.x; fcFinalY = finalXfm.v.y; fcFinalZ = finalXfm.v.z;
                    }
#endif
#ifdef HX_NATIVE
                    {
                        extern int HamDirector_NativeSetFrameCount();
                        static int sBackLog = 0;
                        const char *bp = PathName(this);
                        bool bMain = bp && strstr(bp, "main.milo") && !strstr(bp, "backup");
                        bool bLA = bp && strstr(bp, "bone_L-ankle.ikf");
                        if (sBackLog < 40 && bMain && bLA
                            && HamDirector_NativeSetFrameCount() > 800) {
                            sBackLog++;
                            const Transform &fingW = finger->WorldXfm();
                            const Transform &fingL = finger->LocalXfm();
                            const Transform &effW = mEffector->WorldXfm();
                            RndTransformable *fp = finger->TransParent();
                            fprintf(stderr,
                                "DC3_IK_DIAG BackXform[%d] f=%d eff=%s\n"
                                "  q.v=(%.3f,%.3f,%.3f) finalBefore.v=(%.3f,%.3f,%.3f)\n"
                                "  finger=%s fingerW.v=(%.3f,%.3f,%.3f) fingerL.v=(%.3f,%.3f,%.3f)\n"
                                "  fingerParent=%s isEffChild=%d backXform=%d\n"
                                "  effW.v=(%.3f,%.3f,%.3f)\n"
                                "  inv.v=(%.3f,%.3f,%.3f) inv.m.x=(%.3f,%.3f,%.3f)\n"
                                "  finalAfter.v=(%.3f,%.3f,%.3f)\n",
                                sBackLog, HamDirector_NativeSetFrameCount(), bp,
                                q.v.x, q.v.y, q.v.z,
                                dbgFinalBefore.v.x, dbgFinalBefore.v.y, dbgFinalBefore.v.z,
                                finger->Name(),
                                fingW.v.x, fingW.v.y, fingW.v.z,
                                fingL.v.x, fingL.v.y, fingL.v.z,
                                fp ? fp->Name() : "<null>",
                                (int)dbgFingerIsChild, (int)dbgBackXform,
                                effW.v.x, effW.v.y, effW.v.z,
                                dbgInv.v.x, dbgInv.v.y, dbgInv.v.z,
                                dbgInv.m.x.x, dbgInv.m.x.y, dbgInv.m.x.z,
                                finalXfm.v.x, finalXfm.v.y, finalXfm.v.z);
                            // Walk the effector's parent chain to find where the
                            // large venue-world placement (X~55) enters. Xbox
                            // ground truth: ankle effector world ~(0.1,0,0) i.e.
                            // CHARACTER-LOCAL; native is venue-world (X~55).
                            fprintf(stderr, "  PARENTCHAIN:");
                            RndTransformable *pc = mEffector.Ptr();
                            for (int depth = 0; pc && depth < 10; depth++) {
                                const Transform &pw = pc->WorldXfm();
                                const Transform &pl = pc->LocalXfm();
                                fprintf(stderr,
                                    " [%s W=(%.2f,%.2f,%.2f) L=(%.2f,%.2f,%.2f)]",
                                    pc->Name() ? pc->Name() : "?",
                                    pw.v.x, pw.v.y, pw.v.z,
                                    pl.v.x, pl.v.y, pl.v.z);
                                pc = pc->TransParent();
                            }
                            fprintf(stderr, "\n");
                        }
                    }
#endif

                    if (mOther) {
                        mEffector->SetWorldXfm(finalXfm);
                        mOther->Poll();
                        finalXfm = mEffector->WorldXfm();
                    }

                    if (t == kEffectorTypeAnkle || t == kEffectorTypeHand) {
                        IKElbow(finalXfm.v);
                    }


                    mEffector->SetWorldXfm(finalXfm);
#ifdef HX_NATIVE
                    if (fcTrace) {
                        extern int HamDirector_NativeSetFrameCount();
                        // Post-write world Z of the ankle and toe (the rendered result).
                        // mEffector is the L-ankle bone here. Toe via Dir() lookup,
                        // matching the ChainZ diag bone names.
                        float wroteAnkleZ = mEffector->WorldXfm().v.z;
                        float wroteAnkleX = mEffector->WorldXfm().v.x;
                        float wroteAnkleY = mEffector->WorldXfm().v.y;
                        ObjectDir *fcd = Dir();
                        float toeZ = -999, toeX = -999, toeY = -999;
                        RndTransformable *toe = fcd
                            ? fcd->Find<RndTransformable>("bone_L-toe.mesh", false)
                            : nullptr;
                        if (toe) {
                            const Transform &tw = toe->WorldXfm();
                            toeZ = tw.v.z; toeX = tw.v.x; toeY = tw.v.y;
                        }
                        // ONE consolidated, frame-tagged line: the full chain.
                        fprintf(stderr,
                            "DC3_FCHAIN f=%d"
                            " neutral=(%.3f,%.3f,%.3f)"
                            " eff=(%.3f,%.3f,%.3f)"
                            " finger=(%.3f,%.3f,%.3f)"
                            " clampF=%.4f totWin=%.4f totWout=%.4f"
                            " q.v=(%.3f,%.3f,%.3f)"
                            " effW=(%.3f,%.3f,%.3f)"
                            " fingerW=(%.3f,%.3f,%.3f)"
                            " inv=(%.3f,%.3f,%.3f)"
                            " finalXfm=(%.3f,%.3f,%.3f)"
                            " => ankleW=(%.3f,%.3f,%.3f) toeW=(%.3f,%.3f,%.3f)\n",
                            HamDirector_NativeSetFrameCount(),
                            fcNeutralX, fcNeutralY, fcNeutralZ,
                            fcEffX, fcEffY, fcEffZ,
                            fcFingerX, fcFingerY, fcFingerZ,
                            fcClampFactor, fcTotalWeightIn, totalWeight,
                            fcQvX, fcQvY, fcQvZ,
                            fcEffWX, fcEffWY, fcEffWZ,
                            fcFingerWX, fcFingerWY, fcFingerWZ,
                            fcInvX, fcInvY, fcInvZ,
                            fcFinalX, fcFinalY, fcFinalZ,
                            wroteAnkleX, wroteAnkleY, wroteAnkleZ,
                            toeX, toeY, toeZ);
                    }
#endif
                }
            }
        }
    }
done:;
}

void HamIKEffector::ComputeHandPullAndQuat(
    QuatXfm &quatOut, Transform &xfmOut, const Transform &parentXfm, const Vector3 &targetPos
) {
    float dz = targetPos.z - parentXfm.v.z;
    RndTransformable *effector = mEffector;
    float dx = targetPos.x - parentXfm.v.x;
    RndTransformable *parent = effector->TransParent();
    float dy = targetPos.y - parentXfm.v.y;
    quatOut.v.z = dz;
    quatOut.v.x = dx;
    quatOut.v.y = dy;

    float effectorLen = effector->LocalXfm().v.x;
    float parentLen = parent->LocalXfm().v.x;
    float maxReach = (parentLen + effectorLen) * 0.99f;
    float maxReachSq = maxReach * maxReach;
    float distSq = dz * dz + dy * dy + dx * dx;

    if (distSq <= maxReachSq
        || (GetType() != kEffectorTypeHand && GetType() != kEffectorTypeAnkle)) {
        quatOut.v.z = 0.0f;
        quatOut.v.y = 0.0f;
        quatOut.v.x = 0.0f;
    } else {
        float factor = 1.0f - maxReach / sqrtf(distSq);
        quatOut.v.x *= factor;
        quatOut.v.y *= factor;
        quatOut.v.z *= factor;
        distSq = maxReachSq;
    }

    RndTransformable *effParent = mEffector->TransParent();
    xfmOut.v = effParent->LocalXfm().v;

    float sumSq = effectorLen * effectorLen + parentLen * parentLen;
    float cosAngle = (distSq - sumSq) / (parentLen * effectorLen * 2.0f);

    xfmOut.m.x.z = 0.0f;

    float clampedCos = -1.0f - cosAngle < 0.0f ? cosAngle : -1.0f;
    clampedCos = clampedCos - 1.0f < 0.0f ? clampedCos : 1.0f;

    xfmOut.m.x.x = clampedCos;
    float sinAngle = -sqrtf(-(clampedCos * clampedCos - 1.0f));
    xfmOut.m.x.y = sinAngle;
    const Vector3 &effLocalV = mEffector->LocalXfm().v;
    xfmOut.m.y.y = clampedCos;
    xfmOut.m.y.z = 0.0f;
    xfmOut.m.y.x = -sinAngle;
    xfmOut.m.z.x = 0.0f;
    xfmOut.m.z.y = 0.0f;
    xfmOut.m.z.z = 1.0f;

    Vector3 localDir;
    Multiply(effLocalV, xfmOut, localDir);
    Vector3 localTarget;
    MultiplyTranspose(targetPos, parentXfm, localTarget);
    MakeRotQuat(localDir, localTarget, quatOut.q);
}

void HamIKEffector::DoFancyElbow(QuatXfm &handQ, float handWeight) {
    RndTransformable *parent = mEffector->TransParent();
    if (parent != nullptr) {
        RndTransformable *grandparent = parent->TransParent();
        if (grandparent != nullptr) {
            // Get neutral world transform of parent
            Transform neutralParent;
            mSkeleton->NeutralWorldXfm(parent, neutralParent);

            // Apply elbow position constraints
            Vector3 posAccum;
            posAccum.z = 0.0f;
            posAccum.y = 0.0f;
            posAccum.x = 0.0f;
            float elbowWeight = mElbow->ApplyPosConstraints(posAccum, neutralParent.v, this);

            float totalWeight = elbowWeight + handWeight;
            if (totalWeight == 0.0f)
                return;

            // Initialize pull and quaternion accumulators
            QuatXfm accum;
            accum.v.x = 0.0f;
            accum.v.y = 0.0f;
            accum.v.z = 0.0f;
            accum.q.x = 0.0f;
            accum.q.y = 0.0f;
            accum.q.z = 0.0f;
            accum.q.w = 0.0f;
            float remaining = 0.0f;

            if (totalWeight < 1.0f) {
                remaining = 1.0f - totalWeight;
                totalWeight += remaining;
            }

            // Copy grandparent world transform
            Transform elbowXfm;
            Transform gpXfm = grandparent->WorldXfm();

            // Apply elbow contribution
            if (elbowWeight > 0.0f) {
                float invElbow = 1.0f / elbowWeight;
                posAccum.x *= invElbow;
                posAccum.y *= invElbow;
                posAccum.z *= invElbow;

                QuatXfm elbowQ;
                ComputeElbowPullAndQuat(elbowQ, gpXfm, posAccum);

                accum.v.x += elbowQ.v.x * elbowWeight;
                accum.v.y += elbowQ.v.y * elbowWeight;
                accum.v.z += elbowQ.v.z * elbowWeight;
                ScaleAddEq(accum.q, elbowQ.q, elbowWeight);
            }

            // Apply hand contribution
            if (handWeight > 0.0f) {
                float invHand = 1.0f / handWeight;
                Vector3 handPos;
                handPos.x = handQ.v.x * invHand;
                handPos.y = handQ.v.y * invHand;
                handPos.z = handQ.v.z * invHand;

                QuatXfm handPullQ;
                ComputeHandPullAndQuat(handPullQ, elbowXfm, gpXfm, handPos);

                accum.v.x += handPullQ.v.x * handWeight;
                accum.v.y += handPullQ.v.y * handWeight;
                accum.v.z += handPullQ.v.z * handWeight;
                ScaleAddEq(accum.q, handPullQ.q, handWeight);
            }

            // Normalize quaternion and compute final rotation
            Normalize(accum.q, accum.q);
            float invTotal = 1.0f / totalWeight;
            accum.v.x *= invTotal;
            accum.v.y *= invTotal;
            accum.v.z *= invTotal;

            Hmx::Matrix3 rotMat;
            MakeRotMatrix(accum.q, rotMat);
            Multiply(rotMat, gpXfm.m, gpXfm.m);

            // Apply scaled pull to grandparent position
            gpXfm.v.x += accum.v.x;
            gpXfm.v.y += accum.v.y;
            gpXfm.v.z += accum.v.z;

            grandparent->SetWorldXfm(gpXfm);

            // If hand contributes, blend parent (forearm) and effector (hand) rotations
            if (handWeight > 0.0f) {
                // Blend parent rotation between local neutral and hand-computed elbow xfm
                Hmx::Quat parentQ;
                parentQ.Set(parent->LocalXfm().m);

                float otherWeight = remaining + elbowWeight;
                parentQ.x *= otherWeight;
                parentQ.y *= otherWeight;
                parentQ.z *= otherWeight;
                parentQ.w *= otherWeight;

                Hmx::Quat elbowXfmQ;
                elbowXfmQ.Set(elbowXfm.m);
                ScaleAddEq(parentQ, elbowXfmQ, handWeight);
                Normalize(parentQ, parentQ);

                MakeRotMatrix(parentQ, rotMat);

                // Build new parent world transform: blended rotation + current position
                Transform parentNewXfm;
                const Transform &parentWorld = parent->WorldXfm();
                parentNewXfm.v = parentWorld.v;
                Multiply(rotMat, gpXfm.m, parentNewXfm.m);
                parent->SetWorldXfm(parentNewXfm);

                // Blend effector rotation
                const Transform &effWorld = mEffector->WorldXfm();
                Transform effXfm;
                effXfm.v = effWorld.v;

                Hmx::Quat effQ;
                effQ.Set(effWorld.m);
                ScaleAddEq(handQ.q, effQ, otherWeight);
                Normalize(handQ.q, handQ.q);

                MakeRotMatrix(handQ.q, effXfm.m);
                mEffector->SetWorldXfm(effXfm);
            }
        }
    }
}

void HamIKEffector::ComputeElbowPullAndQuat(
    QuatXfm &q, const Transform &xfm, const Vector3 &v
) {
    Vector3 v40;
    MultiplyTranspose(v, xfm, v40);
    const Vector3 &effectorV = mEffector->TransParent()->LocalXfm().v;
    MakeRotQuat(effectorV, v40, q.q);
    float dy = v.y - xfm.v.y;
    float dx = v.x - xfm.v.x;
    float dz = v.z - xfm.v.z;
    q.v.x = dx;
    float len = sqrtf(dy * dy + q.v.x * q.v.x + dz * dz);
    float factor = 1.0f - effectorV.x / len;
    q.v.x = q.v.x * factor;
    q.v.y = dy * factor;
    q.v.z = dz * factor;
}
