// DC3 Native Port — WebGPU Renderer Implementation
// Replaces Rnd_Stub.cpp with real WebGPU rendering via Dawn

#include "platform/Rnd_Wgpu.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Mat.h"
#include "ui/UI.h"

#include <cstdio>
#include <cstring>

// ============================================================================
// Global instances
// ============================================================================

static WgpuShaderMgr gWgpuShaderMgr;
static WgpuRnd gWgpuRndInstance;

Rnd& TheRnd = gWgpuRndInstance;
NgRnd& TheNgRnd = gWgpuRndInstance;
RndShaderMgr& TheShaderMgr = gWgpuShaderMgr;
WgpuRnd* gWgpuRnd = &gWgpuRndInstance;

UIManager* TheUI = nullptr;

// ============================================================================
// UniformRingBuffer
// ============================================================================

void UniformRingBuffer::Init(wgpu::Device& device, uint32_t capacity) {
    wgpu::BufferDescriptor desc{};
    desc.size = capacity;
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mBuffer = device.CreateBuffer(&desc);
    mCapacity = capacity;
    mOffset = 0;
}

uint32_t UniformRingBuffer::Write(wgpu::Queue& queue, const void* data, uint32_t size) {
    uint32_t alignedSize = (size + kAlignment - 1) & ~(kAlignment - 1);
    if (mOffset + alignedSize > mCapacity) {
        // Ring buffer full — wrap around (safe for single-frame use)
        mOffset = 0;
    }
    uint32_t offset = mOffset;
    queue.WriteBuffer(mBuffer, offset, data, size);
    mOffset += alignedSize;
    return offset;
}

// ============================================================================
// Helper: Convert Milo Transform to 16-float row-major 4x4 matrix
// (WGSL interprets as column-major, giving the correct transpose for M*v)
// ============================================================================

static void TransformToFloat16(const Transform& xfm, float* out) {
    out[0]  = xfm.m.x.x; out[1]  = xfm.m.x.y; out[2]  = xfm.m.x.z; out[3]  = 0;
    out[4]  = xfm.m.y.x; out[5]  = xfm.m.y.y; out[6]  = xfm.m.y.z; out[7]  = 0;
    out[8]  = xfm.m.z.x; out[9]  = xfm.m.z.y; out[10] = xfm.m.z.z; out[11] = 0;
    out[12] = xfm.v.x;   out[13] = xfm.v.y;   out[14] = xfm.v.z;   out[15] = 1;
}

// ============================================================================
// WgpuRnd Implementation
// ============================================================================

void WgpuRnd::Init() {
    printf("DC3 Native: WgpuRnd::Init() — WebGPU renderer\n");

    // Register subsystem types (creates default cam/env/mat/etc.)
    PreInit();

    // Create GPU device and window
    GpuDeviceDesc desc{};
    desc.headless = (getenv("MILO_HEADLESS") != nullptr);
    desc.width = 1280;
    desc.height = 720;
    desc.title = "DC3 Native — WebGPU";

    if (!mGpu.Init(desc)) {
        printf("DC3 Native: GPU init failed, falling back to headless\n");
        desc.headless = true;
        if (!mGpu.Init(desc)) {
            printf("DC3 Native: headless GPU init also failed!\n");
            return;
        }
    }

    mWidth = mGpu.WindowWidth();
    mHeight = mGpu.WindowHeight();

    // Initialize pipeline manager
    mPipelines.Init(&mGpu);

    // Create scene uniform buffer (224 bytes, updated once per frame)
    {
        wgpu::BufferDescriptor bd{};
        bd.size = sizeof(SceneUniforms);
        bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        mSceneBuffer = mGpu.Device().CreateBuffer(&bd);
    }

    // Create per-draw ring buffers (64KB each — enough for ~250 draws/frame at 256-byte alignment)
    mMaterialRing.Init(mGpu.Device(), 64 * 1024);
    mObjectRing.Init(mGpu.Device(), 64 * 1024);

    // Create depth texture
    CreateDepthTexture(mWidth, mHeight);

    // Create default textures (1x1 white for untextured materials)
    CreateDefaultTextures();

    printf("DC3 Native: WgpuRnd initialized (%dx%d, %s)\n",
           mWidth, mHeight, mGpu.HasBCCompression() ? "BC supported" : "software DXT");
}

void WgpuRnd::Terminate() {
    mDepthTex = nullptr;
    mDepthView = nullptr;
    mWhiteTex = nullptr;
    mWhiteTexView = nullptr;
    mDefaultSampler = nullptr;
    mSceneBuffer = nullptr;
    mSceneBindGroup = nullptr;
    mGpu.Shutdown();
}

void WgpuRnd::Clear(unsigned int flags, const Hmx::Color& color) {
    mWgpuClearColor = color;
}

void WgpuRnd::BeginDrawing() {
    // Poll GLFW events
    if (!mGpu.IsHeadless()) {
        mGpu.PollEvents();
        if (mGpu.ShouldClose()) {
            // Window closed — request shutdown
            // For now, just skip drawing
            mDrawing = true;
            mWorldEnded = false;
            mDrawCount++;
            mFrameID++;
            return;
        }
    }

    mDrawing = true;
    mWorldEnded = false;
    mDrawCount++;
    mFrameID++;

    // Reset ring buffers for this frame
    mMaterialRing.Reset();
    mObjectRing.Reset();

    // Acquire next frame
    if (mGpu.IsHeadless()) {
        mFrameView = mGpu.AcquireHeadlessFrame();
    } else {
        mFrameView = mGpu.AcquireNextFrame();
    }
    if (!mFrameView) return;

    // Resize depth texture if window size changed
    int curW = mGpu.WindowWidth();
    int curH = mGpu.WindowHeight();
    if (curW != mDepthWidth || curH != mDepthHeight) {
        CreateDepthTexture(curW, curH);
        mWidth = curW;
        mHeight = curH;
    }

    // Write scene uniforms from current camera and environment
    WriteSceneUniforms();

    // Create command encoder
    mEncoder = mGpu.Device().CreateCommandEncoder();

    // Begin render pass
    wgpu::RenderPassColorAttachment colorAtt{};
    colorAtt.view = mFrameView;
    colorAtt.loadOp = wgpu::LoadOp::Clear;
    colorAtt.storeOp = wgpu::StoreOp::Store;
    colorAtt.clearValue = {
        mWgpuClearColor.red,
        mWgpuClearColor.green,
        mWgpuClearColor.blue,
        mWgpuClearColor.alpha
    };

    wgpu::RenderPassDepthStencilAttachment depthAtt{};
    depthAtt.view = mDepthView;
    depthAtt.depthLoadOp = wgpu::LoadOp::Clear;
    depthAtt.depthStoreOp = wgpu::StoreOp::Store;
    depthAtt.depthClearValue = 1.0f;
    depthAtt.stencilLoadOp = wgpu::LoadOp::Clear;
    depthAtt.stencilStoreOp = wgpu::StoreOp::Store;
    depthAtt.stencilClearValue = 0;

    wgpu::RenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;
    rpDesc.depthStencilAttachment = &depthAtt;

    mPass = mEncoder.BeginRenderPass(&rpDesc);
    mInPass = true;

    // Bind scene uniforms (group 0) — stays bound for entire frame
    mPass.SetBindGroup(0, mSceneBindGroup);
}

void WgpuRnd::EndDrawing() {
    if (mInPass) {
        mPass.End();
        mInPass = false;

        wgpu::CommandBuffer cmd = mEncoder.Finish();
        mGpu.Queue().Submit(1, &cmd);

        if (!mGpu.IsHeadless()) {
            mGpu.PresentFrame();
        }
    }

    mFrameView = nullptr;
    mDrawing = false;
}

void WgpuRnd::CreateDepthTexture(int w, int h) {
    if (w <= 0 || h <= 0) return;

    wgpu::TextureDescriptor desc{};
    desc.size.width = w;
    desc.size.height = h;
    desc.size.depthOrArrayLayers = 1;
    desc.format = wgpu::TextureFormat::Depth24PlusStencil8;
    desc.usage = wgpu::TextureUsage::RenderAttachment;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    mDepthTex = mGpu.Device().CreateTexture(&desc);
    mDepthView = mDepthTex.CreateView();
    mDepthWidth = w;
    mDepthHeight = h;
}

void WgpuRnd::CreateDefaultTextures() {
    // 1x1 white texture for untextured materials
    {
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        mWhiteTex = mGpu.Device().CreateTexture(&desc);
        mWhiteTexView = mWhiteTex.CreateView();

        uint8_t white[4] = {255, 255, 255, 255};
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = mWhiteTex;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        wgpu::Extent3D extent = {1, 1, 1};
        mGpu.Queue().WriteTexture(&dst, white, 4, &layout, &extent);
    }

    // Default sampler (linear filtering, repeat)
    {
        SamplerDesc sd{};
        mDefaultSampler = mGpu.GetSampler(sd);
    }
}

void WgpuRnd::WriteSceneUniforms() {
    SceneUniforms scene{};
    memset(&scene, 0, sizeof(scene));

    // Camera
    RndCam* cam = RndCam::Current();
    if (cam) {
        // ViewProj matrix — memcpy row-major data, WGSL reads as column-major (correct transpose)
        memcpy(scene.viewProj, &cam->GetViewProjMatrix(), 64);

        // View matrix from camera's inverse world transform
        Transform invWorld;
        // Use the camera's world transform inverse
        const Transform& worldXfm = cam->WorldXfm();
        // For view matrix: invert the camera's world transform
        // Rotation transpose + negated translation
        scene.view[0]  = worldXfm.m.x.x; scene.view[1]  = worldXfm.m.y.x; scene.view[2]  = worldXfm.m.z.x; scene.view[3]  = 0;
        scene.view[4]  = worldXfm.m.x.y; scene.view[5]  = worldXfm.m.y.y; scene.view[6]  = worldXfm.m.z.y; scene.view[7]  = 0;
        scene.view[8]  = worldXfm.m.x.z; scene.view[9]  = worldXfm.m.y.z; scene.view[10] = worldXfm.m.z.z; scene.view[11] = 0;
        float tx = -(worldXfm.m.x.x * worldXfm.v.x + worldXfm.m.x.y * worldXfm.v.y + worldXfm.m.x.z * worldXfm.v.z);
        float ty = -(worldXfm.m.y.x * worldXfm.v.x + worldXfm.m.y.y * worldXfm.v.y + worldXfm.m.y.z * worldXfm.v.z);
        float tz = -(worldXfm.m.z.x * worldXfm.v.x + worldXfm.m.z.y * worldXfm.v.y + worldXfm.m.z.z * worldXfm.v.z);
        scene.view[12] = tx; scene.view[13] = ty; scene.view[14] = tz; scene.view[15] = 1;

        // Camera position
        scene.cameraPos[0] = worldXfm.v.x;
        scene.cameraPos[1] = worldXfm.v.y;
        scene.cameraPos[2] = worldXfm.v.z;
    } else {
        // Identity viewProj if no camera
        scene.viewProj[0] = scene.viewProj[5] = scene.viewProj[10] = scene.viewProj[15] = 1;
        scene.view[0] = scene.view[5] = scene.view[10] = scene.view[15] = 1;
    }

    // Environment (fog, ambient, lights)
    RndEnviron* env = RndEnviron::Current();
    if (env) {
        // Ambient color (with minimum floor for visibility)
        const Hmx::Color& amb = env->AmbientColor();
        float minAmbient = 0.35f;
        scene.ambientColor[0] = amb.red > minAmbient ? amb.red : minAmbient;
        scene.ambientColor[1] = amb.green > minAmbient ? amb.green : minAmbient;
        scene.ambientColor[2] = amb.blue > minAmbient ? amb.blue : minAmbient;
        scene.ambientColor[3] = 1.0f;

        // Fog
        if (env->FogEnable()) {
            scene.fogEnabled = 1.0f;
            scene.fogStart = env->FogStart();
            scene.fogEnd = env->FogEnd();
            const Hmx::Color& fc = env->FogColor();
            scene.fogColor[0] = fc.red;
            scene.fogColor[1] = fc.green;
            scene.fogColor[2] = fc.blue;
        }

        // First directional light — try to get from the environment's light list
        // For Tier 1, use a default three-quarter light for good visibility
        scene.lightDir[0] = -0.4f;
        scene.lightDir[1] = -0.7f;
        scene.lightDir[2] = 0.5f;
        scene.lightColor[0] = scene.lightColor[1] = scene.lightColor[2] = 1.0f;
    } else {
        // Default lighting
        scene.ambientColor[0] = scene.ambientColor[1] = scene.ambientColor[2] = 0.35f;
        scene.ambientColor[3] = 1.0f;
        scene.lightDir[0] = -0.4f;
        scene.lightDir[1] = -0.7f;
        scene.lightDir[2] = 0.5f;
        scene.lightColor[0] = scene.lightColor[1] = scene.lightColor[2] = 0.9f;
    }

    // Upload scene uniforms
    mGpu.Queue().WriteBuffer(mSceneBuffer, 0, &scene, sizeof(scene));

    // Create scene bind group (group 0)
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = mSceneBuffer;
    entry.offset = 0;
    entry.size = sizeof(SceneUniforms);

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = mPipelines.SceneLayout();
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    mSceneBindGroup = mGpu.Device().CreateBindGroup(&bgDesc);
}

wgpu::BindGroup WgpuRnd::CreateMaterialBindGroup(
    uint32_t bufferOffset, uint32_t bufferSize,
    wgpu::TextureView& texView, wgpu::Sampler& sampler)
{
    wgpu::BindGroupEntry entries[3] = {};

    entries[0].binding = 0;
    entries[0].buffer = mMaterialRing.Buffer();
    entries[0].offset = bufferOffset;
    entries[0].size = bufferSize;

    entries[1].binding = 1;
    entries[1].textureView = texView;

    entries[2].binding = 2;
    entries[2].sampler = sampler;

    wgpu::BindGroupDescriptor desc{};
    desc.layout = mPipelines.MaterialLayout();
    desc.entryCount = 3;
    desc.entries = entries;

    return mGpu.Device().CreateBindGroup(&desc);
}

wgpu::BindGroup WgpuRnd::CreateObjectBindGroup(uint32_t bufferOffset, uint32_t bufferSize) {
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = mObjectRing.Buffer();
    entry.offset = bufferOffset;
    entry.size = bufferSize;

    wgpu::BindGroupDescriptor desc{};
    desc.layout = mPipelines.ObjectLayout();
    desc.entryCount = 1;
    desc.entries = &entry;

    return mGpu.Device().CreateBindGroup(&desc);
}
