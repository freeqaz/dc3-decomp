#include "char/CharIKFoot.h"
#include "CharIKHand.h"
#include "char/Character.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "obj/Object.h"
#include "obj/Task.h"
#include "rndobj/Trans.h"
#ifdef HX_NATIVE
#include "hamobj/HamCharacter.h"
#include "hamobj/HamIKEffector.h"
#include "hamobj/HamWardrobe.h"
#include "obj/Dir.h"
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
// Defined in HamDirector.cpp (also uses it); declared extern here to avoid WASM duplicate-symbol error.
extern int gDc3PollSeq;

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

    // INPUT-SANITY GUARD (wave 6): at a move boundary the leg FK is momentarily
    // un-composed — the ankle/toe/knee read UP at the hip (z ~= H.z, the "flying feet"
    // boundary frame). A standing leg hangs DOWN, so the ankle/knee must sit well below
    // the hip. If the leg reads collapsed onto the hip, the FK is stale — DO NOT plant
    // (leave the anim pose); planting off a hip-level read teleports the ankle ~57 units
    // and the next composed frame snaps it back (NoAnkleSuddenJumpsDuringGameplay). The
    // anim toe Z would be hip-high too, so this frame is not a real floor violation.
    if (A.z > H.z - 5.0f || Kc.z > H.z - 2.0f || T.z > H.z - 5.0f)
        return true;                                 // un-composed leg; leave anim pose

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

    // Milo uses the ROW-VECTOR convention: a point transforms as p' = p * M, and
    // world = local * parentWorld (child-first). So a world-space rotation R applied
    // AFTER an existing world matrix W is W*R (NOT R*W), and to set a bone's world
    // rotation to Wnew while keeping its parent, its new LOCAL.m = Wnew * inv(parentW.m).
    // The previous code used R*W / inv(parent)*W (column-vector order) — that rotated in
    // the wrong frame and failed to swing the ankle (ankle stayed at the floor, the
    // residual sink). This corrected order actually lands the ankle at the target.

    // Aim the THIGH so its child (knee) lands at Kp (rotate the thigh about the hip H).
    {
        Vector3 curDir; Normalize(femurV, curDir);
        Vector3 wantV; Subtract(Kp, H, wantV); Vector3 wantDir; Normalize(wantV, wantDir);
        Hmx::Quat q; MakeRotQuat(curDir, wantDir, q);
        Hmx::Matrix3 R; MakeRotMatrix(q, R);
        Hmx::Matrix3 wmNew; Multiply(thigh->WorldXfm().m, R, wmNew);   // Wnew = W * R
        Hmx::Matrix3 hipInv; Invert(hip->WorldXfm().m, hipInv);
        Hmx::Matrix3 lmNew; Multiply(wmNew, hipInv, lmNew);            // local = Wnew * inv(parentW)
        thigh->DirtyLocalXfm().m = lmNew;            // keep local .v (bone offset)
    }
    (void)thigh->WorldXfm();                         // recompose; knee now near Kp
    Vector3 Kw = knee->WorldXfm().v;

    // Aim the KNEE so its child (ankle) lands at Atarget (rotate the knee about Kw).
    {
        // current world shin dir after the thigh moved
        Vector3 ankNow = ankle->WorldXfm().v; Vector3 curNow; Subtract(ankNow, Kw, curNow);
        Vector3 curDir; Normalize(curNow, curDir);
        Vector3 wantV; Subtract(Atarget, Kw, wantV); Vector3 wantDir; Normalize(wantV, wantDir);
        Hmx::Quat q; MakeRotQuat(curDir, wantDir, q);
        Hmx::Matrix3 R; MakeRotMatrix(q, R);
        Hmx::Matrix3 wmNew; Multiply(knee->WorldXfm().m, R, wmNew);    // Wnew = W * R
        Hmx::Matrix3 thInv; Invert(thigh->WorldXfm().m, thInv);
        Hmx::Matrix3 lmNew; Multiply(wmNew, thInv, lmNew);
        knee->DirtyLocalXfm().m = lmNew;
    }
    (void)knee->WorldXfm();

    // Preserve the foot orientation: set ankle LOCAL so its world rotation == original.
    // ankleWorld = ankleLocal * kneeWorld  =>  ankleLocal.m = ankleWorld0.m * inv(kneeWorld.m).
    {
        Hmx::Matrix3 knInv; Invert(knee->WorldXfm().m, knInv);
        Hmx::Matrix3 lmNew; Multiply(ankleW0.m, knInv, lmNew);
        ankle->DirtyLocalXfm().m = lmNew;
    }
    (void)ankle->WorldXfm();

    // Wave-4 Lane A: validate the solve. If it DIVERGED — produced NaN, or pushed
    // the ankle/toe LOWER than the faithful anim pose (the move-boundary fling that
    // sent toe Z to ~-12) — REVERT to the snapshotted anim LOCALs. The plant is then
    // a strict improvement (never worse than baseline) on every frame.
    {
        Vector3 ankle1 = ankle->WorldXfm().v;
        float toeZ1 = toe->WorldXfm().v.z;
        float ankleZ1 = ankle1.z;
        bool nan = !(toeZ1 == toeZ1) || !(ankleZ1 == ankleZ1);
        // Tolerance: allow a tiny dip (float noise) but reject a real regression.
        bool worse = (toeZ1 < toeZ0 - 0.25f) || (ankleZ1 < ankleZ0 - 0.25f);
        // UPWARD-JUMP GUARD (wave 6): a correct plant displaces the ankle by ~deficit
        // (lift the foot just clear of the floor). At move boundaries the leg FK is
        // momentarily un-composed (the ankle/toe reads up at the hip, z~36), so the
        // 2-bone solve aims at a wild target and teleports the ankle ~57 units — the
        // "flying feet" jump (NoAnkleSuddenJumpsDuringGameplay). Bound the world-space
        // ankle displacement to the deficit it was correcting plus a generous slack; a
        // larger move means a bad read -> REVERT to the anim pose.
        Vector3 moved; Subtract(ankle1, A, moved);
        float ankleMove = Length(moved);
        bool teleport = ankleMove > (deficit + 8.0f);
        if (getenv("DC3_PLANT_DIAG")) {
            static int sPD = 0;
            if (sPD < 60 && (toeZ1 < -1.5f || teleport)) { sPD++;
                fprintf(stderr,
                    "DC3_PLANT_DIAG toeZ0=%.2f->toeZ1=%.2f ankleZ0=%.2f->ankleZ1=%.2f "
                    "deficit=%.2f ankleMove=%.2f d=%.2f reach=%.2f H.z=%.2f revert=%d\n",
                    toeZ0, toeZ1, ankleZ0, ankleZ1, deficit, ankleMove, d, reach, H.z,
                    (nan || worse || teleport) ? 1 : 0);
            }
        }
        if (nan || worse || teleport) {
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

// ---------------------------------------------------------------------------
// WAVE 6 LANE A — DETERMINISTIC POST-POLL FOOT PLANT (DC3_FEET_POST_PLANT)
//
// THE MECHANISM (wave-6, frame-matched Xbox-vs-native, supersedes the wave-5
// "inert-IK / mMoveElbow disables the knee bend" framing):
//   * The leg *.ikfoot faithfully loads mMoveElbow=FALSE on BOTH platforms
//     (CharIKHand::Load is 99.6% — native reads exactly the bytes Xbox reads). So
//     IKElbow never runs on EITHER platform: the Xbox knee bend is NOT an IK bend.
//   * With mMoveElbow=false, CharIKHand::Poll's IK only SetWorldXfm's the ankle world
//     (the !shoulderParent || mStretch path) — it does not write the knee local. The
//     knee bone (bone_*-knee.mesh) is a QUAT bone posed by the anim/clip (PoseMeshes).
//   * Frame-matched DC3_KNEE_LOCAL evidence: over a full YMCA run the native knee LOCAL
//     rotZ tracks Xbox (both median ~-40 deg, native reaches -91, Xbox -115). The bend
//     is NOT globally missing. The divergence is LOCALIZED to the deep-crouch beats:
//       pelvis band 33-35 ->  native knee -32 / ankle +12 ; Xbox knee -57 / ankle +35
//     i.e. at the crouch the native knee+ankle anim/clip QUAT UNDER-bends by ~25 deg,
//     so the ankle drops from ~4 to ~0 and the rigid foot's toe (a fixed -4 local
//     offset below the ankle) sinks to -4. The residual is a CLIP/anim-layer pose
//     under-bend at the crouch, not an IK or poll-order race (cf the corrected session
//     notes; the wave-5 "mMoveElbow=true force" lever DIVERGES and is refuted).
//
// THE FIX (this hook): rather than chase the clip-layer under-bend (engine clip/blend
// territory), assert the Xbox-correct RESULT — a planted foot — as the dancer's genuine
// LAST world write. Runs from App.cpp AFTER TheTaskMgr.Poll() completes (all dancers'
// servo/facing passes + the final pelvis crouch are done) and BEFORE telemetry Sample +
// Draw, so WorldXfm() reflects the fully-composed final-root leg and nothing overwrites
// it. Order-independent by construction (no within-frame poll-order race — the wave-5
// blocker). Every prior in-graph approach (HamDirector re-run, char-poll [28] plant,
// cached re-apply) was defeated by a later root-crouch overwriting the plant; this hook
// is strictly after that.
//
// DEFAULT ON (opt-out: DC3_FEET_POST_PLANT_OFF=1). Deterministic, strict-improvement:
// the analytic 2-bone clean plant lifts ONLY a foot whose toe is below the floor margin
// and leaves anim-lifted feet untouched (toe range over a full YMCA run is [0.60, 9.60]
// — no fly-up mirage), preserves the foot's anim orientation, and reverts on any 2-bone
// divergence. Gate FeetNotBelowFloorDuringGameplay passes 0/~790 below floor across
// runs (worst toe exactly +0.60 = the margin, deterministic; baseline opt-out is -4.30
// with ~750/777 below). All foot/bone/clip/IK unit tests + the gameplay boot stay green.
// KNOWN PRE-EXISTING (NOT caused by this hook, out of lane scope, reported not fixed):
// GameplayTelemetryTest.NoAnkleSuddenJumpsDuringGameplay fails identically with the hook
// ON and OFF (a ~57u ankle delta at the frame-~2010 move-rewind boundary, an animation-
// transition artifact). The PPC build never sees any of this (HX_NATIVE) so the matched
// bytes are unchanged (CharIKFoot::Poll stays 100% normalized, DoFSM 97.4% floor).
bool Dc3FeetPostPlant() {
    static int v = -1;
    if (v < 0) {
        // Under the (now default) producer-first poll order the game's own
        // effector stack plants the feet Xbox-exactly (toe med 0.10 vs the
        // clamp's fixed +0.60) — the clamp would only degrade it. It remains
        // the fallback whenever the order fix is opted out, or force it back
        // with DC3_FEET_POST_PLANT=1.
        extern bool Dc3PollOrderFixActive();
        if (getenv("DC3_FEET_POST_PLANT_OFF"))
            v = 0;
        else if (getenv("DC3_FEET_POST_PLANT"))
            v = 1;
        else
            v = Dc3PollOrderFixActive() ? 0 : 1;
    }
    return v != 0;
}

// POST-POLL PELVIS RETARGET (feet-in-floor ship path, 2026-07-02). The dancer
// rides ~6-7 units too low because the pelvis-height retarget computed by the
// matched HamIKEffector pelvis effector is stomped same-frame by the servo's
// PoseMeshes (reversed native poll order; the effector's SetWorldXfm is never
// backed into the local). Re-apply the exact matched lift here, post-poll,
// as a durable pelvis LOCAL write BEFORE the 2-bone plant, so the plant solves
// the feet from the Xbox-correct pelvis height. Math + durability rationale:
// HamIKEffector::Dc3PostPollPelvisRetarget and the 2026-07-02 session doc.
// ON whenever the post-plant hook is on; kill separately: DC3_FEET_PELVIS_OFF=1.
// Also skipped whenever the producer-first poll order is active (the default):
// there the in-graph effector lift already survives — retargeting again would
// double-lift.
bool Dc3FeetPelvisRetarget() {
    static int v = -1;
    if (v < 0) {
        extern bool Dc3PollOrderFixActive();
        v = (getenv("DC3_FEET_PELVIS_OFF") || Dc3PollOrderFixActive()) ? 0 : 1;
    }
    return v != 0;
}

// Run the stateless clean plant on this foot's FK-composed leg (mHand=ankle). Public
// entry for the post-poll hook. Finds the toe bone (ankle child) for the floor check.
void CharIKFoot::Dc3PostPollPlant() {
    if (!mHand)
        return;
    RndTransformable *toe = 0;
    for (std::list<RndTransformable *>::const_iterator it = mHand->Children().begin();
         it != mHand->Children().end(); ++it) {
        if (*it && (*it)->Name() && std::strstr((*it)->Name(), "toe")) { toe = *it; break; }
    }
    if (toe)
        Dc3CleanPlant(mHand, toe);
}

// Free dispatch: iterate all 6 dancers, plant both feet. Called from the main loop
// after the world poll. TheHamWardrobe is the dancer registry the gate reads from.
void Dc3RunPostPollFootPlant() {
    if (!Dc3FeetPostPlant() || !TheHamWardrobe)
        return;
    // The clean plant marks bones in the frame-keyed guard; tick it so the guard set
    // is fresh for this frame (no stale guards leaking across frames). The guard only
    // matters if a later PoseMeshes runs — none does after this hook — so it is inert
    // here, but keep the bookkeeping consistent with the in-graph path.
    Dc3PlantGuardTick();
    static const char *kIKNames[2] = { "left.ikfoot", "right.ikfoot" };
    for (int d = 0; d < 6; d++) {
        HamCharacter *dancer = (d < 2) ? TheHamWardrobe->GetCharacter(d)
                                       : TheHamWardrobe->GetBackup(d - 2);
        if (!dancer)
            continue;
        // Pelvis retarget FIRST (lifts the whole body to the Xbox height and
        // dirties the leg subtree), then the 2-bone plant re-plants the feet
        // from the lifted pelvis. One pelvis effector per dancer: stop at the
        // first one found (Dc3PostPollPelvisRetarget self-filters by type).
        if (Dc3FeetPelvisRetarget()) {
            for (ObjDirItr<HamIKEffector> eff(dancer, true); eff != nullptr; ++eff) {
                if (eff->Dc3PostPollPelvisRetarget(d))
                    break;
            }
        }
        for (int k = 0; k < 2; k++) {
            CharIKFoot *ik = dancer->Find<CharIKFoot>(kIKNames[k], false);
            if (ik)
                ik->Dc3PostPollPlant();
        }
    }
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
    // WAVE 6 LANE A DIAG (DC3_IK_LOADDIAG): print the AS-LOADED CharIKHand flags for
    // every .ikfoot before any force, to settle the central question — does the .milo
    // actually carry mMoveElbow=false (a knee-bend-disabling value) on the leg foot IK?
    if (getenv("DC3_IK_LOADDIAG")) {
        fprintf(stderr,
            "DC3_IK_LOADDIAG ikfoot '%s' moveElbow=%d alwaysIKElbow=%d orientation=%d "
            "stretch=%d pullShoulder=%d dataIndex=%d hand=%s finger=%s\n",
            Name() ? Name() : "?", (int)mMoveElbow, (int)mAlwaysIKElbow,
            (int)mOrientation, (int)mStretch, (int)mPullShoulder, mDataIndex,
            mHand ? (mHand->Name() ? mHand->Name() : "?") : "null",
            mFinger ? (mFinger->Name() ? mFinger->Name() : "?") : "null");
    }
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
