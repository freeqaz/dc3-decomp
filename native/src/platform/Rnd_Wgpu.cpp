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

    // Create command encoder
    mEncoder = mGpu.Device().CreateCommandEncoder();

    // Begin render pass
    wgpu::RenderPassColorAttachment colorAtt{};
    colorAtt.view = mMsaaView;            // Render to MSAA target
    colorAtt.resolveTarget = mFrameView;   // Resolve to surface/readback
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

void WgpuRnd::EndDrawing() {
    if (!mGpu.Device()) {
        mDrawing = false;
        return;
    }
    if (mInPass) {
        mPass.End();
        mInPass = false;

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
        // If it's still identity, compute viewProj from camera's view + projection transforms.
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
            // Milo convention: Y = forward/depth, Z = up, X = right.
            // WebGPU clip space: Z in [0,1], perspective divide by w.

            // View matrix from inverse world transform (row-major, right-multiply)
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

            // Perspective projection with Milo axis convention and WebGPU depth [0,1]
            float near = cam->NearPlane();
            float far = cam->FarPlane();
            float yfov = cam->YFov();
            float aspect = (float)mWidth / (float)mHeight;
            float cot = 1.0f / tanf(yfov * 0.5f);
            float zRange = far - near;

            // Row-major: v_clip = v_view * Proj
            // Milo: X=right, Y=forward(depth), Z=up
            // Clip: X=right, Z=depth[0,1], Y=up (w from view-Y)
            float proj[16] = {
                cot / aspect, 0,   0,                     0,
                0,            0,   far / zRange,           1,
                0,            cot, 0,                     0,
                0,            0,   -near * far / zRange,  0
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
        }

        // View matrix from camera's inverse world transform
        const Transform& worldXfm = cam->WorldXfm();
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
    wgpu::BindGroupEntry entries[10] = {};

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

    wgpu::BindGroupDescriptor desc{};
    desc.layout = mPipelines.MaterialLayout();
    desc.entryCount = 10;
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
