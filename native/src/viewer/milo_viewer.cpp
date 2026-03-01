// DC3 Native Port — Milo Viewer
// Standalone tool: loads a .milo_xbox file and renders it with an orbit camera.
// Usage: milo-viewer <path-to-file.milo_xbox> [--screenshot <output.ppm>]

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Cam.h"
#include "rndobj/Dir.h"
#include "rndobj/Env.h"
#include "rndobj/Rnd.h"
#include "rndobj/Trans.h"
#include "rndobj/Mesh.h"
#include "math/Mtx.h"
#include "math/Vec.h"
#include "utl/FilePath.h"

#include "world/World.h"
#include "char/Char.h"
#include "hamobj/Ham.h"
#include "flow/Flow.h"
#include "platform/Rnd_Wgpu.h"
#include "gfx/GpuDevice.h"

#include <GLFW/glfw3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

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

        // Camera position from spherical coordinates
        float cosElev = cosf(elevation);
        float eyeX = targetX + distance * cosElev * sinf(azimuth);
        float eyeY = targetY + distance * sinf(elevation);
        float eyeZ = targetZ + distance * cosElev * cosf(azimuth);

        // Build look-at vectors
        Vector3 eye, tgt, fwd, right, up;
        eye.Set(eyeX, eyeY, eyeZ);
        tgt.Set(targetX, targetY, targetZ);
        Subtract(tgt, eye, fwd);
        Normalize(fwd, fwd);

        // Handle near-vertical case
        Vector3 worldUp;
        worldUp.Set(0, 1, 0);
        Cross(fwd, worldUp, right);
        float rightLen = Length(right);
        if (rightLen < 0.001f) {
            worldUp.Set(0, 0, 1);
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
// GLFW Callbacks
// ============================================================================
static OrbitCamera gOrbitCam;

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
// Write PPM image file (simple, no dependencies)
// ============================================================================
static bool WritePPM(const char* path, const uint8_t* rgba, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        fputc(rgba[i * 4 + 0], f);  // R
        fputc(rgba[i * 4 + 1], f);  // G
        fputc(rgba[i * 4 + 2], f);  // B
    }
    fclose(f);
    return true;
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
        fprintf(f, "  --screenshot <file.ppm>    Render headlessly and save screenshot\n");
        fprintf(f, "  --azimuth <degrees>        Camera azimuth angle (default: ~23)\n");
        fprintf(f, "  --elevation <degrees>      Camera elevation angle (default: ~17)\n");
        fprintf(f, "  --verbose, -v              Print detailed object/drawable info\n\n");
        fprintf(f, "Controls (windowed mode):\n");
        fprintf(f, "  Left drag     orbit\n");
        fprintf(f, "  Scroll        zoom\n");
        fprintf(f, "  Middle drag   pan\n");
        fprintf(f, "  R             reset camera\n");
        fprintf(f, "  Escape        quit\n\n");
        fprintf(f, "Examples:\n");
        fprintf(f, "  milo-viewer world/shared/props/gen/discoball.milo_xbox\n");
        fprintf(f, "  milo-viewer scene.milo_xbox --screenshot out.ppm\n");
        fprintf(f, "  milo-viewer scene.milo_xbox --screenshot out.ppm --azimuth 45 --elevation 30\n");
    };

    if (argc < 2) {
        showHelp(stderr);
        return 1;
    }

    // Parse arguments
    const char* miloPath = nullptr;
    const char* screenshotPath = nullptr;
    float camAzimuthDeg = -999.0f;  // sentinel: use default
    float camElevationDeg = -999.0f;
    bool verbose = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            showHelp(stdout);
            return 0;
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            screenshotPath = argv[++i];
        } else if (strcmp(argv[i], "--azimuth") == 0 && i + 1 < argc) {
            camAzimuthDeg = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--elevation") == 0 && i + 1 < argc) {
            camElevationDeg = (float)atof(argv[++i]);
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

    // Force headless if screenshot mode or no display available
    if (screenshotPath) {
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

    // ---- Screenshot mode: render a few frames then save and exit ----
    if (screenshotPath) {
        printf("Milo Viewer: rendering frames for screenshot...\n");

        // Render a few frames to let GPU resources settle
        for (int frame = 0; frame < 3; frame++) {
            gOrbitCam.Update(cam);
            TheRnd.BeginDrawing();
            if (scene) {
                scene->DrawShowing();
            }
            // Also draw all mesh objects directly (fallback for non-RndDir scenes)
            if (!scene) {
                ObjDirItr<RndMesh> meshIt(baseScene, true);
                while (meshIt) {
                    meshIt->DrawShowing();
                    ++meshIt;
                }
            }
            TheRnd.EndDrawing();
        }

        // Readback the framebuffer
        int w = gWgpuRnd->Gpu().WindowWidth();
        int h = gWgpuRnd->Gpu().WindowHeight();
        size_t pixelSize = (size_t)w * h * 4;
        uint8_t* pixels = (uint8_t*)malloc(pixelSize);

        if (pixels && gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize)) {
            if (WritePPM(screenshotPath, pixels, w, h)) {
                printf("Milo Viewer: screenshot saved to %s (%dx%d)\n", screenshotPath, w, h);
            } else {
                fprintf(stderr, "Error: failed to write screenshot to '%s'\n", screenshotPath);
            }
        } else {
            fprintf(stderr, "Error: failed to readback framebuffer (headless mode required)\n");
        }

        free(pixels);
        baseDir = nullptr;
        gWgpuRnd->Terminate();
        return 0;
    }

    // ---- Render loop (windowed mode) ----
    printf("Milo Viewer: entering render loop (press ESC to quit, R to reset camera)\n");

    while (!gWgpuRnd->Gpu().ShouldClose()) {
        gWgpuRnd->Gpu().PollEvents();

        // Update orbit camera
        gOrbitCam.Update(cam);

        // Draw
        TheRnd.BeginDrawing();
        if (scene) {
            scene->DrawShowing();
        }
        TheRnd.EndDrawing();
    }

    // ---- Cleanup ----
    printf("Milo Viewer: shutting down\n");
    baseDir = nullptr;  // release the loaded dir
    gWgpuRnd->Terminate();

    return 0;
}
