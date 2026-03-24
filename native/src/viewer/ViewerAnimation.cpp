#include "viewer/ViewerAnimation.h"
#include "viewer/ViewerArgs.h"

#include "obj/Dir.h"
#include "rndobj/TransAnim.h"
#include "rndobj/PropAnim.h"
#include "rndobj/Group.h"
#include "char/Character.h"
#include "char/CharClip.h"
#include "char/CharServoBone.h"
#include "char/CharFaceServo.h"
#include "char/CharEyes.h"
#include "char/CharLipSyncDriver.h"
#include "char/CharPollable.h"
#include "char/CharBonesMeshes.h"
#include "char/CharBone.h"
#include "math/Rot.h"
#include "math/Vec.h"
#include "rndobj/Trans.h"
#include "obj/Task.h"

#include <cstdio>
#include <cmath>

// ============================================================================
// BlinkState
// ============================================================================

void BlinkState::Advance(float dt) {
    if (phase > 0.0f) {
        phase -= dt;
        if (phase <= 0.0f) {
            phase = 0.0f;
            timer = 2.0f + (rand() % 400) * 0.01f;
        }
    } else {
        timer -= dt;
        if (timer <= 0.0f) {
            phase = kDuration;
        }
    }
}

float BlinkState::Weight() const {
    if (phase <= 0.0f) return 0.0f;
    float half = kDuration * 0.5f;
    float w = (phase < half) ? phase / half : (kDuration - phase) / half;
    if (w < 0.0f) w = 0.0f;
    if (w > 1.0f) w = 1.0f;
    return w;
}

// ============================================================================
// AnimState
// ============================================================================

AnimState gAnim;

void AnimState::ScanScene(ObjectDir* dir, const ViewerConfig& cfg) {
    int transAnimCount = 0, propAnimCount = 0, otherAnimCount = 0;
    float globalStart = 1e10f, globalEnd = -1e10f;

    // Use non-recursive iteration to avoid traversing into potentially
    // corrupted subdirs after FileMerger merge
    ObjDirItr<RndAnimatable> animIt(dir, false);
    while (animIt) {
        RndAnimatable* anim = animIt;

        // Skip container animatables (RndDir, RndGroup) — their EndFrame()
        // recurses into children causing infinite loops between Dir<->Group
        if (dynamic_cast<ObjectDir*>((Hmx::Object*)anim) ||
            dynamic_cast<RndGroup*>((Hmx::Object*)anim)) {
            ++animIt;
            continue;
        }

        float sf = anim->StartFrame();
        float ef = anim->EndFrame();

        if (ef > sf) {
            if (sf < globalStart) globalStart = sf;
            if (ef > globalEnd) globalEnd = ef;
            animatables.push_back(anim);

            if (cfg.verbose) {
                const char* cn = anim->ClassName().Str();
                printf("  anim '%s' class='%s' frames=[%.1f, %.1f]\n",
                       anim->Name(), cn, sf, ef);
            }
        }

        if (dynamic_cast<RndTransAnim*>(anim)) transAnimCount++;
        else if (dynamic_cast<RndPropAnim*>(anim)) propAnimCount++;
        else otherAnimCount++;

        ++animIt;
    }

    animCount = (int)animatables.size();
    if (globalEnd > globalStart) {
        hasAnimation = true;
        startFrame = globalStart;
        endFrame = globalEnd;
        currentFrame = (cfg.startFrame >= 0.0f) ? cfg.startFrame : globalStart;
        speed = cfg.animSpeed;
        paused = cfg.startPaused;

        printf("Milo Viewer: %d animatables with keyframes (range [%.1f, %.1f] = %.1f frames)\n",
               animCount, startFrame, endFrame, endFrame - startFrame);
        printf("  TransAnim: %d, PropAnim: %d, other: %d\n",
               transAnimCount, propAnimCount, otherAnimCount);
        if (paused) printf("  Starting paused\n");
        if (speed != 1.0f) printf("  Speed: %.2fx\n", speed);
    } else {
        printf("Milo Viewer: no animation data found (%d TransAnim, %d PropAnim, %d other — all empty)\n",
               transAnimCount, propAnimCount, otherAnimCount);
    }
}

// ============================================================================
// CharAnimState
// ============================================================================

void CharAnimState::CollectPollables() {
    if (!character || !pollables.empty()) return;
    ObjDirItr<CharPollable> it(character, true);
    for (; it != nullptr; ++it) {
        pollables.push_back(it);
        printf("Milo Viewer: found CharPollable '%s' (%s)\n",
               it->Name(), it->ClassName());
    }
}

void CharAnimState::PollFace() {
    if (!faceServo) return;
    if (lipDriver) {
        lipDriver->Poll();
    }
    if (eyes) {
        // Bridge BlinkState -> CharEyes: trigger periodic blinks when no
        // interest objects are driving natural gaze-shift blinks
        if (blink.Weight() > 0.0f) {
            eyes->ForceBlink();
        }
    } else {
        faceServo->SetProceduralBlinkWeight(blink.Weight());
    }
    faceServo->ApplyProceduralWeights();
    faceServo->Poll();
}

void CharAnimState::ResetPlaybackClock() {
    startBeat = clip ? clip->StartBeat() : 0.0f;
    lastBeat = startBeat;
    lastSeconds = 0.0f;
    TheTaskMgr.SetSecondsAndBeat(lastSeconds, lastBeat, true);
}

float CharAnimState::BeatForSeconds(float seconds, float bpm) const {
    return startBeat + seconds * (bpm / 60.0f);
}

float CharAnimState::SecondsForBeat(float beat, float bpm) const {
    return (beat - startBeat) * 60.0f / bpm;
}

void CharAnimState::AdvanceBeat(float targetSeconds, float targetBeat, float bpm) {
    if (!active || !character || !character->Driver()) return;

    // Use the engine's native poll path: Character::Poll() -> RndDir::Poll()
    // -> CharPollGroup::Poll() which iterates all CharPollable objects in
    // dependency-sorted order (via CharPollableSorter / PollDeps).
    // This matches the game engine's exact rendering path for bones:
    //   CharDriver -> CharServoBone -> CharIKHand -> CharUpperTwist -> CharForeTwist -> etc.
    float stepBeats = 0.1f;
    float stepSeconds = stepBeats * 60.0f / bpm;
    while (lastBeat + stepBeats < targetBeat) {
        lastBeat += stepBeats;
        lastSeconds += stepSeconds;
        TheTaskMgr.SetSecondsAndBeat(lastSeconds, lastBeat, false);
        character->Poll();
    }
    lastBeat = targetBeat;
    lastSeconds = targetSeconds;
    TheTaskMgr.SetSecondsAndBeat(targetSeconds, targetBeat, false);
    character->Poll();
    PollFace();
}

void CharAnimState::DirectPose(float beat, float bpm) {
    if (!active || !clip || !character) return;

    // Apply clip at exact beat with facing/root motion correction,
    // then run all pollables via engine's dependency-sorted order.
    PoseMeshesWithFacing(clip, character, beat);
    character->Poll();
    PollFace();
}

// ============================================================================
// PoseMeshesWithFacing
// ============================================================================

static CharBonesMeshes* sCachedFacingBones = nullptr;
static CharClip*        sCachedFacingClip  = nullptr;
static Character*       sCachedFacingChr   = nullptr;

void ResetFacingCache() {
    delete sCachedFacingBones;
    sCachedFacingBones = nullptr;
    sCachedFacingClip  = nullptr;
    sCachedFacingChr   = nullptr;
}

void PoseMeshesWithFacing(CharClip* clip, Character* chr, float beat) {
    if (!clip || !chr) return;

    if (!sCachedFacingBones || sCachedFacingClip != clip || sCachedFacingChr != chr) {
        delete sCachedFacingBones;
        sCachedFacingBones = new CharBonesMeshes();
        sCachedFacingBones->SetName("tmp_facing_bones", chr);
        clip->StuffBones(*sCachedFacingBones);
        sCachedFacingClip = clip;
        sCachedFacingChr  = chr;
    }

    CharBonesMeshes& meshes = *sCachedFacingBones;
    clip->ScaleDown(meshes, 0.0f);
    clip->ScaleAdd(meshes, 1.0f, beat, 0.0f);
    meshes.PoseMeshes();

    Vector3* facingPos = (Vector3*)meshes.FindPtr("bone_facing.pos");
    float*   facingRot = (float*)meshes.FindPtr("bone_facing.rotz");

    if (facingPos) {
        RndTransformable* pelvis = chr->Find<RndTransformable>("bone_pelvis.mesh", false);
        if (pelvis) {
            Transform tf = pelvis->LocalXfm();
            if (facingRot) {
                RotateAboutZ(tf.m, *facingRot, tf.m);
                RotateAboutZ(tf.v, *facingRot, tf.v);
                Normalize(tf.m, tf.m);
            }
            tf.v += *facingPos;
            pelvis->SetLocalXfm(tf);
        }
    }

    {
        Transform rootXfm = chr->LocalXfm();
        rootXfm.v.z = 0.0f;
        chr->SetLocalXfm(rootXfm);
    }

    static const char* footBones[] = {
        "bone_L-toe.mesh", "bone_R-toe.mesh",
        "bone_L-ankle.mesh", "bone_R-ankle.mesh",
    };
    float lowestZ = 1e6f;
    for (const char* name : footBones) {
        RndTransformable* bone = chr->Find<RndTransformable>(name, false);
        if (bone) {
            float z = bone->WorldXfm().v.z;
            if (z < lowestZ) lowestZ = z;
        }
    }
    if (lowestZ < 1e5f && lowestZ != 0.0f) {
        Transform rootXfm = chr->LocalXfm();
        rootXfm.v.z = -lowestZ;
        chr->SetLocalXfm(rootXfm);
    }
}
