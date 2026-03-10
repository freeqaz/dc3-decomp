// DC3 Native Port — Render Test
// Standalone tool: builds programmatic test scenes and renders them to PNG.
// Geometry tests need no .milo files. Text tests require a font .milo_xbox.
// Venue+UI composite test requires a venue .milo_xbox.

#include "os/Debug.h"
#include "os/System.h"
#include "obj/Dir.h"
#include "rndobj/Dir.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"
#include "rndobj/Draw.h"
#include "rndobj/Text.h"
#include "rndobj/Font.h"
#include "utl/MakeString.h"

#include "platform/Rnd_Wgpu.h"
#include "gfx/GpuDevice.h"
#include "gfx/Screenshot.h"

#include "render_test/test_scene.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>

// Forward declarations from engine
extern Rnd& TheRnd;
extern void NativeDetectDataDir();
void SetFileChecksumData();

// Default font path (DC3 asset library, relative to native/build)
static const char* kDefaultFontPath =
    "../../../milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/"
    "ui/resource/fonts/gen/default.milo_xbox";

// Default venue path (DC3 asset library, relative to native/build)
static const char* kDefaultVenuePath =
    "../../../milo-engine-libs/harmonix-repos/milo-rnd-library/dc3/"
    "world/glitterati/gen/glitterati_set.milo_xbox";

// ============================================================================
// Signal handler
// ============================================================================

static void SignalHandler(int sig) {
    fprintf(stderr, "\nRender Test: Caught signal %d\n", sig);
    void* bt[32];
    int n = backtrace(bt, 32);
    char** syms = backtrace_symbols(bt, n);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "  [%d] %s\n", i, syms ? syms[i] : "??");
    }
    free(syms);
    _exit(128 + sig);
}

// ============================================================================
// Usage
// ============================================================================

static void PrintUsage(FILE* f) {
    fprintf(f, "Usage: render-test [options]\n\n");
    fprintf(f, "Options:\n");
    fprintf(f, "  --output <path>    Output PNG path (required)\n");
    fprintf(f, "  --width <n>        Window width (default: 640)\n");
    fprintf(f, "  --height <n>       Window height (default: 480)\n");
    fprintf(f, "  --test <name>      Run only named test (default: all)\n");
    fprintf(f, "  --font <path>      Font .milo_xbox path (for text tests)\n");
    fprintf(f, "  --venue <path>     Venue .milo_xbox path (for venue_with_ui test)\n");
    fprintf(f, "  --list             List available test cases and exit\n");
    fprintf(f, "  --help             Show this help\n");
}

// ============================================================================
// Test registry
// ============================================================================

struct TestCase {
    const char* name;
    void (*build)(RndDir*);
    void (*buildFont)(RndDir*, RndFont*);
    bool needsFont;
    bool needsVenue;
    const char* description;
};

static const TestCase sTests[] = {
    {"solid_quads",      BuildSolidQuads,    nullptr,           false, false, "Red, green, blue solid quads"},
    {"vertex_colors",    BuildVertexColors,  nullptr,           false, false, "Per-vertex color interpolation"},
    {"alpha_blend",      BuildAlphaBlend,    nullptr,           false, false, "SrcAlpha blend: transparent blue over red"},
    {"additive_blend",   BuildAdditiveBlend, nullptr,           false, false, "Additive blend: green over dark background"},
    {"multiply_blend",   BuildMultiplyBlend, nullptr,           false, false, "Multiply blend: orange over white"},
    {"z_ordering",       BuildZOrdering,     nullptr,           false, false, "Overlapping quads at different depths"},
    {"text_basic",       nullptr,            BuildTextBasic,    true,  false, "Simple text rendering with font"},
    {"text_clipping",    nullptr,            BuildTextClipping, true,  false, "Text clipping (OKAY->KAY bug)"},
    {"text_wrap",        nullptr,            BuildTextWrap,     true,  false, "Text word wrapping"},
    {"text_menu",        nullptr,            BuildTextMenu,     true,  false, "DC3 main menu layout (no venue)"},
    {"venue_with_ui",    nullptr,            nullptr,           true,  true,  "Venue scene with UI text overlays"},
};
static const int sNumTests = sizeof(sTests) / sizeof(sTests[0]);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    signal(SIGSEGV, SignalHandler);
    signal(SIGABRT, SignalHandler);

    // Parse args
    const char* outputPath = nullptr;
    int width = 640;
    int height = 480;
    const char* testFilter = nullptr;
    const char* fontPath = kDefaultFontPath;
    const char* venuePath = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            testFilter = argv[++i];
        } else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            fontPath = argv[++i];
        } else if (strcmp(argv[i], "--venue") == 0 && i + 1 < argc) {
            venuePath = argv[++i];
        } else if (strcmp(argv[i], "--list") == 0) {
            printf("Available test cases:\n");
            for (int j = 0; j < sNumTests; j++) {
                printf("  %-20s %s\n", sTests[j].name, sTests[j].description);
            }
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            PrintUsage(stdout);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            PrintUsage(stderr);
            return 1;
        }
    }

    if (!outputPath) {
        fprintf(stderr, "Error: --output is required\n\n");
        PrintUsage(stderr);
        return 1;
    }

    // Validate test filter
    if (testFilter) {
        bool found = false;
        for (int i = 0; i < sNumTests; i++) {
            if (strcmp(testFilter, sTests[i].name) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "Error: unknown test '%s'. Use --list to see available tests.\n", testFilter);
            return 1;
        }
    }

    // Auto-enable venue path for venue_with_ui test
    bool needsVenue = false;
    if (testFilter && strcmp(testFilter, "venue_with_ui") == 0) {
        needsVenue = true;
    } else if (!testFilter) {
        // Running all tests — venue_with_ui is included
        needsVenue = true;
    }
    if (needsVenue && !venuePath) {
        venuePath = kDefaultVenuePath;
    }

    // ---- Engine init (headless, with rendering) ----
    setenv("MILO_RENDER", "1", 1);
    setenv("MILO_HEADLESS", "1", 1);

    char widthStr[16], heightStr[16];
    snprintf(widthStr, sizeof(widthStr), "%d", width);
    snprintf(heightStr, sizeof(heightStr), "%d", height);
    setenv("MILO_WIDTH", widthStr, 1);
    setenv("MILO_HEIGHT", heightStr, 1);

    printf("Render Test: initializing engine (%dx%d headless)...\n", width, height);

    InitMakeString();
    SetFileChecksumData();
    SystemPreInit(argc, argv, "config/ham_preinit_keep.dta");

    TheRnd.PreInit();
    SystemInit("config/ham_keep.dta");
    TheRnd.Init();

    if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady()) {
        fprintf(stderr, "ERROR: GPU initialization failed.\n");
        fprintf(stderr, "  Vulkan ICD access may be blocked by sandbox.\n");
        return 2;
    }
    if (gWgpuRnd->Gpu().IsNullBackend()) {
        fprintf(stderr, "ERROR: GPU fell back to Null backend — no real GPU.\n");
        return 2;
    }

    printf("Render Test: GPU ready (%dx%d)\n",
           gWgpuRnd->Gpu().WindowWidth(), gWgpuRnd->Gpu().WindowHeight());

    // ---- Load font (for text tests) ----
    RndFont* font = nullptr;
    ObjectDir* fontDir = LoadFontDir(fontPath);
    if (fontDir) {
        font = FindFirstFont(fontDir);
    }

    // ---- Load venue (for venue_with_ui test) ----
    ObjectDir* venueDir = nullptr;
    if (needsVenue && venuePath) {
        venueDir = LoadVenueDir(venuePath);
    }

    // ---- Build the test scene ----
    RndDir* testDir = Hmx::Object::New<RndDir>();
    testDir->SetName("render_test_scene", testDir);

    RndEnviron* env = SetupTestEnvironment(testDir);
    testDir->SetEnv(env);

    RndCam* cam = SetupTestCamera(testDir);
    cam->Select();

    if (testFilter) {
        if (strcmp(testFilter, "venue_with_ui") == 0) {
            printf("Render Test: building test 'venue_with_ui'...\n");
            // Override camera for venue — the venue geometry is huge
            if (venueDir) {
                RndCam* venueCam = SetupVenueCamera(testDir, venueDir);
                venueCam->Select();
                cam = venueCam;  // use venue cam for rendering
            }
            BuildVenueWithUI(testDir, venueDir, font, cam);
        } else {
            for (int i = 0; i < sNumTests; i++) {
                if (strcmp(testFilter, sTests[i].name) == 0) {
                    printf("Render Test: building test '%s'...\n", testFilter);
                    if (sTests[i].needsFont) {
                        if (font)
                            sTests[i].buildFont(testDir, font);
                        else
                            printf("Render Test: skipping '%s' (no font)\n", testFilter);
                    } else {
                        sTests[i].build(testDir);
                    }
                    break;
                }
            }
        }
    } else {
        printf("Render Test: building all %d tests...\n", sNumTests);
        BuildAllTests(testDir, font);
        // Note: venue_with_ui is NOT included in "all" — it uses the full viewport
        // and would overlay the test grid. Run it standalone with --test venue_with_ui.
    }

    // Count what we built
    int meshCount = 0, matCount = 0, textCount = 0;
    {
        ObjDirItr<RndMesh> it(testDir, false);
        while (it) { meshCount++; ++it; }
    }
    {
        ObjDirItr<RndMat> it(testDir, false);
        while (it) { matCount++; ++it; }
    }
    {
        ObjDirItr<RndText> it(testDir, false);
        while (it) { textCount++; ++it; }
    }
    printf("Render Test: scene has %d meshes, %d materials, %d texts\n",
           meshCount, matCount, textCount);

    // ---- Render ----
    printf("Render Test: rendering frame...\n");

    // Render a few frames (GPU pipeline settle)
    for (int frame = 0; frame < 3; frame++) {
        TheRnd.BeginDrawing();

        Vector3 origin(0, 0, 0);
        RndEnvironTracker tracker(env, &origin);

        // If venue is loaded and we're rendering venue_with_ui,
        // draw venue meshes first (background layer)
        if (venueDir && (!testFilter || strcmp(testFilter, "venue_with_ui") == 0)) {
            // Use venue's own environment if available
            RndDir* venueRndDir = dynamic_cast<RndDir*>(venueDir);
            RndEnviron* venueEnv = nullptr;
            if (venueRndDir)
                venueEnv = venueRndDir->GetEnv();

            if (venueEnv) {
                Vector3 venueOrigin(0, 0, 0);
                RndEnvironTracker venueTracker(venueEnv, &venueOrigin);

                ObjDirItr<RndMesh> meshIt(venueDir, true);
                while (meshIt) {
                    meshIt->DrawShowing();
                    ++meshIt;
                }
            } else {
                ObjDirItr<RndMesh> meshIt(venueDir, true);
                while (meshIt) {
                    meshIt->DrawShowing();
                    ++meshIt;
                }
            }
        }

        // Draw all drawables in test dir (meshes + text objects)
        ObjDirItr<RndDrawable> drawIt(testDir, false);
        while (drawIt) {
            drawIt->DrawShowing();
            ++drawIt;
        }

        TheRnd.EndDrawing();
    }

    // ---- Screenshot ----
    int w = gWgpuRnd->Gpu().WindowWidth();
    int h = gWgpuRnd->Gpu().WindowHeight();
    size_t pixelSize = (size_t)w * h * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelSize);

    if (!pixels) {
        fprintf(stderr, "ERROR: failed to allocate %zu bytes for framebuffer\n", pixelSize);
        return 3;
    }

    bool readback = gWgpuRnd->Gpu().ReadbackHeadlessFrame(pixels, pixelSize);
    if (!readback) {
        fprintf(stderr, "ERROR: framebuffer readback failed\n");
        free(pixels);
        return 3;
    }

    // Count non-black pixels as a sanity check
    int nonBlack = 0;
    for (int i = 0; i < w * h; i++) {
        if (pixels[i * 4] || pixels[i * 4 + 1] || pixels[i * 4 + 2])
            nonBlack++;
    }
    printf("Render Test: readback %dx%d — %d/%d non-black pixels (%.1f%%)\n",
           w, h, nonBlack, w * h, 100.0f * nonBlack / (w * h));

    if (WriteScreenshot(outputPath, pixels, w, h)) {
        printf("Render Test: wrote %s\n", outputPath);
    } else {
        fprintf(stderr, "ERROR: failed to write %s\n", outputPath);
        free(pixels);
        return 4;
    }

    free(pixels);

    printf("Render Test: done.\n");
    // Exit immediately — engine cleanup triggers SIGSEGV in static destructors
    _exit(0);
}
