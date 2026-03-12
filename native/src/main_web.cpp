// DC3 Web Port — Entry Point
// Bootstraps the engine in the browser via Emscripten.
// Uses emscripten_set_main_loop for the browser event loop.

#ifdef __EMSCRIPTEN__

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <cstdlib>

// Engine globals
class App;
extern App* TheApp;

// Forward declarations from App
void WebRunOneFrame();

// For now — minimal boot that clears the screen
// Full engine boot comes later (needs ASYNCIFY for file I/O)

#include "gfx/GpuDevice.h"

static GpuDevice sGpu;
static bool sGpuReady = false;
static int sFrameCount = 0;

static void mainLoop() {
    if (!sGpu.IsReady()) {
        // Still waiting for async WebGPU init
        sGpu.PollEvents();
        return;
    }

    if (!sGpuReady) {
        sGpuReady = true;
        printf("DC3 Web: GPU ready, starting render\n");
    }

    // Acquire frame and clear
    wgpu::TextureView frameView = sGpu.AcquireNextFrame();
    if (!frameView) return;

    wgpu::RenderPassColorAttachment colorAtt{};
    colorAtt.view = frameView;
    colorAtt.loadOp = wgpu::LoadOp::Clear;
    colorAtt.storeOp = wgpu::StoreOp::Store;
    // DC3 teal clear color (matches native port)
    colorAtt.clearValue = {0.06, 0.09, 0.12, 1.0};
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    wgpu::RenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;

    wgpu::CommandEncoder encoder = sGpu.Device().CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rpDesc);
    pass.End();

    wgpu::CommandBuffer cmd = encoder.Finish();
    sGpu.Queue().Submit(1, &cmd);

    sGpu.PresentFrame();

    sFrameCount++;
    if (sFrameCount == 1 || sFrameCount % 100 == 0) {
        printf("DC3 Web: frame %d\n", sFrameCount);
    }
}

int main(int argc, char** argv) {
    printf("DC3 Web Port — Initializing\n");

    // Read canvas size from HTML element
    int canvasW = 1280, canvasH = 720;
    emscripten_get_canvas_element_size("#dc3-canvas", &canvasW, &canvasH);

    GpuDeviceDesc desc{};
    desc.width = canvasW;
    desc.height = canvasH;
    desc.headless = false;

    if (!sGpu.Init(desc)) {
        fprintf(stderr, "DC3 Web: GPU init failed\n");
        return EXIT_FAILURE;
    }

    printf("DC3 Web: starting main loop (waiting for GPU...)\n");
    emscripten_set_main_loop(mainLoop, 0, true);

    return EXIT_SUCCESS;
}

#endif // __EMSCRIPTEN__
