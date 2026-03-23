// DC3 Web Port — Entry Point
// Bootstraps the engine in the browser via Emscripten.
//
// Boot sequence (state machine, driven by emscripten_set_main_loop):
//   BOOT_INIT         → create MEMFS dirs, start bundle download
//   BOOT_FETCHING     → poll until bundle download complete
//   BOOT_ENGINE_INIT  → App constructor (shared with native desktop)
//   BOOT_GPU_WAIT     → wait for async WebGPU adapter/device
//   BOOT_GPU_READY    → initialize GPU resources (pipelines, buffers)
//   BOOT_RUNNING      → per-frame via App::RunOneFrame()

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/em_asm.h>
#include <cstdio>
#include <cstdlib>

#include "App.h"
#include "platform/WebAssets.h"
#include "platform/Rnd_Wgpu.h"

extern void NativeSetDataDir(const char *);
extern WgpuRnd *gWgpuRnd;

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
static App *sApp = nullptr;
static int sFrameCount = 0;
static int sGpuWaitFrames = 0;
static const int kGpuWaitTimeout = 300; // ~5 seconds at 60fps

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
        printf("DC3 Web: initializing engine via App...\n");
        NativeSetDataDir("/data");
        sApp = new App(0, nullptr);
        sBootState = BOOT_GPU_WAIT;
        printf("DC3 Web: waiting for GPU...\n");
        break;
    }

    case BOOT_GPU_WAIT: {
        // GPU may already be initialized if JSPI yielding during App constructor
        // allowed async callbacks to fire and InitGpuResources was called.
        if (gWgpuRnd && gWgpuRnd->GpuResourcesReady()) {
            printf("DC3 Web: GPU already initialized during App constructor (JSPI)\n");
            sBootState = BOOT_RUNNING;
            break;
        }
        sGpuWaitFrames++;
        if (gWgpuRnd) {
            gWgpuRnd->Gpu().PollEvents();
            if (gWgpuRnd->Gpu().IsReady()) {
                sBootState = BOOT_GPU_READY;
                break;
            }
        }
        if (sGpuWaitFrames >= kGpuWaitTimeout) {
            printf("DC3 Web: GPU not ready after %d frames — proceeding without rendering\n", sGpuWaitFrames);
            sBootState = BOOT_RUNNING;
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
        sFrameCount++;
        sApp->RunOneFrame();
        EM_ASM({ window.dc3FrameCount = $0; }, sFrameCount);
        if (sFrameCount == 1 || sFrameCount % 300 == 0) {
            printf("DC3 Web: frame %d\n", sFrameCount);
            fflush(stdout);
        }
        break;
    }

    case BOOT_ERROR:
        break;
    }
}

// ============================================================================
// Exported C API (called from JS)
// ============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE
void dc3_resize_canvas(int w, int h) {
    if (gWgpuRnd && w > 0 && h > 0) {
        gWgpuRnd->Gpu().ResizeSurface(w, h);
    }
}

EMSCRIPTEN_KEEPALIVE
void dc3MainLoopTick() {
    mainLoop();
}

} // extern "C"

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv) {
    printf("DC3 Web Port — Initializing\n");
#ifdef DC3_WEB_ASYNCIFY
    // JSPI mode: JS drives the frame loop via requestAnimationFrame + await.
    // Each tick calls dc3MainLoopTick() which can yield to the browser
    // (via emscripten_sleep) during synchronous file loads, letting the
    // loading screen render while .milo_xbox files download.
    EM_ASM({
        async function tick() {
            await Module._dc3MainLoopTick();
            requestAnimationFrame(tick);
        }
        requestAnimationFrame(tick);
    });
    emscripten_exit_with_live_runtime();
#else
    // Fallback: cooperative main loop (no async yielding — file loads block).
    emscripten_set_main_loop(mainLoop, 0, true);
#endif
    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
