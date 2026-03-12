// DC3 Web Port — Minimal WebGPU Canvas Test
// Clears an HTML canvas to cornflower blue via WebGPU in the browser.
// This is the Phase 0 proof-of-life: C++ → WASM → browser WebGPU.

#include <webgpu/webgpu.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <cstdio>
#include <cstdlib>

static WGPUDevice gDevice = nullptr;
static WGPUQueue gQueue = nullptr;
static WGPUSurface gSurface = nullptr;
static WGPUTextureFormat gSurfaceFormat = WGPUTextureFormat_BGRA8Unorm;
static int gWidth = 1280;
static int gHeight = 720;

static void frame() {
    WGPUSurfaceTexture surfTex;
    wgpuSurfaceGetCurrentTexture(gSurface, &surfTex);
    if (surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfTex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        return;
    }

    WGPUTextureView view = wgpuTextureCreateView(surfTex.texture, nullptr);

    WGPURenderPassColorAttachment colorAtt = {};
    colorAtt.view = view;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = {0.392, 0.584, 0.929, 1.0}; // Cornflower blue
    colorAtt.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor rpDesc = {};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;

    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gDevice, nullptr);
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(gQueue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    wgpuSurfacePresent(gSurface);
    wgpuTextureViewRelease(view);
}

static void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView message, void* /*userdata*/) {
    if (status != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "Device request failed: %.*s\n", (int)message.length, message.data);
        return;
    }

    gDevice = device;
    gQueue = wgpuDeviceGetQueue(gDevice);

    // Create surface from the canvas
    WGPUSurfaceSourceCanvasHTMLSelector_HTMLString canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_SurfaceSourceCanvasHTMLSelector_HTMLString;
    canvasDesc.selector = (WGPUStringView){"#dc3-canvas", 11};

    WGPUSurfaceDescriptor surfDesc = {};
    surfDesc.nextInChain = &canvasDesc.chain;
    // Get the instance from emscripten
    WGPUInstance instance = emscripten_webgpu_get_instance();
    gSurface = wgpuInstanceCreateSurface(instance, &surfDesc);

    if (!gSurface) {
        fprintf(stderr, "Failed to create surface from canvas\n");
        return;
    }

    // Configure surface
    WGPUSurfaceConfiguration config = {};
    config.device = gDevice;
    config.format = gSurfaceFormat;
    config.width = gWidth;
    config.height = gHeight;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(gSurface, &config);

    printf("WebGPU device ready, starting render loop\n");
    emscripten_set_main_loop(frame, 0, false);
}

static void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                           WGPUStringView message, void* /*userdata*/) {
    if (status != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "Adapter request failed: %.*s\n", (int)message.length, message.data);
        return;
    }

    printf("WebGPU adapter acquired\n");

    WGPUDeviceDescriptor deviceDesc = {};
    wgpuAdapterRequestDevice(adapter, &deviceDesc, onDeviceReady, nullptr);
}

int main() {
    printf("DC3 Web Port — Phase 0 WebGPU Test\n");

    // Get the pre-created WebGPU instance from Emscripten
    WGPUInstance instance = emscripten_webgpu_get_instance();
    if (!instance) {
        fprintf(stderr, "No WebGPU instance (browser may not support WebGPU)\n");
        return EXIT_FAILURE;
    }

    // Request adapter (async in browser)
    WGPURequestAdapterOptions opts = {};
    opts.powerPreference = WGPUPowerPreference_HighPerformance;
    wgpuInstanceRequestAdapter(instance, &opts, onAdapterReady, nullptr);

    // Don't return from main — Emscripten keeps the runtime alive
    return EXIT_SUCCESS;
}
