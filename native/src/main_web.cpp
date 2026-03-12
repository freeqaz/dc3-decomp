// DC3 Web Port — Entry Point (Phase 5: Engine Rendering)
// Bootstraps the engine in the browser via Emscripten.
//
// Boot sequence (state machine, driven by emscripten_set_main_loop):
//   BOOT_INIT         → create MEMFS dirs, start bundle download
//   BOOT_FETCHING     → poll until bundle download complete
//   BOOT_ENGINE_INIT  → SystemPreInit + SystemInit + TheRnd.Init()
//   BOOT_GPU_WAIT     → wait for async WebGPU adapter/device
//   BOOT_GPU_READY    → initialize GPU resources (pipelines, buffers)
//   BOOT_RUNNING      → per-frame engine render loop

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/WebAssets.h"

// Engine headers
#include "os/Debug.h"
#include "os/System.h"
#include "rndobj/Rnd_NG.h"
#include "utl/MakeString.h"

// WgpuRnd access
#include "platform/Rnd_Wgpu.h"
extern WgpuRnd *gWgpuRnd;

// Forward declarations from other TUs
extern void NativeSetDataDir(const char *);
extern void InitMakeString();
void SetFileChecksumData();
void SystemPreInit(const char *cmdLine, const char *cfg);
void SystemInit(const char *cfg);

// ============================================================================
// Boot state machine
// ============================================================================

enum BootState {
    BOOT_INIT,
    BOOT_FETCHING,
    BOOT_ENGINE_INIT,
    BOOT_GPU_WAIT,
    BOOT_GPU_READY,
    BOOT_RUNNING,
    BOOT_ERROR,
};

static BootState sBootState = BOOT_INIT;
static int sFrameCount = 0;

// ============================================================================
// Main loop — drives the boot state machine
// ============================================================================

static void mainLoop() {
    switch (sBootState) {

    case BOOT_INIT: {
        printf("DC3 Web: downloading assets (bundle)...\n");
        WebAssetsInit();
        WebAssetsFetchBundle();
        sBootState = BOOT_FETCHING;
        break;
    }

    case BOOT_FETCHING: {
        if (!WebAssetsAllDone()) break;

        int ok = WebAssetsCompletedCount();
        int fail = WebAssetsFailedCount();
        printf("DC3 Web: assets ready (%d files, %d errors)\n", ok, fail);
        sBootState = BOOT_ENGINE_INIT;
        break;
    }

    case BOOT_ENGINE_INIT: {
        printf("DC3 Web: initializing engine...\n");

        // Initialize string utilities (must be first)
        InitMakeString();
        SetFileChecksumData();

        // Engine pre-init — loads ham_preinit_keep.dta from MEMFS
        // Note: SetUsingCD(false) is the default, so files open directly from MEMFS
        printf("DC3 Web: SystemPreInit...\n");
        SystemPreInit("dc3-web", "config/ham_preinit_keep.dta");

        // Full engine init — loads ham_keep.dta and all subsystems
        printf("DC3 Web: SystemInit...\n");
        SystemInit("config/ham_keep.dta");

        // Initialize renderer — starts async GPU init on web
        printf("DC3 Web: TheRnd.Init()...\n");
        TheRnd.Init();

        sBootState = BOOT_GPU_WAIT;
        printf("DC3 Web: waiting for GPU...\n");
        break;
    }

    case BOOT_GPU_WAIT: {
        // Poll WebGPU instance to process async callbacks
        if (gWgpuRnd) {
            gWgpuRnd->Gpu().PollEvents();
            if (gWgpuRnd->Gpu().IsReady()) {
                sBootState = BOOT_GPU_READY;
            }
        } else {
            printf("DC3 Web: ERROR — gWgpuRnd is null\n");
            sBootState = BOOT_ERROR;
        }
        break;
    }

    case BOOT_GPU_READY: {
        printf("DC3 Web: GPU ready, initializing resources...\n");
        gWgpuRnd->InitGpuResources();
        printf("DC3 Web: entering render loop\n");
        sBootState = BOOT_RUNNING;
        break;
    }

    case BOOT_RUNNING: {
        // Use engine's renderer
        TheRnd.BeginDrawing();
        TheRnd.EndDrawing();

        sFrameCount++;
        if (sFrameCount == 1 || sFrameCount % 300 == 0) {
            printf("DC3 Web: frame %d\n", sFrameCount);
        }
        break;
    }

    case BOOT_ERROR:
        break;
    }
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv) {
    printf("DC3 Web Port — Initializing\n");

    // Set MEMFS data directory for File_Web.cpp
    NativeSetDataDir("/data");

    emscripten_set_main_loop(mainLoop, 0, true);
    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
