#include "gfx/PipelineManager.h"
#include "gfx/GpuDevice.h"
#include "gfx/VertexFormats.h"

#include <cstdlib>
#include <cstdio>

// Embedded shader source — standard.wgsl is compiled into the binary
static const char* kStandardShaderSource =
#include "gfx/standard_wgsl.inc"
;

void PipelineManager::Init(GpuDevice* device) {
    mDevice = device;
    auto& dev = device->Device();

    // === Create bind group layouts ===

    // Group 0: Scene uniforms + shadow map + projected light texture
    wgpu::BindGroupLayoutEntry sceneEntries[5] = {};
    sceneEntries[0].binding = 0;
    sceneEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    sceneEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    sceneEntries[0].buffer.minBindingSize = 0;

    sceneEntries[1].binding = 1;
    sceneEntries[1].visibility = wgpu::ShaderStage::Fragment;
    sceneEntries[1].texture.sampleType = wgpu::TextureSampleType::Depth;
    sceneEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    sceneEntries[2].binding = 2;
    sceneEntries[2].visibility = wgpu::ShaderStage::Fragment;
    sceneEntries[2].sampler.type = wgpu::SamplerBindingType::Comparison;

    sceneEntries[3].binding = 3;
    sceneEntries[3].visibility = wgpu::ShaderStage::Fragment;
    sceneEntries[3].texture.sampleType = wgpu::TextureSampleType::Float;
    sceneEntries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    sceneEntries[4].binding = 4;
    sceneEntries[4].visibility = wgpu::ShaderStage::Fragment;
    sceneEntries[4].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor sceneLayoutDesc{};
    sceneLayoutDesc.label = "SceneBGL";
    sceneLayoutDesc.entryCount = 5;
    sceneLayoutDesc.entries = sceneEntries;
    mLayouts[0] = dev.CreateBindGroupLayout(&sceneLayoutDesc);

    // Group 1: Material uniforms + textures + samplers
    wgpu::BindGroupLayoutEntry matEntries[11] = {};
    matEntries[0].binding = 0;
    matEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    matEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    matEntries[0].buffer.minBindingSize = 0;

    matEntries[1].binding = 1;
    matEntries[1].visibility = wgpu::ShaderStage::Fragment;
    matEntries[1].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    matEntries[2].binding = 2;
    matEntries[2].visibility = wgpu::ShaderStage::Fragment;
    matEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

    // Binding 3: normal map
    matEntries[3].binding = 3;
    matEntries[3].visibility = wgpu::ShaderStage::Fragment;
    matEntries[3].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[3].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // Binding 4: specular map
    matEntries[4].binding = 4;
    matEntries[4].visibility = wgpu::ShaderStage::Fragment;
    matEntries[4].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[4].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // Binding 5: emissive map
    matEntries[5].binding = 5;
    matEntries[5].visibility = wgpu::ShaderStage::Fragment;
    matEntries[5].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[5].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // Binding 6: rim map
    matEntries[6].binding = 6;
    matEntries[6].visibility = wgpu::ShaderStage::Fragment;
    matEntries[6].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[6].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    // Binding 7: shared sampler for maps 3-6
    matEntries[7].binding = 7;
    matEntries[7].visibility = wgpu::ShaderStage::Fragment;
    matEntries[7].sampler.type = wgpu::SamplerBindingType::Filtering;

    // Binding 8: environment cube map
    matEntries[8].binding = 8;
    matEntries[8].visibility = wgpu::ShaderStage::Fragment;
    matEntries[8].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[8].texture.viewDimension = wgpu::TextureViewDimension::Cube;

    // Binding 9: cube map sampler
    matEntries[9].binding = 9;
    matEntries[9].visibility = wgpu::ShaderStage::Fragment;
    matEntries[9].sampler.type = wgpu::SamplerBindingType::Filtering;

    // Binding 10: detail normal map
    matEntries[10].binding = 10;
    matEntries[10].visibility = wgpu::ShaderStage::Fragment;
    matEntries[10].texture.sampleType = wgpu::TextureSampleType::Float;
    matEntries[10].texture.viewDimension = wgpu::TextureViewDimension::e2D;

    wgpu::BindGroupLayoutDescriptor matLayoutDesc{};
    matLayoutDesc.label = "MaterialBGL";
    matLayoutDesc.entryCount = 11;
    matLayoutDesc.entries = matEntries;
    mLayouts[1] = dev.CreateBindGroupLayout(&matLayoutDesc);

    // Group 2: Object uniforms (world transform)
    wgpu::BindGroupLayoutEntry objEntries[1] = {};
    objEntries[0].binding = 0;
    objEntries[0].visibility = wgpu::ShaderStage::Vertex;
    objEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    objEntries[0].buffer.minBindingSize = 0;

    wgpu::BindGroupLayoutDescriptor objLayoutDesc{};
    objLayoutDesc.label = "ObjectBGL";
    objLayoutDesc.entryCount = 1;
    objLayoutDesc.entries = objEntries;
    mLayouts[2] = dev.CreateBindGroupLayout(&objLayoutDesc);

    // Group 3: Bone uniforms (skinned mesh — per-draw)
    wgpu::BindGroupLayoutEntry boneEntries[1] = {};
    boneEntries[0].binding = 0;
    boneEntries[0].visibility = wgpu::ShaderStage::Vertex;
    boneEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    boneEntries[0].buffer.minBindingSize = 0;

    wgpu::BindGroupLayoutDescriptor boneLayoutDesc{};
    boneLayoutDesc.label = "BoneBGL";
    boneLayoutDesc.entryCount = 1;
    boneLayoutDesc.entries = boneEntries;
    mLayouts[3] = dev.CreateBindGroupLayout(&boneLayoutDesc);

    // === Create pipeline layout ===
    wgpu::PipelineLayoutDescriptor plDesc{};
    plDesc.label = "MainPipelineLayout";
    plDesc.bindGroupLayoutCount = 4;
    plDesc.bindGroupLayouts = mLayouts;
    mPipelineLayout = dev.CreatePipelineLayout(&plDesc);

    printf("PipelineManager: initialized with 4 bind group layouts\n");
}

wgpu::ShaderModule PipelineManager::GetOrCreateShader(uint32_t shaderType) {
    auto it = mShaderCache.find(shaderType);
    if (it != mShaderCache.end()) return it->second;

    // For Tier 1, all shader types use the standard shader
    const char* src = kStandardShaderSource;

    wgpu::ShaderSourceWGSL wgslSource;
    wgslSource.code = src;

    wgpu::ShaderModuleDescriptor desc{};
    desc.label = "StandardShader";
    desc.nextInChain = &wgslSource;
    wgpu::ShaderModule module = mDevice->Device().CreateShaderModule(&desc);

    mShaderCache[shaderType] = module;
    return module;
}

wgpu::BlendState PipelineManager::MapBlend(WgpuBlend blend) {
    wgpu::BlendState bs{};
    auto& color = bs.color;
    auto& alpha = bs.alpha;

    // Default alpha blend: same as color
    alpha.operation = wgpu::BlendOperation::Add;

    switch (blend) {
    case WgpuBlend::Dest:
        color.srcFactor = wgpu::BlendFactor::Zero;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::Src:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::Zero;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::Add:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::SrcAlpha:
        color.srcFactor = wgpu::BlendFactor::SrcAlpha;
        color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::SrcAlphaAdd:
        color.srcFactor = wgpu::BlendFactor::SrcAlpha;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::Subtract:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::ReverseSubtract;
        break;
    case WgpuBlend::Multiply:
        color.srcFactor = wgpu::BlendFactor::Dst;
        color.dstFactor = wgpu::BlendFactor::Zero;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::PreMultAlpha:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::Screen:
        color.srcFactor = wgpu::BlendFactor::OneMinusDst;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Add;
        break;
    case WgpuBlend::Lighten:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Max;
        break;
    case WgpuBlend::Darken:
        color.srcFactor = wgpu::BlendFactor::One;
        color.dstFactor = wgpu::BlendFactor::One;
        color.operation = wgpu::BlendOperation::Min;
        break;
    }

    alpha.srcFactor = color.srcFactor;
    alpha.dstFactor = color.dstFactor;
    alpha.operation = color.operation;

    return bs;
}

wgpu::DepthStencilState PipelineManager::MapDepthStencil(WgpuZMode z, WgpuStencil s) {
    wgpu::DepthStencilState ds{};
    ds.format = wgpu::TextureFormat::Depth24PlusStencil8;

    switch (z) {
    case WgpuZMode::Disable:
        ds.depthWriteEnabled = wgpu::OptionalBool::False;
        ds.depthCompare = wgpu::CompareFunction::Always;
        break;
    case WgpuZMode::Normal:
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::Less;
        break;
    case WgpuZMode::Transparent:
        ds.depthWriteEnabled = wgpu::OptionalBool::False;
        ds.depthCompare = wgpu::CompareFunction::LessEqual;
        break;
    case WgpuZMode::Force:
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::Always;
        break;
    case WgpuZMode::Decal:
        ds.depthWriteEnabled = wgpu::OptionalBool::True;
        ds.depthCompare = wgpu::CompareFunction::LessEqual;
        break;
    }

    // Stencil (Tier 1: basic support)
    if (s == WgpuStencil::Write) {
        ds.stencilFront.compare = wgpu::CompareFunction::Always;
        ds.stencilFront.passOp = wgpu::StencilOperation::Replace;
        ds.stencilBack = ds.stencilFront;
    } else if (s == WgpuStencil::Test) {
        ds.stencilFront.compare = wgpu::CompareFunction::Equal;
        ds.stencilBack = ds.stencilFront;
    }

    return ds;
}

wgpu::CullMode PipelineManager::MapCull(WgpuCull cull) {
    switch (cull) {
    case WgpuCull::None:      return wgpu::CullMode::None;
    case WgpuCull::Regular:   return wgpu::CullMode::Back;
    case WgpuCull::Backwards: return wgpu::CullMode::Front;
    default:                  return wgpu::CullMode::None;
    }
}

wgpu::RenderPipeline PipelineManager::CreatePipeline(const PipelineKey& key) {
    wgpu::ShaderModule shader = GetOrCreateShader(key.shaderType);

    // Vertex layout
    const wgpu::VertexBufferLayout* vtxLayout;
    if (key.layout == VertexLayoutType::Skinned) {
        vtxLayout = &VertexFormats::SkinnedLayout();
    } else {
        vtxLayout = &VertexFormats::StaticLayout();
    }

    // Blend state
    wgpu::BlendState blendState = MapBlend(key.blend);

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = key.targetFormat;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = key.alphaWrite
        ? wgpu::ColorWriteMask::All
        : (wgpu::ColorWriteMask::Red | wgpu::ColorWriteMask::Green | wgpu::ColorWriteMask::Blue);

    wgpu::FragmentState fragment{};
    fragment.module = shader;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    wgpu::DepthStencilState ds{};
    if (key.hasDepth) {
        ds = MapDepthStencil(key.zMode, key.stencil);
        if (key.depthBias != 0) {
            ds.depthBias = key.depthBias;
            ds.depthBiasSlopeScale = 0.0f;
            ds.depthBiasClamp = 0.0f;
        }
    }

    wgpu::RenderPipelineDescriptor pipeDesc{};
    pipeDesc.label = (key.layout == VertexLayoutType::Skinned) ? "MainSkinned" : "MainStatic";
    pipeDesc.layout = mPipelineLayout;
    pipeDesc.vertex.module = shader;
    pipeDesc.vertex.entryPoint = (key.layout == VertexLayoutType::Skinned) ? "vs_skinned" : "vs_main";
    pipeDesc.vertex.bufferCount = 1;
    pipeDesc.vertex.buffers = vtxLayout;
    pipeDesc.fragment = &fragment;
    pipeDesc.depthStencil = key.hasDepth ? &ds : nullptr;
    pipeDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipeDesc.primitive.frontFace = wgpu::FrontFace::CCW; // D3D LH CW front → WebGPU RH CCW front
    pipeDesc.primitive.cullMode = MapCull(key.cull);
    pipeDesc.multisample.count = key.sampleCount;
    // WebGPU spec: alphaToCoverageEnabled requires count > 1
    pipeDesc.multisample.alphaToCoverageEnabled = key.alphaToCoverage && key.sampleCount > 1;

    static bool sLog = getenv("MILO_DEBUG_PIPELINES") != nullptr;
    if (sLog) {
        printf(
            "DC3 Pipeline: label=%s fmt=%d samples=%u depth=%d shader=%u alphaCut=%d alphaWrite=%d\n",
            key.layout == VertexLayoutType::Skinned ? "MainSkinned" : "MainStatic",
            (int)key.targetFormat,
            key.sampleCount,
            key.hasDepth ? 1 : 0,
            key.shaderType,
            key.alphaCut ? 1 : 0,
            key.alphaWrite ? 1 : 0
        );
    }

    return mDevice->Device().CreateRenderPipeline(&pipeDesc);
}

wgpu::RenderPipeline PipelineManager::GetPipeline(const PipelineKey& key) {
    auto it = mPipelineCache.find(key);
    if (it != mPipelineCache.end()) return it->second;

    wgpu::RenderPipeline pipeline = CreatePipeline(key);
    mPipelineCache[key] = pipeline;

    if (mPipelineCache.size() == 512) {
        fprintf(stderr, "PipelineManager: warning — cache reached 512 entries, possible leak\n");
    }
    return pipeline;
}
