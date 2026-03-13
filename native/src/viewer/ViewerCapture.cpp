#include "viewer/ViewerCapture.h"
#include "viewer/ViewerArgs.h"
#include "viewer/ViewerScene.h"
#include "viewer/ViewerCamera.h"
#include "viewer/ViewerAnimation.h"
#include "viewer/ViewerPoseDump.h"

#include "char/CharTwistSolver.h"
#include "char/Character.h"
#include "char/CharClip.h"
#include "char/CharServoBone.h"
#include "char/CharBone.h"
#include "char/CharUtl.h"
#include "rndobj/Cam.h"
#include "rndobj/Trans.h"
#include "rndobj/Rnd.h"
#include "platform/Rnd_Wgpu.h"
#include "gfx/Screenshot.h"
#include "gfx/VideoEncoder.h"
#include "math/Vec.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

extern Rnd& TheRnd;
extern OrbitCamera gOrbitCam;

// ============================================================================
// Shared: smooth pelvis tracking
// ============================================================================

static void TrackPelvis(RndTransformable* pelvis, float& smoothX, float& smoothY,
                        float smoothFactor, bool hasEye) {
    if (!pelvis || hasEye) return;
    const Transform& bxfm = pelvis->WorldXfm();
    smoothX += (bxfm.v.x - smoothX) * smoothFactor;
    smoothY += (bxfm.v.y - smoothY) * smoothFactor;
    gOrbitCam.targetX = smoothX;
    gOrbitCam.targetY = smoothY;
}

// ============================================================================
// Mode selection
// ============================================================================

ViewerMode SelectMode(const ViewerConfig& cfg) {
    if (cfg.screenshotPath) {
        ScreenshotMode m;
        m.poseDumpBones = ParseCommaSeparatedList(cfg.poseDumpBonesCsv);
        if (cfg.maxFrames > 0) {
            m.warmupFrames = cfg.maxFrames;
        }
        return m;
    }
    if (cfg.videoPath) {
        VideoMode m;
        m.totalFrames = cfg.maxFrames > 0 ? cfg.maxFrames : (int)(cfg.videoDuration * cfg.videoFps);
        m.dt          = 1.0f / (float)cfg.videoFps;
        return m;
    }
    return InteractiveMode{};
}

// ============================================================================
// RunScreenshot
// ============================================================================

int RunScreenshot(ScreenshotMode& m, ViewerScene& scene,
                  AnimState& anim, CharAnimState& charAnim,
                  RndCam* cam, const ViewerConfig& cfg,
                  const char* absPath) {
    printf("Milo Viewer: rendering frames for screenshot...\n");
    ObjectDir* baseScene = scene.baseScene;

    float poseDumpBeatResolved = 0.0f;
    bool  poseDumpBeatSet      = false;

    // Apply animation frame if specified
    if (anim.hasAnimation && cfg.startFrame >= 0.0f) {
        printf("Milo Viewer: setting animation to frame %.1f\n", cfg.startFrame);
        for (auto* a : anim.animatables) {
            a->SetFrame(cfg.startFrame, 1.0f);
        }
    }

    // Test bone: manually rotate a specific bone from T-pose
    if (cfg.testBoneName && baseScene) {
        RndTransformable* bone = baseScene->Find<RndTransformable>(cfg.testBoneName, true);
        if (bone) {
            float rad = cfg.testBoneAngle * (3.14159265f / 180.0f);
            float c = cosf(rad), s = sinf(rad);
            Transform& tf = bone->DirtyLocalXfm();
            Hmx::Matrix3 rot;
            rot.Zero();
            if (strcmp(cfg.testBoneAxis, "x") == 0) {
                rot.x.x = 1; rot.y.y = c; rot.y.z = s; rot.z.y = -s; rot.z.z = c;
            } else if (strcmp(cfg.testBoneAxis, "y") == 0) {
                rot.x.x = c; rot.x.z = -s; rot.y.y = 1; rot.z.x = s; rot.z.z = c;
            } else {
                rot.x.x = c; rot.x.y = -s; rot.y.x = s; rot.y.y = c; rot.z.z = 1;
            }
            Hmx::Matrix3 oldm = tf.m;
            Multiply(rot, oldm, tf.m);
            printf("Milo Viewer: test-bone '%s' rotated %.1f deg around %s\n",
                   cfg.testBoneName, cfg.testBoneAngle, cfg.testBoneAxis);
        } else {
            printf("Milo Viewer: WARNING: bone '%s' not found\n", cfg.testBoneName);
        }
    }

    // Advance character animation to a target pose
    if (charAnim.active) {
        float beatSelector = cfg.startFrame;
        if (cfg.poseDumpBeatArg && cfg.poseDumpBeatArg[0]) {
            if (strcmp(cfg.poseDumpBeatArg, "START") == 0 || strcmp(cfg.poseDumpBeatArg, "start") == 0) {
                beatSelector = -1.0f;
            } else if (strcmp(cfg.poseDumpBeatArg, "MID") == 0 || strcmp(cfg.poseDumpBeatArg, "mid") == 0) {
                beatSelector = -2.0f;
            } else {
                beatSelector = (float)atof(cfg.poseDumpBeatArg);
            }
            printf("Milo Viewer: pose-dump beat selector '%s' -> %.2f\n", cfg.poseDumpBeatArg, beatSelector);
        }

        float beat;
        if (beatSelector >= 0.0f) {
            beat = beatSelector;
        } else if (beatSelector <= -1.5f && charAnim.clip) {
            beat = (charAnim.clip->StartBeat() + charAnim.clip->EndBeat()) * 0.5f;
        } else if (charAnim.clip) {
            beat = charAnim.clip->StartBeat();
        } else {
            beat = 4.0f;
        }
        printf("Milo Viewer: advancing animation to beat %.1f (seconds=%.2f)\n", beat, beat * 60.0f / cfg.bpm);
        poseDumpBeatResolved = beat;
        poseDumpBeatSet = true;

        if (charAnim.clip && cfg.directPose) {
            printf("Milo Viewer: using CharClip::PoseMeshes(dir, %.1f)\n", beat);
            charAnim.DirectPose(beat, cfg.bpm);
        } else {
            charAnim.AdvanceBeat(beat * 60.0f / cfg.bpm, beat, cfg.bpm);
        }

        // Dump raw bone buffer values
        if (cfg.dumpBones && charAnim.servo) {
            float dumpBeat = (cfg.startFrame >= 0.0f) ? cfg.startFrame : 4.0f;
            printf("=== RAW BONE BUFFER DUMP (beat %.1f) ===\n", dumpBeat);

            auto bones = charAnim.servo->GetBones();
            char* start = charAnim.servo->GetStart();
            int posEnd   = charAnim.servo->GetOffset(CharBones::TYPE_SCALE);
            int scaleEnd = charAnim.servo->GetOffset(CharBones::TYPE_QUAT);
            int quatEnd  = charAnim.servo->GetOffset(CharBones::TYPE_ROTX);
            int rotxEnd  = charAnim.servo->GetOffset(CharBones::TYPE_ROTY);
            int rotyEnd  = charAnim.servo->GetOffset(CharBones::TYPE_ROTZ);
            int rotzEnd  = charAnim.servo->GetOffset(CharBones::TYPE_END);

            printf("  Buffer layout: POS[0..%d] SCALE[%d..%d] QUAT[%d..%d] ROTX[%d..%d] ROTY[%d..%d] ROTZ[%d..%d]\n",
                   posEnd, posEnd, scaleEnd, scaleEnd, quatEnd, quatEnd, rotxEnd, rotxEnd, rotyEnd, rotyEnd, rotzEnd);
            printf("  Bone count: %d\n", (int)bones.size());

            printf("\n  --- POSITIONS ---\n");
            Vector3* posData = (Vector3*)start;
            int numPos = posEnd / (int)sizeof(Vector3);
            for (int i = 0; i < numPos && i < (int)bones.size(); i++) {
                RndTransformable* mesh = CharUtlFindBoneTrans(bones[i].name.Str(), charAnim.character);
                printf("  [%2d] %-35s buf=(%8.3f,%8.3f,%8.3f) w=%.3f",
                       i, bones[i].name.Str(), posData[i].x, posData[i].y, posData[i].z, bones[i].weight);
                if (mesh) {
                    printf("  mesh='%s' local.v=(%8.3f,%8.3f,%8.3f)",
                           mesh->Name(), mesh->LocalXfm().v.x, mesh->LocalXfm().v.y, mesh->LocalXfm().v.z);
                } else {
                    printf("  ** NO MESH **");
                }
                printf("\n");
            }

            printf("\n  --- QUATERNIONS ---\n");
            Hmx::Quat* quatData = (Hmx::Quat*)(start + scaleEnd);
            int numQuat = (quatEnd - scaleEnd) / (int)sizeof(Hmx::Quat);
            int quatBoneIdx = posEnd / (int)sizeof(Vector3);
            for (int i = 0; i < numQuat; i++) {
                int bi = quatBoneIdx + i;
                if (bi >= (int)bones.size()) break;
                RndTransformable* mesh = CharUtlFindBoneTrans(bones[bi].name.Str(), charAnim.character);
                printf("  [%2d] %-35s quat=(%7.4f,%7.4f,%7.4f,%7.4f) w=%.3f",
                       bi, bones[bi].name.Str(),
                       quatData[i].x, quatData[i].y, quatData[i].z, quatData[i].w,
                       bones[bi].weight);
                if (mesh) {
                    const Hmx::Matrix3& mtx = mesh->LocalXfm().m;
                    printf("  mesh='%s' m.x=(%6.3f,%6.3f,%6.3f)", mesh->Name(), mtx.x.x, mtx.x.y, mtx.x.z);
                }
                printf("\n");
            }

            printf("\n  --- ROTZ ---\n");
            float* rotzData = (float*)(start + rotyEnd);
            int numRotz = (rotzEnd - rotyEnd) / (int)sizeof(float);
            int rotzBoneIdx = quatBoneIdx + numQuat;
            for (int i = 0; i < numRotz; i++) {
                int bi = rotzBoneIdx + i;
                if (bi >= (int)bones.size()) break;
                RndTransformable* mesh = CharUtlFindBoneTrans(bones[bi].name.Str(), charAnim.character);
                printf("  [%2d] %-35s rotz=%8.4f (%.1f deg) w=%.3f",
                       bi, bones[bi].name.Str(), rotzData[i], rotzData[i] * 57.2958f, bones[bi].weight);
                if (mesh) printf("  mesh='%s'", mesh->Name());
                printf("\n");
            }
            printf("=== END BONE BUFFER DUMP ===\n");
        }

        // Re-center camera on animated character's pelvis bone
        if (charAnim.character && !cfg.hasEye) {
            RndTransformable* pelvis = charAnim.character->Find<RndTransformable>("bone_pelvis.mesh", false);
            if (pelvis) {
                const Transform& bxfm = pelvis->WorldXfm();
                gOrbitCam.targetX = bxfm.v.x;
                gOrbitCam.targetY = bxfm.v.y;
                gOrbitCam.targetZ = bxfm.v.z;
                printf("Milo Viewer: centered on pelvis (%.2f, %.2f, %.2f) dist=%.1f\n",
                       bxfm.v.x, bxfm.v.y, bxfm.v.z, gOrbitCam.distance);
            }
        }
    }

    // Render a few frames to let GPU resources settle
    for (int frame = 0; frame < m.warmupFrames; frame++) {
        float warmupSec = (float)frame / 30.0f; // virtual time at 30fps
        scene.PollMovies(warmupSec);
        gOrbitCam.Update(cam);
        TheRnd.BeginDrawing();
        scene.DrawAllMeshes(cfg);
        scene.DrawMovieOverlay();
        TheRnd.EndDrawing();
    }

    // Pose dump
    if (cfg.poseDumpPath) {
        if (!poseDumpBeatSet) {
            poseDumpBeatResolved = (cfg.startFrame >= 0.0f) ? cfg.startFrame : 0.0f;
        }
        const char* dumpClipName = charAnim.clip ? charAnim.clip->Name() : "";
        if (WritePoseDumpJson(cfg.poseDumpPath, baseScene, m.poseDumpBones,
                              absPath, dumpClipName, poseDumpBeatResolved)) {
            printf("Milo Viewer: pose dump saved to %s (%zu selected filters)\n",
                   cfg.poseDumpPath, m.poseDumpBones.size());
        } else {
            fprintf(stderr, "Error: failed to write pose dump '%s'\n", cfg.poseDumpPath);
        }
    }

    // Readback framebuffer and save
    int w = gWgpuRnd->Gpu().WindowWidth();
    int h = gWgpuRnd->Gpu().WindowHeight();
    size_t pixelSize = (size_t)w * h * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelSize);

    int rc = 0;
    if (pixels && gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize)) {
        if (WriteScreenshot(cfg.screenshotPath, pixels, w, h)) {
            printf("Milo Viewer: screenshot saved to %s (%dx%d, PNG)\n", cfg.screenshotPath, w, h);
        } else {
            fprintf(stderr, "Error: failed to write screenshot to '%s'\n", cfg.screenshotPath);
            rc = 1;
        }
    } else {
        fprintf(stderr, "Error: failed to readback framebuffer (headless mode required)\n");
        rc = 1;
    }
    free(pixels);
    return rc;
}

// ============================================================================
// RunVideo
// ============================================================================

int RunVideo(VideoMode& m, ViewerScene& scene,
             AnimState& anim, CharAnimState& charAnim,
             RndCam* cam, const ViewerConfig& cfg) {
    int w = gWgpuRnd->Gpu().WindowWidth();
    int h = gWgpuRnd->Gpu().WindowHeight();
    size_t pixelSize = (size_t)w * h * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelSize);

    if (!pixels) {
        fprintf(stderr, "Error: failed to allocate framebuffer (%d x %d)\n", w, h);
        return 1;
    }

    VideoEncoder encoder;
    if (!encoder.Start(cfg.videoPath, w, h, cfg.videoFps)) {
        free(pixels);
        return 1;
    }

    bool autoOrbit = (strcmp(cfg.cameraMode, "auto-orbit") == 0);
    if (autoOrbit && cfg.camAzimuthDeg <= -900.0f) {
        gOrbitCam.azimuth = -0.5f;
    }

    // Pelvis tracking
    RndTransformable* pelvisBone = nullptr;
    float smoothX = gOrbitCam.targetX;
    float smoothY = gOrbitCam.targetY;
    if (charAnim.active && charAnim.character) {
        pelvisBone = charAnim.character->Find<RndTransformable>("bone_pelvis.mesh", false);
    }

    // Pre-advance for initial camera setup
    if (charAnim.active && charAnim.clip) {
        PoseMeshesWithFacing(charAnim.clip, charAnim.character, 20.0f);
        CharTwistSolver::SolveAll(charAnim.character);
        if (pelvisBone && !cfg.hasEye) {
            const Transform& bxfm = pelvisBone->WorldXfm();
            gOrbitCam.targetX = bxfm.v.x;
            gOrbitCam.targetY = bxfm.v.y;
            gOrbitCam.targetZ = bxfm.v.z;
            smoothX = bxfm.v.x;
            smoothY = bxfm.v.y;
            printf("Milo Viewer: video centered on pelvis (%.2f, %.2f, %.2f)\n",
                   bxfm.v.x, bxfm.v.y, bxfm.v.z);
        }
    }

    printf("Milo Viewer: recording %d frames (%.1fs @ %d fps)...\n",
           m.totalFrames, cfg.videoDuration, cfg.videoFps);

    int rc = 0;
    for (int frame = 0; frame < m.totalFrames; frame++) {
        float seconds = (float)frame * m.dt;
        float beat    = seconds * (cfg.bpm / 60.0f) * anim.speed;

        if (charAnim.active && charAnim.clip) {
            float clipStart = charAnim.clip->StartBeat();
            float clipEnd   = charAnim.clip->EndBeat();
            float clipLen   = clipEnd - clipStart;
            float clipBeat  = clipStart + fmodf(beat, clipLen);
            if (clipBeat < clipStart) clipBeat += clipLen;
            PoseMeshesWithFacing(charAnim.clip, charAnim.character, clipBeat);
            CharTwistSolver::SolveAll(charAnim.character);
        }

        if (anim.hasAnimation) {
            float animFrame = anim.startFrame + fmodf(seconds * 30.0f * anim.speed,
                anim.endFrame - anim.startFrame);
            for (auto* a : anim.animatables) {
                float sf = a->StartFrame();
                float ef = a->EndFrame();
                if (ef > sf) {
                    float r = ef - sf;
                    float f = fmodf(animFrame - sf, r);
                    if (f < 0.0f) f += r;
                    a->SetFrame(f + sf, 1.0f);
                }
            }
        }

        TrackPelvis(pelvisBone, smoothX, smoothY, 0.05f, cfg.hasEye);

        if (autoOrbit) gOrbitCam.azimuth += 0.005f;

        scene.PollMovies(seconds);
        gOrbitCam.Update(cam);
        TheRnd.BeginDrawing();
        scene.DrawAllMeshes(cfg);
        scene.DrawMovieOverlay();
        TheRnd.EndDrawing();

        if (gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize)) {
            encoder.WriteFrame(pixels, pixelSize);
        } else {
            fprintf(stderr, "Error: framebuffer readback failed at frame %d\n", frame);
            rc = 1;
            break;
        }

        if (frame > 0 && frame % (cfg.videoFps * 5) == 0) {
            printf("  encoded %d / %d frames (%.0f%%)\n",
                   frame, m.totalFrames, 100.0f * frame / m.totalFrames);
        }
    }

    encoder.Finish();
    free(pixels);
    if (rc == 0) printf("Milo Viewer: video saved to %s\n", cfg.videoPath);
    return rc;
}

// ============================================================================
// RunInteractive
// ============================================================================

int RunInteractive(InteractiveMode& /*m*/, ViewerScene& scene,
                   AnimState& anim, CharAnimState& charAnim,
                   RndCam* cam, const ViewerConfig& cfg) {
    if (anim.hasAnimation) {
        printf("Milo Viewer: entering render loop — animation [%.0f..%.0f] at %.1fx "
               "(Space=pause, ./,=step, Up/Down=speed, Home=reset, ESC=quit)\n",
               anim.startFrame, anim.endFrame, anim.speed);
    } else {
        printf("Milo Viewer: entering render loop (press ESC to quit, R to reset camera)\n");
    }

    anim.lastTime = glfwGetTime();
    bool autoOrbit = (strcmp(cfg.cameraMode, "auto-orbit") == 0);

    RndTransformable* interactivePelvis = nullptr;
    if (charAnim.active && charAnim.character) {
        interactivePelvis = charAnim.character->Find<RndTransformable>("bone_pelvis.mesh", false);
    }

    int frameCount = 0;
    while (!gWgpuRnd->Gpu().ShouldClose()) {
        gWgpuRnd->Gpu().PollEvents();

        double now = glfwGetTime();
        double dt  = now - anim.lastTime;
        anim.lastTime = now;

        // Advance character animation (beat-based)
        if (charAnim.active && !anim.paused && dt > 0.0 && dt < 0.5) {
            float seconds = (float)now;
            float beat    = seconds * (cfg.bpm / 60.0f) * anim.speed;
            charAnim.AdvanceBeat(seconds, beat, cfg.bpm);
        }

        // Update procedural blink timer
        if (charAnim.faceServo && !charAnim.eyes && dt > 0.0 && dt < 0.5) {
            charAnim.blink.Advance((float)dt);
        }

        // Advance prop/TransAnim animations (frame-based)
        if (anim.hasAnimation) {
            if (!anim.paused && dt > 0.0 && dt < 0.5) {
                float frameDelta = (float)dt * 30.0f * anim.speed;
                anim.currentFrame += frameDelta;
                float range = anim.endFrame - anim.startFrame;
                if (range > 0.0f && anim.currentFrame > anim.endFrame) {
                    anim.currentFrame = fmodf(anim.currentFrame - anim.startFrame, range) + anim.startFrame;
                }
            }
            for (auto* a : anim.animatables) {
                float sf = a->StartFrame();
                float ef = a->EndFrame();
                float frame = anim.currentFrame;
                if (ef > sf) {
                    float r = ef - sf;
                    frame = fmodf(frame - sf, r);
                    if (frame < 0.0f) frame += r;
                    frame += sf;
                }
                a->SetFrame(frame, 1.0f);
            }
        }

        // Smooth-follow pelvis (unless user is dragging)
        if (interactivePelvis && !cfg.hasEye && !gOrbitCam.leftDrag && !gOrbitCam.middleDrag) {
            const Transform& bxfm = interactivePelvis->WorldXfm();
            const float iSmooth = 0.08f;
            gOrbitCam.targetX += (bxfm.v.x - gOrbitCam.targetX) * iSmooth;
            gOrbitCam.targetY += (bxfm.v.y - gOrbitCam.targetY) * iSmooth;
            gOrbitCam.targetZ += (bxfm.v.z - gOrbitCam.targetZ) * iSmooth;
        }

        if (autoOrbit && !anim.paused) {
            gOrbitCam.azimuth += 0.002f * (float)dt * 60.0f;
        }

        scene.PollMovies();
        gOrbitCam.Update(cam);
        TheRnd.BeginDrawing();
        scene.DrawAllMeshes(cfg);
        scene.DrawMovieOverlay();
        TheRnd.EndDrawing();

        frameCount++;
        if (cfg.maxFrames > 0 && frameCount >= cfg.maxFrames) {
            break;
        }
    }

    return 0;
}
