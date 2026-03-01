// DC3 Native Port — WebGPU Renderer Header
// WgpuRnd (extends NgRnd) and WgpuShaderMgr (extends RndShaderMgr)

#pragma once

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"

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
};
static_assert(sizeof(SceneUniforms) == 336, "SceneUniforms must match WGSL layout");

struct MaterialUniforms {
    float color[4];             // vec4f
    float alphaThreshold;       // f32
    float useTexture;           // f32
    float specularPower;        // f32
    float emissiveMultiplier;   // f32
    float specularColor[4];     // vec4f
    float rimColor[4];          // vec4f — .rgb = color, .a = power
    float intensify;            // f32
    float _pad[3];
};
static_assert(sizeof(MaterialUniforms) == 80, "MaterialUniforms must match WGSL layout");

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
    void Init(wgpu::Device& device, uint32_t capacity);
    void Reset() { mOffset = 0; }

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
};

// ============================================================================
// WgpuShaderMgr — captures SetVConstant/SetPConstant into staging area
// ============================================================================

class WgpuShaderMgr : public RndShaderMgr {
public:
    WgpuShaderMgr() {}
    virtual ~WgpuShaderMgr() {}

    void Init() override {}
    void Terminate() override {}

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
    void Terminate() override;
    void Clear(unsigned int, const Hmx::Color&) override;
    void BeginDrawing() override;
    void EndDrawing() override;

    // Accessors for Mesh_Wgpu.cpp / Tex_Wgpu.cpp
    GpuDevice& Gpu() { return mGpu; }
    PipelineManager& Pipelines() { return mPipelines; }
    wgpu::RenderPassEncoder& CurrentPass() { return mPass; }
    bool IsInPass() const { return mInPass; }

    // Scene bind group (group 0) — set once per frame
    wgpu::BindGroup& SceneBindGroup() { return mSceneBindGroup; }

    // Default 1x1 white texture for untextured materials
    wgpu::TextureView& WhiteTexView() { return mWhiteTexView; }
    wgpu::Sampler& DefaultSampler() { return mDefaultSampler; }

    // Create material bind group (group 1)
    wgpu::BindGroup CreateMaterialBindGroup(
        uint32_t bufferOffset, uint32_t bufferSize,
        wgpu::TextureView& texView, wgpu::Sampler& sampler);

    // Create object bind group (group 2)
    wgpu::BindGroup CreateObjectBindGroup(uint32_t bufferOffset, uint32_t bufferSize);

    // Create bone bind group (group 3) — for skinned meshes
    wgpu::BindGroup CreateBoneBindGroup(uint32_t bufferOffset, uint32_t bufferSize);

    // Ring buffers for per-draw uniforms
    UniformRingBuffer& MaterialRing() { return mMaterialRing; }
    UniformRingBuffer& ObjectRing() { return mObjectRing; }
    UniformRingBuffer& BoneRing() { return mBoneRing; }

private:
    void CreateDepthTexture(int w, int h);
    void CreateDefaultTextures();
    void WriteSceneUniforms();

    GpuDevice mGpu;
    PipelineManager mPipelines;

    // Per-frame state
    wgpu::CommandEncoder mEncoder;
    wgpu::RenderPassEncoder mPass;
    wgpu::TextureView mFrameView;
    bool mInPass = false;

    // Uniform buffers
    wgpu::Buffer mSceneBuffer;
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

    // Default textures
    wgpu::Texture mWhiteTex;
    wgpu::TextureView mWhiteTexView;
    wgpu::Sampler mDefaultSampler;

    // Clear color
    Hmx::Color mWgpuClearColor;
};

// Global accessor — set during Init
extern WgpuRnd* gWgpuRnd;
