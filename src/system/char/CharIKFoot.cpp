#include "char/CharIKFoot.h"
#include "CharIKHand.h"
#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Trans.h"
#ifdef HX_NATIVE
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <set>
// FRAME-KEYED PLANT GUARD: the leg foot-plant writes leg bone LOCALs, but a later
// CharBonesMeshes::PoseMeshes in the same frame (nondeterministic LP64 poll order) re-poses
// the leg from the anim and overwrites the plant. The plant adds its bones here; PoseMeshes
// skips a guarded bone; the guard clears at each new frame's first PoseMeshes (keyed on
// TheTaskMgr.UISeconds()) so the legit pose always runs first and only LATER overwrites skip.
std::set<RndTransformable *> gDc3PlantGuard;
bool gDc3PlantGuardActive = false;
static float gDc3PlantGuardFrame = -1.0f;
void Dc3PlantGuardTick() {
    if (!gDc3PlantGuardActive)
        return;
    float t = TheTaskMgr.UISeconds();
    if (t != gDc3PlantGuardFrame) {
        gDc3PlantGuard.clear();
        gDc3PlantGuardFrame = t;
    }
}
bool Dc3PlantGuarded(RndTransformable *b) {
    return gDc3PlantGuardActive && gDc3PlantGuard.count(b) != 0;
}
// FEET-IN-FLOOR FIX (opt-out: DC3_FEET_PLANT_FIX_OFF=1). The leg foot-plant IK must
// bend the leg; native loads mMoveElbow=false for the *.ikfoot, which disables the
// IKElbow knee bend AND drops the knee/thigh dependency from CharIKHand::PollDeps, so
// the sorter polls the IK before the skeleton pose and the bend is overwritten. See
// CharIKFoot::Load (forces mMoveElbow=true) + Character::SyncObjects (IK sorts last).
bool Dc3FeetPlantFix() {
    // OPT-IN (DC3_FEET_PLANT_FIX=1), default OFF, NON-FUNCTIONAL as of Push 13. With the fix
    // on, the active leg IK DIVERGES: the rendered LEFT leg points ~horizontal (ankle composes
    // to z~35 = straight-leg length, every frame) and the right sinks — a coupled instability.
    // The gate's one-sided "toe < -2" check makes the up-flung left look planted (0/737) but it
    // is NOT. Root: CharIKHand::IKElbow's thigh Z-rotation assumes a rest frame that bends the
    // leg DOWN on Xbox but ~horizontal on native (bone rest-frame / SetWorldXfm-vs-LOCAL compose
    // mismatch). Do NOT ship this on; baseline (off) is stable at toe ~-4.2. See Push 13/14 in
    // docs/sessions/2026-06-09-xenia-xbox-foot-truth.md for the three candidate real fixes.
    static int v = -1;
    if (v < 0)
        v = getenv("DC3_FEET_PLANT_FIX") ? 1 : 0;
    return v != 0;
}
// When the fix is on, the leg IK runs ONCE per frame from HamDirector::Poll AFTER the
// song-move pose (set true around that re-run). During the normal char poll it is skipped
// (the move pose would overwrite it, and running it twice destabilizes the foot-plant FSM).
bool gDc3DirectorIKReRun = false;
// Monotonic poll-sequence counter (DC3_IK_DIAG2) to find who writes a leg bone LAST in a frame.
int gDc3PollSeq = 0;

// Force a transform's whole parent chain to recompose its cached WorldXfm from LOCAL,
// top-down. The leg IK intermittently reads a stale/un-composed WorldXfm (a bone whose
// WorldXfm was SetWorldXfm'd clean by another system, or whose ancestor wasn't recomposed
// after the move pose) -> the toe-target/ankle reads a wild position (z 150 / -120) and the
// solver diverges (ankle -22). Walking to the root, marking dirty, then reading WorldXfm
// top-down rebuilds each cached world from the (correct) posed LOCAL before the IK uses it.
static void Dc3RecomposeChain(RndTransformable *t) {
    if (!t)
        return;
    RndTransformable *chain[40];
    int n = 0;
    for (RndTransformable *p = t; p && n < 40; p = p->TransParent())
        chain[n++] = p;
    for (int i = 0; i < n; i++)
        (void)chain[i]->DirtyLocalXfm(); // public; marks dirty + propagates to children
    for (int i = n - 1; i >= 0; i--)
        (void)chain[i]->WorldXfm();
}

// CLEAN STATELESS 2-BONE FOOT PLANT (DC3_FEET_CLEAN_PLANT). Bypasses the diverging
// CharIKHand solver (its thigh pure-Z-rotation assumes a rest frame that bends the leg
// ~horizontal on native). Works entirely in world space on the FK-composed leg, writes
// bone LOCALs at the end (survives render), and is ONE-DIRECTIONAL (only lifts a foot
// whose toe is below the floor) so it can't oscillate. Preserves the ankle's WORLD
// rotation so the rigid foot's toe rises exactly with the ankle.
//   ankle=mHand, knee=ankle parent (shin), thigh=knee parent, hip=thigh parent (pelvis).
static bool Dc3CleanPlant(RndTransformable *ankle, RndTransformable *toe) {
    if (!ankle || !toe)
        return false;
    RndTransformable *knee = ankle->TransParent();
    RndTransformable *thigh = knee ? knee->TransParent() : 0;
    RndTransformable *hip = thigh ? thigh->TransParent() : 0;
    if (!knee || !thigh || !hip)
        return false;

    // Wave-4 Lane A: snapshot the leg LOCAL matrices so we can REVERT to the
    // faithful anim pose if the 2-bone solve diverges. The clean-plant must be
    // strictly improve-or-no-op: at move boundaries (activeMoveCount==0, mid-blend
    // pose) the solve was flinging a foot to toe Z ~= -12 (worse than the baseline
    // -4). Reverting on divergence keeps those frames at the (sane) anim pose.
    Hmx::Matrix3 thigh0 = thigh->LocalXfm().m;
    Hmx::Matrix3 knee0 = knee->LocalXfm().m;
    Hmx::Matrix3 ankle0 = ankle->LocalXfm().m;
    float toeZ0 = toe->WorldXfm().v.z;              // anim toe Z (pre-solve)
    float ankleZ0 = ankle->WorldXfm().v.z;          // anim ankle Z (pre-solve)

    Transform ankleW0 = ankle->WorldXfm();          // foot orientation to preserve
    Vector3 H = thigh->WorldXfm().v;                 // hip joint (fixed)
    Vector3 Kc = knee->WorldXfm().v;                 // current knee joint
    Vector3 A = ankleW0.v;                           // current ankle joint
    Vector3 T = toe->WorldXfm().v;                   // current toe (foot tip)

    const float kFloor = 0.0f, kMargin = 0.6f;
    float deficit = (kFloor + kMargin) - T.z;
    if (deficit <= 0.0f)
        return true;                                 // foot already clear; leave anim pose

    Vector3 Atarget(A.x, A.y, A.z + deficit);        // lift ankle by the toe's deficit

    Vector3 femurV; Subtract(Kc, H, femurV);  float a = Length(femurV);
    Vector3 shinV;  Subtract(A, Kc, shinV);   float b = Length(shinV);
    Vector3 dV;     Subtract(Atarget, H, dV); float d = Length(dV);
    if (a < 1e-3f || b < 1e-3f || d < 1e-3f)
        return false;
    float reach = a + b - 0.02f;
    if (d > reach) {                                 // clamp target onto max reach
        Vector3 dir; Normalize(dV, dir); Scale(dir, reach, dV); Add(H, dV, Atarget); d = reach;
    }

    // Knee position: planar 2-bone IK; pole preserves the anim bend plane (current knee).
    Vector3 hToA; Normalize(dV, hToA);
    float l = (a * a - b * b + d * d) / (2.0f * d);
    float hsq = a * a - l * l; float hgt = hsq > 0.0f ? std::sqrt(hsq) : 0.0f;
    Vector3 kRel; Subtract(Kc, H, kRel);
    float dp = Dot(kRel, hToA);
    Vector3 proj; Scale(hToA, dp, proj);
    Vector3 perp; Subtract(kRel, proj, perp);
    if (Length(perp) < 1e-3f) {                      // degenerate: pick forward as pole
        Vector3 fwd(0.0f, 1.0f, 0.0f);
        float dp2 = Dot(fwd, hToA); Vector3 pr2; Scale(hToA, dp2, pr2); Subtract(fwd, pr2, perp);
        if (Length(perp) < 1e-3f) { perp = Vector3(1.0f, 0.0f, 0.0f); }
    }
    Normalize(perp, perp);
    Vector3 Kp; { Vector3 t1; Scale(hToA, l, t1); Vector3 t2; Scale(perp, hgt, t2);
                  Add(H, t1, Kp); Add(Kp, t2, Kp); }

    // Aim the THIGH so its child (knee) lands at Kp.
    {
        Vector3 curDir; Normalize(femurV, curDir);
        Vector3 wantV; Subtract(Kp, H, wantV); Vector3 wantDir; Normalize(wantV, wantDir);
        Hmx::Quat q; MakeRotQuat(curDir, wantDir, q);
        Hmx::Matrix3 dR; MakeRotMatrix(q, dR);
        Hmx::Matrix3 wmNew; Multiply(dR, thigh->WorldXfm().m, wmNew);
        Hmx::Matrix3 hipInv; Invert(hip->WorldXfm().m, hipInv);
        Hmx::Matrix3 lmNew; Multiply(hipInv, wmNew, lmNew);
        thigh->DirtyLocalXfm().m = lmNew;            // keep local .v (bone offset)
    }
    (void)thigh->WorldXfm();                         // recompose; knee now near Kp
    Vector3 Kw = knee->WorldXfm().v;

    // Aim the KNEE so its child (ankle) lands at Atarget.
    {
        Vector3 curV; Subtract(A, Kc, curV); Vector3 curDir; Normalize(curV, curDir);
        // recompute current world shin dir after the thigh moved
        Vector3 ankNow = ankle->WorldXfm().v; Vector3 curNow; Subtract(ankNow, Kw, curNow);
        Normalize(curNow, curDir);
        Vector3 wantV; Subtract(Atarget, Kw, wantV); Vector3 wantDir; Normalize(wantV, wantDir);
        Hmx::Quat q; MakeRotQuat(curDir, wantDir, q);
        Hmx::Matrix3 dR; MakeRotMatrix(q, dR);
        Hmx::Matrix3 wmNew; Multiply(dR, knee->WorldXfm().m, wmNew);
        Hmx::Matrix3 thInv; Invert(thigh->WorldXfm().m, thInv);
        Hmx::Matrix3 lmNew; Multiply(thInv, wmNew, lmNew);
        knee->DirtyLocalXfm().m = lmNew;
    }
    (void)knee->WorldXfm();

    // Preserve the foot orientation: set ankle LOCAL so its world rotation == original.
    {
        Hmx::Matrix3 knInv; Invert(knee->WorldXfm().m, knInv);
        Hmx::Matrix3 lmNew; Multiply(knInv, ankleW0.m, lmNew);
        ankle->DirtyLocalXfm().m = lmNew;
    }
    (void)ankle->WorldXfm();

    // Wave-4 Lane A: validate the solve. If it DIVERGED — produced NaN, or pushed
    // the ankle/toe LOWER than the faithful anim pose (the move-boundary fling that
    // sent toe Z to ~-12) — REVERT to the snapshotted anim LOCALs. The plant is then
    // a strict improvement (never worse than baseline) on every frame.
    {
        float toeZ1 = toe->WorldXfm().v.z;
        float ankleZ1 = ankle->WorldXfm().v.z;
        bool nan = !(toeZ1 == toeZ1) || !(ankleZ1 == ankleZ1);
        // Tolerance: allow a tiny dip (float noise) but reject a real regression.
        bool worse = (toeZ1 < toeZ0 - 0.25f) || (ankleZ1 < ankleZ0 - 0.25f);
        if (nan || worse) {
            thigh->DirtyLocalXfm().m = thigh0;
            knee->DirtyLocalXfm().m = knee0;
            ankle->DirtyLocalXfm().m = ankle0;
            (void)thigh->WorldXfm();
            (void)knee->WorldXfm();
            (void)ankle->WorldXfm();
            return true;                            // leave the anim pose; no guard
        }
    }

    // Guard the bend bones so a later PoseMeshes this frame can't overwrite the plant.
    // Also guard the hip (pelvis): a later pose dropping the pelvis would drag the whole
    // planted leg down (the extreme-move drift). DC3_NO_GUARD_HIP keeps the pelvis poseable.
    gDc3PlantGuardActive = true;
    gDc3PlantGuard.insert(thigh);
    gDc3PlantGuard.insert(knee);
    gDc3PlantGuard.insert(ankle);
    if (!getenv("DC3_NO_GUARD_HIP"))
        gDc3PlantGuard.insert(hip);
    return true;
}
#endif

CharIKFoot::CharIKFoot() : mFootBone(this), mFootFsmState(0), mData(this), mDataIndex(0) {
    mFootBone = Hmx::Object::New<RndTransformable>();
    mFootBone->DirtyLocalXfm().Reset();
}

CharIKFoot::~CharIKFoot() { delete mFootBone; }

BEGIN_HANDLERS(CharIKFoot)
    HANDLE_SUPERCLASS(CharIKHand)
END_HANDLERS

BEGIN_PROPSYNCS(CharIKFoot)
    SYNC_PROP(data, mData)
    SYNC_PROP(data_index, mDataIndex)
    SYNC_SUPERCLASS(CharIKHand)
END_PROPSYNCS

BEGIN_SAVES(CharIKFoot)
    SAVE_REVS(6, 0)
    SAVE_SUPERCLASS(CharIKHand)
    bs << mData;
    bs << mDataIndex;
END_SAVES

BEGIN_COPYS(CharIKFoot)
    COPY_SUPERCLASS(CharIKHand)
    CREATE_COPY(CharIKFoot)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mData)
        COPY_MEMBER(mDataIndex)
    END_COPYING_MEMBERS
END_COPYS

INIT_REVS(6, 0)

BEGIN_LOADS(CharIKFoot)
    LOAD_REVS(bs)
    ASSERT_REVS(6, 0)
    LOAD_SUPERCLASS(CharIKHand)
    if (d.rev < 6) {
        Symbol s;
        d >> s;
    }
    if (d.rev < 5) {
        int i;
        if (d.rev > 1)
            d >> i;
        if (d.rev > 2)
            d >> i;
        if (d.rev > 3)
            d >> i;
    } else {
        d >> mData;
        d >> mDataIndex;
    }
#ifdef HX_NATIVE
    // A foot-plant IK has to move the knee/thigh to plant the foot. Native loads
    // mMoveElbow=false here (the leg over-extends and the foot sinks); Xbox renders a
    // bent, planted knee. Force the elbow-move path so IKElbow bends the knee and
    // PollDeps declares the knee/thigh dep (which the poll-order fix relies on).
    if (Dc3FeetPlantFix())
        mMoveElbow = true;
#endif
END_LOADS

void CharIKFoot::Enter() {
    mFootFsmState = 0;
    mFootBlendTime = 0.0f;
}

void CharIKFoot::PollDeps(std::list<Hmx::Object *> &l1, std::list<Hmx::Object *> &l2) {
    CharIKHand::PollDeps(l1, l2);
}

void CharIKFoot::Poll() {
#ifdef HX_NATIVE
    {
        // EXPERIMENT (DC3_IK_CHARFOOT_SKIP=1): skip CharIKFoot entirely. DoFSM
        // writes the foot bone LOCAL z = mFinger (toe-target) world z, and a local
        // write SURVIVES the render recompute (unlike HamIKEffector's SetWorldXfm).
        // So this is the proximate driver of the rendered foot Z. Disambiguates
        // whether CharIKFoot (fed a sunk toe-target) sinks the foot, vs the raw
        // anim. Remove before shipping.
        static int sCharFootSkip = -1;
        if (sCharFootSkip < 0)
            sCharFootSkip = getenv("DC3_IK_CHARFOOT_SKIP") ? 1 : 0;
        if (sCharFootSkip)
            return;
    }
    {
        static int sCharIKFootPollLog = 0;
        if (getenv("DC3_IK_DIAG2") && sCharIKFootPollLog < 400) {
            sCharIKFootPollLog++;
            const Vector3 fw = mFinger ? mFinger->WorldXfm().v : Vector3(0,0,0);
            const Vector3 hw = mHand ? mHand->WorldXfm().v : Vector3(0,0,0);
            fprintf(stderr,
                "DC3_IK_DIAG CharIKFootPoll[%d]: path=%s finger='%s'@(%.2f,%.2f,%.2f) "
                "hand='%s'@(%.2f,%.2f,%.2f) data='%s'\n",
                sCharIKFootPollLog,
                PathName(this),
                mFinger ? mFinger->Name() : "null", fw.x, fw.y, fw.z,
                mHand ? mHand->Name() : "null", hw.x, hw.y, hw.z,
                mData ? mData->Name() : "null");
        }
    }
#endif
#ifdef HX_NATIVE
    // Run the leg IK ONCE, from HamDirector::Poll after the move pose (gDc3DirectorIKReRun), where
    // the FSM-lock gives a fixed floor target -> stable solve. Skipping the normal char poll avoids
    // the double-apply and the char-poll IK's intermittent un-composed-leg spike.
    // Clean plant runs in BOTH the normal char poll (feetandhands.pgrp, sorts last [28]) AND the
    // HamDirector re-run, so whichever fires later wins (covers an overwrite between them). The
    // diverging CharIKHand path only runs in the re-run.
    if (Dc3FeetPlantFix() && !gDc3DirectorIKReRun && !getenv("DC3_FEET_CLEAN_PLANT"))
        return;
#endif
    if (mFinger && mHand && mData) {
#ifdef HX_NATIVE
        // Rebuild the toe-target + ankle world transforms from their posed LOCALs before the
        // IK reads them, so an intermittently un-composed chain can't feed the solver a wild
        // goal (see Dc3RecomposeChain). Opt-out: DC3_NO_IK_RECOMPOSE.
        if (Dc3FeetPlantFix() && !getenv("DC3_NO_IK_RECOMPOSE")) {
            Dc3RecomposeChain(mFinger);
            Dc3RecomposeChain(mHand);
            if (getenv("DC3_IK_DIAG2")) {
                const char *pn = PathName(this);
                if (pn && std::strstr(pn, "main.milo") && std::strstr(pn, ".ikfoot")) {
                    static int sRC = 0;
                    if (sRC < 40) {
                        sRC++;
                        Vector3 sp = mFinger->WorldXfm().v;
                        Vector3 an = mHand->WorldXfm().v;
                        fprintf(stderr, "DC3_IK_DIAG SpotZ %s spot=(%.2f,%.2f,%.2f) ankle=(%.2f,%.2f,%.2f)\n",
                                std::strstr(pn, "left") ? "L" : "R", sp.x, sp.y, sp.z, an.x, an.y, an.z);
                    }
                }
            }
        }
#endif
#ifdef HX_NATIVE
        // CLEAN PLANT path (DC3_FEET_CLEAN_PLANT): replace the diverging CharIKHand solve with
        // a stateless world-space 2-bone plant on the FK-composed leg (mHand=ankle). Find the
        // actual toe bone (child of the ankle) for the floor check.
        if (Dc3FeetPlantFix() && getenv("DC3_FEET_CLEAN_PLANT")) {
            RndTransformable *toe = 0;
            for (std::list<RndTransformable *>::const_iterator it = mHand->Children().begin();
                 it != mHand->Children().end(); ++it) {
                if (*it && (*it)->Name() && std::strstr((*it)->Name(), "toe")) { toe = *it; break; }
            }
            if (toe) {
                static int sCP = 0;
                bool ok = Dc3CleanPlant(mHand, toe);
                if (getenv("DC3_IK_DIAG2") && sCP < 60) {
                    sCP++;
                    Vector3 tw = toe->WorldXfm().v; Vector3 aw = mHand->WorldXfm().v;
                    fprintf(stderr, "DC3_IK_DIAG CleanPlant seq=%d %s ankleptr=%p ok=%d toe=(%.2f,%.2f,%.2f) ankle=(%.2f,%.2f,%.2f)\n",
                            ++gDc3PollSeq, (mDataIndex == 0) ? "L" : "R", (void *)mHand, ok ? 1 : 0,
                            tw.x, tw.y, tw.z, aw.x, aw.y, aw.z);
                }
            }
        } else {
#endif
        mTargets.clear();
        mTargets.push_back(IKTarget(mFootBone, 0));
        DoFSM(Character::Current(), mFootBone->DirtyLocalXfm());
        CharIKHand::Poll();
        mTargets.clear();
#ifdef HX_NATIVE
        }
#endif
    }
#ifdef HX_NATIVE
    {
        extern void Dc3KneeLog(const char *);
        Dc3KneeLog("CharIKFoot-POST");
    }
#endif
}

void CharIKFoot::DoFSM(Character *mMe, Transform &tf) {
    mFootTransform = mFinger->WorldXfm();
    if (mMe && mMe->Teleported())
        mFootFsmState = 0;
    float deltasecs = TheTaskMgr.DeltaSeconds();
    if (deltasecs < 0.0f)
        deltasecs = 0.0f;
    tf.m = mFinger->WorldXfm().m;
    tf.v.z = mFinger->WorldXfm().v.z;
#ifdef HX_NATIVE
    // EXPERIMENT (DC3_IK_FOOTPLANT=1): clamp the foot IK GOAL Z to the floor.
    // CharIKHand::Poll (called from CharIKFoot::Poll) solves the leg to reach this
    // target and writes the leg bone LOCALs (which SURVIVE render, unlike
    // HamIKEffector's SetWorldXfm). Tests whether a floor-clamped goal re-plants
    // the rendered foot during the dance crouch (the toe-target sinks to -4 with
    // the over-extended leg). Remove before shipping.
    if (Dc3FeetPlantFix() || getenv("DC3_IK_FOOTPLANT")) {
        if (tf.v.z < 0.0f)
            tf.v.z = 0.0f;
    }
    // STABLE STATELESS FOOT PLANT (Dc3FeetPlantFix). The original FSM locks/holds/unlocks
    // mFootPosition based on the footik plant flag (mData->LocalXfm().v[idx] = vecat).
    // On native that data is 0 (the runtime clips lack the analyze_footik bake), so the
    // FSM ran on a forged height heuristic and oscillated -> the leg solver diverged
    // (one foot flung to ankle -25). Replace the FSM with a stateless goal: track the
    // anim foot world pos, but never let it sink below the floor. Lifted foot -> goal
    // follows the anim (IK ~no-op); planted/sinking foot -> goal pins to the floor and
    // CharIKHand::Poll bends the knee to reach it. No state, no feedback, no oscillation.
    if (Dc3FeetPlantFix() && !getenv("DC3_IK_FOOTPLANT_FSM")) {
        const Transform &wt = mFinger->WorldXfm();
        tf.v.x = wt.v.x;
        tf.v.y = wt.v.y;
        // tf.v.z already floor-clamped above; tf.m already = anim foot orientation.
        mFootPosition = tf.v;
        mFootFsmState = 1;
        return;
    }
#endif
    mFootPosition.z = tf.v.z;
    float f10;
    bool b2 = false;
    float vecat = mData->LocalXfm().v[mDataIndex];
    if (!(vecat < 1.0f)) {
        b2 = true;
    } else {
        if (vecat <= 0.0f) {
            ;
        } else {
            if (mFootFsmState == 1) {
                f10 = 0.6f;
            } else {
                f10 = 0.5f;
            }
            if (tf.v.z < f10) {
                b2 = true;
            }
        }
    }
#ifdef HX_NATIVE
    // Native: the foot-plant data (mData/vecat) is not driven, so the FSM never locks and the
    // foot follows the (sometimes un-composed) finger -> sinks/spikes. Substitute height-based
    // ground detection: when the foot is at/near the floor, treat it as planted so the FSM locks
    // mFootPosition (at the floor, via the clamp above) and then HOLDS it (stable, no feedback).
    if (Dc3FeetPlantFix() && tf.v.z < 2.0f)
        b2 = true;
    // Bad-read guard: the native leg FK is intermittently un-composed (the toe-target/finger reads
    // up at the hip, z ~36+) -> an unlocked FSM would follow that wild target and the IK diverges
    // (foot to +/-50). Reject implausible reads: if already planted, hold at the locked floor pos.
    if (Dc3FeetPlantFix() && mFinger->WorldXfm().v.z > 20.0f) {
        b2 = true;
        if (mFootFsmState != 0) {
            tf.v = mFootPosition;
            return;
        }
    }
    if (getenv("DC3_IK_DIAG")) {
        static int sFsmLog = 0;
        const char *pn = PathName(this);
        if (sFsmLog < 40 && pn && strstr(pn, "main.milo")) {
            sFsmLog++;
            const Vector3 &dv = mData ? mData->LocalXfm().v : tf.v;
            fprintf(stderr, "DC3_IK_DIAG DoFSM[%d] %s fsm=%d vecat=%.4f b2=%d dataName=%s dataPtr=%p "
                    "dataLocalV=(%.4f,%.4f,%.4f) idx=%d tf.z=%.2f\n",
                    sFsmLog, pn, mFootFsmState, vecat, b2 ? 1 : 0,
                    mData ? mData->Name() : "null", (void *)mData.Ptr(),
                    dv.x, dv.y, dv.z, mDataIndex, tf.v.z);
        }
    }
#endif
    if (mFootFsmState == 0) {
        const Transform &wt = mFinger->WorldXfm();
        tf.v.x = wt.v.x;
        tf.v.y = wt.v.y;
        if (b2) {
            mFootPosition = tf.v;
            mFootFsmState = 1;
        }
    }
    if (mFootFsmState == 1) {
        if (!b2) {
            mFootFsmState = 2;
            mFootBlendTime = Distance(mFinger->WorldXfm().v, tf.v);
        } else {
            Vector3 v3c;
            Subtract(mFinger->WorldXfm().v, mFootPosition, v3c);
            float len = Length(v3c);
            if (len > 0.125f)
                v3c *= 0.125f / len;
            Add(mFootPosition, v3c, tf.v);
            return;
        }
    }
    if (mFootFsmState == 2) {
        Vector3 delta;
        Subtract(mFinger->WorldXfm().v, mFootPosition, delta);
        float len = Length(delta);
        mFootBlendTime = Min(-(deltasecs * 25.0f - mFootBlendTime), len);
        if (mFootBlendTime <= 0.0f)
            mFootFsmState = 0;
        else
            delta *= (len - mFootBlendTime) / len;
        Add(mFootPosition, delta, tf.v);
        if (b2) {
            mFootPosition = tf.v;
            mFootFsmState = 1;
        }
    }
}
