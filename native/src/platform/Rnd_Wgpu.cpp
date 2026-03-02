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
#include "rndobj/Dir.h"
#include "rndobj/Mesh.h"
#include "gfx/VertexFormats.h"
#include "obj/Dir.h"
#include "ui/UI.h"

#include <algorithm>
#include <cmath>
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

        // Create shadow map resources early (depth texture starts cleared to 1.0 = fully lit)
        EnsureShadowPipelines();

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
    mLastSceneEnv = RndEnviron::Current();
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

    // Shadow pre-pass: render depth from light's perspective
    // RenderShadowPass();  // disabled: red-screen debug

    // Begin render pass
    wgpu::RenderPassColorAttachment colorAtt{};
    bool hasPostProc = RndPostProc::Current() != nullptr;
    if (kMSAASamples > 1) {
        colorAtt.view = mMsaaView;            // Render to MSAA target
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
        colorAtt.storeOp = wgpu::StoreOp::Discard;  // MSAA data discarded after resolve
    } else {
        // No MSAA — render directly to target
        if (hasPostProc) {
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
            colorAtt.view = mIntermediateView;
        } else {
            colorAtt.view = mFrameView;
        }
        colorAtt.storeOp = wgpu::StoreOp::Store;
    }
    colorAtt.loadOp = wgpu::LoadOp::Clear;
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
    RndEnviron* env = RndEnviron::Current();
    if (cam != mLastSceneCam || env != mLastSceneEnv) {
        WriteSceneUniforms();
        // Re-bind the new scene bind group on the active render pass
        if (mInPass) {
            mPass.SetBindGroup(0, mSceneBindGroup);
        }
        mLastSceneCam = cam;
        mLastSceneEnv = env;
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
            // Use engine's GetViewProjectXfms which accounts for mLocalProjectXfm,
            // screen rect, and the Y/Z axis flip (Milo Y-forward → D3D/WebGPU Z-forward).
            // Ensure mInvWorldXfm is up to date (may be zero if camera was never dirty).
            cam->UpdatedWorldXfm();

            Transform viewXfm;
            Hmx::Matrix4 projMtx;
            cam->GetViewProjectXfms(viewXfm, projMtx);

            // Convert Transform (3x4 row-major) to 4x4 view matrix
            float view[16] = {
                viewXfm.m.x.x, viewXfm.m.x.y, viewXfm.m.x.z, 0,
                viewXfm.m.y.x, viewXfm.m.y.y, viewXfm.m.y.z, 0,
                viewXfm.m.z.x, viewXfm.m.z.y, viewXfm.m.z.z, 0,
                viewXfm.v.x,   viewXfm.v.y,   viewXfm.v.z,   1
            };

            // Projection from engine (already handles FOV, aspect, screen rect, axis flip)
            float proj[16] = {
                projMtx.x.x, projMtx.x.y, projMtx.x.z, projMtx.x.w,
                projMtx.y.x, projMtx.y.y, projMtx.y.z, projMtx.y.w,
                projMtx.z.x, projMtx.z.y, projMtx.z.z, projMtx.z.w,
                projMtx.w.x, projMtx.w.y, projMtx.w.z, projMtx.w.w,
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

        // Read directional lights from the environment's approx light list.
        // RndEnviron::IsValidRealLight() classifies only kPoint and kFakeSpot
        // as "real" lights — directional lights always go into LightsApprox().
        int lightIdx = 0;
        ObjPtrList<RndLight>& approxLights = env->LightsApprox();
        for (ObjPtrList<RndLight>::iterator it = approxLights.begin();
             it != approxLights.end() && lightIdx < 4; ++it) {
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

        // Point lights (kPoint and kFakeSpot are in LightsReal)
        int pointIdx = 0;
        ObjPtrList<RndLight>& realLights = env->LightsReal();
        for (ObjPtrList<RndLight>::iterator it = realLights.begin();
             it != realLights.end() && pointIdx < 4; ++it) {
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

    // Shadow mapping data
    if (mShadowAvailable) {
        memcpy(scene.lightViewProj, mLightViewProj, 64);
        scene.shadowEnabled = 1.0f;
        scene.shadowBias = 0.002f;
        scene.shadowMapSize = (float)kShadowMapSize;
        scene.shadowStrength = 0.3f;  // minimum brightness in shadow
    }

    // Upload scene uniforms
    mGpu.Queue().WriteBuffer(mSceneBuffer, 0, &scene, sizeof(scene));

    // Create scene bind group (group 0) — includes shadow map texture + sampler
    wgpu::BindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = mSceneBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(SceneUniforms);

    entries[1].binding = 1;
    entries[1].textureView = mShadowDepthView;  // always valid after Init

    entries[2].binding = 2;
    entries[2].sampler = mShadowSampler;  // comparison sampler

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = mPipelines.SceneLayout();
    bgDesc.entryCount = 3;
    bgDesc.entries = entries;
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
    // Bloom
    float bloomIntensity;    // 0=no bloom
    float _pad0;
    float _pad1;
    float _pad2;
    float bloomColor[4];     // tint RGBA (w unused, for WGSL vec4f alignment)
};
static_assert(sizeof(PostProcUniforms) == 144, "PostProcUniforms must be 144 bytes");

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
    bloomIntensity: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
    bloomColor: vec4f,
};

@group(0) @binding(0) var sceneTex: texture_2d<f32>;
@group(0) @binding(1) var sceneSampler: sampler;
@group(0) @binding(2) var<uniform> pp: PostProcUB;
@group(0) @binding(3) var bloomTex: texture_2d<f32>;

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

    // Bloom composite
    if (pp.bloomIntensity > 0.0) {
        let bloom = textureSample(bloomTex, sceneSampler, in.uv).rgb;
        color += bloom * pp.bloomIntensity * pp.bloomColor.rgb;
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

    // Bind group layout: texture + sampler + uniforms + bloom texture
    wgpu::BindGroupLayoutEntry entries[4] = {};
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

    entries[3].binding = 3;
    entries[3].visibility = wgpu::ShaderStage::Fragment;
    entries[3].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 4;
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

    // Run DOF before bloom/composite (modifies mIntermediateView in-place via swap)
    RunDepthOfField();

    // Run bloom if active
    float bloomIntensity = pp->GetBloomIntensity();
    if (bloomIntensity > 0.0f) {
        RunBloom(bloomIntensity, pp->GetBloomThreshold(), pp->GetBloomColor());
    }

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

    // Bloom
    uni.bloomIntensity = bloomIntensity;
    const Hmx::Color& bc = pp->GetBloomColor();
    uni.bloomColor[0] = bc.red;
    uni.bloomColor[1] = bc.green;
    uni.bloomColor[2] = bc.blue;
    uni.bloomColor[3] = 1.0f;

    mGpu.Queue().WriteBuffer(mPostProcUniformBuffer, 0, &uni, sizeof(uni));

    // Bloom texture view — use bloom mip 0 if available, else black texture
    wgpu::TextureView bloomView = (bloomIntensity > 0.0f && mBloomView[0])
        ? mBloomView[0] : mBlackTexView;

    // Create bind group
    wgpu::BindGroupEntry bgEntries[4] = {};
    bgEntries[0].binding = 0;
    bgEntries[0].textureView = mIntermediateView;
    bgEntries[1].binding = 1;
    bgEntries[1].sampler = mDefaultSampler;
    bgEntries[2].binding = 2;
    bgEntries[2].buffer = mPostProcUniformBuffer;
    bgEntries[2].size = sizeof(PostProcUniforms);
    bgEntries[3].binding = 3;
    bgEntries[3].textureView = bloomView;

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.layout = mPostProcBGL;
    bgDesc.entryCount = 4;
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

// ============================================================================
// Bloom
// ============================================================================

struct BloomUniforms {
    float threshold;
    float texelSizeX;
    float texelSizeY;
    float intensity;
};
static_assert(sizeof(BloomUniforms) == 16, "BloomUniforms must be 16 bytes");

static const char* kBloomShaderSource = R"WGSL(
struct BloomUB {
    threshold: f32,
    texelSizeX: f32,
    texelSizeY: f32,
    intensity: f32,
};

@group(0) @binding(0) var srcTex: texture_2d<f32>;
@group(0) @binding(1) var srcSampler: sampler;
@group(0) @binding(2) var<uniform> bloom: BloomUB;

struct VOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

@vertex fn vs_bloom(@builtin(vertex_index) idx: u32) -> VOut {
    var out: VOut;
    let x = f32(i32(idx & 1u)) * 4.0 - 1.0;
    let y = f32(i32(idx >> 1u)) * 4.0 - 1.0;
    out.pos = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

// Threshold extract — keep pixels above luminance threshold
@fragment fn fs_bloom_threshold(in: VOut) -> @location(0) vec4f {
    let color = textureSample(srcTex, srcSampler, in.uv).rgb;
    let luma = dot(color, vec3f(0.2126, 0.7152, 0.0722));
    let contribution = max(luma - bloom.threshold, 0.0) / max(luma, 0.001);
    return vec4f(color * contribution, 1.0);
}

// 9-tap Gaussian blur (horizontal)
@fragment fn fs_bloom_blur_h(in: VOut) -> @location(0) vec4f {
    let ts = vec2f(bloom.texelSizeX, 0.0);
    var color = textureSample(srcTex, srcSampler, in.uv).rgb * 0.2270270270;
    color += textureSample(srcTex, srcSampler, in.uv + ts * 1.3846153846).rgb * 0.3162162162;
    color += textureSample(srcTex, srcSampler, in.uv - ts * 1.3846153846).rgb * 0.3162162162;
    color += textureSample(srcTex, srcSampler, in.uv + ts * 3.2307692308).rgb * 0.0702702703;
    color += textureSample(srcTex, srcSampler, in.uv - ts * 3.2307692308).rgb * 0.0702702703;
    return vec4f(color, 1.0);
}

// 9-tap Gaussian blur (vertical)
@fragment fn fs_bloom_blur_v(in: VOut) -> @location(0) vec4f {
    let ts = vec2f(0.0, bloom.texelSizeY);
    var color = textureSample(srcTex, srcSampler, in.uv).rgb * 0.2270270270;
    color += textureSample(srcTex, srcSampler, in.uv + ts * 1.3846153846).rgb * 0.3162162162;
    color += textureSample(srcTex, srcSampler, in.uv - ts * 1.3846153846).rgb * 0.3162162162;
    color += textureSample(srcTex, srcSampler, in.uv + ts * 3.2307692308).rgb * 0.0702702703;
    color += textureSample(srcTex, srcSampler, in.uv - ts * 3.2307692308).rgb * 0.0702702703;
    return vec4f(color, 1.0);
}

// Upsample + additive blend (reads source, adds to current render target via blend state)
@fragment fn fs_bloom_upsample(in: VOut) -> @location(0) vec4f {
    // 4-tap bilinear for smoother upsample
    let tx = bloom.texelSizeX * 0.5;
    let ty = bloom.texelSizeY * 0.5;
    var color = textureSample(srcTex, srcSampler, in.uv + vec2f(-tx, -ty)).rgb;
    color += textureSample(srcTex, srcSampler, in.uv + vec2f( tx, -ty)).rgb;
    color += textureSample(srcTex, srcSampler, in.uv + vec2f(-tx,  ty)).rgb;
    color += textureSample(srcTex, srcSampler, in.uv + vec2f( tx,  ty)).rgb;
    return vec4f(color * 0.25, 1.0);
}
)WGSL";

void WgpuRnd::EnsureBloomPipelines() {
    if (mBloomReady) return;
    auto& dev = mGpu.Device();

    // Shader module
    wgpu::ShaderSourceWGSL src;
    src.code = kBloomShaderSource;
    wgpu::ShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = &src;
    mBloomShader = dev.CreateShaderModule(&smDesc);

    // Bind group layout: texture + sampler + uniform
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
    entries[2].buffer.minBindingSize = sizeof(BloomUniforms);

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 3;
    bglDesc.entries = entries;
    mBloomBGL = dev.CreateBindGroupLayout(&bglDesc);

    wgpu::PipelineLayoutDescriptor plDesc{};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &mBloomBGL;
    mBloomPipelineLayout = dev.CreatePipelineLayout(&plDesc);

    // Uniform buffer
    wgpu::BufferDescriptor bufDesc{};
    bufDesc.size = sizeof(BloomUniforms);
    bufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mBloomUniformBuffer = dev.CreateBuffer(&bufDesc);

    // Helper to create a bloom pipeline for a specific fragment entry point
    auto makePipeline = [&](const char* fsEntry, bool additiveBlend) -> wgpu::RenderPipeline {
        wgpu::ColorTargetState ct{};
        ct.format = mGpu.SurfaceFormat();
        if (additiveBlend) {
            wgpu::BlendState blend{};
            blend.color.operation = wgpu::BlendOperation::Add;
            blend.color.srcFactor = wgpu::BlendFactor::One;
            blend.color.dstFactor = wgpu::BlendFactor::One;
            blend.alpha.operation = wgpu::BlendOperation::Add;
            blend.alpha.srcFactor = wgpu::BlendFactor::One;
            blend.alpha.dstFactor = wgpu::BlendFactor::Zero;
            ct.blend = &blend;
        }
        ct.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState frag{};
        frag.module = mBloomShader;
        frag.entryPoint = fsEntry;
        frag.targetCount = 1;
        frag.targets = &ct;

        wgpu::RenderPipelineDescriptor pipeDesc{};
        pipeDesc.layout = mBloomPipelineLayout;
        pipeDesc.vertex.module = mBloomShader;
        pipeDesc.vertex.entryPoint = "vs_bloom";
        pipeDesc.fragment = &frag;
        pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        return dev.CreateRenderPipeline(&pipeDesc);
    };

    mBloomThresholdPipeline = makePipeline("fs_bloom_threshold", false);
    mBloomBlurHPipeline = makePipeline("fs_bloom_blur_h", false);
    mBloomBlurVPipeline = makePipeline("fs_bloom_blur_v", false);
    mBloomUpsamplePipeline = makePipeline("fs_bloom_upsample", true);

    mBloomReady = true;
}

void WgpuRnd::EnsureBloomTextures(int sceneW, int sceneH) {
    auto& dev = mGpu.Device();
    for (int i = 0; i < kBloomMips; i++) {
        int w = std::max(1, sceneW >> (i + 1));
        int h = std::max(1, sceneH >> (i + 1));
        if (mBloomWidth[i] == w && mBloomHeight[i] == h && mBloomTex[i])
            continue;

        wgpu::TextureDescriptor desc{};
        desc.size = {(uint32_t)w, (uint32_t)h, 1};
        desc.format = mGpu.SurfaceFormat();
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        desc.mipLevelCount = 1;

        mBloomTex[i] = dev.CreateTexture(&desc);
        mBloomView[i] = mBloomTex[i].CreateView();
        mBloomTempTex[i] = dev.CreateTexture(&desc);
        mBloomTempView[i] = mBloomTempTex[i].CreateView();
        mBloomWidth[i] = w;
        mBloomHeight[i] = h;
    }
}

void WgpuRnd::RunBloom(float intensity, float threshold, const Hmx::Color& tint) {
    EnsureBloomPipelines();
    EnsureBloomTextures(mIntermediateWidth, mIntermediateHeight);

    auto& queue = mGpu.Queue();
    auto& dev = mGpu.Device();

    // Helper: run a fullscreen pass reading srcView, writing to dstView
    auto bloomPass = [&](wgpu::TextureView& srcView, wgpu::TextureView& dstView,
                         wgpu::RenderPipeline& pipeline, int targetW, int targetH,
                         float param = 0.0f) {
        BloomUniforms uni{};
        uni.threshold = param;  // reused as threshold for threshold pass
        uni.texelSizeX = 1.0f / targetW;
        uni.texelSizeY = 1.0f / targetH;
        uni.intensity = intensity;
        queue.WriteBuffer(mBloomUniformBuffer, 0, &uni, sizeof(uni));

        wgpu::BindGroupEntry bgEntries[3] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = srcView;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = mDefaultSampler;
        bgEntries[2].binding = 2;
        bgEntries[2].buffer = mBloomUniformBuffer;
        bgEntries[2].size = sizeof(BloomUniforms);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = mBloomBGL;
        bgDesc.entryCount = 3;
        bgDesc.entries = bgEntries;
        wgpu::BindGroup bg = dev.CreateBindGroup(&bgDesc);

        wgpu::RenderPassColorAttachment colorAtt{};
        colorAtt.view = dstView;
        colorAtt.loadOp = wgpu::LoadOp::Clear;
        colorAtt.storeOp = wgpu::StoreOp::Store;
        colorAtt.clearValue = {0, 0, 0, 1};

        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAtt;

        auto pass = mEncoder.BeginRenderPass(&rpDesc);
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, bg);
        pass.Draw(3);
        pass.End();
    };

    // Helper for additive upsample pass (load existing content, blend on top)
    auto upsamplePass = [&](wgpu::TextureView& srcView, wgpu::TextureView& dstView,
                            int targetW, int targetH, int srcW, int srcH) {
        BloomUniforms uni{};
        uni.threshold = 0;
        uni.texelSizeX = 1.0f / srcW;
        uni.texelSizeY = 1.0f / srcH;
        uni.intensity = intensity;
        queue.WriteBuffer(mBloomUniformBuffer, 0, &uni, sizeof(uni));

        wgpu::BindGroupEntry bgEntries[3] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = srcView;
        bgEntries[1].binding = 1;
        bgEntries[1].sampler = mDefaultSampler;
        bgEntries[2].binding = 2;
        bgEntries[2].buffer = mBloomUniformBuffer;
        bgEntries[2].size = sizeof(BloomUniforms);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = mBloomBGL;
        bgDesc.entryCount = 3;
        bgDesc.entries = bgEntries;
        wgpu::BindGroup bg = dev.CreateBindGroup(&bgDesc);

        wgpu::RenderPassColorAttachment colorAtt{};
        colorAtt.view = dstView;
        colorAtt.loadOp = wgpu::LoadOp::Load;  // preserve existing content
        colorAtt.storeOp = wgpu::StoreOp::Store;

        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAtt;

        auto pass = mEncoder.BeginRenderPass(&rpDesc);
        pass.SetPipeline(mBloomUpsamplePipeline);
        pass.SetBindGroup(0, bg);
        pass.Draw(3);
        pass.End();
    };

    // 1. Threshold: intermediate → bloom[0]
    bloomPass(mIntermediateView, mBloomView[0], mBloomThresholdPipeline,
              mBloomWidth[0], mBloomHeight[0], threshold);

    // 2. For each mip: blur H+V, then downsample to next mip
    for (int i = 0; i < kBloomMips; i++) {
        // Blur H: bloom[i] → bloomTemp[i]
        bloomPass(mBloomView[i], mBloomTempView[i], mBloomBlurHPipeline,
                  mBloomWidth[i], mBloomHeight[i]);
        // Blur V: bloomTemp[i] → bloom[i]
        bloomPass(mBloomTempView[i], mBloomView[i], mBloomBlurVPipeline,
                  mBloomWidth[i], mBloomHeight[i]);
        // Downsample to next level (reuse threshold pipeline as simple copy/downsample)
        if (i + 1 < kBloomMips) {
            BloomUniforms uni{};
            uni.threshold = 0;  // pass everything through
            uni.texelSizeX = 1.0f / mBloomWidth[i + 1];
            uni.texelSizeY = 1.0f / mBloomHeight[i + 1];
            queue.WriteBuffer(mBloomUniformBuffer, 0, &uni, sizeof(uni));

            wgpu::BindGroupEntry bgEntries[3] = {};
            bgEntries[0].binding = 0;
            bgEntries[0].textureView = mBloomView[i];
            bgEntries[1].binding = 1;
            bgEntries[1].sampler = mDefaultSampler;
            bgEntries[2].binding = 2;
            bgEntries[2].buffer = mBloomUniformBuffer;
            bgEntries[2].size = sizeof(BloomUniforms);

            wgpu::BindGroupDescriptor bgDesc{};
            bgDesc.layout = mBloomBGL;
            bgDesc.entryCount = 3;
            bgDesc.entries = bgEntries;
            wgpu::BindGroup bg = mGpu.Device().CreateBindGroup(&bgDesc);

            wgpu::RenderPassColorAttachment colorAtt{};
            colorAtt.view = mBloomView[i + 1];
            colorAtt.loadOp = wgpu::LoadOp::Clear;
            colorAtt.storeOp = wgpu::StoreOp::Store;
            colorAtt.clearValue = {0, 0, 0, 1};

            wgpu::RenderPassDescriptor rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments = &colorAtt;

            // Use blur_v as a simple pass-through (bilinear downsample via sampling)
            auto pass = mEncoder.BeginRenderPass(&rpDesc);
            pass.SetPipeline(mBloomBlurVPipeline);
            pass.SetBindGroup(0, bg);
            pass.Draw(3);
            pass.End();
        }
    }

    // 3. Upsample chain: blend lower mips back up
    for (int i = kBloomMips - 2; i >= 0; i--) {
        upsamplePass(mBloomView[i + 1], mBloomView[i],
                      mBloomWidth[i], mBloomHeight[i],
                      mBloomWidth[i + 1], mBloomHeight[i + 1]);
    }
}

// ============================================================================
// Shadow Mapping
// ============================================================================

static const char* kShadowShaderSource = R"WGSL(
struct LightVP {
    matrix: mat4x4f,
};
@group(0) @binding(0) var<uniform> lightVP: LightVP;

struct ObjectUB {
    world: mat4x4f,
    worldInvTranspose: mat4x4f,
};
@group(1) @binding(0) var<uniform> object: ObjectUB;

@vertex fn vs_shadow(@location(0) pos: vec3f) -> @builtin(position) vec4f {
    return lightVP.matrix * object.world * vec4f(pos, 1.0);
}

// Skinned variant
struct BoneUB {
    bones: array<mat4x4f, 40>,
};
@group(2) @binding(0) var<uniform> bones: BoneUB;

struct SkinInput {
    @location(0) pos: vec3f,
    @location(4) boneWeights: vec4f,
    @location(5) boneIndices: vec4u,
};

@vertex fn vs_shadow_skinned(in: SkinInput) -> @builtin(position) vec4f {
    var skinnedPos = vec4f(0.0);
    let p = vec4f(in.pos, 1.0);
    for (var i = 0u; i < 4u; i++) {
        let w = in.boneWeights[i];
        if (w > 0.0) {
            skinnedPos += bones.bones[in.boneIndices[i]] * p * w;
        }
    }
    skinnedPos.w = 1.0;
    return lightVP.matrix * object.world * skinnedPos;
}
)WGSL";

void WgpuRnd::EnsureShadowPipelines() {
    if (mShadowReady) return;
    auto& dev = mGpu.Device();

    // Shadow depth texture
    wgpu::TextureDescriptor texDesc{};
    texDesc.size = {kShadowMapSize, kShadowMapSize, 1};
    texDesc.format = wgpu::TextureFormat::Depth32Float;
    texDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    texDesc.mipLevelCount = 1;
    mShadowDepthTex = dev.CreateTexture(&texDesc);
    mShadowDepthView = mShadowDepthTex.CreateView();

    // Comparison sampler for shadow mapping
    wgpu::SamplerDescriptor sampDesc{};
    sampDesc.compare = wgpu::CompareFunction::LessEqual;
    sampDesc.magFilter = wgpu::FilterMode::Linear;
    sampDesc.minFilter = wgpu::FilterMode::Linear;
    sampDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    sampDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    mShadowSampler = dev.CreateSampler(&sampDesc);

    // Shader module
    wgpu::ShaderSourceWGSL src;
    src.code = kShadowShaderSource;
    wgpu::ShaderModuleDescriptor smDesc{};
    smDesc.nextInChain = &src;
    mShadowShader = dev.CreateShaderModule(&smDesc);

    // Light VP uniform buffer
    wgpu::BufferDescriptor bufDesc{};
    bufDesc.size = 64;  // mat4x4f
    bufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mShadowLightVPBuffer = dev.CreateBuffer(&bufDesc);

    // Group 0: lightVP uniform
    wgpu::BindGroupLayoutEntry sceneEntry{};
    sceneEntry.binding = 0;
    sceneEntry.visibility = wgpu::ShaderStage::Vertex;
    sceneEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    sceneEntry.buffer.minBindingSize = 64;

    wgpu::BindGroupLayoutDescriptor sceneBglDesc{};
    sceneBglDesc.entryCount = 1;
    sceneBglDesc.entries = &sceneEntry;
    mShadowSceneBGL = dev.CreateBindGroupLayout(&sceneBglDesc);

    // Group 1: object world (reuse ObjectUniforms — 128 bytes)
    wgpu::BindGroupLayoutEntry objEntry{};
    objEntry.binding = 0;
    objEntry.visibility = wgpu::ShaderStage::Vertex;
    objEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    objEntry.buffer.minBindingSize = 0;

    wgpu::BindGroupLayoutDescriptor objBglDesc{};
    objBglDesc.entryCount = 1;
    objBglDesc.entries = &objEntry;
    mShadowObjectBGL = dev.CreateBindGroupLayout(&objBglDesc);

    // Group 2: bones (reuse BoneUniforms — 2560 bytes)
    wgpu::BindGroupLayoutEntry boneEntry{};
    boneEntry.binding = 0;
    boneEntry.visibility = wgpu::ShaderStage::Vertex;
    boneEntry.buffer.type = wgpu::BufferBindingType::Uniform;
    boneEntry.buffer.minBindingSize = 0;

    wgpu::BindGroupLayoutDescriptor boneBglDesc{};
    boneBglDesc.entryCount = 1;
    boneBglDesc.entries = &boneEntry;
    mShadowBoneBGL = dev.CreateBindGroupLayout(&boneBglDesc);

    // Pipeline layouts: static (2 groups), skinned (3 groups)
    {
        wgpu::BindGroupLayout layouts[2] = {mShadowSceneBGL, mShadowObjectBGL};
        wgpu::PipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 2;
        plDesc.bindGroupLayouts = layouts;
        mShadowPipelineLayout = dev.CreatePipelineLayout(&plDesc);
    }
    {
        wgpu::BindGroupLayout layouts[3] = {mShadowSceneBGL, mShadowObjectBGL, mShadowBoneBGL};
        wgpu::PipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 3;
        plDesc.bindGroupLayouts = layouts;
        mShadowSkinnedPipelineLayout = dev.CreatePipelineLayout(&plDesc);
    }

    // Depth-stencil state (depth-only, no color)
    wgpu::DepthStencilState ds{};
    ds.format = wgpu::TextureFormat::Depth32Float;
    ds.depthWriteEnabled = wgpu::OptionalBool::True;
    ds.depthCompare = wgpu::CompareFunction::Less;
    // Slight depth bias to reduce shadow acne
    ds.depthBias = 2;
    ds.depthBiasSlopeScale = 1.5f;

    // Static shadow pipeline
    {
        const wgpu::VertexBufferLayout* vtxLayout = &VertexFormats::StaticLayout();
        wgpu::RenderPipelineDescriptor pipeDesc{};
        pipeDesc.layout = mShadowPipelineLayout;
        pipeDesc.vertex.module = mShadowShader;
        pipeDesc.vertex.entryPoint = "vs_shadow";
        pipeDesc.vertex.bufferCount = 1;
        pipeDesc.vertex.buffers = vtxLayout;
        pipeDesc.depthStencil = &ds;
        pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipeDesc.primitive.frontFace = wgpu::FrontFace::CCW;
        pipeDesc.primitive.cullMode = wgpu::CullMode::Back;
        // No fragment shader — depth-only pass
        mShadowStaticPipeline = dev.CreateRenderPipeline(&pipeDesc);
    }

    // Skinned shadow pipeline
    {
        const wgpu::VertexBufferLayout* vtxLayout = &VertexFormats::SkinnedLayout();
        wgpu::RenderPipelineDescriptor pipeDesc{};
        pipeDesc.layout = mShadowSkinnedPipelineLayout;
        pipeDesc.vertex.module = mShadowShader;
        pipeDesc.vertex.entryPoint = "vs_shadow_skinned";
        pipeDesc.vertex.bufferCount = 1;
        pipeDesc.vertex.buffers = vtxLayout;
        pipeDesc.depthStencil = &ds;
        pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        pipeDesc.primitive.frontFace = wgpu::FrontFace::CCW;
        pipeDesc.primitive.cullMode = wgpu::CullMode::Back;
        mShadowSkinnedPipeline = dev.CreateRenderPipeline(&pipeDesc);
    }

    printf("WgpuRnd: Shadow pipelines initialized (%dx%d depth texture)\n",
           kShadowMapSize, kShadowMapSize);
    mShadowReady = true;
}

static void BuildOrthoMatrix(float left, float right, float bottom, float top,
                              float near, float far, float* out) {
    memset(out, 0, 64);
    out[0]  = 2.0f / (right - left);
    out[5]  = 2.0f / (top - bottom);
    out[10] = 1.0f / (far - near);
    out[12] = -(right + left) / (right - left);
    out[13] = -(top + bottom) / (top - bottom);
    out[14] = -near / (far - near);
    out[15] = 1.0f;
}

static void BuildLookAtMatrix(const float* eye, const float* at, const float* up, float* out) {
    // Forward
    float f[3] = { at[0]-eye[0], at[1]-eye[1], at[2]-eye[2] };
    float flen = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (flen > 0) { f[0]/=flen; f[1]/=flen; f[2]/=flen; }

    // Right = f x up
    float r[3] = { f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] };
    float rlen = sqrtf(r[0]*r[0]+r[1]*r[1]+r[2]*r[2]);
    if (rlen > 0) { r[0]/=rlen; r[1]/=rlen; r[2]/=rlen; }

    // True up = r x f
    float u[3] = { r[1]*f[2]-r[2]*f[1], r[2]*f[0]-r[0]*f[2], r[0]*f[1]-r[1]*f[0] };

    memset(out, 0, 64);
    out[0] = r[0]; out[1] = u[0]; out[2]  = -f[0];
    out[4] = r[1]; out[5] = u[1]; out[6]  = -f[1];
    out[8] = r[2]; out[9] = u[2]; out[10] = -f[2];
    out[12] = -(r[0]*eye[0]+r[1]*eye[1]+r[2]*eye[2]);
    out[13] = -(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    out[14] =  (f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2]);
    out[15] = 1.0f;
}

static void MultiplyMatrix4x4(const float* a, const float* b, float* out) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            out[i*4+j] = 0;
            for (int k = 0; k < 4; k++) {
                out[i*4+j] += a[i*4+k] * b[k*4+j];
            }
        }
    }
}

// Forward declaration — implemented in Mesh_Wgpu.cpp
extern void DrawMeshShadow(RndMesh* mesh);

static bool IsShadowTransparentBlend(int blend) {
    return blend == BaseMaterial::kBlendSrcAlpha ||
           blend == BaseMaterial::kBlendSrcAlphaAdd ||
           blend == BaseMaterial::kBlendAdd ||
           blend == BaseMaterial::kBlendSubtract ||
           blend == BaseMaterial::kPreMultAlpha;
}

void WgpuRnd::RenderShadowPass() {
    EnsureShadowPipelines();

    // Get the primary light direction
    float lightDir[3] = {0, -1, 0};  // default: straight down
    RndEnviron* env = RndEnviron::Current();
    if (env) {
        ObjPtrList<RndLight>& lights = env->LightsApprox();
        for (ObjPtrList<RndLight>::iterator it = lights.begin(); it != lights.end(); ++it) {
            RndLight* light = *it;
            if (light && light->GetType() == RndLight::kDirectional) {
                Transform xfm = light->WorldXfm();
                lightDir[0] = -xfm.m.z.x;
                lightDir[1] = -xfm.m.z.y;
                lightDir[2] = -xfm.m.z.z;
                break;
            }
        }
    }

    // Build light view-projection matrix
    float lightDist = 20.0f;
    float eye[3] = { -lightDir[0]*lightDist, -lightDir[1]*lightDist, -lightDir[2]*lightDist };
    float at[3] = {0, 0, 0};
    float up[3] = {0, 1, 0};
    if (fabsf(lightDir[1]) > 0.9f) { up[0] = 0; up[1] = 0; up[2] = 1; }

    float viewMat[16], projMat[16];
    BuildLookAtMatrix(eye, at, up, viewMat);
    BuildOrthoMatrix(-10, 10, -10, 10, 0.1f, 50.0f, projMat);
    MultiplyMatrix4x4(projMat, viewMat, mLightViewProj);

    // Upload light VP to shadow uniform buffer
    mGpu.Queue().WriteBuffer(mShadowLightVPBuffer, 0, mLightViewProj, 64);

    // Create scene bind group for shadow pass (light VP only)
    wgpu::BindGroupEntry sceneEntry{};
    sceneEntry.binding = 0;
    sceneEntry.buffer = mShadowLightVPBuffer;
    sceneEntry.size = 64;
    wgpu::BindGroupDescriptor sceneBgDesc{};
    sceneBgDesc.layout = mShadowSceneBGL;
    sceneBgDesc.entryCount = 1;
    sceneBgDesc.entries = &sceneEntry;
    mShadowSceneBindGroup = mGpu.Device().CreateBindGroup(&sceneBgDesc);

    // Begin shadow depth render pass
    wgpu::RenderPassDepthStencilAttachment depthAtt{};
    depthAtt.view = mShadowDepthView;
    depthAtt.depthLoadOp = wgpu::LoadOp::Clear;
    depthAtt.depthStoreOp = wgpu::StoreOp::Store;
    depthAtt.depthClearValue = 1.0f;

    wgpu::RenderPassDescriptor rpDesc{};
    rpDesc.depthStencilAttachment = &depthAtt;

    mShadowPass = mEncoder.BeginRenderPass(&rpDesc);
    mShadowPass.SetBindGroup(0, mShadowSceneBindGroup);
    mInShadowPass = true;

    // Draw all opaque meshes into shadow map
    // Find the world ObjectDir from the current environment
    ObjectDir* worldDir = nullptr;
    if (env) worldDir = env->Dir();
    if (!worldDir) {
        RndCam* cam = RndCam::Current();
        if (cam) worldDir = cam->Dir();
    }
    if (worldDir) {
        ObjDirItr<RndMesh> meshItr(worldDir, true);
        for (; meshItr; ++meshItr) {
            RndMesh* mesh = meshItr;
            if (!mesh->Showing()) continue;
            if (strstr(mesh->Name(), "_lod")) continue;
            RndMat* mat = mesh->Mat();
            if (mat && !IsShadowTransparentBlend(mat->GetBlend())) {
                DrawMeshShadow(mesh);
            }
        }
    }

    mShadowPass.End();
    mShadowPass = nullptr;
    mInShadowPass = false;
    mShadowAvailable = true;
}

// ============================================================================
// Depth of Field
// ============================================================================

struct DofUniforms {
    float focalPlane;
    float blurDepth;
    float maxBlur;
    float minBlur;
    float texelSizeX;
    float texelSizeY;
    float nearPlane;
    float farPlane;
};
static_assert(sizeof(DofUniforms) == 32, "DofUniforms must be 32 bytes");

static const char* kDofShaderSource = R"WGSL(
struct DofUB {
    focalPlane: f32,
    blurDepth: f32,
    maxBlur: f32,
    minBlur: f32,
    texelSizeX: f32,
    texelSizeY: f32,
    nearPlane: f32,
    farPlane: f32,
};

@group(0) @binding(0) var sceneTex: texture_2d<f32>;
@group(0) @binding(1) var depthTex: texture_2d<f32>;
@group(0) @binding(2) var sceneSampler: sampler;
@group(0) @binding(3) var<uniform> dof: DofUB;

struct VOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

@vertex fn vs_dof(@builtin(vertex_index) idx: u32) -> VOut {
    var out: VOut;
    let x = f32(i32(idx & 1u)) * 4.0 - 1.0;
    let y = f32(i32(idx >> 1u)) * 4.0 - 1.0;
    out.pos = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

fn linearizeDepth(d: f32, near: f32, far: f32) -> f32 {
    return near * far / (far - d * (far - near));
}

// 8-tap Poisson disc
const poissonDisc = array<vec2f, 8>(
    vec2f(-0.613392, 0.617481),
    vec2f( 0.170019,-0.040254),
    vec2f(-0.299417, 0.791925),
    vec2f( 0.645680, 0.493210),
    vec2f(-0.651784, 0.717887),
    vec2f( 0.421003, 0.027070),
    vec2f(-0.817194,-0.271096),
    vec2f( 0.977050,-0.108615),
);

@fragment fn fs_dof(in: VOut) -> @location(0) vec4f {
    let color = textureSample(sceneTex, sceneSampler, in.uv);
    let rawDepth = textureSample(depthTex, sceneSampler, in.uv).r;
    let linearDepth = linearizeDepth(rawDepth, dof.nearPlane, dof.farPlane);

    let diff = abs(linearDepth - dof.focalPlane);
    let coc = clamp(diff / max(dof.blurDepth, 0.001), dof.minBlur, dof.maxBlur);

    if (coc < 0.01) {
        return color;
    }

    let radius = coc;
    let texel = vec2f(dof.texelSizeX, dof.texelSizeY);
    var blurred = color.rgb;
    for (var i = 0; i < 8; i++) {
        let offset = poissonDisc[i] * radius * texel * 8.0;
        blurred += textureSample(sceneTex, sceneSampler, in.uv + offset).rgb;
    }
    blurred /= 9.0;

    return vec4f(mix(color.rgb, blurred, coc), color.a);
}
)WGSL";

// Depth resolve shader — renders MSAA depth to single-sample color texture
static const char* kDepthResolveShaderSource = R"WGSL(
@group(0) @binding(0) var depthTexMS: texture_depth_multisampled_2d;

struct VOut {
    @builtin(position) pos: vec4f,
    @location(0) uv: vec2f,
};

@vertex fn vs_depth_resolve(@builtin(vertex_index) idx: u32) -> VOut {
    var out: VOut;
    let x = f32(i32(idx & 1u)) * 4.0 - 1.0;
    let y = f32(i32(idx >> 1u)) * 4.0 - 1.0;
    out.pos = vec4f(x, y, 0.0, 1.0);
    out.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return out;
}

@fragment fn fs_depth_resolve(in: VOut) -> @location(0) vec4f {
    let coords = vec2i(in.pos.xy);
    let d = textureLoad(depthTexMS, coords, 0);
    return vec4f(d, 0.0, 0.0, 1.0);
}
)WGSL";

void WgpuRnd::EnsureDofPipeline() {
    if (mDofReady) return;
    auto& dev = mGpu.Device();

    // Depth resolve BGL + pipeline
    {
        wgpu::ShaderSourceWGSL src;
        src.code = kDepthResolveShaderSource;
        wgpu::ShaderModuleDescriptor smDesc{};
        smDesc.nextInChain = &src;
        auto shader = dev.CreateShaderModule(&smDesc);

        wgpu::BindGroupLayoutEntry entry{};
        entry.binding = 0;
        entry.visibility = wgpu::ShaderStage::Fragment;
        entry.texture.sampleType = wgpu::TextureSampleType::Depth;
        entry.texture.viewDimension = wgpu::TextureViewDimension::e2D;
        entry.texture.multisampled = true;

        wgpu::BindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = 1;
        bglDesc.entries = &entry;
        mDepthResolveBGL = dev.CreateBindGroupLayout(&bglDesc);

        wgpu::PipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &mDepthResolveBGL;
        mDepthResolvePipelineLayout = dev.CreatePipelineLayout(&plDesc);

        wgpu::ColorTargetState ct{};
        ct.format = wgpu::TextureFormat::R32Float;
        ct.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState frag{};
        frag.module = shader;
        frag.entryPoint = "fs_depth_resolve";
        frag.targetCount = 1;
        frag.targets = &ct;

        wgpu::RenderPipelineDescriptor pipeDesc{};
        pipeDesc.layout = mDepthResolvePipelineLayout;
        pipeDesc.vertex.module = shader;
        pipeDesc.vertex.entryPoint = "vs_depth_resolve";
        pipeDesc.fragment = &frag;
        pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        mDepthResolvePipeline = dev.CreateRenderPipeline(&pipeDesc);
    }

    // DOF BGL + pipeline
    {
        wgpu::ShaderSourceWGSL src;
        src.code = kDofShaderSource;
        wgpu::ShaderModuleDescriptor smDesc{};
        smDesc.nextInChain = &src;
        mDofShader = dev.CreateShaderModule(&smDesc);

        wgpu::BindGroupLayoutEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].visibility = wgpu::ShaderStage::Fragment;
        entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
        entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;

        entries[1].binding = 1;
        entries[1].visibility = wgpu::ShaderStage::Fragment;
        entries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
        entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

        entries[2].binding = 2;
        entries[2].visibility = wgpu::ShaderStage::Fragment;
        entries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

        entries[3].binding = 3;
        entries[3].visibility = wgpu::ShaderStage::Fragment;
        entries[3].buffer.type = wgpu::BufferBindingType::Uniform;
        entries[3].buffer.minBindingSize = sizeof(DofUniforms);

        wgpu::BindGroupLayoutDescriptor bglDesc{};
        bglDesc.entryCount = 4;
        bglDesc.entries = entries;
        mDofBGL = dev.CreateBindGroupLayout(&bglDesc);

        wgpu::PipelineLayoutDescriptor plDesc{};
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts = &mDofBGL;
        mDofPipelineLayout = dev.CreatePipelineLayout(&plDesc);

        wgpu::BufferDescriptor bufDesc{};
        bufDesc.size = sizeof(DofUniforms);
        bufDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        mDofUniformBuffer = dev.CreateBuffer(&bufDesc);

        wgpu::ColorTargetState ct{};
        ct.format = mGpu.SurfaceFormat();
        ct.writeMask = wgpu::ColorWriteMask::All;

        wgpu::FragmentState frag{};
        frag.module = mDofShader;
        frag.entryPoint = "fs_dof";
        frag.targetCount = 1;
        frag.targets = &ct;

        wgpu::RenderPipelineDescriptor pipeDesc{};
        pipeDesc.layout = mDofPipelineLayout;
        pipeDesc.vertex.module = mDofShader;
        pipeDesc.vertex.entryPoint = "vs_dof";
        pipeDesc.fragment = &frag;
        pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
        mDofPipeline = dev.CreateRenderPipeline(&pipeDesc);
    }

    mDofReady = true;
}

void WgpuRnd::RunDepthOfField() {
    extern DOFProc* TheDOFProc;
    if (!TheDOFProc || !TheDOFProc->Enabled()) return;

    EnsureDofPipeline();
    auto& dev = mGpu.Device();
    auto& queue = mGpu.Queue();
    int w = mIntermediateWidth;
    int h = mIntermediateHeight;

    // Ensure depth resolve texture
    if (mDofWidth != w || mDofHeight != h || !mDepthResolveTex) {
        wgpu::TextureDescriptor desc{};
        desc.size = {(uint32_t)w, (uint32_t)h, 1};
        desc.format = wgpu::TextureFormat::R32Float;
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        desc.mipLevelCount = 1;
        mDepthResolveTex = dev.CreateTexture(&desc);
        mDepthResolveView = mDepthResolveTex.CreateView();

        // DOF intermediate (ping-pong target)
        wgpu::TextureDescriptor iDesc{};
        iDesc.size = {(uint32_t)w, (uint32_t)h, 1};
        iDesc.format = mGpu.SurfaceFormat();
        iDesc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
        iDesc.mipLevelCount = 1;
        mDofIntermediateTex = dev.CreateTexture(&iDesc);
        mDofIntermediateView = mDofIntermediateTex.CreateView();
        mDofWidth = w;
        mDofHeight = h;
    }

    // Step 1: Resolve MSAA depth → single-sample R32Float
    {
        wgpu::BindGroupEntry bgEntry{};
        bgEntry.binding = 0;
        bgEntry.textureView = mDepthView;  // MSAA depth

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = mDepthResolveBGL;
        bgDesc.entryCount = 1;
        bgDesc.entries = &bgEntry;
        wgpu::BindGroup bg = dev.CreateBindGroup(&bgDesc);

        wgpu::RenderPassColorAttachment colorAtt{};
        colorAtt.view = mDepthResolveView;
        colorAtt.loadOp = wgpu::LoadOp::Clear;
        colorAtt.storeOp = wgpu::StoreOp::Store;
        colorAtt.clearValue = {1, 0, 0, 1};

        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAtt;

        auto pass = mEncoder.BeginRenderPass(&rpDesc);
        pass.SetPipeline(mDepthResolvePipeline);
        pass.SetBindGroup(0, bg);
        pass.Draw(3);
        pass.End();
    }

    // Step 2: DOF blur pass — read intermediate + resolved depth, write to DOF intermediate
    {
        RndCam* cam = RndCam::Current();
        float nearPlane = cam ? cam->NearPlane() : 0.1f;
        float farPlane = cam ? cam->FarPlane() : 1000.0f;

        DofUniforms uni{};
        uni.focalPlane = TheDOFProc->FocalPlane();
        uni.blurDepth = TheDOFProc->BlurDepth();
        uni.maxBlur = TheDOFProc->MaxBlur();
        uni.minBlur = TheDOFProc->MinBlur();
        uni.texelSizeX = 1.0f / w;
        uni.texelSizeY = 1.0f / h;
        uni.nearPlane = nearPlane;
        uni.farPlane = farPlane;
        queue.WriteBuffer(mDofUniformBuffer, 0, &uni, sizeof(uni));

        wgpu::BindGroupEntry bgEntries[4] = {};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = mIntermediateView;
        bgEntries[1].binding = 1;
        bgEntries[1].textureView = mDepthResolveView;
        bgEntries[2].binding = 2;
        bgEntries[2].sampler = mDefaultSampler;
        bgEntries[3].binding = 3;
        bgEntries[3].buffer = mDofUniformBuffer;
        bgEntries[3].size = sizeof(DofUniforms);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = mDofBGL;
        bgDesc.entryCount = 4;
        bgDesc.entries = bgEntries;
        wgpu::BindGroup bg = dev.CreateBindGroup(&bgDesc);

        wgpu::RenderPassColorAttachment colorAtt{};
        colorAtt.view = mDofIntermediateView;
        colorAtt.loadOp = wgpu::LoadOp::Clear;
        colorAtt.storeOp = wgpu::StoreOp::Store;
        colorAtt.clearValue = {0, 0, 0, 1};

        wgpu::RenderPassDescriptor rpDesc{};
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments = &colorAtt;

        auto pass = mEncoder.BeginRenderPass(&rpDesc);
        pass.SetPipeline(mDofPipeline);
        pass.SetBindGroup(0, bg);
        pass.Draw(3);
        pass.End();
    }

    // Step 3: Copy DOF result back to intermediate (swap views)
    std::swap(mIntermediateView, mDofIntermediateView);
    std::swap(mIntermediateTex, mDofIntermediateTex);
}
