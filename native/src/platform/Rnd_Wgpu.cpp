// DC3 Native Port — WebGPU Renderer Implementation
// Replaces Rnd_Stub.cpp with real WebGPU rendering via Dawn

#include "platform/Rnd_Wgpu.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "gfx/Screenshot.h"
#include "rndobj/Cam.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Mat.h"
#include "rndobj/PostProc.h"
#include "rndobj/ColorXfm.h"
#include "ui/UI.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

// ============================================================================
// Global instances
// ============================================================================

static WgpuShaderMgr gWgpuShaderMgr;
static WgpuRnd gWgpuRndInstance;

Rnd& TheRnd = gWgpuRndInstance;
NgRnd& TheNgRnd = gWgpuRndInstance;
RndShaderMgr& TheShaderMgr = gWgpuShaderMgr;
WgpuRnd* gWgpuRnd = &gWgpuRndInstance;

// Exposed for input subsystem (Joypad_Native, Keyboard_Native)
GLFWwindow *gNativeWindow = nullptr;

UIManager* TheUI = nullptr;
int gDebugFrameID = 0;

// ============================================================================
// UniformRingBuffer
// ============================================================================

void UniformRingBuffer::Init(wgpu::Device& device, uint32_t capacity) {
    mDevice = device;
    wgpu::BufferDescriptor desc{};
    desc.size = capacity;
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mBuffer = device.CreateBuffer(&desc);
    mCapacity = capacity;
    mOffset = 0;
}

void UniformRingBuffer::Grow(wgpu::Device& device) {
    uint32_t newCapacity = mCapacity * 2;
    fprintf(stderr, "UniformRingBuffer: growing %u -> %u bytes\n", mCapacity, newCapacity);

    wgpu::BufferDescriptor desc{};
    desc.size = newCapacity;
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    // Old buffer stays alive until GPU is done with current frame (ref-counted by Dawn)
    mBuffer = device.CreateBuffer(&desc);
    mCapacity = newCapacity;
    mOffset = 0;
}

uint32_t UniformRingBuffer::Write(wgpu::Queue& queue, const void* data, uint32_t size) {
    uint32_t alignedSize = (size + kAlignment - 1) & ~(kAlignment - 1);
    if (mOffset + alignedSize > mCapacity) {
        Grow(mDevice);
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
    // Skip GPU initialization unless MILO_RENDER is set (Phase 1A: just reach main loop)
    if (getenv("MILO_RENDER")) {
        desc.headless = (getenv("MILO_HEADLESS") != nullptr);
        desc.width = getenv("MILO_WIDTH") ? atoi(getenv("MILO_WIDTH")) : 1280;
        desc.height = getenv("MILO_HEIGHT") ? atoi(getenv("MILO_HEIGHT")) : 720;
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
        gNativeWindow = mGpu.Window();

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
        // Bone ring needs more space: 2560 bytes per skinned draw (rounded to 2816 at 256 alignment)
        mBoneRing.Init(mGpu.Device(), 256 * 1024);

        // Create depth texture
        CreateDepthTexture(mWidth, mHeight);

        // Create default textures (1x1 white for untextured materials)
        CreateDefaultTextures();

        printf("DC3 Native: WgpuRnd initialized (%dx%d, %s)\n",
               mWidth, mHeight, mGpu.HasBCCompression() ? "BC supported" : "software DXT");

        // Auto-screenshot setup (env-var controlled)
        const char* ssDir = getenv("MILO_SCREENSHOT_DIR");
        if (ssDir && ssDir[0]) {
            mScreenshotDir = ssDir;
            const char* ssFrames = getenv("MILO_SCREENSHOT_FRAMES");
            if (!ssFrames || !ssFrames[0]) ssFrames = "100,600,900,1500";
            std::istringstream iss(ssFrames);
            std::string token;
            while (std::getline(iss, token, ',')) {
                int frame = atoi(token.c_str());
                if (frame > 0) mCaptureFrames.push_back(frame);
            }
            mCaptureIndex = 0;
            printf("DC3 Native: auto-screenshot enabled — dir=%s frames=", mScreenshotDir.c_str());
            for (size_t i = 0; i < mCaptureFrames.size(); i++) {
                if (i > 0) printf(",");
                printf("%d", mCaptureFrames[i]);
            }
            printf("\n");
        }
    } else {
        printf("DC3 Native: GPU init skipped (set MILO_RENDER=1 to enable)\n");
        mWidth = 1280;
        mHeight = 720;
    }
}

void WgpuRnd::Terminate() {
    gNativeWindow = nullptr;
    mDepthTex = nullptr;
    mDepthView = nullptr;
    mWhiteTex = nullptr;
    mWhiteTexView = nullptr;
    mFlatNormalTex = nullptr;
    mFlatNormalTexView = nullptr;
    mBlackTex = nullptr;
    mBlackTexView = nullptr;
    mBlackCubeTex = nullptr;
    mBlackCubeTexView = nullptr;
    mDefaultSampler = nullptr;
    mSceneBuffer = nullptr;
    mSceneBindGroup = nullptr;
    mGpu.Shutdown();
}

void WgpuRnd::Clear(unsigned int flags, const Hmx::Color& color) {
    mWgpuClearColor = color;
}

// Defined in Mesh_Wgpu.cpp
extern void RndMesh_ResetFrameStats();

void WgpuRnd::BeginDrawing() {
    RndMesh_ResetFrameStats();

    // Skip if GPU not initialized (Phase 1A headless mode)
    if (!mGpu.Device()) {
        mDrawing = true;
        mWorldEnded = false;
        mDrawCount++;
        mFrameID++;
        return;
    }
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
    extern int gDebugFrameID;
    gDebugFrameID = mFrameID;

    // Select default camera and environment (base Rnd::BeginDrawing does this)
    // Only if no camera is already current (viewer sets its own orbit camera)
    if (mDefaultCam && !RndCam::Current())
        mDefaultCam->Select();
    if (mDefaultEnv && !RndEnviron::Current())
        mDefaultEnv->Select(nullptr);

    // Reset ring buffers for this frame
    mMaterialRing.Reset();
    mObjectRing.Reset();
    mBoneRing.Reset();

    // Acquire next frame
    if (mGpu.IsHeadless()) {
        mFrameView = mGpu.AcquireHeadlessFrame();
    } else {
        mFrameView = mGpu.AcquireNextFrame();
    }
    if (!mFrameView) {
        static int sFailCount = 0;
        if (sFailCount < 3) {
            printf("DC3 Native: BeginDrawing — frame acquisition failed (headless=%d, frame=%d)\n",
                   mGpu.IsHeadless(), mFrameID);
            sFailCount++;
        }
        return;
    }

    // Resize depth/MSAA textures if window size changed
    int curW = mGpu.WindowWidth();
    int curH = mGpu.WindowHeight();
    if (curW != mDepthWidth || curH != mDepthHeight) {
        CreateDepthTexture(curW, curH);
        mWidth = curW;
        mHeight = curH;
    }
    // Ensure MSAA color target exists (format may not be known until first frame)
    if (mMsaaWidth != curW || mMsaaHeight != curH || !mMsaaTex) {
        wgpu::TextureDescriptor desc{};
        desc.size.width = curW;
        desc.size.height = curH;
        desc.size.depthOrArrayLayers = 1;
        desc.format = mGpu.SurfaceFormat();
        desc.usage = wgpu::TextureUsage::RenderAttachment;
        desc.mipLevelCount = 1;
        desc.sampleCount = kMSAASamples;
        mMsaaTex = mGpu.Device().CreateTexture(&desc);
        mMsaaView = mMsaaTex.CreateView();
        mMsaaWidth = curW;
        mMsaaHeight = curH;
    }

    // Write scene uniforms from current camera and environment
    WriteSceneUniforms();
    mLastSceneCam = RndCam::Current();
    // Debug: log camera state every 500 frames
    if (mFrameID % 500 == 0) {
        RndCam* dbgCam = RndCam::Current();
        if (dbgCam) {
            printf("DC3 Debug [frame %d]: cam='%s' near=%.2f far=%.2f yfov=%.4f pos=(%.2f,%.2f,%.2f)\n",
                   mFrameID, dbgCam->Name(),
                   dbgCam->NearPlane(), dbgCam->FarPlane(), dbgCam->YFov(),
                   dbgCam->WorldXfm().v.x, dbgCam->WorldXfm().v.y, dbgCam->WorldXfm().v.z);
        }
    }

    // Create command encoder
    mEncoder = mGpu.Device().CreateCommandEncoder();

    // Begin render pass
    wgpu::RenderPassColorAttachment colorAtt{};
    colorAtt.view = mMsaaView;            // Render to MSAA target
    // If post-processing available, resolve to intermediate; otherwise direct to swapchain
    bool hasPostProc = RndPostProc::Current() != nullptr;
    if (hasPostProc) {
        // Ensure intermediate texture exists
        if (mIntermediateWidth != curW || mIntermediateHeight != curH || !mIntermediateTex) {
            wgpu::TextureDescriptor iDesc{};
            iDesc.size = {(uint32_t)curW, (uint32_t)curH, 1};
            iDesc.format = mGpu.SurfaceFormat();
            iDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
            iDesc.mipLevelCount = 1;
            mIntermediateTex = mGpu.Device().CreateTexture(&iDesc);
            mIntermediateView = mIntermediateTex.CreateView();
            mIntermediateWidth = curW;
            mIntermediateHeight = curH;
        }
        colorAtt.resolveTarget = mIntermediateView;
    } else {
        colorAtt.resolveTarget = mFrameView;
    }
    colorAtt.loadOp = wgpu::LoadOp::Clear;
    colorAtt.storeOp = wgpu::StoreOp::Discard;  // MSAA data discarded after resolve
    colorAtt.clearValue = {
        mWgpuClearColor.red,
        mWgpuClearColor.green,
        mWgpuClearColor.blue,
        1.0  // Always clear with opaque alpha (PNG/readback needs alpha=1)
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

extern void FlushTransparentDraws();

wgpu::Buffer& GetSceneBuffer() { return gWgpuRnd->SceneBuffer(); }

void WgpuRnd::EnsureSceneUniformsCurrent() {
    RndCam* cam = RndCam::Current();
    if (cam != mLastSceneCam) {
        WriteSceneUniforms();
        // Re-bind the new scene bind group on the active render pass
        if (mInPass) {
            mPass.SetBindGroup(0, mSceneBindGroup);
        }
        mLastSceneCam = cam;
    }
}

void WgpuRnd::EndDrawing() {
    if (!mGpu.Device()) {
        mDrawing = false;
        return;
    }
    if (mInPass) {
        // Flush deferred transparent draws (sorted back-to-front)
        FlushTransparentDraws();

        mPass.End();
        mInPass = false;

        // Post-processing: if active, read from intermediate and draw to swapchain
        if (mIntermediateView && RndPostProc::Current()) {
            RunPostProcessing();
        }

        wgpu::CommandBuffer cmd = mEncoder.Finish();
        mGpu.Queue().Submit(1, &cmd);

        MaybeCaptureFrame();

        if (!mGpu.IsHeadless()) {
            mGpu.PresentFrame();
        }
    }

    mFrameView = nullptr;
    mDrawing = false;
}

void WgpuRnd::CreateDepthTexture(int w, int h) {
    if (w <= 0 || h <= 0) return;

    // Depth texture (MSAA)
    {
        wgpu::TextureDescriptor desc{};
        desc.size.width = w;
        desc.size.height = h;
        desc.size.depthOrArrayLayers = 1;
        desc.format = wgpu::TextureFormat::Depth24PlusStencil8;
        desc.usage = wgpu::TextureUsage::RenderAttachment;
        desc.mipLevelCount = 1;
        desc.sampleCount = kMSAASamples;

        mDepthTex = mGpu.Device().CreateTexture(&desc);
        mDepthView = mDepthTex.CreateView();
        mDepthWidth = w;
        mDepthHeight = h;
    }

}

void WgpuRnd::CreateDefaultTextures() {
    // 1x1 white texture for untextured materials
    {
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
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

    // 1x1 flat normal texture (tangent-space up: 128,128,255)
    {
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm; // linear, not sRGB
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        mFlatNormalTex = mGpu.Device().CreateTexture(&desc);
        mFlatNormalTexView = mFlatNormalTex.CreateView();

        uint8_t flatNormal[4] = {128, 128, 255, 255};
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = mFlatNormalTex;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        wgpu::Extent3D extent = {1, 1, 1};
        mGpu.Queue().WriteTexture(&dst, flatNormal, 4, &layout, &extent);
    }

    // 1x1 black texture (no emission)
    {
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        mBlackTex = mGpu.Device().CreateTexture(&desc);
        mBlackTexView = mBlackTex.CreateView();

        uint8_t black[4] = {0, 0, 0, 255};
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = mBlackTex;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        wgpu::Extent3D extent = {1, 1, 1};
        mGpu.Queue().WriteTexture(&dst, black, 4, &layout, &extent);
    }

    // 1x1x6 black cube texture (no environment reflection)
    {
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 6};
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        desc.mipLevelCount = 1;
        mBlackCubeTex = mGpu.Device().CreateTexture(&desc);

        uint8_t black[4] = {0, 0, 0, 255};
        for (uint32_t face = 0; face < 6; face++) {
            wgpu::TexelCopyTextureInfo dst{};
            dst.texture = mBlackCubeTex;
            dst.origin = {0, 0, face};
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow = 4;
            layout.rowsPerImage = 1;
            wgpu::Extent3D extent = {1, 1, 1};
            mGpu.Queue().WriteTexture(&dst, black, 4, &layout, &extent);
        }

        wgpu::TextureViewDescriptor viewDesc{};
        viewDesc.dimension = wgpu::TextureViewDimension::Cube;
        viewDesc.arrayLayerCount = 6;
        mBlackCubeTexView = mBlackCubeTex.CreateView(&viewDesc);
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
        // Check if mViewProjMatrix was externally set (milo-viewer does this).
        const Hmx::Matrix4& vp = cam->GetViewProjMatrix();
        bool isIdentity = (vp.x.x == 1 && vp.x.y == 0 && vp.x.z == 0 && vp.x.w == 0 &&
                           vp.y.x == 0 && vp.y.y == 1 && vp.y.z == 0 && vp.y.w == 0 &&
                           vp.z.x == 0 && vp.z.y == 0 && vp.z.z == 1 && vp.z.w == 0 &&
                           vp.w.x == 0 && vp.w.y == 0 && vp.w.z == 0 && vp.w.w == 1);

        if (!isIdentity) {
            // Use externally-set viewProj (milo-viewer orbit cam path)
            memcpy(scene.viewProj, &vp, 64);
        } else {
            // Build WebGPU-compatible viewProj from Milo camera state.
            // Milo convention: X=right, Y=forward/depth, Z=up.
            // WebGPU clip space: X=right, Y=up, Z=depth [0,1].

            // View matrix: inverse of camera world transform (row-major)
            // Transpose rotation (orthonormal), negate translated position
            const Transform& w = cam->WorldXfm();
            float view[16] = {
                w.m.x.x, w.m.y.x, w.m.z.x, 0,
                w.m.x.y, w.m.y.y, w.m.z.y, 0,
                w.m.x.z, w.m.y.z, w.m.z.z, 0,
                -(w.m.x.x*w.v.x + w.m.x.y*w.v.y + w.m.x.z*w.v.z),
                -(w.m.y.x*w.v.x + w.m.y.y*w.v.y + w.m.y.z*w.v.z),
                -(w.m.z.x*w.v.x + w.m.z.y*w.v.y + w.m.z.z*w.v.z),
                1
            };

            // Perspective projection with Milo axis convention
            // Milo: X=right, Y=forward(depth), Z=up
            // Map to clip: X→X, Z→Y(up), Y→Z(depth [0,1])
            float n = cam->NearPlane();
            float f = cam->FarPlane();
            float yfov = cam->YFov();
            float aspect = (float)mWidth / (float)mHeight;
            float cot = 1.0f / tanf(yfov * 0.5f);
            float zRange = f - n;

            float proj[16] = {
                cot / aspect, 0,   0,                0,
                0,            0,   f / zRange,       1,
                0,            cot, 0,                0,
                0,            0,   -n * f / zRange,  0
            };

            // ViewProj = View * Proj (row-major multiply)
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) {
                    float sum = 0;
                    for (int k = 0; k < 4; k++) {
                        sum += view[i * 4 + k] * proj[k * 4 + j];
                    }
                    scene.viewProj[i * 4 + j] = sum;
                }
            }
            memcpy(scene.view, view, sizeof(view));
        }

        // Camera position (in world space, before axis flip)
        const Transform& worldXfm = cam->WorldXfm();
        scene.cameraPos[0] = worldXfm.v.x;
        scene.cameraPos[1] = worldXfm.v.y;
        scene.cameraPos[2] = worldXfm.v.z;

        // Debug: dump viewProj matrix once
        {
            static int sVPLog = 0;
            if (sVPLog < 3) {
                printf("DC3 ViewProj cam='%s': [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]\n",
                       cam->Name(),
                       scene.viewProj[0], scene.viewProj[1], scene.viewProj[2], scene.viewProj[3],
                       scene.viewProj[4], scene.viewProj[5], scene.viewProj[6], scene.viewProj[7],
                       scene.viewProj[8], scene.viewProj[9], scene.viewProj[10], scene.viewProj[11],
                       scene.viewProj[12], scene.viewProj[13], scene.viewProj[14], scene.viewProj[15]);
                sVPLog++;
            }
        }
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
        float minAmbient = 0.15f;
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

        // Read directional lights from the environment's real light list
        int lightIdx = 0;
        ObjPtrList<RndLight>& lights = env->LightsReal();
        for (ObjPtrList<RndLight>::iterator it = lights.begin();
             it != lights.end() && lightIdx < 4; ++it) {
            RndLight* light = *it;
            if (!light || !light->Showing()) continue;
            if (light->GetType() != RndLight::kDirectional) continue;

            // Light direction = Y-axis of the light's world transform
            const Transform& lxfm = light->WorldXfm();
            scene.lightDirs[lightIdx][0] = lxfm.m.y.x;
            scene.lightDirs[lightIdx][1] = lxfm.m.y.y;
            scene.lightDirs[lightIdx][2] = lxfm.m.y.z;
            scene.lightDirs[lightIdx][3] = 0.0f;

            const Hmx::Color& lc = light->GetColor();
            scene.lightColors[lightIdx][0] = lc.red;
            scene.lightColors[lightIdx][1] = lc.green;
            scene.lightColors[lightIdx][2] = lc.blue;
            scene.lightColors[lightIdx][3] = 1.0f;
            lightIdx++;
        }

        // Fallback: if no lights found, use a default three-quarter light
        if (lightIdx == 0) {
            scene.lightDirs[0][0] = -0.4f;
            scene.lightDirs[0][1] = -0.7f;
            scene.lightDirs[0][2] = 0.5f;
            scene.lightDirs[0][3] = 0.0f;
            scene.lightColors[0][0] = scene.lightColors[0][1] = scene.lightColors[0][2] = 0.9f;
            scene.lightColors[0][3] = 1.0f;
            lightIdx = 1;
        }
        scene.numLights = (float)lightIdx;

        // Point lights
        int pointIdx = 0;
        for (ObjPtrList<RndLight>::iterator it = lights.begin();
             it != lights.end() && pointIdx < 4; ++it) {
            RndLight* light = *it;
            if (!light || !light->Showing()) continue;
            if (light->GetType() != RndLight::kPoint) continue;

            const Transform& lxfm = light->WorldXfm();
            scene.pointLightPos[pointIdx][0] = lxfm.v.x;
            scene.pointLightPos[pointIdx][1] = lxfm.v.y;
            scene.pointLightPos[pointIdx][2] = lxfm.v.z;
            scene.pointLightPos[pointIdx][3] = 0.0f;

            const Hmx::Color& lc = light->GetColor();
            scene.pointLightColors[pointIdx][0] = lc.red;
            scene.pointLightColors[pointIdx][1] = lc.green;
            scene.pointLightColors[pointIdx][2] = lc.blue;
            scene.pointLightColors[pointIdx][3] = 1.0f;

            scene.pointLightRanges[pointIdx] = light->Range();
            pointIdx++;
        }
        scene.numPointLights = (float)pointIdx;
    } else {
        // Default lighting — single directional light
        scene.ambientColor[0] = scene.ambientColor[1] = scene.ambientColor[2] = 0.15f;
        scene.ambientColor[3] = 1.0f;
        scene.lightDirs[0][0] = -0.4f;
        scene.lightDirs[0][1] = -0.7f;
        scene.lightDirs[0][2] = 0.5f;
        scene.lightDirs[0][3] = 0.0f;
        scene.lightColors[0][0] = scene.lightColors[0][1] = scene.lightColors[0][2] = 0.9f;
        scene.lightColors[0][3] = 1.0f;
        scene.numLights = 1.0f;
    }

    // Debug: dump viewProj at frame 3000
    if (mFrameID == 3000) {
        printf("DC3 ViewProj@3000: cam='%s'\n", cam ? cam->Name() : "NULL");
        printf("  [%.4f %.4f %.4f %.4f]\n", scene.viewProj[0], scene.viewProj[1], scene.viewProj[2], scene.viewProj[3]);
        printf("  [%.4f %.4f %.4f %.4f]\n", scene.viewProj[4], scene.viewProj[5], scene.viewProj[6], scene.viewProj[7]);
        printf("  [%.4f %.4f %.4f %.4f]\n", scene.viewProj[8], scene.viewProj[9], scene.viewProj[10], scene.viewProj[11]);
        printf("  [%.4f %.4f %.4f %.4f]\n", scene.viewProj[12], scene.viewProj[13], scene.viewProj[14], scene.viewProj[15]);
        printf("  ambient=(%.2f,%.2f,%.2f) numLights=%.0f\n",
               scene.ambientColor[0], scene.ambientColor[1], scene.ambientColor[2], scene.numLights);
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
    const MaterialTexViews& texViews,
    wgpu::Sampler& diffuseSampler, wgpu::Sampler& mapSampler)
{
    wgpu::BindGroupEntry entries[11] = {};

    entries[0].binding = 0;
    entries[0].buffer = mMaterialRing.Buffer();
    entries[0].offset = bufferOffset;
    entries[0].size = bufferSize;

    entries[1].binding = 1;
    entries[1].textureView = texViews.diffuse;

    entries[2].binding = 2;
    entries[2].sampler = diffuseSampler;

    entries[3].binding = 3;
    entries[3].textureView = texViews.normal;

    entries[4].binding = 4;
    entries[4].textureView = texViews.specular;

    entries[5].binding = 5;
    entries[5].textureView = texViews.emissive;

    entries[6].binding = 6;
    entries[6].textureView = texViews.rim;

    entries[7].binding = 7;
    entries[7].sampler = mapSampler;

    entries[8].binding = 8;
    entries[8].textureView = texViews.environCube;

    entries[9].binding = 9;
    entries[9].sampler = mapSampler;  // reuse map sampler for cube

    entries[10].binding = 10;
    entries[10].textureView = texViews.normDetail;

    wgpu::BindGroupDescriptor desc{};
    desc.layout = mPipelines.MaterialLayout();
    desc.entryCount = 11;
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

wgpu::BindGroup WgpuRnd::CreateBoneBindGroup(uint32_t bufferOffset, uint32_t bufferSize) {
    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = mBoneRing.Buffer();
    entry.offset = bufferOffset;
    entry.size = bufferSize;

    wgpu::BindGroupDescriptor desc{};
    desc.layout = mPipelines.BoneLayout();
    desc.entryCount = 1;
    desc.entries = &entry;

    return mGpu.Device().CreateBindGroup(&desc);
}

void WgpuRnd::MaybeCaptureFrame() {
    if (mCaptureIndex >= (int)mCaptureFrames.size()) return;
    static int sLogCount = 0;
    if (sLogCount < 5) {
        printf("DC3 Native: MaybeCaptureFrame mFrameID=%d, next target=%d\n",
               mFrameID, mCaptureFrames[mCaptureIndex]);
        sLogCount++;
    }
    if (mFrameID != mCaptureFrames[mCaptureIndex]) return;

    int w = mGpu.WindowWidth();
    int h = mGpu.WindowHeight();
    size_t pixelSize = (size_t)w * h * 4;
    uint8_t* pixels = (uint8_t*)malloc(pixelSize);
    if (!pixels) return;

    if (mGpu.ReadbackHeadlessFrame(pixels, pixelSize)) {
        // Debug: count non-black pixels (check RGB, not alpha)
        int nonBlack = 0;
        for (int i = 0; i < w * h; i++) {
            if (pixels[i*4] || pixels[i*4+1] || pixels[i*4+2]) nonBlack++;
        }
        printf("DC3 Native: frame %d readback — %d/%d non-black pixels, alpha[0]=%d\n",
               mFrameID, nonBlack, w*h, pixels[3]);
        char path[512];
        snprintf(path, sizeof(path), "%s/frame_%05d.png", mScreenshotDir.c_str(), mFrameID);
        if (WriteScreenshot(path, pixels, w, h)) {
            printf("DC3 Native: captured frame %d -> %s\n", mFrameID, path);
        } else {
            fprintf(stderr, "DC3 Native: failed to write screenshot %s\n", path);
        }
    } else {
        fprintf(stderr, "DC3 Native: failed to readback frame %d (headless mode required)\n", mFrameID);
    }

    free(pixels);
    mCaptureIndex++;
}

// ============================================================================
// DrawRect — screen-space 2D textured/colored quad
// ============================================================================

static const char* k2DShaderSource = R"WGSL(
struct Vertex2D {
    @location(0) pos: vec2f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};

struct VSOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

@vertex fn vs_2d(in: Vertex2D) -> VSOut {
    var out: VSOut;
    out.pos = vec4f(in.pos, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

@group(0) @binding(0) var rectTex: texture_2d<f32>;
@group(0) @binding(1) var rectSampler: sampler;

@fragment fn fs_2d(in: VSOut) -> @location(0) vec4f {
    let texColor = textureSample(rectTex, rectSampler, in.uv);
    return texColor * in.color;
}

@fragment fn fs_2d_notex(in: VSOut) -> @location(0) vec4f {
    return in.color;
}
)WGSL";

struct Vertex2D {
    float pos[2];
    float uv[2];
    float color[4];
};

void WgpuRnd::EnsureDrawRect2DPipeline() {
    if (m2dPipelineReady) return;

    auto& dev = mGpu.Device();

    // Shader
    wgpu::ShaderSourceWGSL wgslSource;
    wgslSource.code = k2DShaderSource;
    wgpu::ShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = &wgslSource;
    m2dShader = dev.CreateShaderModule(&smDesc);

    // Bind group layout: texture + sampler
    wgpu::BindGroupLayoutEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 2;
    bglDesc.entries = entries;
    m2dBindGroupLayout = dev.CreateBindGroupLayout(&bglDesc);

    wgpu::PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &m2dBindGroupLayout;
    m2dPipelineLayout = dev.CreatePipelineLayout(&plDesc);

    // Vertex buffer (6 vertices for a quad, rewritten each DrawRect)
    wgpu::BufferDescriptor vbDesc{};
    vbDesc.size = 6 * sizeof(Vertex2D);
    vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    m2dVertexBuffer = dev.CreateBuffer(&vbDesc);

    m2dPipelineReady = true;
}

void WgpuRnd::DrawRect(const Hmx::Rect& rect, RndMat* mat, ShaderType,
                        const Hmx::Color& color, const Hmx::Color* topRight,
                        const Hmx::Color* botLeft) {
    if (!mInPass) return;
    EnsureDrawRect2DPipeline();

    auto& dev = mGpu.Device();

    // Convert rect from screen coords [0..width, 0..height] to NDC [-1..1]
    float w = (float)mGpu.WindowWidth();
    float h = (float)mGpu.WindowHeight();
    if (w <= 0 || h <= 0) return;

    float x0 = rect.x / w * 2.0f - 1.0f;
    float y0 = 1.0f - rect.y / h * 2.0f;  // flip Y
    float x1 = (rect.x + rect.w) / w * 2.0f - 1.0f;
    float y1 = 1.0f - (rect.y + rect.h) / h * 2.0f;  // flip Y

    // Colors for four corners (support gradient)
    float cTL[4] = { color.red, color.green, color.blue, color.alpha };
    float cTR[4], cBL[4], cBR[4];
    if (topRight) {
        cTR[0] = topRight->red; cTR[1] = topRight->green;
        cTR[2] = topRight->blue; cTR[3] = topRight->alpha;
    } else {
        memcpy(cTR, cTL, sizeof(cTL));
    }
    if (botLeft) {
        cBL[0] = botLeft->red; cBL[1] = botLeft->green;
        cBL[2] = botLeft->blue; cBL[3] = botLeft->alpha;
    } else {
        memcpy(cBL, cTL, sizeof(cTL));
    }
    // Bottom-right = average of topRight and botLeft
    cBR[0] = (cTR[0] + cBL[0]) * 0.5f;
    cBR[1] = (cTR[1] + cBL[1]) * 0.5f;
    cBR[2] = (cTR[2] + cBL[2]) * 0.5f;
    cBR[3] = (cTR[3] + cBL[3]) * 0.5f;

    // Two triangles: TL, BL, TR, TR, BL, BR
    Vertex2D verts[6] = {
        {{x0, y0}, {0, 0}, {cTL[0], cTL[1], cTL[2], cTL[3]}},
        {{x0, y1}, {0, 1}, {cBL[0], cBL[1], cBL[2], cBL[3]}},
        {{x1, y0}, {1, 0}, {cTR[0], cTR[1], cTR[2], cTR[3]}},
        {{x1, y0}, {1, 0}, {cTR[0], cTR[1], cTR[2], cTR[3]}},
        {{x0, y1}, {0, 1}, {cBL[0], cBL[1], cBL[2], cBL[3]}},
        {{x1, y1}, {1, 1}, {cBR[0], cBR[1], cBR[2], cBR[3]}},
    };

    mGpu.Queue().WriteBuffer(m2dVertexBuffer, 0, verts, sizeof(verts));

    // Determine if textured
    bool hasTex = false;
    wgpu::TextureView texView;
    if (mat && mat->GetDiffuseTex()) {
        extern wgpu::TextureView GetGpuTexView(RndTex*);
        texView = GetGpuTexView(mat->GetDiffuseTex());
        if (texView) hasTex = true;
    }
    if (!hasTex) texView = mWhiteTexView;

    // Create pipeline (cached by blend mode)
    WgpuBlend blend = WgpuBlend::SrcAlpha;
    if (mat) blend = (WgpuBlend)mat->GetBlend();

    wgpu::BlendState bs = mPipelines.MapBlend(blend);

    wgpu::ColorTargetState ct{};
    ct.format = mGpu.SurfaceFormat();
    ct.blend = &bs;
    ct.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState frag{};
    frag.module = m2dShader;
    frag.entryPoint = hasTex ? "fs_2d" : "fs_2d_notex";
    frag.targetCount = 1;
    frag.targets = &ct;

    // Vertex layout
    wgpu::VertexAttribute attrs[3] = {};
    attrs[0].format = wgpu::VertexFormat::Float32x2; attrs[0].offset = 0; attrs[0].shaderLocation = 0;
    attrs[1].format = wgpu::VertexFormat::Float32x2; attrs[1].offset = 8; attrs[1].shaderLocation = 1;
    attrs[2].format = wgpu::VertexFormat::Float32x4; attrs[2].offset = 16; attrs[2].shaderLocation = 2;

    wgpu::VertexBufferLayout vbl{};
    vbl.arrayStride = sizeof(Vertex2D);
    vbl.stepMode = wgpu::VertexStepMode::Vertex;
    vbl.attributeCount = 3;
    vbl.attributes = attrs;

    wgpu::DepthStencilState ds{};
    ds.format = wgpu::TextureFormat::Depth24PlusStencil8;
    ds.depthWriteEnabled = wgpu::OptionalBool::False;
    ds.depthCompare = wgpu::CompareFunction::Always;

    wgpu::RenderPipelineDescriptor pipeDesc{};
    pipeDesc.layout = m2dPipelineLayout;
    pipeDesc.vertex.module = m2dShader;
    pipeDesc.vertex.entryPoint = "vs_2d";
    pipeDesc.vertex.bufferCount = 1;
    pipeDesc.vertex.buffers = &vbl;
    pipeDesc.fragment = &frag;
    pipeDesc.depthStencil = &ds;
    pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipeDesc.multisample.count = 4;

    // TODO: cache this pipeline
    wgpu::RenderPipeline pipe = dev.CreateRenderPipeline(&pipeDesc);

    // Bind group
    wgpu::BindGroupEntry bgEntries[2] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = texView;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = mDefaultSampler;

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = m2dBindGroupLayout;
    bgDesc.entryCount = 2;
    bgDesc.entries = bgEntries;
    wgpu::BindGroup bg = dev.CreateBindGroup(&bgDesc);

    mPass.SetPipeline(pipe);
    mPass.SetBindGroup(0, bg);
    mPass.SetVertexBuffer(0, m2dVertexBuffer, 0, sizeof(verts));
    mPass.Draw(6);
}

// ============================================================================
// Post-Processing
// ============================================================================

struct PostProcUniforms {
    float contrast;          // 0=neutral
    float brightness;        // 0=neutral
    float saturation;        // 0=neutral (added to 1.0 in shader)
    float vignetteIntensity; // 0=off
    float vignetteColor[4];  // rgba
    float chromaticOffset;   // pixels
    float chromaticSharpen;  // 1.0 if sharpen mode
    float posterLevels;      // 0=off
    float posterMin;         // min intensity
    // Color levels
    float levelInLo[4];
    float levelInHi[4];
    float levelOutLo[4];
    float levelOutHi[4];
};
static_assert(sizeof(PostProcUniforms) == 112, "PostProcUniforms must be 112 bytes");

static const char* kPostProcShaderSource = R"WGSL(
struct PostProcUB {
    contrast: f32,
    brightness: f32,
    saturation: f32,
    vignetteIntensity: f32,
    vignetteColor: vec4f,
    chromaticOffset: f32,
    chromaticSharpen: f32,
    posterLevels: f32,
    posterMin: f32,
    levelInLo: vec4f,
    levelInHi: vec4f,
    levelOutLo: vec4f,
    levelOutHi: vec4f,
};

@group(0) @binding(0) var sceneTex: texture_2d<f32>;
@group(0) @binding(1) var sceneSampler: sampler;
@group(0) @binding(2) var<uniform> pp: PostProcUB;

struct VOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

// Fullscreen triangle (no vertex buffer needed)
@vertex fn vs_postproc(@builtin(vertex_index) idx: u32) -> VOut {
    var out: VOut;
    // Generate fullscreen triangle: 3 vertices covering [-1,1] clip space
    let x = f32(i32(idx & 1u)) * 4.0 - 1.0;
    let y = f32(i32(idx >> 1u)) * 4.0 - 1.0;
    out.pos = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

@fragment fn fs_postproc(in: VOut) -> @location(0) vec4f {
    let texSize = vec2f(textureDimensions(sceneTex));

    // Chromatic aberration / sharpen
    var color: vec3f;
    if (pp.chromaticOffset > 0.0) {
        let offset = pp.chromaticOffset / texSize;
        let r = textureSample(sceneTex, sceneSampler, in.uv + vec2f(offset.x, 0.0)).r;
        let g = textureSample(sceneTex, sceneSampler, in.uv).g;
        let b = textureSample(sceneTex, sceneSampler, in.uv - vec2f(offset.x, 0.0)).b;
        if (pp.chromaticSharpen > 0.5) {
            // Sharpen: use center minus offset as unsharp mask
            let center = textureSample(sceneTex, sceneSampler, in.uv).rgb;
            let blur = vec3f(r, g, b);
            color = center + (center - blur) * 1.5;
        } else {
            color = vec3f(r, g, b);
        }
    } else {
        color = textureSample(sceneTex, sceneSampler, in.uv).rgb;
    }

    // Levels: remap input range to output range
    let inRange = max(pp.levelInHi.rgb - pp.levelInLo.rgb, vec3f(0.001));
    let normalized = clamp((color - pp.levelInLo.rgb) / inRange, vec3f(0.0), vec3f(1.0));
    color = mix(pp.levelOutLo.rgb, pp.levelOutHi.rgb, normalized);

    // Contrast: scale around mid-gray
    color = (color - 0.5) * (1.0 + pp.contrast / 100.0) + 0.5;

    // Brightness: additive shift
    color = color + pp.brightness / 100.0;

    // Saturation
    let luma = dot(color, vec3f(0.2126, 0.7152, 0.0722));
    color = mix(vec3f(luma), color, 1.0 + pp.saturation / 100.0);

    // Posterize
    if (pp.posterLevels > 1.0) {
        let levels = pp.posterLevels;
        let intensity = max(max(color.r, color.g), color.b);
        if (intensity >= pp.posterMin) {
            color = floor(color * levels + 0.5) / levels;
        }
    }

    // Vignette
    if (pp.vignetteIntensity > 0.0) {
        let center = in.uv - 0.5;
        let dist = length(center) * 1.414; // normalize so corners = 1.0
        let vig = 1.0 - smoothstep(0.4, 1.0, dist) * pp.vignetteIntensity;
        color = mix(pp.vignetteColor.rgb, color, vig);
    }

    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
)WGSL";

void WgpuRnd::EnsurePostProcPipeline() {
    if (mPostProcReady) return;
    auto& dev = mGpu.Device();

    // Shader
    wgpu::ShaderSourceWGSL src;
    src.code = kPostProcShaderSource;
    wgpu::ShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = &src;
    mPostProcShader = dev.CreateShaderModule(&smDesc);

    // Bind group layout: texture + sampler + uniforms
    wgpu::BindGroupLayoutEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    entries[1].binding = 1;
    entries[1].visibility = wgpu::ShaderStage::Fragment;
    entries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    entries[2].binding = 2;
    entries[2].visibility = wgpu::ShaderStage::Fragment;
    entries[2].buffer.type = wgpu::BufferBindingType::Uniform;
    entries[2].buffer.minBindingSize = sizeof(PostProcUniforms);

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    mPostProcBGL = dev.CreateBindGroupLayout(&bglDesc);

    wgpu::PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &mPostProcBGL;
    mPostProcPipelineLayout = dev.CreatePipelineLayout(&plDesc);

    // Uniform buffer
    wgpu::BufferDescriptor bufDesc{};
    bufDesc.size = sizeof(PostProcUniforms);
    bufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mPostProcUniformBuffer = dev.CreateBuffer(&bufDesc);

    // Pipeline (no depth, no MSAA, renders to swapchain directly)
    wgpu::ColorTargetState ct{};
    ct.format = mGpu.SurfaceFormat();
    ct.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState frag{};
    frag.module = mPostProcShader;
    frag.entryPoint = "fs_postproc";
    frag.targetCount = 1;
    frag.targets = &ct;

    wgpu::RenderPipelineDescriptor pipeDesc{};
    pipeDesc.layout = mPostProcPipelineLayout;
    pipeDesc.vertex.module = mPostProcShader;
    pipeDesc.vertex.entryPoint = "vs_postproc";
    pipeDesc.fragment = &frag;
    pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;

    mPostProcPipeline = dev.CreateRenderPipeline(&pipeDesc);
    mPostProcReady = true;
}

void WgpuRnd::RunPostProcessing() {
    EnsurePostProcPipeline();

    RndPostProc* pp = RndPostProc::Current();
    if (!pp) return;

    // Fill uniforms from PostProc
    PostProcUniforms uni{};
    const RndColorXfm& cxfm = pp->GetColorXfm();
    uni.contrast = cxfm.mContrast;
    uni.brightness = cxfm.mBrightness;
    uni.saturation = cxfm.mSaturation;
    uni.vignetteIntensity = pp->GetVignetteIntensity();
    const Hmx::Color& vc = pp->GetVignetteColor();
    uni.vignetteColor[0] = vc.red;
    uni.vignetteColor[1] = vc.green;
    uni.vignetteColor[2] = vc.blue;
    uni.vignetteColor[3] = vc.alpha;
    uni.chromaticOffset = pp->GetChromaticAberrationOffset();
    uni.chromaticSharpen = pp->GetChromaticSharpen() ? 1.0f : 0.0f;
    uni.posterLevels = pp->GetPosterLevels();
    uni.posterMin = pp->GetPosterMin();

    // Levels
    uni.levelInLo[0] = cxfm.mLevelInLo.red;   uni.levelInLo[1] = cxfm.mLevelInLo.green;
    uni.levelInLo[2] = cxfm.mLevelInLo.blue;   uni.levelInLo[3] = 0;
    uni.levelInHi[0] = cxfm.mLevelInHi.red;   uni.levelInHi[1] = cxfm.mLevelInHi.green;
    uni.levelInHi[2] = cxfm.mLevelInHi.blue;   uni.levelInHi[3] = 1;
    uni.levelOutLo[0] = cxfm.mLevelOutLo.red; uni.levelOutLo[1] = cxfm.mLevelOutLo.green;
    uni.levelOutLo[2] = cxfm.mLevelOutLo.blue; uni.levelOutLo[3] = 0;
    uni.levelOutHi[0] = cxfm.mLevelOutHi.red; uni.levelOutHi[1] = cxfm.mLevelOutHi.green;
    uni.levelOutHi[2] = cxfm.mLevelOutHi.blue; uni.levelOutHi[3] = 1;

    mGpu.Queue().WriteBuffer(mPostProcUniformBuffer, 0, &uni, sizeof(uni));

    // Create bind group
    wgpu::BindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = mIntermediateView;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = mDefaultSampler;
    bgEntries[2].binding = 2;
    bgEntries[2].buffer = mPostProcUniformBuffer;
    bgEntries[2].size = sizeof(PostProcUniforms);

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = mPostProcBGL;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    wgpu::BindGroup bg = mGpu.Device().CreateBindGroup(&bgDesc);

    // Render pass targeting swapchain (mFrameView)
    wgpu::RenderPassColorAttachment colorAtt{};
    colorAtt.view = mFrameView;
    colorAtt.loadOp = wgpu::LoadOp::Clear;
    colorAtt.storeOp = wgpu::StoreOp::Store;
    colorAtt.clearValue = {0, 0, 0, 1};

    wgpu::RenderPassDescriptor rpDesc{};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;

    auto pass = mEncoder.BeginRenderPass(&rpDesc);
    pass.SetPipeline(mPostProcPipeline);
    pass.SetBindGroup(0, bg);
    pass.Draw(3);  // Fullscreen triangle
    pass.End();
}
