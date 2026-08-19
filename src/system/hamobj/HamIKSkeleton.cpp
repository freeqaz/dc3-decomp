#include "hamobj\HamIKSkeleton.h"
#include "hamobj\HamCharacter.h"
#include "math\Mtx.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Trans.h"

HamIKSkeleton::HamIKSkeleton() : mNeutralSkelDir(nullptr), mChar(this) {}

BEGIN_HANDLERS(HamIKSkeleton)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(HamIKSkeleton)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(HamIKSkeleton)
    SAVE_REVS(0, 0)
    SAVE_SUPERCLASS(Hmx::Object)
END_SAVES

BEGIN_COPYS(HamIKSkeleton)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(HamIKSkeleton)
END_COPYS

INIT_REVS(0, 0)

BEGIN_LOADS(HamIKSkeleton)
    LOAD_REVS(bs)
    ASSERT_REVS(0, 0)
    LOAD_SUPERCLASS(Hmx::Object)
END_LOADS

void HamIKSkeleton::Poll() {
    if (mChar) {
#ifdef HX_NATIVE
        // Guard: character skeleton may not be ready (no outfit loaded yet).
        // GetNeutralSkeleton does raw pointer casts that are unsafe when
        // skeleton data isn't fully initialized.
        // NOTE: bones live in the character's "skeleton" subdir, so the lookup
        // MUST be recursive (true) — the Xbox/#else path uses recursive Find.
        // A non-recursive (false) lookup always fails during gameplay, which
        // early-returns here and leaves mNeutralSkelDir null. NeutralWorldXfm
        // then collapses the neutral pose onto the live (dropped) pose, so the
        // IK ankle clamp loses its anchor and the feet sink through the floor.
        if (!mChar->Find<RndTransformable>("bone_pelvis.mesh", true))
            return;
        mNeutralSkelDir = mChar->GetNeutralSkeleton();
        if (!mNeutralSkelDir)
            return;
        RndTransformable *charTrans =
            mChar->Find<RndTransformable>("bone_pelvis.mesh", true);
        RndTransformable *neutralSkelTrans =
            mNeutralSkelDir->Find<RndTransformable>("bone_pelvis.mesh", true);
        if (charTrans && neutralSkelTrans) {
            // FEET-IN-FLOOR FIX (HX_NATIVE only; Wii/Xbox match path is the #else
            // below, byte-untouched).
            //
            // The matched code stamps the LIVE char pelvis world onto the neutral
            // skeleton root. On Xbox the per-character IK runs character-local, so
            // that world is small/origin-rooted (clamp-cave ground truth: neutral
            // toe Z ~= 0.017, planted). On native the live pelvis world is
            // venue-placed (X ~= +/-37) AND crouch-dropped (Z ~= 35 vs REST ~42),
            // so stamping it collapses the neutral (planting) pose onto the live
            // (sunk) pose: the matched ankle clamp (Interp returns `neutral` when
            // clampFactor ~= 0) loses its floor anchor and q.v = neutral + eff
            // doubles -> the ankle world explodes -> render-time WorldXfm_Force
            // discards it -> the foot sinks ~3.3u below the floor.
            //
            // FIX: leave the neutral skeleton at its origin-rooted REST world.
            // GetNeutralSkeleton() re-derives the neutral LOCAL pose from the
            // skeleton clips every frame, so the neutral pelvis stays at REST hip
            // height (~42) with REST bone lengths -> the neutral toe lands at the
            // floor (~0), the small origin-rooted correction the matched IK needs
            // (the iconman case). The back-transform re-introduces the venue
            // placement via `eff`, so the foot still plants UNDER the dancer, at
            // REST (floor) height. This replicates the Xbox character-local flow
            // surgically, without touching the matched HamIKEffector::Poll /
            // SetBone / NeutralWorldXfm math.
            //
            // DIAGNOSTIC (opt-in DC3_IK_NEUTRAL=local): origin-root the neutral
            // X/Y so the matched ankle-clamp expression q.v = neutral + eff stops
            // DOUBLING (venue X 27+27=54 -> back-transform explodes the ankle world
            // to ~150-220). This makes the (otherwise garbage) IK computation sane
            // and character-local, matching Xbox. BUT it has NO effect on the
            // rendered foot: the foot follows the ANIMATION pose, not the IK output
            // (SetWorldXfm never writes the local, so the pelvis-last re-dirty
            // recomputes the ankle from the anim local and discards every IK write
            // — proven 2026-06-08, see the session doc). Default = the original
            // matched stamp, so default native behavior is unchanged.
            Transform t = charTrans->WorldXfm();
            const char *neutralMode = getenv("DC3_IK_NEUTRAL");
            if (neutralMode && streq(neutralMode, "local")) {
                t.v.x = 0.0f;
                t.v.y = 0.0f;
            }
            neutralSkelTrans->SetWorldXfm(t);
        }
#else
        mNeutralSkelDir = mChar->GetNeutralSkeleton();
        RndTransformable *charTrans =
            mChar->Find<RndTransformable>("bone_pelvis.mesh", true);
        RndTransformable *neutralSkelTrans =
            mNeutralSkelDir->Find<RndTransformable>("bone_pelvis.mesh", true);
        neutralSkelTrans->SetWorldXfm(charTrans->WorldXfm());
#endif
    }
}

void HamIKSkeleton::PollDeps(
    std::list<Hmx::Object *> &changedBy, std::list<Hmx::Object *> &change
) {
    changedBy.push_back(mChar->Find<RndTransformable>("bone_pelvis.mesh", false));
}

void HamIKSkeleton::SetName(const char *name, ObjectDir *dir) {
    Hmx::Object::SetName(name, dir);
    mChar = dynamic_cast<HamCharacter *>(dir);
}

void HamIKSkeleton::NeutralLocalPos(RndTransformable *t, Vector3 &v) {
    if (mNeutralSkelDir) {
        if (!streq(t->Name(), "bone_pelvis.mesh")) {
            t = mNeutralSkelDir->Find<RndTransformable>(t->Name(), true);
        }
    }
    v = t->LocalXfm().v;
}

void HamIKSkeleton::NeutralWorldXfm(RndTransformable *t, Transform &xfm) {
    ObjectDir *skelDir = mNeutralSkelDir;
    HamCharacter *charPtr = mChar;
    if (skelDir && skelDir != (ObjectDir *)charPtr) {
        RndTransformable *charTrans = skelDir->Find<RndTransformable>(t->Name(), false);
#ifdef HX_NATIVE
        {
            // Diagnose the neutral-anchor collapse: if the non-recursive Find
            // in the neutral dir misses (bones live in a subdir on native),
            // charTrans is null, t stays = the live (sunk) finger, and the
            // neutral xfm collapses onto the live pose -> the clamp loses its
            // planting anchor.
            static int sNwxLog = 0;
            const char *p = PathName(this);
            bool isMain = p && !strstr(p, "backup");
            // Capture the VENUE-PLACED player (live X large), not iconman.
            if (sNwxLog < 8 && isMain && t && strstr(t->Name(), "toe")
                && fabsf(t->WorldXfm().v.x) > 20.0f) {
                sNwxLog++;
                // recFind reads the neutral bone's REST world Z (before SetBone
                // copies the live local rotations onto it). nonRecFind==recFind
                // proves the non-recursive Find resolves (no subdir miss). The
                // gap liveWorldZ vs neutralRecWorldZ is the un-IK'd planting
                // anchor SetBone then collapses onto the live (sunk) pose.
                RndTransformable *rec =
                    skelDir->Find<RndTransformable>(t->Name(), true);
                fprintf(stderr,
                    "DC3_IK_DIAG NeutralWXfm[%d]: self=%s skel=%s bone=%s "
                    "nonRecFind=%p recFind=%p liveWorldV=(%.2f,%.2f,%.2f) "
                    "neutralRestWorldV=(%.2f,%.2f,%.2f)\n",
                    sNwxLog, p, skelDir->Name(), t->Name(),
                    (void*)charTrans, (void*)rec,
                    t->WorldXfm().v.x, t->WorldXfm().v.y, t->WorldXfm().v.z,
                    rec ? rec->WorldXfm().v.x : -999.0f,
                    rec ? rec->WorldXfm().v.y : -999.0f,
                    rec ? rec->WorldXfm().v.z : -999.0f);
                // Walk the NEUTRAL bone's parent chain to find where the venue
                // placement (X~54) leaks into the neutral skeleton frame.
                if (rec) {
                    fprintf(stderr, "  NEUTRALCHAIN:");
                    RndTransformable *nc = rec;
                    for (int d2 = 0; nc && d2 < 12; d2++) {
                        const Transform &nw = nc->WorldXfm();
                        const Transform &nl = nc->LocalXfm();
                        fprintf(stderr, " [%s W=(%.2f,%.2f,%.2f) L=(%.2f,%.2f,%.2f)]",
                            nc->Name() ? nc->Name() : "?",
                            nw.v.x, nw.v.y, nw.v.z, nl.v.x, nl.v.y, nl.v.z);
                        nc = nc->TransParent();
                    }
                    fprintf(stderr, "\n");
                }
            }
        }
#endif
        if (charTrans) {
            SetBone(t, charTrans);
            t = charTrans;
        }
    }
    xfm = t->WorldXfm();
}

void HamIKSkeleton::SetBone(RndTransformable *t1, RndTransformable *t2) {
#ifdef HX_NATIVE
    // Native-only crash guard, added by 5d19777db (venue rendering) and left
    // unguarded, which cost 6 instructions of match. The Xbox build has no such
    // test: the target goes straight to `lbz r11, 0xbd(r5)`, so t2 is always
    // dereferenced. Keep the guard only where the bone graph can be incomplete.
    if (!t2)
        return;
#endif
    if (t2->Dirty()) {
        if (!t1) {
            MILO_NOTIFY_ONCE("%s bone is NULL, neutral is %s", PathName(this), t2->Name());
        } else {
            SetBone(t1->TransParent(), t2->TransParent());
            const Hmx::Matrix3 &rot = t1->LocalXfm().m;
            t2->SetLocalRot(rot);
            t2->WorldXfm();
        }
    }
}
