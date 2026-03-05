// DC3 Native Port — Milo Viewer
// Standalone tool: loads a .milo_xbox file and renders it with an orbit camera.
// Supports character animation (CharClip), subdirectory loading, and video recording.

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Task.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Rnd.h"
#include "char/Char.h"
#include "char/Character.h"
#include "char/CharDriver.h"
#include "char/CharClip.h"
#include "char/CharServoBone.h"
#include "char/CharPollable.h"
#include "char/CharBoneDir.h"
#include "char/CharEyes.h"
#include "char/CharFaceServo.h"
#include "char/CharLipSyncDriver.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"

#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "platform/Rnd_Wgpu.h"
#include "gfx/GpuDevice.h"
#include "export/TextureExporter.h"
#include "export/MaterialExporter.h"
#include "export/GltfExporter.h"

#include "viewer/ViewerArgs.h"
#include "viewer/ViewerCamera.h"
#include "viewer/ViewerScene.h"
#include "viewer/ViewerAnimation.h"
#include "viewer/ViewerCapture.h"

#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <algorithm>
#include <variant>

// Forward declarations from engine
extern Rnd& TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

// ============================================================================
// KeyCallback — references gAnim and gOrbitCam
// ============================================================================

static void KeyCallback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_R) {
            gOrbitCam.Reset();
            printf("Camera reset\n");
        }
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (key == GLFW_KEY_SPACE && gAnim.hasAnimation) {
            gAnim.paused = !gAnim.paused;
            printf("Animation %s (frame %.1f / %.1f)\n",
                   gAnim.paused ? "paused" : "playing",
                   gAnim.currentFrame, gAnim.endFrame);
        }
        if (key == GLFW_KEY_PERIOD && gAnim.hasAnimation) {
            gAnim.currentFrame += 1.0f;
            if (gAnim.endFrame > gAnim.startFrame) {
                float range = gAnim.endFrame - gAnim.startFrame;
                gAnim.currentFrame = fmodf(gAnim.currentFrame - gAnim.startFrame, range) + gAnim.startFrame;
            }
            printf("Frame: %.1f\n", gAnim.currentFrame);
        }
        if (key == GLFW_KEY_COMMA && gAnim.hasAnimation) {
            gAnim.currentFrame -= 1.0f;
            if (gAnim.currentFrame < gAnim.startFrame) {
                float range = gAnim.endFrame - gAnim.startFrame;
                gAnim.currentFrame = gAnim.endFrame - fmodf(gAnim.startFrame - gAnim.currentFrame, range);
            }
            printf("Frame: %.1f\n", gAnim.currentFrame);
        }
        if (key == GLFW_KEY_UP && gAnim.hasAnimation) {
            gAnim.speed *= 2.0f;
            if (gAnim.speed > 16.0f) gAnim.speed = 16.0f;
            printf("Animation speed: %.1fx\n", gAnim.speed);
        }
        if (key == GLFW_KEY_DOWN && gAnim.hasAnimation) {
            gAnim.speed *= 0.5f;
            if (gAnim.speed < 0.0625f) gAnim.speed = 0.0625f;
            printf("Animation speed: %.1fx\n", gAnim.speed);
        }
        if (key == GLFW_KEY_HOME && gAnim.hasAnimation) {
            gAnim.currentFrame = gAnim.startFrame;
            printf("Animation reset to start (frame %.1f)\n", gAnim.currentFrame);
        }
    }
}

// ============================================================================
// Signal handler
// ============================================================================

#include <csignal>
#include <execinfo.h>
#include <unistd.h>

static void SignalHandler(int sig) {
    fprintf(stderr, "\nMilo Viewer: Caught signal %d\n", sig);
    void* bt[32];
    int n = backtrace(bt, 32);
    char** syms = backtrace_symbols(bt, n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  [%d] %s\n", i, syms ? syms[i] : "??");
    }
    fflush(stderr);
    _exit(1);
}

// ============================================================================
// Main
// ============================================================================

// overloaded helper for std::visit
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);

    if (argc < 2) {
        ViewerConfig::PrintHelp(stderr);
        return 1;
    }

    ViewerConfig cfg = ViewerConfig::Parse(argc, argv);

    if (!cfg.miloPath) {
        fprintf(stderr, "Error: no .milo file specified\n\n");
        ViewerConfig::PrintHelp(stderr);
        return 1;
    }

    // When --char-setup is provided, load the base HamCharacter as primary
    // and use FileMerger to merge outfit (miloPath) and visemes
    const char* primaryPath = cfg.charSetupPath ? cfg.charSetupPath : cfg.miloPath;

    char absPath[PATH_MAX];
    if (!realpath(primaryPath, absPath)) {
        fprintf(stderr, "Error: cannot resolve path '%s'\n", primaryPath);
        return 1;
    }
    printf("Milo Viewer: loading %s\n", absPath);
    if (cfg.screenshotPath) {
        printf("Milo Viewer: screenshot mode — will save to %s\n", cfg.screenshotPath);
    }

    // ---- Engine init ----
    if (cfg.IsExportOnly()) {
        setenv("MILO_RENDER", "0", 1);
    } else {
        setenv("MILO_RENDER", "1", 1);
    }
    if (cfg.screenshotPath || cfg.videoPath) {
        setenv("MILO_HEADLESS", "1", 1);
    }

    printf("Milo Viewer: SystemPreInit...\n");
    InitMakeString();
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");

    printf("Milo Viewer: TheRnd.PreInit...\n");
    TheRnd.PreInit();

    printf("Milo Viewer: SystemInit...\n");
    SystemInit("config/ham_keep.dta");

    printf("Milo Viewer: TheRnd.Init...\n");
    TheRnd.Init();

    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) {
        fprintf(stderr, "ERROR: GPU initialization failed. Renders will be black/empty.\n");
        fprintf(stderr, "  This often happens when Vulkan ICD access is blocked (e.g. sandbox).\n");
        fprintf(stderr, "  Try running outside the sandbox or with dangerouslyDisableSandbox.\n");
        return 2;
    }
    if (gWgpuRnd->Gpu().IsNullBackend()) {
        fprintf(stderr, "ERROR: GPU fell back to Null backend — no real GPU available.\n");
        fprintf(stderr, "  Vulkan ICD access is likely blocked by sandbox restrictions.\n");
        fprintf(stderr, "  Renders will be black/empty. Exiting.\n");
        return 2;
    }

    printf("Milo Viewer: registering subsystem types...\n");
    FlowInit();
    CharInit();
    WorldInit();
    HamInit();
    printf("Milo Viewer: subsystem init complete\n");

    GLFWwindow* window = gWgpuRnd->Gpu().Window();
    InstallCameraCallbacks(window);
    if (window) {
        glfwSetKeyCallback(window, KeyCallback);
    }

    RndCam* cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 0.6024f, 1.0f);
    cam->Select();

    // ---- Load the scene ----
    ViewerScene scene;
    if (!scene.Load(absPath, cfg)) {
        return 1;
    }
    ObjectDir* baseScene = scene.baseScene;
    Character* charObj = scene.character;

    // ---- Export-and-exit modes ----
    if (cfg.IsExportOnly()) {
        if (cfg.exportTexturesDir) {
            TextureExporter::Options texOpts;
            texOpts.verbose = cfg.verbose;
            int count = TextureExporter::ExportAll(baseScene, cfg.exportTexturesDir, texOpts);
            printf("Exported %d textures to %s\n", count, cfg.exportTexturesDir);
        }
        if (cfg.exportMaterialsDir) {
            MaterialExporter::Options matOpts;
            matOpts.verbose = cfg.verbose;
            int count = MaterialExporter::ExportAll(baseScene, cfg.exportMaterialsDir, matOpts);
            printf("Exported %d materials to %s\n", count, cfg.exportMaterialsDir);
        }
        if (cfg.exportGltfPath) {
            GltfExporter::Options gltfOpts;
            gltfOpts.verbose = cfg.verbose;
            bool ok = GltfExporter::Export(baseScene, cfg.exportGltfPath, gltfOpts);
            if (ok) printf("Exported glTF to %s\n", cfg.exportGltfPath);
            else fprintf(stderr, "Error: glTF export failed\n");
        }
        scene.ReleaseResources();
        _exit(0);
    }

    // ---- FileMerger-based outfit/viseme loading (--char-setup) ----
    scene.LoadFileMerger(cfg);

    // ---- Load animation clips (--clips) ----
    CharAnimState charAnim;
    charAnim.character = charObj;

    if (cfg.clipsPath && charObj) {
        char clipsAbsPath[PATH_MAX];
        if (realpath(cfg.clipsPath, clipsAbsPath)) {
            printf("Milo Viewer: loading clips from '%s'...\n", clipsAbsPath);
            FilePath clipsFp(clipsAbsPath);
            scene.clipsDir.LoadFile(clipsFp, false, false, kLoadFront, false);

            ObjectDir* clipsDirPtr = scene.clipsDir;
            if (clipsDirPtr) {
                printf("Milo Viewer: clips dir loaded (class '%s')\n",
                       clipsDirPtr->ClassName().Str());

                CharDriver* driver = charObj->Driver();
                if (!driver) {
                    printf("Milo Viewer: creating CharDriver 'main.drv'...\n");
                    charObj->New<CharDriver>("main.drv");
                    driver = charObj->Driver();
                    if (driver) {
                        CharServoBone* servo = charObj->Find<CharServoBone>("bone.servo", false);
                        if (!servo) {
                            servo = charObj->New<CharServoBone>("bone.servo");
                            printf("Milo Viewer: created CharServoBone 'bone.servo'\n");
                        }
                        driver->SetBones(servo);
                        printf("Milo Viewer: CharDriver created and wired to bones\n");
                    }
                }

                if (driver) {
                    driver->SetClips(clipsDirPtr);

                    CharClip* clipToPlay = nullptr;
                    if (cfg.clipName) {
                        clipToPlay = clipsDirPtr->Find<CharClip>(cfg.clipName, false);
                        if (!clipToPlay) {
                            fprintf(stderr, "Warning: clip '%s' not found, listing available:\n", cfg.clipName);
                        }
                    }
                    if (!clipToPlay) {
                        ObjDirItr<CharClip> clipIt(clipsDirPtr, true);
                        int count = 0;
                        while (clipIt) {
                            if (!clipToPlay) clipToPlay = clipIt;
                            if (cfg.verbose || (cfg.clipName && count < 20)) {
                                printf("  clip: '%s' (%.1f - %.1f beats)\n",
                                       clipIt->Name(), clipIt->StartBeat(), clipIt->EndBeat());
                            }
                            count++;
                            ++clipIt;
                        }
                        printf("Milo Viewer: %d clips available\n", count);
                    }

                    if (clipToPlay) {
                        printf("Milo Viewer: playing clip '%s' (beats %.1f-%.1f)\n",
                               clipToPlay->Name(),
                               clipToPlay->StartBeat(), clipToPlay->EndBeat());

                        CharServoBone* servo = charObj->Find<CharServoBone>("bone.servo", false);
                        charAnim.servo = servo;
                        if (servo) {
                            ObjDirItr<CharClip> allClips(clipsDirPtr, true);
                            while (allClips) {
                                allClips->StuffBones(*servo);
                                ++allClips;
                            }
                            printf("Milo Viewer: bones stuffed from clips (%d bones)\n",
                                   (int)servo->GetBones().size());
                        }

                        driver->Enter();
                        int flags = CharClip::kPlayNow | CharClip::kPlayLoop;
                        driver->Play(clipToPlay, flags, -1.0f, 1e30f, 0.0f);
                        charAnim.clip   = clipToPlay;
                        charAnim.active = true;
                        printf("Milo Viewer: character animation active (cfg.bpm=%.0f)\n", cfg.bpm);
                    }
                } else {
                    fprintf(stderr, "Warning: failed to create CharDriver\n");
                }
            } else {
                fprintf(stderr, "Warning: failed to load clips dir\n");
            }
        } else {
            fprintf(stderr, "Warning: cannot resolve clips path '%s'\n", cfg.clipsPath);
        }
    } else if (cfg.clipsPath && !charObj) {
        fprintf(stderr, "Warning: --clips specified but no Character found in scene\n");
    }

    // ---- Load viseme clips (--visemes) — manual path, skipped when FileMerger is active ----
    if (!scene.fileMergerActive && cfg.visemesPath && charObj) {
        char visAbsPath[PATH_MAX];
        if (realpath(cfg.visemesPath, visAbsPath)) {
            printf("Milo Viewer: loading visemes from '%s'...\n", visAbsPath);
            FilePath visFp(visAbsPath);
            scene.visemeDir.LoadFile(visFp, false, false, kLoadFront, false);

            ObjectDir* vd = scene.visemeDir;
            if (vd) {
                printf("Milo Viewer: viseme dir loaded (class '%s')\n", vd->ClassName().Str());

                Symbol clipType = vd->Type();
                if (clipType.Null()) {
                    for (ObjDirItr<CharClip> it(vd, true); it != nullptr; ++it) {
                        clipType = it->Type();
                        break;
                    }
                }

                CharFaceServo* faceServo = charObj->New<CharFaceServo>("face.servo");
                faceServo->SetBlinkClipLeft("Blink");
                faceServo->SetBlinkClipRight("Blink");
                faceServo->SetClips(vd);
                faceServo->SetClipType(clipType);

                if (faceServo->GetBones().empty()) {
                    ObjDirItr<CharClip> allVis(vd, true);
                    while (allVis) {
                        allVis->StuffBones(*faceServo);
                        ++allVis;
                    }
                    printf("Milo Viewer: face servo bones stuffed from clips (%d bones)\n",
                           (int)faceServo->GetBones().size());
                }

                faceServo->Enter();
                printf("Milo Viewer: CharFaceServo created (base='%s', blink='%s', type='%s')\n",
                       faceServo->BaseClip() ? faceServo->BaseClip()->Name() : "(none)",
                       faceServo->BlinkClipLeftName().Str(),
                       clipType.Str());
                charAnim.faceServo = faceServo;
            } else {
                fprintf(stderr, "Warning: failed to load viseme dir\n");
            }
        } else {
            fprintf(stderr, "Warning: cannot resolve visemes path '%s'\n", cfg.visemesPath);
        }
    } else if (cfg.visemesPath && !charObj) {
        fprintf(stderr, "Warning: --visemes specified but no Character found in scene\n");
    }

    // ---- Wire facial components ----
    if (scene.fileMergerActive && charObj) {
        // FileMerger path: components are auto-wired by SyncObjects, just find them
        charAnim.faceServo = charObj->Find<CharFaceServo>("face.faceservo", false);
        if (charAnim.faceServo) {
            printf("Milo Viewer: CharFaceServo 'face.faceservo' auto-wired\n");
        }
        charAnim.eyes = charObj->Find<CharEyes>("CharEyes.eyes", false);
        if (charAnim.eyes) {
            printf("Milo Viewer: CharEyes 'CharEyes.eyes' auto-wired (eyes=%d, interests=%d)\n",
                   charAnim.eyes->NumEyes(), charAnim.eyes->NumInterests());
        }
        charAnim.lipDriver = charObj->Find<CharLipSyncDriver>("face.lipdrv", false);
        if (charAnim.lipDriver) {
            printf("Milo Viewer: CharLipSyncDriver 'face.lipdrv' wired\n");
        }
    } else if (charObj && charAnim.faceServo) {
        // Manual path: wire CharEyes to manually-created faceServo
        CharEyes* charEyes = charObj->Find<CharEyes>("CharEyes.eyes", false);
        if (charEyes) {
            charEyes->SetFaceServo(charAnim.faceServo);
            charEyes->Enter();
            printf("Milo Viewer: CharEyes '%s' wired (eyes=%d, interests=%d)\n",
                   charEyes->Name(),
                   charEyes->NumEyes(),
                   charEyes->NumInterests());
            charAnim.eyes = charEyes;
        } else {
            printf("Milo Viewer: no CharEyes found in character dir\n");
        }
    }

    scene.ResolveMeshVisibility(cfg);
    scene.PrintSummary(cfg.verbose);

    // ---- Scan for animation data ----
    printf("DBG: before ScanScene\n"); fflush(stdout);
    if (!scene.fileMergerActive) {
        gAnim.ScanScene(baseScene, cfg);
    } else {
        printf("DBG: skipping ScanScene (FileMerger active)\n"); fflush(stdout);
    }
    printf("DBG: after ScanScene\n"); fflush(stdout);

    // Set window title
    if (window) {
        const char* basename = strrchr(absPath, '/');
        basename = basename ? basename + 1 : absPath;
        char title[256];
        snprintf(title, sizeof(title), "DC3 Viewer — %s", basename);
        glfwSetWindowTitle(window, title);
    }

    // Activate scene environment
    {
        printf("DBG: before FindEnvironment\n"); fflush(stdout);
        RndEnviron* env = scene.FindEnvironment();
        printf("DBG: FindEnvironment = %p\n", env); fflush(stdout);
        if (env) {
            Vector3 origin(0, 0, 0);
            env->Select(&origin);
            printf("Milo Viewer: using scene environment '%s'\n", env->Name());
        }
    }

    printf("DBG: before AutoFrameCamera\n"); fflush(stdout);
    scene.AutoFrameCamera(gOrbitCam, cam, cfg);
    printf("DBG: after AutoFrameCamera\n"); fflush(stdout);
    scene.SetupSyntheticLights(cfg);
    printf("DBG: after SetupSyntheticLights\n"); fflush(stdout);

    // ---- Dispatch to mode runner ----
    ViewerMode mode = SelectMode(cfg);
    int rc = std::visit(overloaded{
        [&](ScreenshotMode& m)  { return RunScreenshot(m, scene, gAnim, charAnim, cam, cfg, absPath); },
        [&](VideoMode& m)       { return RunVideo(m, scene, gAnim, charAnim, cam, cfg); },
        [&](InteractiveMode& m) { return RunInteractive(m, scene, gAnim, charAnim, cam, cfg); },
    }, mode);

    printf("Milo Viewer: shutting down\n");
    scene.ReleaseResources();
    _exit(rc);
    return rc;
}
