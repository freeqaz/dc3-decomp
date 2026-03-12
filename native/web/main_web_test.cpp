// DC3 Web Port — Minimal WebGPU Canvas Test
// Clears an HTML canvas to cornflower blue via WebGPU in the browser.
// Phase 0 proof-of-life: C++ → WASM → browser WebGPU.

#include <webgpu/webgpu.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <cstdlib>

static WGPUInstance gInstance = nullptr;
static WGPUDevice gDevice = nullptr;
static WGPUQueue gQueue = nullptr;
static WGPUSurface gSurface = nullptr;
static int gWidth = 1280;
static int gHeight = 720;
static bool gReady = false;
static bool gInitStarted = false;

static void renderFrame() {
    if (!gReady) return;

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

    // No wgpuSurfacePresent() — browser auto-presents at end of requestAnimationFrame
    wgpuTextureViewRelease(view);
}

static void configureSurfaceAndStart() {
    // Create surface from the HTML canvas
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = {"#dc3-canvas", 11};

    WGPUSurfaceDescriptor surfDesc = {};
    surfDesc.nextInChain = &canvasDesc.chain;
    gSurface = wgpuInstanceCreateSurface(gInstance, &surfDesc);

    if (!gSurface) {
        fprintf(stderr, "Failed to create surface from canvas\n");
        return;
    }

    // Configure surface
    WGPUSurfaceConfiguration config = {};
    config.device = gDevice;
    config.format = WGPUTextureFormat_RGBA8Unorm;  // Browser preferred format
    config.width = gWidth;
    config.height = gHeight;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(gSurface, &config);

    printf("WebGPU ready, rendering\n");
    gReady = true;
}

static void onDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView message, void* /*userdata1*/, void* /*userdata2*/) {
    if (status != WGPURequestDeviceStatus_Success) {
        fprintf(stderr, "Device request failed: %.*s\n", (int)message.length, message.data);
        return;
    }

    printf("WebGPU device acquired\n");
    gDevice = device;
    gQueue = wgpuDeviceGetQueue(gDevice);
    configureSurfaceAndStart();
}

static void onAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                           WGPUStringView message, void* /*userdata1*/, void* /*userdata2*/) {
    if (status != WGPURequestAdapterStatus_Success) {
        fprintf(stderr, "Adapter request failed: %.*s\n", (int)message.length, message.data);
        return;
    }

    printf("WebGPU adapter acquired\n");

    WGPUDeviceDescriptor deviceDesc = {};
    WGPURequestDeviceCallbackInfo cbInfo = {};
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    cbInfo.callback = onDeviceReady;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, cbInfo);
}

// Main loop callback — drives both init and rendering
static void mainLoop() {
    if (!gInitStarted) {
        gInitStarted = true;

        // Create WebGPU instance
        gInstance = wgpuCreateInstance(nullptr);
        if (!gInstance) {
            fprintf(stderr, "Failed to create WebGPU instance\n");
            return;
        }

        // Request adapter (async — callback fires on a future frame)
        WGPURequestAdapterOptions opts = {};
        opts.powerPreference = WGPUPowerPreference_HighPerformance;

        WGPURequestAdapterCallbackInfo cbInfo = {};
        cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        cbInfo.callback = onAdapterReady;
        wgpuInstanceRequestAdapter(gInstance, &opts, cbInfo);

        printf("Waiting for WebGPU adapter...\n");
        return;
    }

    // Process pending WebGPU events (delivers async callbacks)
    if (gInstance) {
        wgpuInstanceProcessEvents(gInstance);
    }

    renderFrame();
}

int main() {
    printf("DC3 Web Port — Phase 0 WebGPU Test\n");

    // Start the main loop immediately — this keeps the runtime alive
    // and provides a frame callback for async WebGPU initialization
    emscripten_set_main_loop(mainLoop, 0, true);

    return EXIT_SUCCESS;
}
