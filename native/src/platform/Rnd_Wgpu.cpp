// DC3 Native Port — WebGPU Renderer Implementation
// Replaces Rnd_Stub.cpp with real WebGPU rendering via Dawn

#include "platform/Rnd_Wgpu.h"

#include "gfx/FrameCapture.h"
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

#include <GLFW/glfw3.h>

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

void UniformRingBuffer::Init(wgpu::Device& device, uint32_t capacity, const char* label) {
    mDevice = device;
    mLabel = label ? label : "UniformRing";
    wgpu::BufferDescriptor desc{};
    desc.label = mLabel;
    desc.size = capacity;
    desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    mBuffer = device.CreateBuffer(&desc);
    mCapacity = capacity;
    mOffset = 0;
}

void UniformRingBuffer::Grow(wgpu::Device& device) {
    uint32_t newCapacity = mCapacity * 2;
    fprintf(stderr, "UniformRingBuffer: growing %s %u -> %u bytes\n", mLabel, mCapacity, newCapacity);

    wgpu::BufferDescriptor desc{};
    desc.label = mLabel;
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

    // Override clear color — DTA config isn't loaded in the native port.
    // Default to medium-dark teal to approximate the turbo_shell venue.
    // Without venue geometry, this is the only background; brighter values
    // make the multiply-blend overlays visible and SrcAlpha overlays less jarish.
    // MILO_CLEAR_COLOR=r,g,b overrides (e.g. "1,1,1" for white, "0.5,0.5,0.5" for gray)
    const char* clearEnv = getenv("MILO_CLEAR_COLOR");
    if (clearEnv) {
        float r = 0, g = 0, b = 0;
        sscanf(clearEnv, "%f,%f,%f", &r, &g, &b);
        mClearColor = Hmx::Color(r, g, b);
    } else {
        mClearColor = Hmx::Color(0.06f, 0.09f, 0.12f);
    }

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
        // Create per-draw ring buffers (64KB each — enough for ~250 draws/frame at 256-byte alignment)
        // Scene ring handles mid-frame camera switches (each camera gets its own offset)
        mSceneRing.Init(mGpu.Device(), 16 * 1024, "SceneUniforms");
        mMaterialRing.Init(mGpu.Device(), 64 * 1024, "MaterialUniforms");
        mObjectRing.Init(mGpu.Device(), 64 * 1024, "ObjectUniforms");
        // Bone ring needs more space: 2560 bytes per skinned draw (rounded to 2816 at 256 alignment)
        mBoneRing.Init(mGpu.Device(), 256 * 1024, "BoneUniforms");

        // Create depth texture
        CreateDepthTexture(mWidth, mHeight);

        // Create default textures (1x1 white for untextured materials)
        CreateDefaultTextures();

        // Initialize render passes
        mShadowPass.Init(mGpu);
        mPostProcPass.Init(mGpu);

        printf("DC3 Native: WgpuRnd initialized (%dx%d, %s)\n",
               mWidth, mHeight, mGpu.HasBCCompression() ? "BC supported" : "software DXT");

        // Frame capture setup (env-var controlled)
        const char* captureFrame = getenv("MILO_CAPTURE_FRAME");
        if (captureFrame && captureFrame[0]) {
            int frame = atoi(captureFrame);
            if (frame > 0) {
                FrameCapture::Get().SetCaptureFrame(frame);
                printf("DC3 Native: frame capture armed for frame %d\n", frame);
            }
        }

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

    // Release all GPU objects BEFORE device shutdown to avoid
    // use-after-free in static destructor (Dawn/Vulkan teardown ordering)
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
    mSceneRing.Release();
    mSceneBindGroup = nullptr;
    mMsaaTex = nullptr;
    mMsaaView = nullptr;

    // Ring buffers
    mMaterialRing.Release();
    mObjectRing.Release();
    mBoneRing.Release();

    // PipelineManager
    mPipelines.Terminate();

    // Render passes
    mDrawRect2D.Terminate();
    mPostProcPass.Terminate();
    mShadowPass.Terminate();

    // Intermediate texture
    mIntermediateTex = nullptr;
    mIntermediateView = nullptr;

    mGpu.Shutdown();
}

void WgpuRnd::Clear(unsigned int flags, const Hmx::Color& color) {
    mClearColor = color;
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

    FrameCapture::Get().BeginFrame(mFrameID);

    // F12 triggers capture for next frame
    if (gNativeWindow && !mGpu.IsHeadless()) {
        if (glfwGetKey(gNativeWindow, GLFW_KEY_F12) == GLFW_PRESS)
            FrameCapture::Get().CaptureNextFrame();
    }

    // Select default camera and environment (base Rnd::BeginDrawing does this)
    // Only if no camera is already current (viewer sets its own orbit camera)
    if (mDefaultCam && !RndCam::Current())
        mDefaultCam->Select();
    if (mDefaultEnv && !RndEnviron::Current())
        mDefaultEnv->Select(nullptr);

    // Reset ring buffers for this frame
    mSceneRing.Reset();
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
        if (sFailCount < 10) {
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
        desc.label = "MSAA4xColor";
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
            const Hmx::Rect& sr = dbgCam->GetScreenRect();
            printf("DC3 Debug [frame %d]: cam='%s' near=%.2f far=%.2f yfov=%.4f pos=(%.2f,%.2f,%.2f)\n",
                   mFrameID, dbgCam->Name(),
                   dbgCam->NearPlane(), dbgCam->FarPlane(), dbgCam->YFov(),
                   dbgCam->WorldXfm().v.x, dbgCam->WorldXfm().v.y, dbgCam->WorldXfm().v.z);
            printf("  screenRect=(%.4f,%.4f,%.4f,%.4f) aspect=%d YRatio=%.4f Rnd::w=%d h=%d\n",
                   sr.x, sr.y, sr.w, sr.h, (int)mAspect, YRatio(), mWidth, mHeight);
            Transform viewXfm;
            Hmx::Matrix4 projMtx;
            dbgCam->GetViewProjectXfms(viewXfm, projMtx);
            printf("  projMtx: x.x=%.4f y.y=%.4f z.x=%.4f z.y=%.4f z.z=%.4f z.w=%.4f w.z=%.4f w.w=%.4f\n",
                   projMtx.x.x, projMtx.y.y, projMtx.z.x, projMtx.z.y, projMtx.z.z, projMtx.z.w, projMtx.w.z, projMtx.w.w);
        }
    }

    // Create command encoder
    wgpu::CommandEncoderDescriptor encDesc{};
    encDesc.label = "FrameEncoder";
    mEncoder = mGpu.Device().CreateCommandEncoder(&encDesc);

    // Shadow pre-pass: render depth from light's perspective
    mShadowPass.Render(mEncoder, mObjectRing, mBoneRing, mGpu);

    // Begin render pass
    wgpu::RenderPassColorAttachment colorAtt{};
    bool hasPostProc = !mGpu.IsHeadless() && RndPostProc::Current() != nullptr;
    if (kMSAASamples > 1) {
        colorAtt.view = mMsaaView;            // Render to MSAA target
        if (hasPostProc) {
            // Ensure intermediate texture exists
            if (mIntermediateWidth != curW || mIntermediateHeight != curH || !mIntermediateTex) {
                wgpu::TextureDescriptor iDesc{};
                iDesc.label = "PostProcIntermediate";
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
                iDesc.label = "PostProcIntermediate";
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
    // Use mClearColor (set to black in Init; Clear() can override at runtime).
    colorAtt.clearValue = {
        (double)mClearColor.red,
        (double)mClearColor.green,
        (double)mClearColor.blue,
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
    rpDesc.label = "MainPass";
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAtt;
    rpDesc.depthStencilAttachment = &depthAtt;

    mPass = mEncoder.BeginRenderPass(&rpDesc);
    mInPass = true;

    // Bind scene uniforms (group 0) — stays bound for entire frame
    mPass.SetBindGroup(0, mSceneBindGroup);
}

extern void FlushTransparentDraws();
extern bool HasTransparentDraws();
extern bool IsFlushingTransparentDraws();

wgpu::Buffer& GetSceneBuffer() { return gWgpuRnd->SceneBuffer(); }
uint32_t GetSceneOffset() { return gWgpuRnd->SceneOffset(); }

void WgpuRnd::EnsureSceneUniformsCurrent() {
    RndCam* cam = RndCam::Current();
    RndEnviron* env = RndEnviron::Current();
    if (cam != mLastSceneCam || env != mLastSceneEnv) {
        // Log camera switches on debug frames
        if (mFrameID == 500) {
            int drawsSinceLastSwitch = mDrawCount; // approximate
            printf("DC3_LAYER [F%d] cam switch: '%s' -> '%s' (draws so far: %d)\n",
                   mFrameID,
                   mLastSceneCam ? mLastSceneCam->Name() : "(null)",
                   cam ? cam->Name() : "(null)",
                   drawsSinceLastSwitch);
            if (cam) {
                const Transform &w = cam->WorldXfm();
                printf("  cam '%s' worldPos=(%.1f,%.1f,%.1f) worldBasis: X=(%.3f,%.3f,%.3f) Y=(%.3f,%.3f,%.3f) Z=(%.3f,%.3f,%.3f)\n",
                       cam->Name(), w.v.x, w.v.y, w.v.z,
                       w.m.x.x, w.m.x.y, w.m.x.z,
                       w.m.y.x, w.m.y.y, w.m.y.z,
                       w.m.z.x, w.m.z.y, w.m.z.z);
            }
        }
        if (HasTransparentDraws() && !IsFlushingTransparentDraws()) {
            FlushTransparentDraws();
        }
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
            mPostProcPass.Run(mEncoder, mIntermediateView, mIntermediateTex,
                              mIntermediateWidth, mIntermediateHeight,
                              mDepthView, mFrameView, mBlackTexView, mGpu);
        }

        wgpu::CommandBuffer cmd = mEncoder.Finish();
        mGpu.Queue().Submit(1, &cmd);

        MaybeCaptureFrame();
        FrameCapture::Get().EndFrame();

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
        desc.label = "DepthStencil";
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
        desc.label = "DefaultWhite";
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
        desc.label = "DefaultFlatNormal";
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
        desc.label = "DefaultBlack";
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
        desc.label = "DefaultBlackCube";
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

            if (mFrameID == 500) {
                printf("DC3 ViewProj for cam='%s':\n", cam->Name());
                printf("  proj: [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f] [%.3f %.3f %.3f %.3f]\n",
                       proj[0],proj[1],proj[2],proj[3], proj[4],proj[5],proj[6],proj[7],
                       proj[8],proj[9],proj[10],proj[11], proj[12],proj[13],proj[14],proj[15]);
                printf("  view row3: [%.3f %.3f %.3f %.3f]\n", view[12], view[13], view[14], view[15]);
                printf("  VP[0-3]: %.4f %.4f %.4f %.4f\n", scene.viewProj[0], scene.viewProj[1], scene.viewProj[2], scene.viewProj[3]);
                printf("  VP[4-7]: %.4f %.4f %.4f %.4f\n", scene.viewProj[4], scene.viewProj[5], scene.viewProj[6], scene.viewProj[7]);
                printf("  VP[8-11]: %.4f %.4f %.4f %.4f\n", scene.viewProj[8], scene.viewProj[9], scene.viewProj[10], scene.viewProj[11]);
                printf("  VP[12-15]: %.4f %.4f %.4f %.4f\n", scene.viewProj[12], scene.viewProj[13], scene.viewProj[14], scene.viewProj[15]);
            }
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

        // Supplement with fill lights if env has few directional lights
        if (lightIdx > 0 && lightIdx < 3) {
            // Add a front-fill light to ensure faces are visible
            // Hemisphere fill — from below-front to fill eye sockets and faces
            scene.lightDirs[lightIdx][0] = 0.0f;
            scene.lightDirs[lightIdx][1] = 0.0f;
            scene.lightDirs[lightIdx][2] = 1.0f;  // light shines upward
            scene.lightDirs[lightIdx][3] = 0.0f;
            scene.lightColors[lightIdx][0] = scene.lightColors[lightIdx][1] = scene.lightColors[lightIdx][2] = 0.35f;
            scene.lightColors[lightIdx][3] = 1.0f;
            lightIdx++;
        }
        // Fallback: if no lights found, use key + fill + rim lights
        if (lightIdx == 0) {
            // Key light — strong three-quarter light from front-left
            scene.lightDirs[0][0] = -0.4f;
            scene.lightDirs[0][1] = -0.7f;
            scene.lightDirs[0][2] = 0.5f;
            scene.lightDirs[0][3] = 0.0f;
            scene.lightColors[0][0] = scene.lightColors[0][1] = scene.lightColors[0][2] = 0.9f;
            scene.lightColors[0][3] = 1.0f;
            // Fill light — softer from front-right
            scene.lightDirs[1][0] = 0.5f;
            scene.lightDirs[1][1] = -0.5f;
            scene.lightDirs[1][2] = 0.3f;
            scene.lightDirs[1][3] = 0.0f;
            scene.lightColors[1][0] = scene.lightColors[1][1] = scene.lightColors[1][2] = 0.4f;
            scene.lightColors[1][3] = 1.0f;
            // Rim light — from behind for edge definition
            scene.lightDirs[2][0] = 0.0f;
            scene.lightDirs[2][1] = 0.8f;
            scene.lightDirs[2][2] = 0.4f;
            scene.lightDirs[2][3] = 0.0f;
            scene.lightColors[2][0] = scene.lightColors[2][1] = scene.lightColors[2][2] = 0.3f;
            scene.lightColors[2][3] = 1.0f;
            lightIdx = 3;
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
        // Default lighting — three-point light setup
        scene.ambientColor[0] = scene.ambientColor[1] = scene.ambientColor[2] = 0.4f;
        scene.ambientColor[3] = 1.0f;
        // Key (front-facing for character visibility)
        scene.lightDirs[0][0] = 0.5f;
        scene.lightDirs[0][1] = 0.3f;
        scene.lightDirs[0][2] = -0.7f;
        scene.lightDirs[0][3] = 0.0f;
        scene.lightColors[0][0] = scene.lightColors[0][1] = scene.lightColors[0][2] = 1.1f;
        scene.lightColors[0][3] = 1.0f;
        // Fill
        scene.lightDirs[1][0] = 0.5f;
        scene.lightDirs[1][1] = -0.5f;
        scene.lightDirs[1][2] = 0.3f;
        scene.lightDirs[1][3] = 0.0f;
        scene.lightColors[1][0] = scene.lightColors[1][1] = scene.lightColors[1][2] = 0.4f;
        scene.lightColors[1][3] = 1.0f;
        // Rim
        scene.lightDirs[2][0] = 0.0f;
        scene.lightDirs[2][1] = 0.8f;
        scene.lightDirs[2][2] = 0.4f;
        scene.lightDirs[2][3] = 0.0f;
        scene.lightColors[2][0] = scene.lightColors[2][1] = scene.lightColors[2][2] = 0.3f;
        scene.lightColors[2][3] = 1.0f;
        scene.numLights = 3.0f;
    }

    // Shadow mapping data
    if (mShadowPass.Available()) {
        memcpy(scene.lightViewProj, mShadowPass.LightViewProj(), 64);
        scene.shadowEnabled = 1.0f;
        scene.shadowBias = 0.002f;
        scene.shadowMapSize = (float)ShadowPass::kShadowMapSize;
        scene.shadowStrength = 0.3f;  // minimum brightness in shadow
    }

    // Upload scene uniforms at a new ring buffer offset (allows mid-frame camera switches —
    // each camera gets its own data in the ring, so queue.WriteBuffer doesn't overwrite earlier values)
    uint32_t sceneOffset = mSceneRing.Write(mGpu.Queue(), &scene, sizeof(scene));
    mLastSceneOffset = sceneOffset;

    // Create scene bind group (group 0) — includes shadow map texture + sampler
    wgpu::BindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = mSceneRing.Buffer();
    entries[0].offset = sceneOffset;
    entries[0].size = sizeof(SceneUniforms);

    entries[1].binding = 1;
    entries[1].textureView = mShadowPass.DepthView();  // always valid after Init

    entries[2].binding = 2;
    entries[2].sampler = mShadowPass.Sampler();  // comparison sampler

    wgpu::BindGroupDescriptor bgDesc{};
    bgDesc.label = "SceneBindGroup";
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

    fprintf(stderr, "DC3_CAPTURE F%d: headless=%d tex=%p w=%d h=%d\n",
            mFrameID, mGpu.IsHeadless(), (void*)&mGpu, w, h);
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
// DrawRect — screen-space 2D textured/colored quad (delegates to DrawRect2D)
// ============================================================================

void WgpuRnd::DrawRect(const Hmx::Rect& rect, RndMat* mat, ShaderType,
                        const Hmx::Color& color, const Hmx::Color* topRight,
                        const Hmx::Color* botLeft) {
    if (!mInPass) return;
    mDrawRect2D.Draw(mPass, rect, mat, color, topRight, botLeft,
                     mGpu, mPipelines, mWhiteTexView, mDefaultSampler);
}
