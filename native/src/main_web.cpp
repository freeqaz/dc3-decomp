// DC3 Web Port — Entry Point
// Bootstraps the engine in the browser via Emscripten.
//
// Boot sequence (state machine, driven by requestAnimationFrame):
//   BOOT_INIT         → create MEMFS dirs, start bundle download
//   BOOT_FETCHING     → poll until bundle download complete
//   BOOT_ENGINE_INIT  → App constructor (shared with native desktop)
//   BOOT_GPU_WAIT     → wait for async WebGPU adapter/device
//   BOOT_GPU_READY    → initialize GPU resources (pipelines, buffers)
//   BOOT_RUNNING      → per-frame via App::RunOneFrame()
//
// JSPI note: emscripten_set_main_loop calls the callback via the WASM
// function table (indirect call), which bypasses JSPI's WebAssembly.promising
// wrapping. Instead, we export the tick function and drive it from JS via
// requestAnimationFrame. This lets emscripten_sleep() suspend correctly
// during on-demand file fetches.

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
        // Signal readiness after a few frames so the compositor has
        // presented real GPU content (not just a cleared buffer).
        if (sFrameCount == 3) {
            EM_ASM({ window.__webgpuReady = true; });
        }
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
// Exported tick function — called from JS via requestAnimationFrame.
// Must be a WASM export (not a function-table indirect call) so that
// JSPI's WebAssembly.promising wrapper applies, allowing emscripten_sleep()
// to suspend during on-demand file fetches.
// ============================================================================

extern "C" {
EMSCRIPTEN_KEEPALIVE
void dc3MainLoopTick() {
    mainLoop();
}
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv) {
    printf("DC3 Web Port — Initializing\n");

#ifdef DC3_WEB_ASYNCIFY
    // JSPI mode: drive the loop from JS using the exported (promising-wrapped)
    // dc3MainLoopTick. requestAnimationFrame naturally paces at ~60fps.
    // Each tick may suspend via emscripten_sleep() during file fetches;
    // the Promise returned by the promising wrapper handles this correctly.
    EM_ASM({
        async function tick() {
            try {
                await Module._dc3MainLoopTick();
            } catch (e) {
                if (e !== 'unwind') console.error('dc3MainLoopTick error:', e);
            }
            requestAnimationFrame(tick);
        }
        requestAnimationFrame(tick);
    });
    // Keep the runtime alive — we never exit.
    emscripten_exit_with_live_runtime();
#else
    // Non-JSPI: use Emscripten's standard main loop (blocking sync XHR).
    emscripten_set_main_loop(mainLoop, 0, true);
#endif

    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
