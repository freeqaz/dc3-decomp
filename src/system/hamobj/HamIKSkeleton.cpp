#include "hamobj/HamIKSkeleton.h"
#include "hamobj/HamCharacter.h"
#include "math/Mtx.h"
#include "obj/Dir.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "rndobj/Trans.h"

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
            neutralSkelTrans->SetWorldXfm(charTrans->WorldXfm());
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
    if (!t2) return;
    if (t2->Dirty()) {
        if (!t1) {
            MILO_NOTIFY_ONCE("%s bone is NULL, neutral is %s", PathName(this), t2->Name());
        } else {
            SetBone(t1->TransParent(), t2->TransParent());
            t2->SetLocalRot(t1->LocalXfm().m);
            t2->WorldXfm();
        }
    }
}
