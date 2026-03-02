// DC3 Native Port — Milo Viewer
// Standalone tool: loads a .milo_xbox file and renders it with an orbit camera.
// Supports character animation (CharClip), subdirectory loading, and video recording.

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Task.h"
#include "rndobj/Anim.h"
#include "rndobj/Cam.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Rnd.h"
#include "rndobj/Trans.h"
#include "rndobj/TransAnim.h"
#include "rndobj/Mesh.h"
#include "rndobj/PropAnim.h"
#include "char/Char.h"
#include "char/Character.h"
#include "char/CharDriver.h"
#include "char/CharClip.h"
#include "char/CharServoBone.h"
#include "char/CharBoneDir.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "utl/FilePath.h"

#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "platform/Rnd_Wgpu.h"
#include "gfx/GpuDevice.h"
#include "gfx/Screenshot.h"
#include "gfx/VideoEncoder.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <vector>
#include <string>

// Forward declarations from engine
extern Rnd& TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

// ============================================================================
// 4x4 matrix multiply (row-major, right-multiply convention)
// ============================================================================
static void Mat4Multiply(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a[i * 4 + k] * b[k * 4 + j];
            }
            out[i * 4 + j] = sum;
        }
    }
}

// ============================================================================
// Orbit Camera
// ============================================================================
struct OrbitCamera {
    float azimuth = 0.4f;       // radians around Y axis
    float elevation = 0.3f;     // radians above horizon
    float distance = 3.0f;      // distance from target
    float targetX = 0.0f;
    float targetY = 1.0f;       // Y=1 centers on typical character height
    float targetZ = 0.0f;

    // Mouse state
    bool leftDrag = false;
    bool middleDrag = false;
    double lastX = 0, lastY = 0;

    void Reset() {
        azimuth = 0.0f;
        elevation = 0.3f;
        distance = 10.0f;
        targetX = 0.0f;
        targetY = 1.0f;
        targetZ = 0.0f;
    }

    // Update camera transform and viewProj on a RndCam each frame
    void Update(RndCam* cam) {
        // Clamp elevation to avoid gimbal lock
        if (elevation > 1.5f) elevation = 1.5f;
        if (elevation < -1.5f) elevation = -1.5f;
        if (distance < 0.1f) distance = 0.1f;

        // Camera position from spherical coordinates (Milo convention: Z-up)
        float cosElev = cosf(elevation);
        float eyeX = targetX + distance * cosElev * sinf(azimuth);
        float eyeY = targetY + distance * cosElev * cosf(azimuth);
        float eyeZ = targetZ + distance * sinf(elevation);

        // Build look-at vectors
        Vector3 eye, tgt, fwd, right, up;
        eye.Set(eyeX, eyeY, eyeZ);
        tgt.Set(targetX, targetY, targetZ);
        Subtract(tgt, eye, fwd);
        Normalize(fwd, fwd);

        // Milo world: Z is up
        Vector3 worldUp;
        worldUp.Set(0, 0, 1);
        Cross(fwd, worldUp, right);
        float rightLen = Length(right);
        if (rightLen < 0.001f) {
            worldUp.Set(0, 1, 0);
            Cross(fwd, worldUp, right);
        }
        Normalize(right, right);
        Cross(right, fwd, up);
        Normalize(up, up);

        // Set camera's local transform
        // Milo convention: m.x = right, m.y = forward, m.z = up
        Transform xfm;
        xfm.m.x.Set(right.x, right.y, right.z);
        xfm.m.y.Set(fwd.x, fwd.y, fwd.z);
        xfm.m.z.Set(up.x, up.y, up.z);
        xfm.v.Set(eyeX, eyeY, eyeZ);
        cam->SetLocalXfm(xfm);

        // Build viewProj matrix manually (RndCam::UpdateLocal is stubbed)
        // View matrix (row-major, right-multiply convention: v_clip = v_world * V * P)
        float dr = -Dot(right, eye);
        float df = -Dot(fwd, eye);
        float du = -Dot(up, eye);

        float view[16] = {
            right.x, fwd.x, up.x, 0,
            right.y, fwd.y, up.y, 0,
            right.z, fwd.z, up.z, 0,
            dr,      df,    du,   1
        };

        // Perspective projection (row-major, Y-forward depth convention)
        // Milo camera: Y = forward/depth, Z = up, X = right
        // Maps to clip space with Z as depth [0,1] for WebGPU
        float near = cam->NearPlane();
        float far = cam->FarPlane();
        float yfov = cam->YFov();
        float aspect = 16.0f / 9.0f;
        float cot = 1.0f / tanf(yfov / 2.0f);
        float zRange = far - near;

        float proj[16] = {
            cot / aspect, 0,   0,              0,
            0,            0,   far / zRange,   1,
            0,            cot, 0,              0,
            0,            0,   -near * far / zRange, 0
        };

        // ViewProj = View * Proj
        float viewProj[16];
        Mat4Multiply(view, proj, viewProj);

        // Set on camera (bypass stubbed UpdateLocal)
        Hmx::Matrix4 vp;
        memcpy(&vp, viewProj, 64);
        cam->SetViewProj(vp);
    }
};

// ============================================================================
// Animation State
// ============================================================================
struct AnimState {
    bool paused = false;
    float speed = 1.0f;         // playback speed multiplier
    float currentFrame = 0.0f;  // current animation frame
    float startFrame = 0.0f;
    float endFrame = 0.0f;
    bool hasAnimation = false;
    double lastTime = 0.0;      // last glfwGetTime() for delta
    int animCount = 0;          // number of animatable objects found

    // Collected animatables (non-RndDir scene fallback)
    std::vector<RndAnimatable*> animatables;
};

// ============================================================================
// GLFW Callbacks
// ============================================================================
static OrbitCamera gOrbitCam;
static AnimState gAnim;

static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
    double dx = xpos - gOrbitCam.lastX;
    double dy = ypos - gOrbitCam.lastY;
    gOrbitCam.lastX = xpos;
    gOrbitCam.lastY = ypos;

    if (gOrbitCam.leftDrag) {
        gOrbitCam.azimuth -= (float)dx * 0.005f;
        gOrbitCam.elevation += (float)dy * 0.005f;
    }

    if (gOrbitCam.middleDrag) {
        // Pan: move target in the camera's right/up plane
        float cosElev = cosf(gOrbitCam.elevation);
        float sinAz = sinf(gOrbitCam.azimuth);
        float cosAz = cosf(gOrbitCam.azimuth);

        // Right direction (simplified, horizontal only)
        float rx = cosAz, rz = -sinAz;
        // Up direction (world Y)
        float panScale = gOrbitCam.distance * 0.002f;
        gOrbitCam.targetX -= rx * (float)dx * panScale;
        gOrbitCam.targetZ -= rz * (float)dx * panScale;
        gOrbitCam.targetY += (float)dy * panScale;
    }
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        gOrbitCam.leftDrag = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &gOrbitCam.lastX, &gOrbitCam.lastY);
        }
    }
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        gOrbitCam.middleDrag = (action == GLFW_PRESS);
        if (action == GLFW_PRESS) {
            glfwGetCursorPos(window, &gOrbitCam.lastX, &gOrbitCam.lastY);
        }
    }
}

static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    gOrbitCam.distance *= (1.0f - (float)yoffset * 0.1f);
    if (gOrbitCam.distance < 0.1f) gOrbitCam.distance = 0.1f;
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_R) {
            gOrbitCam.Reset();
            printf("Camera reset\n");
        }
        if (key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        // Animation controls
        if (key == GLFW_KEY_SPACE && gAnim.hasAnimation) {
            gAnim.paused = !gAnim.paused;
            printf("Animation %s (frame %.1f / %.1f)\n",
                   gAnim.paused ? "paused" : "playing",
                   gAnim.currentFrame, gAnim.endFrame);
        }
        if (key == GLFW_KEY_PERIOD && gAnim.hasAnimation) {
            // Step forward one frame (30fps = 1 frame)
            gAnim.currentFrame += 1.0f;
            if (gAnim.endFrame > gAnim.startFrame) {
                float range = gAnim.endFrame - gAnim.startFrame;
                gAnim.currentFrame = fmodf(gAnim.currentFrame - gAnim.startFrame, range) + gAnim.startFrame;
            }
            printf("Frame: %.1f\n", gAnim.currentFrame);
        }
        if (key == GLFW_KEY_COMMA && gAnim.hasAnimation) {
            // Step backward one frame
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
int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);

    // Show help
    auto showHelp = [](FILE* f) {
        fprintf(f, "milo-viewer — Dance Central 3 .milo scene viewer\n\n");
        fprintf(f, "Usage: milo-viewer <path.milo_xbox> [options]\n\n");
        fprintf(f, "Options:\n");
        fprintf(f, "  --help                     Show this help message\n");
        fprintf(f, "  --screenshot <file.png>    Render headlessly and save screenshot (PNG)\n");
        fprintf(f, "  --subdir <path.milo_xbox>  Load additional .milo as subdirectory (repeatable)\n");
        fprintf(f, "  --clips <path.milo_xbox>   Load CharClip animation directory\n");
        fprintf(f, "  --clip <name>              Play a specific clip by name\n");
        fprintf(f, "  --bpm <number>             Beats per minute for clip playback (default: 120)\n");
        fprintf(f, "  --video <output.mp4>       Record video via ffmpeg (headless)\n");
        fprintf(f, "  --duration <seconds>       Video duration in seconds (default: 10)\n");
        fprintf(f, "  --fps <number>             Video frame rate (default: 30)\n");
        fprintf(f, "  --camera <mode>            Camera mode: orbit, auto-orbit (default: orbit)\n");
        fprintf(f, "  --azimuth <degrees>        Camera azimuth angle (default: ~23)\n");
        fprintf(f, "  --elevation <degrees>      Camera elevation angle (default: ~17)\n");
        fprintf(f, "  --frame <number>           Start at specific animation frame\n");
        fprintf(f, "  --speed <multiplier>       Animation speed (default: 1.0)\n");
        fprintf(f, "  --paused                   Start with animation paused\n");
        fprintf(f, "  --width <pixels>           Render width (default: 1280)\n");
        fprintf(f, "  --height <pixels>          Render height (default: 720)\n");
        fprintf(f, "  --verbose, -v              Print detailed object/drawable info\n\n");
        fprintf(f, "Controls (windowed mode):\n");
        fprintf(f, "  Left drag     orbit\n");
        fprintf(f, "  Scroll        zoom\n");
        fprintf(f, "  Middle drag   pan\n");
        fprintf(f, "  R             reset camera\n");
        fprintf(f, "  Space         pause/resume animation\n");
        fprintf(f, "  .             step forward one frame\n");
        fprintf(f, "  ,             step backward one frame\n");
        fprintf(f, "  Up/Down       double/halve animation speed\n");
        fprintf(f, "  Home          reset animation to start\n");
        fprintf(f, "  Escape        quit\n\n");
        fprintf(f, "Examples:\n");
        fprintf(f, "  milo-viewer world/shared/props/gen/discoball.milo_xbox\n");
        fprintf(f, "  milo-viewer scene.milo_xbox --screenshot out.png\n");
        fprintf(f, "  milo-viewer aubrey01.milo_xbox --clips clips.milo_xbox --bpm 120\n");
        fprintf(f, "  milo-viewer aubrey01.milo_xbox --clips clips.milo_xbox --video dance.mp4 --duration 10\n");
        fprintf(f, "  milo-viewer parent.milo_xbox --subdir child.milo_xbox\n");
    };

    if (argc < 2) {
        showHelp(stderr);
        return 1;
    }

    // Parse arguments
    const char* miloPath = nullptr;
    const char* screenshotPath = nullptr;
    const char* clipsPath = nullptr;
    const char* clipName = nullptr;
    const char* videoPath = nullptr;
    const char* cameraMode = "orbit";
    std::vector<std::string> subdirPaths;
    float camAzimuthDeg = -999.0f;  // sentinel: use default
    float camElevationDeg = -999.0f;
    float camDistanceOverride = -1.0f;  // sentinel: use auto
    float startFrame = -1.0f;       // sentinel: use default
    float animSpeed = 1.0f;
    float bpm = 120.0f;
    float videoDuration = 10.0f;
    int videoFps = 30;
    bool startPaused = false;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            showHelp(stdout);
            return 0;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--subdir") == 0 && i + 1 < argc) {
            subdirPaths.push_back(argv[++i]);
        } else if (strcmp(argv[i], "--clips") == 0 && i + 1 < argc) {
            clipsPath = argv[++i];
        } else if (strcmp(argv[i], "--clip") == 0 && i + 1 < argc) {
            clipName = argv[++i];
        } else if (strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
            bpm = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--video") == 0 && i + 1 < argc) {
            videoPath = argv[++i];
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            videoDuration = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            videoFps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--camera") == 0 && i + 1 < argc) {
            cameraMode = argv[++i];
        } else if (strcmp(argv[i], "--azimuth") == 0 && i + 1 < argc) {
            camAzimuthDeg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 && i + 1 < argc) {
            camElevationDeg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--distance") == 0 && i + 1 < argc) {
            camDistanceOverride = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            startFrame = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            animSpeed = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--paused") == 0) {
            startPaused = true;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            setenv("MILO_WIDTH", argv[++i], 1);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            setenv("MILO_HEIGHT", argv[++i], 1);
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (!miloPath) {
            miloPath = argv[i];
        }
    }

    if (!miloPath) {
        fprintf(stderr, "Error: no .milo file specified\n\n");
        showHelp(stderr);
        return 1;
    }

    // Resolve file path to absolute
    char absPath[PATH_MAX];
    if (!realpath(miloPath, absPath)) {
        fprintf(stderr, "Error: cannot resolve path '%s'\n", miloPath);
        return 1;
    }
    printf("Milo Viewer: loading %s\n", absPath);
    if (screenshotPath) {
        printf("Milo Viewer: screenshot mode — will save to %s\n", screenshotPath);
    }

    // ---- Engine init (minimal subset of App::App) ----

    // Always enable GPU rendering for the viewer
    setenv("MILO_RENDER", "1", 1);
    // Force headless if screenshot/video mode or no display available
    if (screenshotPath || videoPath) {
        setenv("MILO_HEADLESS", "1", 1);
    }

    printf("Milo Viewer: SystemPreInit...\n");
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");

    printf("Milo Viewer: TheRnd.PreInit...\n");
    TheRnd.PreInit();

    printf("Milo Viewer: SystemInit...\n");
    SystemInit("config/ham_keep.dta");

    printf("Milo Viewer: TheRnd.Init...\n");
    TheRnd.Init();

    // Register subsystem types (needed for .milo object factories)
    printf("Milo Viewer: registering subsystem types...\n");
    FlowInit();
    CharInit();
    WorldInit();
    HamInit();
    printf("Milo Viewer: subsystem init complete\n");

    // ---- Set up GLFW input callbacks ----
    GLFWwindow* window = gWgpuRnd->Gpu().Window();
    if (window) {
        glfwSetCursorPosCallback(window, CursorPosCallback);
        glfwSetMouseButtonCallback(window, MouseButtonCallback);
        glfwSetScrollCallback(window, ScrollCallback);
        glfwSetKeyCallback(window, KeyCallback);
    }

    // ---- Create a camera ----
    RndCam* cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 0.6024f, 1.0f);
    cam->Select();

    // ---- Load the .milo file ----
    printf("Milo Viewer: loading milo file...\n");

    // First, try to identify what type the .milo contains
    {
        Symbol dirClass = DirLoader::GetDirClass(absPath);
        printf("Milo Viewer: .milo dir class = '%s'\n", dirClass.Str());
    }

    ObjDirPtr<ObjectDir> baseDir;
    FilePath fp(absPath);
    baseDir.LoadFile(fp, false, false, kLoadFront, false);

    ObjectDir* baseScene = baseDir;
    if (!baseScene) {
        fprintf(stderr, "Error: failed to load '%s'\n", absPath);
        fprintf(stderr, "  (The file might not be a valid .milo file.)\n");
        return 1;
    }

    printf("Milo Viewer: loaded ObjectDir '%s' (class '%s')\n",
           baseScene->Name(), baseScene->ClassName().Str());

    // Try to cast to RndDir
    RndDir* scene = dynamic_cast<RndDir*>(baseScene);
    if (!scene) {
        fprintf(stderr, "Warning: loaded dir is '%s', not RndDir — drawing may not work\n",
                baseScene->ClassName().Str());
    }

    // Sync objects if RndDir
    if (scene) {
        scene->SyncObjects();
        printf("Milo Viewer: SyncObjects complete\n");
    }

    // ---- Load subdirectories (--subdir) ----
    std::vector<ObjDirPtr<ObjectDir>> subdirs;
    for (const auto& sdPathStr : subdirPaths) {
        char sdAbsPath[PATH_MAX];
        if (!realpath(sdPathStr.c_str(), sdAbsPath)) {
            fprintf(stderr, "Warning: cannot resolve subdir path '%s', skipping\n", sdPathStr.c_str());
            continue;
        }
        printf("Milo Viewer: loading subdir '%s'...\n", sdAbsPath);

        ObjDirPtr<ObjectDir> sd;
        FilePath sdFp(sdAbsPath);
        sd.LoadFile(sdFp, false, false, kLoadFront, false);

        ObjectDir* sdDir = sd;
        if (!sdDir) {
            fprintf(stderr, "Warning: failed to load subdir '%s'\n", sdAbsPath);
            continue;
        }

        baseScene->AppendSubDir(sd);
        subdirs.push_back(sd);
        printf("Milo Viewer: loaded subdir '%s' (class '%s')\n",
               sdDir->Name(), sdDir->ClassName().Str());
    }
    if (!subdirs.empty()) {
        // Re-sync after adding subdirs so cross-dir references resolve
        if (scene) scene->SyncObjects();
        printf("Milo Viewer: %d subdirectories loaded\n", (int)subdirs.size());
    }

    // ---- Load animation clips (--clips) ----
    ObjDirPtr<ObjectDir> clipsDir;
    Character* charObj = nullptr;
    bool charAnimActive = false;
    CharClip* activeClip = nullptr;
    CharServoBone* activeServo = nullptr;

    // Find the Character in the base scene
    {
        ObjDirItr<Character> charIt(baseScene, true);
        if (charIt) {
            charObj = charIt;
            printf("Milo Viewer: found Character '%s'\n", charObj->Name());
        }
    }

    if (clipsPath && charObj) {
        char clipsAbsPath[PATH_MAX];
        if (realpath(clipsPath, clipsAbsPath)) {
            printf("Milo Viewer: loading clips from '%s'...\n", clipsAbsPath);
            FilePath clipsFp(clipsAbsPath);
            clipsDir.LoadFile(clipsFp, false, false, kLoadFront, false);

            ObjectDir* clipsDirPtr = clipsDir;
            if (clipsDirPtr) {
                printf("Milo Viewer: clips dir loaded (class '%s')\n",
                       clipsDirPtr->ClassName().Str());

                // Create a CharDriver if the Character doesn't have one
                // (outfit .milo files don't serialize CharDriver — it's created at runtime)
                CharDriver* driver = charObj->Driver();
                if (!driver) {
                    printf("Milo Viewer: creating CharDriver 'main.drv'...\n");
                    charObj->New<CharDriver>("main.drv");
                    driver = charObj->Driver();

                    // Set up bones target — find CharServoBone or create one
                    if (driver) {
                        CharServoBone* servo = charObj->Find<CharServoBone>("bone.servo", false);
                        if (!servo) {
                            servo = charObj->New<CharServoBone>("bone.servo");
                            printf("Milo Viewer: created CharServoBone 'bone.servo'\n");
                        }
                        driver->SetBones(servo);
                        // Bones will be stuffed from the clip after we find one
                        printf("Milo Viewer: CharDriver created and wired to bones\n");
                    }
                }

                if (driver) {
                    driver->SetClips(clipsDirPtr);

                    // Find a clip to play (need it before stuffing bones)
                    CharClip* clipToPlay = nullptr;
                    if (clipName) {
                        // Find specific clip by name
                        clipToPlay = clipsDirPtr->Find<CharClip>(clipName, false);
                        if (!clipToPlay) {
                            fprintf(stderr, "Warning: clip '%s' not found, listing available:\n", clipName);
                        }
                    }

                    // If no specific clip requested or not found, pick the first one
                    if (!clipToPlay) {
                        ObjDirItr<CharClip> clipIt(clipsDirPtr, true);
                        int count = 0;
                        while (clipIt) {
                            if (!clipToPlay) clipToPlay = clipIt;
                            if (verbose || (clipName && count < 20)) {
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

                        // Stuff ALL clips' bones into the servo so ScaleDown
                        // can find every bone name referenced by any clip
                        CharServoBone* servo = activeServo = charObj->Find<CharServoBone>("bone.servo", false);
                        if (servo) {
                            ObjDirItr<CharClip> allClips(clipsDirPtr, true);
                            while (allClips) {
                                allClips->StuffBones(*servo);
                                ++allClips;
                            }
                            printf("Milo Viewer: bones stuffed from clips (%d bones)\n",
                                   (int)servo->GetBones().size());
                        }

                        // Enter just the CharDriver (not the full Character, which triggers
                        // CharHair/CharCollide initialization that crashes without full scene setup)
                        driver->Enter();

                        // Play with loop + now flags, beat-based timing
                        int flags = CharClip::kPlayNow | CharClip::kPlayLoop;
                        driver->Play(clipToPlay, flags, -1.0f, 1e30f, 0.0f);
                        activeClip = clipToPlay;
                        charAnimActive = true;

                        printf("Milo Viewer: character animation active (bpm=%.0f)\n", bpm);
                    }
                } else {
                    fprintf(stderr, "Warning: failed to create CharDriver\n");
                }
            } else {
                fprintf(stderr, "Warning: failed to load clips dir\n");
            }
        } else {
            fprintf(stderr, "Warning: cannot resolve clips path '%s'\n", clipsPath);
        }
    } else if (clipsPath && !charObj) {
        fprintf(stderr, "Warning: --clips specified but no Character found in scene\n");
    }

    // ---- Resolve combined/split mesh overlap ----
    // DC3 does NOT use CharMeshHide at runtime — the class is registered but zero .milo
    // files in the entire game (5,399 checked) contain CharMeshHide objects. Unlike RB3
    // which evaluates CharMeshHide flags from BandCharacter::SyncObjects(), DC3 relies
    // on pre-baked mShowing values in the serialized .milo data. However, the gen/
    // outfit .milo files have both combined and split meshes showing=true, so this
    // heuristic resolves the overlap by naming convention:
    // Hide LOD meshes (low-detail doubles of full-res geometry) and wrinkle overlays.
    // Leave all other mesh visibility at its default state from the .milo file.
    // TODO(native): Implement proper CharMeshHide evaluation for outfit-aware visibility.
    {
        int hidCount = 0;
        ObjDirItr<RndMesh> meshIt(baseScene, true);
        while (meshIt) {
            const char* name = meshIt->Name();
            size_t len = strlen(name);

            // Hide LOD and wrinkle meshes
            if (strstr(name, "_lod") || strstr(name, "_wrinkle")) {
                meshIt->SetShowing(false);
                hidCount++;
                if (verbose) printf("  hide LOD/wrinkle mesh '%s'\n", name);
            }
            // Push combined meshes behind splits via depth bias to prevent z-fighting.
            // Combined mesh has arm geometry the splits lack, so we can't hide it.
            else if (len > 5 && strcmp(name + len - 5, ".mesh") == 0) {
                bool isSplit = false;
                for (size_t i = 0; i < len - 5; i++) {
                    if (name[i] == '.' && name[i+1] >= '1' && name[i+1] <= '9'
                        && (i + 2 >= len - 5 || name[i+2] == '.')) {
                        isSplit = true;
                        break;
                    }
                }
                if (!isSplit) {
                    char splitName[256];
                    snprintf(splitName, sizeof(splitName), "%.*s.1.mesh", (int)(len - 5), name);
                    RndMesh* split = baseScene->Find<RndMesh>(splitName, false);
                    if (split) {
                        extern void SetMeshDepthBias(RndMesh*, int32_t);
                        SetMeshDepthBias(&(*meshIt), 100);
                        if (verbose)
                            printf("  depth-bias combined mesh '%s'\n", name);
                    }
                }
            }
            ++meshIt;
        }
        if (hidCount > 0) {
            printf("Milo Viewer: hid %d meshes (LOD/wrinkle/combined)\n", hidCount);
        }
    }

    // ---- Print loaded object summary ----
    {
        int meshCount = 0, matCount = 0, texCount = 0, other = 0;
        ObjDirItr<Hmx::Object> it(baseScene, true);
        while (it) {
            const char* cn = it->ClassName().Str();
            if (strcmp(cn, "Mesh") == 0 || strcmp(cn, "RndMesh") == 0) meshCount++;
            else if (strcmp(cn, "Mat") == 0 || strcmp(cn, "RndMat") == 0) matCount++;
            else if (strcmp(cn, "Tex") == 0 || strcmp(cn, "RndTex") == 0) texCount++;
            else other++;
            ++it;
        }
        printf("Milo Viewer: %d meshes, %d materials, %d textures, %d other objects\n",
               meshCount, matCount, texCount, other);
    }

    // Debug: print drawable and mesh state (verbose only)
    if (verbose) {
        int drawableCount = 0;
        ObjDirItr<RndDrawable> drawIt(baseScene, true);
        while (drawIt) {
            if (drawableCount < 20) {
                printf("  drawable[%d]: '%s' class='%s' showing=%d\n",
                       drawableCount, drawIt->Name(), drawIt->ClassName().Str(), drawIt->Showing());
            }
            drawableCount++;
            ++drawIt;
        }
        printf("Milo Viewer: %d total drawables\n", drawableCount);

        ObjDirItr<RndMesh> meshIt(baseScene, true);
        int skinnedCount = 0;
        while (meshIt) {
            bool skinned = meshIt->IsSkinned();
            if (skinned) skinnedCount++;
            printf("  mesh '%s': showing=%d verts=%d faces=%d compressed=%d bones=%d mat=%s pos=%.1f,%.1f,%.1f\n",
                   meshIt->Name(), meshIt->Showing(),
                   meshIt->NumVerts(), meshIt->NumFaces(),
                   meshIt->NumCompressedVerts(), meshIt->NumBones(),
                   meshIt->Mat() ? meshIt->Mat()->Name() : "(none)",
                   meshIt->WorldXfm().v.x, meshIt->WorldXfm().v.y, meshIt->WorldXfm().v.z);
            ++meshIt;
        }
        if (skinnedCount > 0) {
            printf("Milo Viewer: %d skinned meshes detected\n", skinnedCount);
        }
    }

    // If the scene has a camera, mention it
    {
        ObjDirItr<RndCam> camItr(baseScene, true);
        if (camItr) {
            printf("Milo Viewer: found scene camera '%s', using orbit cam anyway\n",
                   camItr->Name());
        }
    }

    // ---- Scan for animation data ----
    {
        int transAnimCount = 0, propAnimCount = 0, otherAnimCount = 0;
        float globalStart = 1e10f, globalEnd = -1e10f;

        ObjDirItr<RndAnimatable> animIt(baseScene, true);
        while (animIt) {
            RndAnimatable* anim = animIt;
            float sf = anim->StartFrame();
            float ef = anim->EndFrame();

            // Skip animatables with no keyframes (StartFrame == EndFrame == 0)
            if (ef > sf) {
                if (sf < globalStart) globalStart = sf;
                if (ef > globalEnd) globalEnd = ef;
                gAnim.animatables.push_back(anim);

                if (verbose) {
                    const char* cn = anim->ClassName().Str();
                    printf("  anim '%s' class='%s' frames=[%.1f, %.1f]\n",
                           anim->Name(), cn, sf, ef);
                }
            }

            // Count by type
            if (dynamic_cast<RndTransAnim*>(anim)) transAnimCount++;
            else if (dynamic_cast<RndPropAnim*>(anim)) propAnimCount++;
            else otherAnimCount++;

            ++animIt;
        }

        gAnim.animCount = (int)gAnim.animatables.size();
        if (globalEnd > globalStart) {
            gAnim.hasAnimation = true;
            gAnim.startFrame = globalStart;
            gAnim.endFrame = globalEnd;
            gAnim.currentFrame = (startFrame >= 0.0f) ? startFrame : globalStart;
            gAnim.speed = animSpeed;
            gAnim.paused = startPaused;

            printf("Milo Viewer: %d animatables with keyframes (range [%.1f, %.1f] = %.1f frames)\n",
                   gAnim.animCount, gAnim.startFrame, gAnim.endFrame,
                   gAnim.endFrame - gAnim.startFrame);
            printf("  TransAnim: %d, PropAnim: %d, other: %d\n",
                   transAnimCount, propAnimCount, otherAnimCount);
            if (gAnim.paused) printf("  Starting paused\n");
            if (gAnim.speed != 1.0f) printf("  Speed: %.2fx\n", gAnim.speed);
        } else {
            printf("Milo Viewer: no animation data found (%d TransAnim, %d PropAnim, %d other — all empty)\n",
                   transAnimCount, propAnimCount, otherAnimCount);
        }
    }

    // Set window title to loaded filename
    if (window) {
        // Extract just the filename from the path
        const char* basename = strrchr(absPath, '/');
        basename = basename ? basename + 1 : absPath;
        char title[256];
        snprintf(title, sizeof(title), "DC3 Viewer — %s", basename);
        glfwSetWindowTitle(window, title);
    }

    // If the scene has an environment, activate it
    if (scene && scene->GetEnv()) {
        Vector3 origin;
        origin.Set(0, 0, 0);
        scene->GetEnv()->Select(&origin);
        printf("Milo Viewer: using scene environment '%s'\n", scene->GetEnv()->Name());
    }

    // ---- Auto-frame: compute bounding box from mesh positions and set orbit camera ----
    {
        float minX = 1e10f, minY = 1e10f, minZ = 1e10f;
        float maxX = -1e10f, maxY = -1e10f, maxZ = -1e10f;
        int meshCount = 0;

        ObjDirItr<RndMesh> bboxIt(baseScene, true);
        while (bboxIt) {
            RndMesh* m = bboxIt;
            if (!m->Showing()) { ++bboxIt; continue; }
            const Transform& xfm = m->WorldXfm();

            // Use mesh world position as approximate center
            float px = xfm.v.x, py = xfm.v.y, pz = xfm.v.z;

            // Try to compute actual vertex bounding box
            RndMesh* owner = m->GetGeomOwner();
            if (!owner) owner = m;

            int nv = owner->NumVerts();
            int ncv = owner->NumCompressedVerts();

            if (nv > 0) {
                for (int i = 0; i < nv; i++) {
                    const RndMesh::Vert& v = owner->Verts(i);
                    // Transform vertex by world matrix
                    float wx = xfm.m.x.x * v.pos.x + xfm.m.y.x * v.pos.y + xfm.m.z.x * v.pos.z + xfm.v.x;
                    float wy = xfm.m.x.y * v.pos.x + xfm.m.y.y * v.pos.y + xfm.m.z.y * v.pos.z + xfm.v.y;
                    float wz = xfm.m.x.z * v.pos.x + xfm.m.y.z * v.pos.y + xfm.m.z.z * v.pos.z + xfm.v.z;
                    if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
                    if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
                    if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
                }
            } else if (ncv > 0 && owner->CompressedVerts()) {
                // Compressed verts: unpack a few to get bounds
                const unsigned char* data = owner->CompressedVerts();
                struct CVert { int px, py, pz, n, c, t1, t2, b1, b2; }; // 36 bytes
                const CVert* cverts = (const CVert*)data;
                for (int i = 0; i < ncv; i++) {
                    // Big-endian floats stored as ints
                    unsigned int bx = __builtin_bswap32((unsigned int)cverts[i].px);
                    unsigned int by = __builtin_bswap32((unsigned int)cverts[i].py);
                    unsigned int bz = __builtin_bswap32((unsigned int)cverts[i].pz);
                    float fx, fy, fz;
                    memcpy(&fx, &bx, 4);
                    memcpy(&fy, &by, 4);
                    memcpy(&fz, &bz, 4);
                    // Transform by world
                    float wx = xfm.m.x.x * fx + xfm.m.y.x * fy + xfm.m.z.x * fz + xfm.v.x;
                    float wy = xfm.m.x.y * fx + xfm.m.y.y * fy + xfm.m.z.y * fz + xfm.v.y;
                    float wz = xfm.m.x.z * fx + xfm.m.y.z * fy + xfm.m.z.z * fz + xfm.v.z;
                    if (wx < minX) minX = wx; if (wx > maxX) maxX = wx;
                    if (wy < minY) minY = wy; if (wy > maxY) maxY = wy;
                    if (wz < minZ) minZ = wz; if (wz > maxZ) maxZ = wz;
                }
            } else {
                // No vertex data — use the transform position as a point
                if (px < minX) minX = px; if (px > maxX) maxX = px;
                if (py < minY) minY = py; if (py > maxY) maxY = py;
                if (pz < minZ) minZ = pz; if (pz > maxZ) maxZ = pz;
            }
            meshCount++;
            ++bboxIt;
        }

        if (meshCount > 0 && maxX > minX - 1e6f) {
            float cx = (minX + maxX) * 0.5f;
            float cy = (minY + maxY) * 0.5f;
            float cz = (minZ + maxZ) * 0.5f;
            float sx = maxX - minX;
            float sy = maxY - minY;
            float sz = maxZ - minZ;
            float extent = sqrtf(sx * sx + sy * sy + sz * sz) * 0.5f;
            if (extent < 0.01f) extent = 1.0f;

            gOrbitCam.targetX = cx;
            gOrbitCam.targetY = cy;
            gOrbitCam.targetZ = cz;
            // Use max single-axis extent for better framing of elongated objects
            float maxAxis = sx;
            if (sy > maxAxis) maxAxis = sy;
            if (sz > maxAxis) maxAxis = sz;
            gOrbitCam.distance = maxAxis * 2.0f;
            if (gOrbitCam.distance < extent * 1.5f) gOrbitCam.distance = extent * 1.5f;
            if (gOrbitCam.distance < 3.0f) gOrbitCam.distance = 3.0f;
            gOrbitCam.elevation = 0.3f;
            gOrbitCam.azimuth = 0.4f;

            printf("Milo Viewer: auto-frame bbox (%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) center=(%.2f,%.2f,%.2f) dist=%.2f\n",
                   minX, minY, minZ, maxX, maxY, maxZ, cx, cy, cz, gOrbitCam.distance);
        }
    }

    // Update frustum far plane to accommodate large scenes
    {
        float farDist = gOrbitCam.distance * 5.0f;
        if (farDist < 1000.0f) farDist = 1000.0f;
        float nearDist = farDist * 0.001f;
        if (nearDist < 0.1f) nearDist = 0.1f;
        cam->SetFrustum(nearDist, farDist, 0.6024f, 1.0f);
    }

    // Apply camera overrides from CLI args (after auto-framing)
    if (camAzimuthDeg > -900.0f) {
        gOrbitCam.azimuth = camAzimuthDeg * (3.14159265f / 180.0f);
    }
    if (camElevationDeg > -900.0f) {
        gOrbitCam.elevation = camElevationDeg * (3.14159265f / 180.0f);
    }
    if (camDistanceOverride > 0.0f) {
        gOrbitCam.distance = camDistanceOverride;
    }

    // Helper lambda: render one frame (draw all meshes from all loaded dirs)
    auto drawFrame = [&]() {
        // Find best environment from any loaded dir
        RndEnviron* env = nullptr;
        if (scene) env = scene->GetEnv();
        // Also check subdirs for environments
        if (!env) {
            for (auto& sd : subdirs) {
                RndDir* sdScene = dynamic_cast<RndDir*>((ObjectDir*)sd);
                if (sdScene && sdScene->GetEnv()) {
                    env = sdScene->GetEnv();
                    break;
                }
            }
        }

        Vector3 origin(0,0,0);
        RndEnvironTracker tracker(env, &origin);

        // Draw meshes from base scene
        ObjDirItr<RndMesh> meshIt(baseScene, true);
        while (meshIt) {
            meshIt->DrawShowing();
            ++meshIt;
        }

        // Draw meshes from subdirs
        for (auto& sd : subdirs) {
            ObjDirItr<RndMesh> sdMeshIt((ObjectDir*)sd, true);
            while (sdMeshIt) {
                sdMeshIt->DrawShowing();
                ++sdMeshIt;
            }
        }
    };

    // Helper lambda: advance character animation by beat
    // Advances incrementally in small steps to avoid huge delta issues
    float lastAnimSeconds = 0.0f;
    float lastAnimBeat = 0.0f;
    auto advanceCharAnim = [&](float targetSeconds, float targetBeat) {
        if (!charAnimActive || !charObj || !charObj->Driver()) return;
        // Advance in small steps (0.1 beat increments) to avoid huge delta
        float stepBeats = 0.1f;
        float stepSeconds = stepBeats * 60.0f / bpm;
        while (lastAnimBeat + stepBeats < targetBeat) {
            lastAnimBeat += stepBeats;
            lastAnimSeconds += stepSeconds;
            TheTaskMgr.SetSecondsAndBeat(lastAnimSeconds, lastAnimBeat, false);
            charObj->Driver()->Poll();
        }
        // Final step to exact target
        lastAnimBeat = targetBeat;
        lastAnimSeconds = targetSeconds;
        TheTaskMgr.SetSecondsAndBeat(targetSeconds, targetBeat, false);
        charObj->Driver()->Poll();
        // Apply bone transforms to mesh nodes
        if (activeServo) {
            activeServo->Poll();
        }
    };

    // ---- Screenshot mode: render a few frames then save and exit ----
    if (screenshotPath) {
        printf("Milo Viewer: rendering frames for screenshot...\n");

        // Apply animation frame if specified
        if (gAnim.hasAnimation && startFrame >= 0.0f) {
            printf("Milo Viewer: setting animation to frame %.1f\n", startFrame);

            for (auto* anim : gAnim.animatables) {
                anim->SetFrame(startFrame, 1.0f);
            }
        }

        // If character animation active, advance to a reasonable pose
        if (charAnimActive) {
            float beat = (startFrame >= 0.0f) ? startFrame : 4.0f;
            printf("Milo Viewer: advancing animation to beat %.1f (seconds=%.2f)\n", beat, beat * 60.0f / bpm);
            advanceCharAnim(beat * 60.0f / bpm, beat);

            // Re-center camera on animated character's pelvis bone
            RndTransformable* pelvis = charObj->Find<RndTransformable>("bone_pelvis.mesh", false);
            if (pelvis) {
                const Transform& bxfm = pelvis->WorldXfm();
                gOrbitCam.targetX = bxfm.v.x;
                gOrbitCam.targetY = bxfm.v.y;
                gOrbitCam.targetZ = bxfm.v.z;
                // Set sensible defaults for character viewing if no distance override
                // Character is ~72 units tall, so distance ~100 frames full body
                if (camDistanceOverride <= 0.0f) {
                    gOrbitCam.distance = 100.0f;
                }
                printf("Milo Viewer: centered on pelvis (%.2f, %.2f, %.2f) dist=%.1f\n",
                       bxfm.v.x, bxfm.v.y, bxfm.v.z, gOrbitCam.distance);
            }
        }

        // Render a few frames to let GPU resources settle
        for (int frame = 0; frame < 3; frame++) {
            gOrbitCam.Update(cam);
            TheRnd.BeginDrawing();
            drawFrame();
            TheRnd.EndDrawing();
        }

        // Readback the framebuffer
        int w = gWgpuRnd->Gpu().WindowWidth();
        int h = gWgpuRnd->Gpu().WindowHeight();
        size_t pixelSize = (size_t)w * h * 4;
        uint8_t* pixels = (uint8_t*)malloc(pixelSize);

        if (pixels && gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize)) {
            if (WriteScreenshot(screenshotPath, pixels, w, h)) {
                printf("Milo Viewer: screenshot saved to %s (%dx%d, PNG)\n", screenshotPath, w, h);
            } else {
                fprintf(stderr, "Error: failed to write screenshot to '%s'\n", screenshotPath);
            }
        } else {
            fprintf(stderr, "Error: failed to readback framebuffer (headless mode required)\n");
        }

        free(pixels);
        baseDir = nullptr;
        for (auto& sd : subdirs) sd = nullptr;
        clipsDir = nullptr;
        gWgpuRnd->Terminate();
        return 0;
    }

    // ---- Video recording mode: headless deterministic frame capture ----
    if (videoPath) {
        int w = gWgpuRnd->Gpu().WindowWidth();
        int h = gWgpuRnd->Gpu().WindowHeight();
        size_t pixelSize = (size_t)w * h * 4;
        uint8_t* pixels = (uint8_t*)malloc(pixelSize);

        if (!pixels) {
            fprintf(stderr, "Error: failed to allocate framebuffer (%d x %d)\n", w, h);
            return 1;
        }

        VideoEncoder encoder;
        if (!encoder.Start(videoPath, w, h, videoFps)) {
            free(pixels);
            return 1;
        }

        int totalFrames = (int)(videoDuration * videoFps);
        float dt = 1.0f / (float)videoFps;
        bool autoOrbit = (strcmp(cameraMode, "auto-orbit") == 0);

        printf("Milo Viewer: recording %d frames (%.1fs @ %d fps)...\n",
               totalFrames, videoDuration, videoFps);

        for (int frame = 0; frame < totalFrames; frame++) {
            float seconds = (float)frame * dt;
            float beat = seconds * (bpm / 60.0f) * gAnim.speed;

            // Advance character animation
            advanceCharAnim(seconds, beat);

            // Advance prop/TransAnim animations
            if (gAnim.hasAnimation) {
                float animFrame = gAnim.startFrame + fmodf(seconds * 30.0f * gAnim.speed,
                    gAnim.endFrame - gAnim.startFrame);
                for (auto* anim : gAnim.animatables) {
                    float sf = anim->StartFrame();
                    float ef = anim->EndFrame();
                    if (ef > sf) {
                        float r = ef - sf;
                        float f = fmodf(animFrame - sf, r);
                        if (f < 0.0f) f += r;
                        anim->SetFrame(f + sf, 1.0f);
                    }
                }
            }

            // Auto-orbit camera
            if (autoOrbit) {
                gOrbitCam.azimuth += 0.005f;
            }

            gOrbitCam.Update(cam);

            TheRnd.BeginDrawing();
            drawFrame();
            TheRnd.EndDrawing();

            // Readback and encode
            if (gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize)) {
                encoder.WriteFrame(pixels, pixelSize);
            } else {
                fprintf(stderr, "Error: framebuffer readback failed at frame %d\n", frame);
                break;
            }

            // Progress indicator
            if (frame > 0 && frame % (videoFps * 5) == 0) {
                printf("  encoded %d / %d frames (%.0f%%)\n",
                       frame, totalFrames, 100.0f * frame / totalFrames);
            }
        }

        encoder.Finish();
        free(pixels);
        printf("Milo Viewer: video saved to %s\n", videoPath);

        baseDir = nullptr;
        for (auto& sd : subdirs) sd = nullptr;
        clipsDir = nullptr;
        gWgpuRnd->Terminate();
        return 0;
    }

    // ---- Render loop (windowed mode) ----
    if (gAnim.hasAnimation) {
        printf("Milo Viewer: entering render loop — animation [%.0f..%.0f] at %.1fx "
               "(Space=pause, ./,=step, Up/Down=speed, Home=reset, ESC=quit)\n",
               gAnim.startFrame, gAnim.endFrame, gAnim.speed);
    } else {
        printf("Milo Viewer: entering render loop (press ESC to quit, R to reset camera)\n");
    }

    gAnim.lastTime = glfwGetTime();

    while (!gWgpuRnd->Gpu().ShouldClose()) {
        gWgpuRnd->Gpu().PollEvents();

        double now = glfwGetTime();
        double dt = now - gAnim.lastTime;
        gAnim.lastTime = now;

        // ---- Advance character animation (beat-based) ----
        if (charAnimActive && !gAnim.paused && dt > 0.0 && dt < 0.5) {
            float seconds = (float)now;
            float beat = seconds * (bpm / 60.0f) * gAnim.speed;
            advanceCharAnim(seconds, beat);
        }

        // ---- Advance prop/TransAnim animations (frame-based) ----
        if (gAnim.hasAnimation) {
            if (!gAnim.paused && dt > 0.0 && dt < 0.5) {
                float frameDelta = (float)dt * 30.0f * gAnim.speed;
                gAnim.currentFrame += frameDelta;

                // Loop within animation range
                float range = gAnim.endFrame - gAnim.startFrame;
                if (range > 0.0f && gAnim.currentFrame > gAnim.endFrame) {
                    gAnim.currentFrame = fmodf(gAnim.currentFrame - gAnim.startFrame, range) + gAnim.startFrame;
                }
            }

            // Apply current frame to all animatables
            for (auto* anim : gAnim.animatables) {
                float sf = anim->StartFrame();
                float ef = anim->EndFrame();
                float frame = gAnim.currentFrame;
                if (ef > sf) {
                    float r = ef - sf;
                    frame = fmodf(frame - sf, r);
                    if (frame < 0.0f) frame += r;
                    frame += sf;
                }
                anim->SetFrame(frame, 1.0f);
            }
        }

        // Update orbit camera
        gOrbitCam.Update(cam);

        // Draw — iterate meshes directly (bypass Character/RndDir complex draw logic)
        TheRnd.BeginDrawing();
        drawFrame();
        TheRnd.EndDrawing();
    }

    // ---- Cleanup ----
    printf("Milo Viewer: shutting down\n");
    clipsDir = nullptr;
    for (auto& sd : subdirs) sd = nullptr;
    baseDir = nullptr;  // release the loaded dir
    gWgpuRnd->Terminate();

    return 0;
}
