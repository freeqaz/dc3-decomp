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
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/PropAnim.h"
#include "char/Char.h"
#include "char/Character.h"
#include "char/CharDriver.h"
#include "char/CharClip.h"
#include "char/CharServoBone.h"
#include "char/CharPollable.h"
#include "math/Rot.h"
#include "math/Trig.h"
#include "char/CharBoneDir.h"
#include "char/CharUtl.h"
#include "char/CharBone.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "utl/FilePath.h"
#include "utl/MakeString.h"

#include "world/World.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "platform/Rnd_Wgpu.h"
#include "gfx/GpuDevice.h"
#include "gfx/Screenshot.h"
#include "gfx/VideoEncoder.h"
#include "export/TextureExporter.h"
#include "export/MaterialExporter.h"
#include "export/GltfExporter.h"

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
// Twist bone solver — replicates CharUpperTwist::Poll() and CharForeTwist::Poll()
// These CharPollable objects don't exist in outfit .milo files; they live in a
// shared character setup dir (char/main/gen/main.milo_xbox) which we don't load.
// ============================================================================
static void NormalizeAboutX_Viewer(Hmx::Matrix3& m) {
    Cross(m.x, m.y, m.z);
    Normalize(m.z, m.z);
    Cross(m.z, m.x, m.y);
}

// Distributes rotation across N intermediate bones between clavicle and the
// reference twist bone. bones[0] is closest to clavicle (twist2/reference),
// bones[1..N-1] are interpolated at even fractions toward clavicle's rest pose.
static void SolveUpperTwistChain(RndTransformable* refBone,
                                  RndTransformable** bones, int count) {
    if (!refBone || count == 0) return;
    RndTransformable* parent = refBone->TransParent();
    if (!parent) return;
    const Transform& parentWorld = parent->WorldXfm();
    const Transform& refWorld = refBone->WorldXfm();
    Hmx::Quat q;
    MakeRotQuat(parentWorld.m.x, refWorld.m.x, q);
    Vector3 rotatedY;
    Multiply(parentWorld.m.y, q, rotatedY);
    // Interpolate each bone: bone[i] gets fraction (i+1)/(count+1)
    // bone[0] is closest to parent → highest fraction (most rotated)
    // bone[count-1] is closest to ref bone → lowest fraction
    for (int i = 0; i < count; i++) {
        if (!bones[i]) continue;
        float frac = (float)(count - i) / (float)(count + 1);
        Transform tf;
        tf.m.x = refWorld.m.x;
        tf.v = bones[i]->WorldXfm().v;
        Interp(rotatedY, refWorld.m.y, frac, tf.m.y);
        NormalizeAboutX_Viewer(tf.m);
        bones[i]->SetWorldXfm(tf);
    }
}

// Replicates CharForeTwist::Poll() — distributes hand twist along forearm
static float LimitAng_Viewer(float ang) {
    float r = fmod(ang + PI, 2.0f * PI);
    return r < 0 ? r + PI : r - PI;
}

static void SolveForeTwist(RndTransformable* hand, RndTransformable* twist2,
                           float offset, float bias) {
    if (!hand || !twist2) return;
    float handAngle = GetXAngle(hand->LocalXfm().m);
    float twist = LimitAng_Viewer(handAngle + offset * DEG2RAD + bias * DEG2RAD) / 3.0f;
    Hmx::Matrix3 rotMat;
    rotMat.x.Set(1, 0, 0);
    rotMat.y.Set(0, Cosine(twist), Sine(twist));
    rotMat.z.Set(0, -Sine(twist), Cosine(twist));
    Multiply(twist2->LocalXfm().m, rotMat, twist2->DirtyLocalXfm().m);
    RndTransformable* twist1 = twist2->TransParent();
    if (twist1) {
        float twist1Ang = 2.0f * twist;
        rotMat.y.Set(0, Cosine(twist1Ang), Sine(twist1Ang));
        rotMat.z.Set(0, -Sine(twist1Ang), Cosine(twist1Ang));
        Multiply(twist1->LocalXfm().m, rotMat, twist1->DirtyLocalXfm().m);
    }
}

// Solve thigh twist — thighTwist01 interpolates between pelvis and thigh rotation
static void SolveThighTwist(RndTransformable* thighTwist, RndTransformable* thigh) {
    if (!thighTwist || !thigh) return;
    RndTransformable* parent = thigh->TransParent(); // pelvis
    if (!parent) return;
    const Transform& parentWorld = parent->WorldXfm();
    const Transform& thighWorld = thigh->WorldXfm();
    Hmx::Quat q;
    MakeRotQuat(parentWorld.m.x, thighWorld.m.x, q);
    Vector3 rotatedY;
    Multiply(parentWorld.m.y, q, rotatedY);
    Transform tf;
    tf.m.x = thighWorld.m.x;
    tf.v = thighTwist->WorldXfm().v;
    Interp(rotatedY, thighWorld.m.y, 0.5f, tf.m.y);
    NormalizeAboutX_Viewer(tf.m);
    thighTwist->SetWorldXfm(tf);
}

// Solve all twist bones for a character
static void SolveAllTwists(ObjectDir* dir) {
    if (!dir) return;

    // Upper arm twists: shoulderTwist2 (closest to clavicle), 3, 4 (closest to upperArm)
    // Reference bone is shoulderTwist2 (has the full twist from clip animation)
    // We interpolate shoulderTwist3 and shoulderTwist4 between clavicle and twist2
    const char* sides[] = {"L", "R"};
    for (auto side : sides) {
        char refName[64], b3Name[64], b4Name[64];
        snprintf(refName, sizeof(refName), "bone_%s-shoulderTwist2.mesh", side);
        snprintf(b3Name, sizeof(b3Name), "bone_%s-shoulderTwist3.mesh", side);
        snprintf(b4Name, sizeof(b4Name), "bone_%s-shoulderTwist4.mesh", side);
        RndTransformable* ref = dir->Find<RndTransformable>(refName, false);
        RndTransformable* bones[2] = {
            dir->Find<RndTransformable>(b3Name, false),
            dir->Find<RndTransformable>(b4Name, false),
        };
        SolveUpperTwistChain(ref, bones, 2);
    }

    // Forearm twists (left offset=0/bias=45, right offset=180/bias=-45)
    struct ForeTwistSetup {
        const char* hand; const char* twist2; float offset; float bias;
    };
    ForeTwistSetup foreSetups[] = {
        {"bone_L-hand.mesh", "bone_L-foreTwist2.mesh", 0.0f, 45.0f},
        {"bone_R-hand.mesh", "bone_R-foreTwist2.mesh", 180.0f, -45.0f},
    };
    for (auto& s : foreSetups) {
        SolveForeTwist(
            dir->Find<RndTransformable>(s.hand, false),
            dir->Find<RndTransformable>(s.twist2, false),
            s.offset, s.bias);
    }

    // Thigh twists
    for (auto side : sides) {
        char twistName[64], thighName[64];
        snprintf(twistName, sizeof(twistName), "bone_%s-thighTwist01.mesh", side);
        snprintf(thighName, sizeof(thighName), "bone_%s-thigh.mesh", side);
        SolveThighTwist(
            dir->Find<RndTransformable>(twistName, false),
            dir->Find<RndTransformable>(thighName, false));
    }
}

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
        fprintf(f, "  --hide <pattern>           Hide meshes matching substring (repeatable)\n");
        fprintf(f, "  --azimuth <degrees>        Camera azimuth angle (default: ~23)\n");
        fprintf(f, "  --elevation <degrees>      Camera elevation angle (default: ~17)\n");
        fprintf(f, "  --frame <number>           Start at specific animation frame\n");
        fprintf(f, "  --speed <multiplier>       Animation speed (default: 1.0)\n");
        fprintf(f, "  --paused                   Start with animation paused\n");
        fprintf(f, "  --width <pixels>           Render width (default: 1280)\n");
        fprintf(f, "  --height <pixels>          Render height (default: 720)\n");
        fprintf(f, "  --verbose, -v              Print detailed object/drawable info\n");
        fprintf(f, "  --export-textures <dir>    Export all textures as PNG and exit\n");
        fprintf(f, "  --export-materials <dir>   Export all materials as JSON and exit\n");
        fprintf(f, "  --export-gltf <path>       Export scene as glTF 2.0 and exit\n\n");
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
    struct SubdirEntry {
        std::string path;
        float offsetX = 0, offsetY = 0, offsetZ = 0;
        float rotateDeg = 0; // rotation around Z axis (up)
    };
    std::vector<SubdirEntry> subdirEntries;
    float camAzimuthDeg = -999.0f;  // sentinel: use default
    float camElevationDeg = -999.0f;
    float camDistanceOverride = -1.0f;  // sentinel: use auto
    float eyeX = 0, eyeY = 0, eyeZ = 0;
    float lookX = 0, lookY = 0, lookZ = 0;
    bool hasEye = false, hasLookat = false;
    float startFrame = -1.0f;       // sentinel: use default
    float animSpeed = 1.0f;
    const char* testBoneName = nullptr;  // --test-bone: manually rotate a bone
    float testBoneAngle = 45.0f;         // degrees around X axis
    const char* testBoneAxis = "x";      // x, y, or z
    float bpm = 120.0f;
    float videoDuration = 10.0f;
    int videoFps = 30;
    bool startPaused = false;
    bool verbose = false;
    const char* exportTexturesDir = nullptr;
    const char* exportMaterialsDir = nullptr;
    const char* exportGltfPath = nullptr;
    bool dumpBones = false;  // --dump-bones: dump raw bone buffer after clip eval
    bool directPose = false; // --direct-pose: use CharClip::PoseMeshes instead of CharDriver
    std::vector<std::string> hidePatterns;  // mesh name substrings to hide
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            showHelp(stdout);
            return 0;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--subdir") == 0 && i + 1 < argc) {
            SubdirEntry e;
            e.path = argv[++i];
            // Check for optional offset: --subdir-offset X Y Z
            // Check for optional modifiers after --subdir <path>
            while (i + 1 < argc) {
                if (strcmp(argv[i + 1], "--subdir-offset") == 0 && i + 4 < argc) {
                    i++;
                    e.offsetX = (float)atof(argv[++i]);
                    e.offsetY = (float)atof(argv[++i]);
                    e.offsetZ = (float)atof(argv[++i]);
                } else if (strcmp(argv[i + 1], "--subdir-rotate") == 0 && i + 2 < argc) {
                    i++;
                    e.rotateDeg = (float)atof(argv[++i]);
                } else {
                    break;
                }
            }
            subdirEntries.push_back(e);
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
        } else if (strcmp(argv[i], "--eye") == 0 && i + 3 < argc) {
            eyeX = (float)atof(argv[++i]);
            eyeY = (float)atof(argv[++i]);
            eyeZ = (float)atof(argv[++i]);
            hasEye = true;
        } else if (strcmp(argv[i], "--lookat") == 0 && i + 3 < argc) {
            lookX = (float)atof(argv[++i]);
            lookY = (float)atof(argv[++i]);
            lookZ = (float)atof(argv[++i]);
            hasLookat = true;
        } else if (strcmp(argv[i], "--test-bone") == 0 && i + 2 < argc) {
            testBoneName = argv[++i];
            testBoneAngle = (float)atof(argv[++i]);
            if (i + 1 < argc && (strcmp(argv[i+1], "x") == 0 || strcmp(argv[i+1], "y") == 0 || strcmp(argv[i+1], "z") == 0)) {
                testBoneAxis = argv[++i];
            }
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
        } else if (strcmp(argv[i], "--export-textures") == 0 && i + 1 < argc) {
            exportTexturesDir = argv[++i];
        } else if (strcmp(argv[i], "--export-materials") == 0 && i + 1 < argc) {
            exportMaterialsDir = argv[++i];
        } else if (strcmp(argv[i], "--export-gltf") == 0 && i + 1 < argc) {
            exportGltfPath = argv[++i];
        } else if (strcmp(argv[i], "--hide") == 0 && i + 1 < argc) {
            hidePatterns.push_back(argv[++i]);
        } else if (strcmp(argv[i], "--dump-bones") == 0) {
            dumpBones = true;
        } else if (strcmp(argv[i], "--direct-pose") == 0) {
            directPose = true;
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

    // Enable GPU rendering unless doing export-only
    bool exportOnly = exportTexturesDir || exportMaterialsDir || exportGltfPath;
    if (exportOnly) {
        setenv("MILO_RENDER", "0", 1);
    } else {
        setenv("MILO_RENDER", "1", 1);
    }
    // Force headless if screenshot/video mode or no display available
    if (screenshotPath || videoPath) {
        setenv("MILO_HEADLESS", "1", 1);
    }

    printf("Milo Viewer: SystemPreInit...\n");
    InitMakeString();  // Must happen before SystemPreInit (SetSystemArgs uses MakeString)
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
    for (const auto& entry : subdirEntries) {
        char sdAbsPath[PATH_MAX];
        if (!realpath(entry.path.c_str(), sdAbsPath)) {
            fprintf(stderr, "Warning: cannot resolve subdir path '%s', skipping\n", entry.path.c_str());
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

        // Apply position offset and/or rotation to all transformables
        if (entry.offsetX != 0 || entry.offsetY != 0 || entry.offsetZ != 0 || entry.rotateDeg != 0) {
            int moved = 0;
            float rad = entry.rotateDeg * (3.14159265f / 180.0f);
            float cosR = cosf(rad), sinR = sinf(rad);
            ObjDirItr<RndTransformable> xfmIt(sdDir, true);
            while (xfmIt) {
                RndTransformable* t = xfmIt;
                Transform wxfm = t->WorldXfm();
                // Rotate around Z axis (up in Milo)
                if (entry.rotateDeg != 0) {
                    float ox = wxfm.v.x, oy = wxfm.v.y;
                    wxfm.v.x = ox * cosR - oy * sinR;
                    wxfm.v.y = ox * sinR + oy * cosR;
                    // Rotate orientation vectors too
                    float xx = wxfm.m.x.x, xy = wxfm.m.x.y;
                    wxfm.m.x.x = xx * cosR - xy * sinR;
                    wxfm.m.x.y = xx * sinR + xy * cosR;
                    float yx = wxfm.m.y.x, yy = wxfm.m.y.y;
                    wxfm.m.y.x = yx * cosR - yy * sinR;
                    wxfm.m.y.y = yx * sinR + yy * cosR;
                    float zx = wxfm.m.z.x, zy = wxfm.m.z.y;
                    wxfm.m.z.x = zx * cosR - zy * sinR;
                    wxfm.m.z.y = zx * sinR + zy * cosR;
                }
                wxfm.v.x += entry.offsetX;
                wxfm.v.y += entry.offsetY;
                wxfm.v.z += entry.offsetZ;
                t->SetWorldXfm(wxfm);
                moved++;
                ++xfmIt;
            }
            printf("Milo Viewer: transformed subdir (offset=%.1f,%.1f,%.1f rot=%.1f°) — %d objects\n",
                   entry.offsetX, entry.offsetY, entry.offsetZ, entry.rotateDeg, moved);
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

    // ---- Export-and-exit modes ----
    if (exportOnly) {
        if (exportTexturesDir) {
            TextureExporter::Options texOpts;
            texOpts.verbose = verbose;
            int count = TextureExporter::ExportAll(baseScene, exportTexturesDir, texOpts);
            printf("Exported %d textures to %s\n", count, exportTexturesDir);
        }
        if (exportMaterialsDir) {
            MaterialExporter::Options matOpts;
            matOpts.verbose = verbose;
            int count = MaterialExporter::ExportAll(baseScene, exportMaterialsDir, matOpts);
            printf("Exported %d materials to %s\n", count, exportMaterialsDir);
        }
        if (exportGltfPath) {
            GltfExporter::Options gltfOpts;
            gltfOpts.verbose = verbose;
            bool ok = GltfExporter::Export(baseScene, exportGltfPath, gltfOpts);
            if (ok) printf("Exported glTF to %s\n", exportGltfPath);
            else fprintf(stderr, "Error: glTF export failed\n");
        }
        return 0;
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

                        // Diagnostic: dump clip data layout
                        {
                            const auto& full = clipToPlay->GetFull();
                            printf("=== CLIP DATA DIAGNOSTIC ===\n");
                            printf("  mFull: compression=%d samples=%d totalSize=%d\n",
                                   full.GetCompression(), full.NumSamples(), full.TotalSize());
                            printf("  mFull offsets: POS=%d SCALE=%d QUAT=%d ROTX=%d ROTY=%d ROTZ=%d END=%d\n",
                                   full.GetOffset(CharBones::TYPE_POS),
                                   full.GetOffset(CharBones::TYPE_SCALE),
                                   full.GetOffset(CharBones::TYPE_QUAT),
                                   full.GetOffset(CharBones::TYPE_ROTX),
                                   full.GetOffset(CharBones::TYPE_ROTY),
                                   full.GetOffset(CharBones::TYPE_ROTZ),
                                   full.GetOffset(CharBones::TYPE_END));
                            printf("  mFull bones: %d, start=%p\n",
                                   (int)const_cast<CharBonesSamples&>(full).GetBones().size(), full.GetStart());
                            // Dump first sample's raw position data
                            if (full.NumSamples() > 0 && full.GetStart()) {
                                const unsigned char* raw = (const unsigned char*)full.GetStart();
                                printf("  sample0 first 32 bytes: ");
                                for (int b = 0; b < 32 && b < full.TotalSize(); b++)
                                    printf("%02x ", raw[b]);
                                printf("\n");
                                // Interpret first position based on compression
                                if (full.GetCompression() >= CharBones::kCompressVects) {
                                    const short* sp = (const short*)raw;
                                    printf("  sample0 pos0 (short): %d %d %d -> %.4f %.4f %.4f\n",
                                           sp[0], sp[1], sp[2],
                                           sp[0]*0.039674062f, sp[1]*0.039674062f, sp[2]*0.039674062f);
                                } else {
                                    const float* fp = (const float*)raw;
                                    printf("  sample0 pos0 (float): %.4f %.4f %.4f\n", fp[0], fp[1], fp[2]);
                                }
                            }
                            const auto& one = clipToPlay->GetOne();
                            printf("  mOne: compression=%d samples=%d totalSize=%d bones=%d\n",
                                   one.GetCompression(), one.NumSamples(), one.TotalSize(),
                                   (int)const_cast<CharBonesSamples&>(one).GetBones().size());
                            printf("=== END DIAGNOSTIC ===\n");
                        }

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

                            // Diagnostic removed — all skeleton bones resolve correctly
                        }

                        // Dump T-pose bone transforms before animation starts
                        {
                            RndTransformable* pelvis = charObj->Find<RndTransformable>("bone_pelvis.mesh", false);
                            RndTransformable* lthigh = charObj->Find<RndTransformable>("bone_L-thigh.mesh", false);
                            RndTransformable* lknee = charObj->Find<RndTransformable>("bone_L-knee.mesh", false);
                            printf("=== T-POSE REFERENCE ===\n");
                            if (pelvis) {
                                printf("  pelvis local.v=(%7.2f,%7.2f,%7.2f) world.v=(%7.2f,%7.2f,%7.2f)\n",
                                       pelvis->LocalXfm().v.x, pelvis->LocalXfm().v.y, pelvis->LocalXfm().v.z,
                                       pelvis->WorldXfm().v.x, pelvis->WorldXfm().v.y, pelvis->WorldXfm().v.z);
                                printf("  pelvis local.m.x=(%6.3f,%6.3f,%6.3f) .y=(%6.3f,%6.3f,%6.3f) .z=(%6.3f,%6.3f,%6.3f)\n",
                                       pelvis->LocalXfm().m.x.x, pelvis->LocalXfm().m.x.y, pelvis->LocalXfm().m.x.z,
                                       pelvis->LocalXfm().m.y.x, pelvis->LocalXfm().m.y.y, pelvis->LocalXfm().m.y.z,
                                       pelvis->LocalXfm().m.z.x, pelvis->LocalXfm().m.z.y, pelvis->LocalXfm().m.z.z);
                            }
                            if (lthigh) {
                                printf("  L-thigh local.v=(%7.2f,%7.2f,%7.2f) world.v=(%7.2f,%7.2f,%7.2f)\n",
                                       lthigh->LocalXfm().v.x, lthigh->LocalXfm().v.y, lthigh->LocalXfm().v.z,
                                       lthigh->WorldXfm().v.x, lthigh->WorldXfm().v.y, lthigh->WorldXfm().v.z);
                            }
                            if (lknee) {
                                printf("  L-knee local.v=(%7.2f,%7.2f,%7.2f) world.v=(%7.2f,%7.2f,%7.2f)\n",
                                       lknee->LocalXfm().v.x, lknee->LocalXfm().v.y, lknee->LocalXfm().v.z,
                                       lknee->WorldXfm().v.x, lknee->WorldXfm().v.y, lknee->WorldXfm().v.z);
                            }
                            printf("=== END T-POSE ===\n");
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
    // When a Character exists with subdirs loaded, frame on character only
    {
        float minX = 1e10f, minY = 1e10f, minZ = 1e10f;
        float maxX = -1e10f, maxY = -1e10f, maxZ = -1e10f;
        int meshCount = 0;

        // When character + venue: frame on character (skip venue meshes)
        bool charFraming = charObj && !subdirEntries.empty();
        ObjDirItr<RndMesh> bboxIt(baseScene, !charFraming);
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

    // Direct camera placement (--eye / --lookat) — bypasses orbit camera for this frame
    if (hasEye) {
        // Set orbit target to lookat point (or bbox center if no lookat)
        if (hasLookat) {
            gOrbitCam.targetX = lookX;
            gOrbitCam.targetY = lookY;
            gOrbitCam.targetZ = lookZ;
        }
        // Compute distance and angles from eye to target
        float dx = eyeX - gOrbitCam.targetX;
        float dy = eyeY - gOrbitCam.targetY;
        float dz = eyeZ - gOrbitCam.targetZ;
        gOrbitCam.distance = sqrtf(dx*dx + dy*dy + dz*dz);
        if (gOrbitCam.distance < 0.01f) gOrbitCam.distance = 1.0f;
        gOrbitCam.azimuth = atan2f(dx, dy);
        gOrbitCam.elevation = asinf(dz / gOrbitCam.distance);
        printf("Milo Viewer: eye=(%.1f,%.1f,%.1f) lookat=(%.1f,%.1f,%.1f) dist=%.1f az=%.1f° el=%.1f°\n",
               eyeX, eyeY, eyeZ, gOrbitCam.targetX, gOrbitCam.targetY, gOrbitCam.targetZ,
               gOrbitCam.distance, gOrbitCam.azimuth * 57.2958f, gOrbitCam.elevation * 57.2958f);
    }

    // Helper: check if a mesh should be hidden (name pattern match)
    auto shouldHideMesh = [&](RndMesh* mesh) -> bool {
        for (auto& pat : hidePatterns) {
            if (strstr(mesh->Name(), pat.c_str())) return true;
        }
        return false;
    };

    // Helper: check if a venue/subdir mesh has unresolved render-target texture
    // (shows as giant white block). Only use for subdir meshes, not base character.
    extern wgpu::TextureView GetGpuTexView(RndTex* tex);
    auto hasUnresolvedTexture = [&](RndMesh* mesh) -> bool {
        RndMat* mat = mesh->Mat();
        if (mat) {
            RndTex* diffTex = mat->GetDiffuseTex();
            if (diffTex && !GetGpuTexView(diffTex)) {
                return true;
            }
        }
        return false;
    };

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
            if (!shouldHideMesh(meshIt)) {
                meshIt->DrawShowing();
            }
            ++meshIt;
        }

        // Draw meshes from subdirs (also filter unresolved render-target textures)
        for (auto& sd : subdirs) {
            ObjDirItr<RndMesh> sdMeshIt((ObjectDir*)sd, true);
            while (sdMeshIt) {
                if (!shouldHideMesh(sdMeshIt) && !hasUnresolvedTexture(sdMeshIt)) {
                    sdMeshIt->DrawShowing();
                }
                ++sdMeshIt;
            }
        }
    };

    // Helper lambda: advance character animation by beat
    // Advances incrementally in small steps to avoid huge delta issues
    float lastAnimSeconds = 0.0f;
    float lastAnimBeat = 0.0f;
    // Collect all CharPollable objects for per-frame polling
    // These include CharDriver, CharServoBone, CharUpperTwist, CharForeTwist,
    // CharBoneTwist, CharNeckTwist — all needed for proper bone animation
    std::vector<CharPollable*> charPollables;

    auto advanceCharAnim = [&](float targetSeconds, float targetBeat) {
        if (!charAnimActive || !charObj || !charObj->Driver()) return;

        // Collect pollables on first call
        if (charPollables.empty()) {
            ObjDirItr<CharPollable> it(charObj, false);
            for (; it != nullptr; ++it) {
                charPollables.push_back(it);
                printf("Milo Viewer: found CharPollable '%s' (%s)\n",
                       it->Name(), it->ClassName());
            }
        }

        // Advance in small steps (0.1 beat increments) to avoid huge delta
        float stepBeats = 0.1f;
        float stepSeconds = stepBeats * 60.0f / bpm;
        while (lastAnimBeat + stepBeats < targetBeat) {
            lastAnimBeat += stepBeats;
            lastAnimSeconds += stepSeconds;
            TheTaskMgr.SetSecondsAndBeat(lastAnimSeconds, lastAnimBeat, false);
            for (auto* p : charPollables) p->Poll();
        }
        // Final step to exact target
        lastAnimBeat = targetBeat;
        lastAnimSeconds = targetSeconds;
        TheTaskMgr.SetSecondsAndBeat(targetSeconds, targetBeat, false);
        for (auto* p : charPollables) p->Poll();
        // Solve twist bones (CharUpperTwist/CharForeTwist not in outfit .milo)
        SolveAllTwists(charObj);
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

        // Test bone: manually rotate a specific bone from T-pose
        if (testBoneName && baseScene) {
            RndTransformable* bone = baseScene->Find<RndTransformable>(testBoneName, true);
            if (bone) {
                float rad = testBoneAngle * (3.14159265f / 180.0f);
                float c = cosf(rad), s = sinf(rad);
                Transform& tf = bone->DirtyLocalXfm();
                Hmx::Matrix3 rot;
                rot.Zero();
                if (strcmp(testBoneAxis, "x") == 0) {
                    rot.x.x = 1; rot.y.y = c; rot.y.z = s; rot.z.y = -s; rot.z.z = c;
                } else if (strcmp(testBoneAxis, "y") == 0) {
                    rot.x.x = c; rot.x.z = -s; rot.y.y = 1; rot.z.x = s; rot.z.z = c;
                } else {
                    rot.x.x = c; rot.x.y = s; rot.y.x = -s; rot.y.y = c; rot.z.z = 1;
                }
                // Apply rotation: new_m = rot * old_m
                Hmx::Matrix3 oldm = tf.m;
                Multiply(rot, oldm, tf.m);
                printf("Milo Viewer: test-bone '%s' rotated %.1f deg around %s\n",
                       testBoneName, testBoneAngle, testBoneAxis);
            } else {
                printf("Milo Viewer: WARNING: bone '%s' not found\n", testBoneName);
            }
        }

        // If character animation active, advance to a reasonable pose
        if (charAnimActive) {
            float beat = (startFrame >= 0.0f) ? startFrame : 4.0f;
            printf("Milo Viewer: advancing animation to beat %.1f (seconds=%.2f)\n", beat, beat * 60.0f / bpm);

            if (activeClip && !directPose) {
                // Direct pose: bypass CharDriver, use CharClip::PoseMeshes
                // This avoids the CharServoBone facing system compounding
                // transforms over hundreds of incremental steps
                printf("Milo Viewer: using CharClip::PoseMeshes(dir, %.1f)\n", beat);
                activeClip->PoseMeshes(charObj, beat);
                SolveAllTwists(charObj);
            } else {
                advanceCharAnim(beat * 60.0f / bpm, beat);
            }

            // Dump raw bone buffer values and mesh transforms
            if (dumpBones && activeServo) {
                float dumpBeat = (startFrame >= 0.0f) ? startFrame : 4.0f;
                printf("=== RAW BONE BUFFER DUMP (beat %.1f) ===\n", dumpBeat);

                auto bones = activeServo->GetBones();
                char* start = activeServo->GetStart();
                int posEnd = activeServo->GetOffset(CharBones::TYPE_SCALE);
                int scaleEnd = activeServo->GetOffset(CharBones::TYPE_QUAT);
                int quatEnd = activeServo->GetOffset(CharBones::TYPE_ROTX);
                int rotxEnd = activeServo->GetOffset(CharBones::TYPE_ROTY);
                int rotyEnd = activeServo->GetOffset(CharBones::TYPE_ROTZ);
                int rotzEnd = activeServo->GetOffset(CharBones::TYPE_END);

                printf("  Buffer layout: POS[0..%d] SCALE[%d..%d] QUAT[%d..%d] ROTX[%d..%d] ROTY[%d..%d] ROTZ[%d..%d]\n",
                       posEnd, posEnd, scaleEnd, scaleEnd, quatEnd, quatEnd, rotxEnd, rotxEnd, rotyEnd, rotyEnd, rotzEnd);
                printf("  Bone count: %d\n", (int)bones.size());

                // Dump raw buffer values AND resulting mesh transforms
                printf("\n  --- POSITIONS ---\n");
                Vector3* posData = (Vector3*)start;
                int numPos = posEnd / (int)sizeof(Vector3);
                for (int i = 0; i < numPos && i < (int)bones.size(); i++) {
                    RndTransformable* mesh = CharUtlFindBoneTrans(bones[i].name.Str(), charObj);
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
                int quatBoneIdx = posEnd / (int)sizeof(Vector3);  // bones after pos (no scales here since scaleEnd==posEnd)
                for (int i = 0; i < numQuat; i++) {
                    int bi = quatBoneIdx + i;
                    if (bi >= (int)bones.size()) break;
                    RndTransformable* mesh = CharUtlFindBoneTrans(bones[bi].name.Str(), charObj);
                    printf("  [%2d] %-35s quat=(%7.4f,%7.4f,%7.4f,%7.4f) w=%.3f",
                           bi, bones[bi].name.Str(),
                           quatData[i].x, quatData[i].y, quatData[i].z, quatData[i].w,
                           bones[bi].weight);
                    if (mesh) {
                        const Hmx::Matrix3& m = mesh->LocalXfm().m;
                        printf("  mesh='%s' m.x=(%6.3f,%6.3f,%6.3f)", mesh->Name(), m.x.x, m.x.y, m.x.z);
                    }
                    printf("\n");
                }

                printf("\n  --- ROTZ ---\n");
                float* rotzData = (float*)(start + rotyEnd);
                int numRotz = (rotzEnd - rotyEnd) / (int)sizeof(float);
                int rotzBoneIdx = quatBoneIdx + numQuat;  // after quat bones (no rotx/roty here)
                for (int i = 0; i < numRotz; i++) {
                    int bi = rotzBoneIdx + i;
                    if (bi >= (int)bones.size()) break;
                    RndTransformable* mesh = CharUtlFindBoneTrans(bones[bi].name.Str(), charObj);
                    printf("  [%2d] %-35s rotz=%8.4f (%.1f deg) w=%.3f",
                           bi, bones[bi].name.Str(), rotzData[i], rotzData[i] * 57.2958f, bones[bi].weight);
                    if (mesh) {
                        printf("  mesh='%s'", mesh->Name());
                    }
                    printf("\n");
                }
                printf("=== END BONE BUFFER DUMP ===\n");
            }

            // Re-center camera on animated character's pelvis bone
            // Skip if user specified --eye (manual camera placement)
            RndTransformable* pelvis = charObj->Find<RndTransformable>("bone_pelvis.mesh", false);
            if (pelvis && !hasEye) {
                const Transform& bxfm = pelvis->WorldXfm();
                gOrbitCam.targetX = bxfm.v.x;
                gOrbitCam.targetY = bxfm.v.y;
                gOrbitCam.targetZ = bxfm.v.z;
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

        // Track pelvis bone for camera centering during video
        RndTransformable* pelvisBone = nullptr;
        if (charObj && charAnimActive) {
            pelvisBone = charObj->Find<RndTransformable>("bone_pelvis.mesh", false);
        }

        // Pre-advance character to a reasonable pose (beat 4) for initial camera setup
        if (charAnimActive && activeClip) {
            activeClip->PoseMeshes(charObj, 20.0f); // use a beat within clip range
            SolveAllTwists(charObj);

            // Center camera on pelvis for initial framing
            if (pelvisBone && !hasEye) {
                const Transform& bxfm = pelvisBone->WorldXfm();
                gOrbitCam.targetX = bxfm.v.x;
                gOrbitCam.targetY = bxfm.v.y;
                gOrbitCam.targetZ = bxfm.v.z;
                printf("Milo Viewer: video centered on pelvis (%.2f, %.2f, %.2f)\n",
                       bxfm.v.x, bxfm.v.y, bxfm.v.z);
            }
        }

        printf("Milo Viewer: recording %d frames (%.1fs @ %d fps)...\n",
               totalFrames, videoDuration, videoFps);

        for (int frame = 0; frame < totalFrames; frame++) {
            float seconds = (float)frame * dt;
            float beat = seconds * (bpm / 60.0f) * gAnim.speed;

            // Advance character animation using direct pose (no incremental stepping)
            if (charAnimActive && activeClip) {
                // Clamp beat to clip range for looping
                float clipStart = activeClip->StartBeat();
                float clipEnd = activeClip->EndBeat();
                float clipLen = clipEnd - clipStart;
                float clipBeat = clipStart + fmodf(beat, clipLen);
                if (clipBeat < clipStart) clipBeat += clipLen;
                activeClip->PoseMeshes(charObj, clipBeat);
                SolveAllTwists(charObj);
            }

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

            // Track pelvis bone — snap to keep character centered
            if (pelvisBone && !hasEye) {
                const Transform& bxfm = pelvisBone->WorldXfm();
                gOrbitCam.targetX = bxfm.v.x;
                gOrbitCam.targetY = bxfm.v.y;
                gOrbitCam.targetZ = bxfm.v.z;
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
    bool interactiveAutoOrbit = (strcmp(cameraMode, "auto-orbit") == 0);

    // Track pelvis for interactive camera follow
    RndTransformable* interactivePelvis = nullptr;
    if (charObj && charAnimActive) {
        interactivePelvis = charObj->Find<RndTransformable>("bone_pelvis.mesh", false);
    }

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

        // Snap camera to pelvis in interactive mode (unless user is dragging)
        if (interactivePelvis && !hasEye && !gOrbitCam.leftDrag && !gOrbitCam.middleDrag) {
            const Transform& bxfm = interactivePelvis->WorldXfm();
            gOrbitCam.targetX = bxfm.v.x;
            gOrbitCam.targetY = bxfm.v.y;
            gOrbitCam.targetZ = bxfm.v.z;
        }

        // Auto-orbit in interactive mode
        if (interactiveAutoOrbit && !gAnim.paused) {
            gOrbitCam.azimuth += 0.002f * (float)dt * 60.0f; // ~0.12 rad/s, frame-rate independent
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
