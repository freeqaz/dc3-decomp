// DC3 Native Port — WebGPU Renderer Header
// WgpuRnd (extends NgRnd) and WgpuShaderMgr (extends RndShaderMgr)

#pragma once

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "gfx/ShadowPass.h"
#include "gfx/PostProcPass.h"
#include "gfx/DrawRect2D.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"

#include "gfx/VideoEncoder.h"

#include <string>
#include <vector>
#include <webgpu/webgpu_cpp.h>

// ============================================================================
// CPU-side uniform structs — must match standard.wgsl layout exactly
// ============================================================================

struct SceneUniforms {
    float viewProj[16];       // mat4x4f
    float view[16];           // mat4x4f
    float cameraPos[3];       // vec3f
    float _pad0;
    float fogColor[3];        // vec3f
    float fogStart;
    float fogEnd;
    float fogEnabled;
    float _pad1[2];
    float lightDirs[4][4];    // array<vec4f, 4> — direction per light
    float lightColors[4][4];  // array<vec4f, 4> — color per light
    float ambientColor[4];    // vec4f
    float numLights;          // f32
    float _padN[3];
    // Point lights (up to 4)
    float pointLightPos[4][4];    // array<vec4f, 4> — world position (.w unused)
    float pointLightColors[4][4]; // array<vec4f, 4> — color per light
    float pointLightRanges[4];    // vec4f — falloff range per light
    float numPointLights;         // f32
    float _padPL[3];
    // Shadow mapping
    float lightViewProj[16];      // mat4x4f — light's VP for shadow lookup
    float shadowEnabled;           // f32 — 1.0 when shadow map valid
    float shadowBias;              // f32 — depth bias (0.002 typical)
    float shadowMapSize;           // f32 — texture dimension (1024)
    float shadowStrength;          // f32 — min brightness in shadow (0.3 typical)
};
static_assert(sizeof(SceneUniforms) == 576, "SceneUniforms must match WGSL layout");

struct MaterialUniforms {
    float color[4];             // vec4f
    float alphaThreshold;       // f32
    float useTexture;           // f32
    float specularPower;        // f32
    float emissiveMultiplier;   // f32
    float specularColor[4];     // vec4f
    float rimColor[4];          // vec4f — .rgb = color, .a = power
    float intensify;            // f32
    float shaderVariation;      // f32 — 0=none, 1=skin, 2=hair
    float rimLightUnder;        // f32 — 1.0 if rim only lights backfaces
    float deNormal;             // f32 — normal map diminish, 0=neutral
    float specular2Color[4];    // vec4f — .rgb = color, .a = power (2nd specular lobe)
    float anisotropy;           // f32
    float hasNormalMap;          // f32 — 1.0 when normal map bound
    float materialFogEnabled;   // f32 — 1.0 if fog applies to this material
    float prelit;               // f32 — 1.0 if vertex color is pre-lit
    float environMapStrength;   // f32 — 1.0 when environ map bound
    float environMapFalloff;    // f32 — 1.0 for Fresnel falloff
    float environMapSpecMask;   // f32 — 1.0 to mask by specular map alpha
    float texGenMode;           // f32 — 0=none, 1=xfm, 2=sphere, 3=projected, 4=xfmOrigin, 5=environ
    float texXfmRow0[4];        // vec4f — UV transform row 0 (u)
    float texXfmRow1[4];        // vec4f — UV transform row 1 (v)
    float normDetailTiling;     // f32 — UV tiling for detail normal map
    float normDetailStrength;   // f32 — blend strength (0 = disabled)
    float hasNormDetailMap;     // f32 — 1.0 when detail map bound
    float useAlphaAsRGB;        // f32 — 1.0 to use texture alpha as grayscale RGB (font textures)
};
static_assert(sizeof(MaterialUniforms) == 176, "MaterialUniforms must match WGSL layout");

struct ObjectUniforms {
    float world[16];            // mat4x4f
    float worldInvTranspose[16]; // mat4x4f
};
static_assert(sizeof(ObjectUniforms) == 128, "ObjectUniforms must match WGSL layout");

// Max bones per mesh (from Mesh.h MaxBones())
static constexpr int kMaxBones = 40;

struct BoneUniforms {
    float bones[kMaxBones][16]; // array<mat4x4f, 40>
};
static_assert(sizeof(BoneUniforms) == 2560, "BoneUniforms must match WGSL layout");

// ============================================================================
// Uniform ring buffer — writes to different offsets per draw call
// ============================================================================

class UniformRingBuffer {
public:
    void Init(wgpu::Device& device, uint32_t capacity, const char* label = nullptr);
    void Reset() { mOffset = 0; }
    void Release() { mBuffer = nullptr; mDevice = nullptr; }

    // Write data at next aligned offset, return the offset used
    uint32_t Write(wgpu::Queue& queue, const void* data, uint32_t size);

    wgpu::Buffer& Buffer() { return mBuffer; }
    uint32_t Capacity() const { return mCapacity; }

private:
    void Grow(wgpu::Device& device);

    static constexpr uint32_t kAlignment = 256; // minUniformBufferOffsetAlignment
    wgpu::Device mDevice;
    wgpu::Buffer mBuffer;
    uint32_t mCapacity = 0;
    uint32_t mOffset = 0;
    const char* mLabel = "UniformRing";
};

// ============================================================================
// WgpuShaderMgr — captures SetVConstant/SetPConstant into staging area
// ============================================================================

class WgpuShaderMgr : public RndShaderMgr {
public:
    WgpuShaderMgr() {}
    virtual ~WgpuShaderMgr() {}

    void Init() override;
    void Terminate() override;

    // For Tier 1, most constants are captured but not used —
    // scene/material/object uniforms are written directly from engine state
    void SetVConstant(VShaderConstant, const Hmx::Matrix4&) override {}
    void SetVConstant4x3(VShaderConstant, const Hmx::Matrix4&) override {}
    void SetVConstant(VShaderConstant, RndTex*) override {}
    void SetVConstant(VShaderConstant, const Vector4&) override {}
    void SetVConstant(VShaderConstant, const float*, unsigned int) override {}
    void SetVConstant(VShaderConstant, int) override {}
    void SetVConstant(VShaderConstant, bool) override {}
    void SetPConstant(PShaderConstant, const Hmx::Matrix4&) override {}
    void SetPConstant(PShaderConstant, RndCubeTex*) override {}
    void SetPConstant(PShaderConstant, const Vector4&) override {}
    void SetPConstant(PShaderConstant, RndTex*) override {}
    void SetPConstant(PShaderConstant, int) override {}
    void SetPConstant(PShaderConstant, bool) override {}
    void SetPConstant4x3(PShaderConstant, const Hmx::Matrix4&) override {}

protected:
    RndShaderProgram* NewShaderProgram() override { return nullptr; }
};

// ============================================================================
// WgpuRnd — WebGPU renderer extending NgRnd
// ============================================================================

class WgpuRnd : public NgRnd {
public:
    WgpuRnd() {}
    virtual ~WgpuRnd() {}

    void Init() override;
    // Deferred GPU resource setup — called after mGpu.IsReady() on web,
    // or inline from Init() on native.  Sets up pipelines, ring buffers,
    // depth texture, default textures, shadow/post-proc passes.
    void InitGpuResources();
    void Terminate() override;
    void Clear(unsigned int, const Hmx::Color&) override;
    void BeginDrawing() override;
    void EndDrawing() override;
    void MakeDrawTarget() override;
    void SetViewport(const Viewport& v) override;

    // Screen-space 2D drawing (NgRnd override)
    void DrawRect(const Hmx::Rect&, RndMat*, ShaderType, const Hmx::Color&,
                  const Hmx::Color*, const Hmx::Color*) override;
    // Base Rnd override
    void DrawRect(const Hmx::Rect& r, const Hmx::Color& c, RndMat* m,
                  const Hmx::Color* tr, const Hmx::Color* bl) override {
        DrawRect(r, m, kStandardShader, c, tr, bl);
    }

    // Accessors for Mesh_Wgpu.cpp / Tex_Wgpu.cpp
    GpuDevice& Gpu() { return mGpu; }
    PipelineManager& Pipelines() { return mPipelines; }
    wgpu::RenderPassEncoder& CurrentPass() { return mPass; }
    bool IsInPass() const { return mInPass; }
    wgpu::TextureFormat CurrentTargetFormat() const { return mCurrentTargetFormat; }
    uint32_t CurrentSampleCount() const { return mCurrentSampleCount; }
    bool CurrentPassHasDepth() const { return mCurrentPassHasDepth; }
    uint32_t CurrentTargetWidth() const { return mCurrentTargetWidth; }
    uint32_t CurrentTargetHeight() const { return mCurrentTargetHeight; }

    // Scene bind group (group 0) — updated when camera changes
    wgpu::BindGroup& SceneBindGroup() { return mSceneBindGroup; }
    wgpu::Buffer& SceneBuffer() { return mSceneRing.Buffer(); }
    uint32_t SceneOffset() const { return mLastSceneOffset; }
    void EnsureSceneUniformsCurrent();  // call before drawing — re-uploads if camera changed

    // Default textures
    wgpu::TextureView& WhiteTexView() { return mWhiteTexView; }
    wgpu::TextureView& FlatNormalTexView() { return mFlatNormalTexView; }
    wgpu::TextureView& BlackTexView() { return mBlackTexView; }
    wgpu::TextureView& BlackCubeTexView() { return mBlackCubeTexView; }
    wgpu::Sampler& DefaultSampler() { return mDefaultSampler; }

    // Material texture views for bind group creation
    struct MaterialTexViews {
        wgpu::TextureView diffuse;
        wgpu::TextureView normal;
        wgpu::TextureView specular;
        wgpu::TextureView emissive;
        wgpu::TextureView rim;
        wgpu::TextureView environCube;
        wgpu::TextureView normDetail;
    };

    // Create material bind group (group 1)
    wgpu::BindGroup CreateMaterialBindGroup(
        uint32_t bufferOffset, uint32_t bufferSize,
        const MaterialTexViews& texViews,
        wgpu::Sampler& diffuseSampler, wgpu::Sampler& mapSampler);

    // Create object bind group (group 2)
    wgpu::BindGroup CreateObjectBindGroup(uint32_t bufferOffset, uint32_t bufferSize);

    // Create bone bind group (group 3) — for skinned meshes
    wgpu::BindGroup CreateBoneBindGroup(uint32_t bufferOffset, uint32_t bufferSize);

    // Ring buffers for per-draw uniforms
    UniformRingBuffer& MaterialRing() { return mMaterialRing; }
    UniformRingBuffer& ObjectRing() { return mObjectRing; }
    UniformRingBuffer& BoneRing() { return mBoneRing; }

    // Shadow mapping — accessed by Mesh_Wgpu.cpp (delegates to ShadowPass)
    bool InShadowPass() const { return mShadowPass.InShadowPass(); }
    wgpu::RenderPassEncoder& ShadowRenderPass() { return mShadowPass.Pass(); }
    wgpu::RenderPipeline& ShadowStaticPipeline() { return mShadowPass.StaticPipeline(); }
    wgpu::RenderPipeline& ShadowSkinnedPipeline() { return mShadowPass.SkinnedPipeline(); }
    wgpu::BindGroupLayout& ShadowObjectBGL() { return mShadowPass.ObjectBGL(); }
    wgpu::BindGroupLayout& ShadowBoneBGL() { return mShadowPass.BoneBGL(); }
    wgpu::TextureView& ShadowDepthView() { return mShadowPass.DepthView(); }
    wgpu::Sampler& ShadowSampler() { return mShadowPass.Sampler(); }
    bool ShadowAvailable() const { return mShadowPass.Available(); }

    void SelectRenderTarget(RndTex* tex);
    void FinishRenderTarget(RndTex* tex);
    RndTex* ActiveTargetTex() const { return mActiveTargetTex; }

private:
    void ApplyViewport();
    void BeginFramePass(bool clear);
    void BeginTexturePass(RndTex* tex);
    void EndActivePass();
    void CreateDepthTexture(int w, int h);
    void CreateDefaultTextures();
    void WriteSceneUniforms();
    void MaybeCaptureFrame();
    void MaybeEncodeVideoFrame();
    void NativeVenueInit();

    GpuDevice mGpu;
    PipelineManager mPipelines;

    // Render passes (extracted subsystems)
    ShadowPass mShadowPass;
    PostProcPass mPostProcPass;
    DrawRect2D mDrawRect2D;

    // Per-frame state
    wgpu::CommandEncoder mEncoder;
    wgpu::RenderPassEncoder mPass;
    wgpu::TextureView mFrameView;
    bool mInPass = false;
    RndTex* mActiveTargetTex = nullptr;
    wgpu::TextureFormat mCurrentTargetFormat = wgpu::TextureFormat::Undefined;
    uint32_t mCurrentSampleCount = 1;
    bool mCurrentPassHasDepth = false;
    uint32_t mCurrentTargetWidth = 0;
    uint32_t mCurrentTargetHeight = 0;

    // Uniform buffers
    UniformRingBuffer mSceneRing;
    UniformRingBuffer mMaterialRing;
    UniformRingBuffer mObjectRing;
    UniformRingBuffer mBoneRing;

    // Bind groups
    wgpu::BindGroup mSceneBindGroup;

    // Depth texture
    wgpu::Texture mDepthTex;
    wgpu::TextureView mDepthView;
    int mDepthWidth = 0;
    int mDepthHeight = 0;

    // MSAA render target (4x) — resolves to surface texture
#ifdef __EMSCRIPTEN__
    static constexpr uint32_t kMSAASamples = 1; // Browser WebGPU — no MSAA for now
#else
    static constexpr uint32_t kMSAASamples = 4;
#endif
    wgpu::Texture mMsaaTex;
    wgpu::TextureView mMsaaView;
    int mMsaaWidth = 0;
    int mMsaaHeight = 0;

    // Intermediate texture for post-processing
    wgpu::Texture mIntermediateTex;
    wgpu::TextureView mIntermediateView;
    int mIntermediateWidth = 0;
    int mIntermediateHeight = 0;
    bool mFramePassValid = false;

    // Default textures
    wgpu::Texture mWhiteTex;
    wgpu::TextureView mWhiteTexView;
    wgpu::Texture mFlatNormalTex;
    wgpu::TextureView mFlatNormalTexView;
    wgpu::Texture mBlackTex;
    wgpu::TextureView mBlackTexView;
    wgpu::Texture mBlackCubeTex;
    wgpu::TextureView mBlackCubeTexView;
    wgpu::Sampler mDefaultSampler;

    // Scene uniform tracking — re-upload when camera or environment changes
    RndCam* mLastSceneCam = nullptr;
    RndEnviron* mLastSceneEnv = nullptr;
    uint32_t mLastSceneOffset = 0;
    float mLastCamPosX = 0.0f; // detect same-pointer position changes
    float mLastCamPosY = 0.0f;
    float mLastCamPosZ = 0.0f;

    // Native venue initialization (one-shot)
    bool mVenueInited = false;

    // Auto-screenshot capture (env-var controlled)
    std::string mScreenshotDir;
    std::vector<int> mCaptureFrames;
    int mCaptureIndex = 0;

    // Video recording (MILO_VIDEO env var)
    VideoEncoder mVideoEncoder;
    uint8_t* mVideoPixels = nullptr;
    size_t mVideoPixelSize = 0;
};

// Global accessor — set during Init
extern WgpuRnd* gWgpuRnd;
