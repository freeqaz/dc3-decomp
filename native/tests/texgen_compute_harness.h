// texgen_compute_harness.h -- run a piece of the engine's standard WGSL vertex
// path in a compute shader on a MaterialUniforms block, one vertex per
// invocation, and read the result back. RunTexGenCompute runs the texgen calls
// vs_main makes (texGenSourceUV + computeTexGenUV); RunMaterialCompute takes
// any compute tail with the same bindings and entry point. Shared by
// test_screen_aligned_texgen.cpp, test_retail_tex_xfm.cpp and
// test_retail_flip_normal.cpp. Needs a GPU device; returns an empty vector
// (with a gtest failure) when the shader does not compile or readback fails.
#pragma once

#include "gfx/GpuDevice.h"
#include "gfx/ShippedWgsl.h"
#include "gfx/UniformStructs.h"
#include "platform/Rnd_Wgpu.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

struct TexGenVertex {
    float clip[4];
    float uv[2];
};

// The bindings every tail gets: per vertex an input pair (clip, uv) -- read as
// two vec4fs, the uv's zw free for a tail's own use -- and one vec4f out.
inline const char *kMaterialComputeTailHeader = R"WGSL(
struct TexGenTestIn {
    clip: vec4f,
    uv: vec4f,
}
@group(3) @binding(1) var<storage, read> texGenTestIn: array<TexGenTestIn>;
@group(3) @binding(2) var<storage, read_write> texGenTestOut: array<vec4f>;
)WGSL";

// tail: WGSL defining `fn texgen_test_main` (@compute, workgroup_size 1) that
// reads texGenTestIn[id.x] and writes texGenTestOut[id.x]. Groups 0 (scene)
// and 1 (material) are bound, so the tail must use both (`_ = scene.x;` is
// enough), or the derived layout lacks the binding. Returns four floats per
// vertex.
inline std::vector<float> RunMaterialCompute(const MaterialUniforms &mat,
                                             const std::vector<TexGenVertex> &verts,
                                             const char *tail) {
    const char *kComputeTail = tail;
    const size_t n = verts.size();
    GpuDevice &gpu = gWgpuRnd->Gpu();
    wgpu::Device &dev = gpu.Device();
    const std::string src =
        std::string(StandardWgslSource()) + kMaterialComputeTailHeader + kComputeTail;
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = src.c_str();
    wgpu::ShaderModuleDescriptor md{};
    md.label = "TexGenComputeHarness";
    md.nextInChain = &wgsl;
    wgpu::ShaderModule module = dev.CreateShaderModule(&md);
    std::string errors;
    gpu.Instance().WaitAny(
        module.GetCompilationInfo(
            wgpu::CallbackMode::WaitAnyOnly,
            [&](wgpu::CompilationInfoRequestStatus, wgpu::CompilationInfo const *info) {
                if (!info) return;
                for (size_t i = 0; i < info->messageCount; i++) {
                    const auto &m = info->messages[i];
                    if (m.type != wgpu::CompilationMessageType::Error) continue;
                    errors += "line " + std::to_string(m.lineNum) + ": " +
                        std::string(m.message.data, m.message.length) + "\n";
                }
            }),
        UINT64_MAX);
    if (!errors.empty()) {
        ADD_FAILURE() << "standard WGSL + compute tail does not compile:\n" << errors;
        return {};
    }
    wgpu::ComputePipelineDescriptor cd{};
    cd.compute.module = module;
    cd.compute.entryPoint = "texgen_test_main";
    wgpu::ComputePipeline pipe = dev.CreateComputePipeline(&cd);

    auto buffer = [&](uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor d{};
        d.size = size;
        d.usage = usage;
        return dev.CreateBuffer(&d);
    };
    const auto U = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    const uint64_t inSize = n * 32, outSize = n * 16;
    wgpu::Buffer sceneBuf = buffer(sizeof(SceneUniforms), U);
    wgpu::Buffer matBuf = buffer(sizeof(MaterialUniforms), U);
    wgpu::Buffer inBuf = buffer(inSize, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
    wgpu::Buffer outBuf =
        buffer(outSize, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
    wgpu::Buffer staging =
        buffer(outSize, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);

    auto group = [&](uint32_t index, std::vector<wgpu::BindGroupEntry> entries) {
        wgpu::BindGroupDescriptor d{};
        d.layout = pipe.GetBindGroupLayout(index);
        d.entryCount = entries.size();
        d.entries = entries.data();
        return dev.CreateBindGroup(&d);
    };
    auto entry = [](uint32_t binding, wgpu::Buffer buf, uint64_t size) {
        wgpu::BindGroupEntry e{};
        e.binding = binding;
        e.buffer = buf;
        e.size = size;
        return e;
    };
    wgpu::BindGroup g0 = group(0, {entry(0, sceneBuf, sizeof(SceneUniforms))});
    wgpu::BindGroup g1 = group(1, {entry(0, matBuf, sizeof(MaterialUniforms))});
    wgpu::BindGroup g2 = group(2, {}); // tails read no object uniforms
    wgpu::BindGroup g3 = group(3, {entry(1, inBuf, inSize), entry(2, outBuf, outSize)});

    SceneUniforms s{};
    std::vector<float> in(n * 8, 0.0f);
    for (size_t i = 0; i < n; i++) {
        memcpy(&in[8 * i], verts[i].clip, 4 * sizeof(float));
        memcpy(&in[8 * i + 4], verts[i].uv, 2 * sizeof(float));
    }
    gpu.Queue().WriteBuffer(sceneBuf, 0, &s, sizeof(s));
    gpu.Queue().WriteBuffer(matBuf, 0, &mat, sizeof(mat));
    gpu.Queue().WriteBuffer(inBuf, 0, in.data(), inSize);
    wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = enc.BeginComputePass();
    pass.SetPipeline(pipe);
    pass.SetBindGroup(0, g0);
    pass.SetBindGroup(1, g1);
    pass.SetBindGroup(2, g2);
    pass.SetBindGroup(3, g3);
    pass.DispatchWorkgroups(n);
    pass.End();
    enc.CopyBufferToBuffer(outBuf, 0, staging, 0, outSize);
    wgpu::CommandBuffer cmd = enc.Finish();
    gpu.Queue().Submit(1, &cmd);
    bool mapped = false;
    gpu.Instance().WaitAny(
        staging.MapAsync(wgpu::MapMode::Read, 0, outSize, wgpu::CallbackMode::WaitAnyOnly,
                         [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                             mapped = st == wgpu::MapAsyncStatus::Success;
                         }),
        UINT64_MAX);
    if (!mapped) {
        ADD_FAILURE() << "readback map failed";
        return {};
    }
    const float *p = static_cast<const float *>(staging.GetConstMappedRange(0, outSize));
    std::vector<float> out(p, p + 4 * n);
    staging.Unmap();
    return out;
}

inline std::vector<float> RunTexGenCompute(const MaterialUniforms &mat,
                                           const std::vector<TexGenVertex> &verts) {
    static const char *kTexGenTail = R"WGSL(
@compute @workgroup_size(1)
fn texgen_test_main(@builtin(global_invocation_id) id: vec3u) {
    let v = texGenTestIn[id.x];
    let uv = computeTexGenUV(texGenSourceUV(v.uv.xy, v.clip), vec3f(0.0), vec3f(0.0, 0.0, 1.0),
                             vec3f(0.0, 0.0, 1.0));
    texGenTestOut[id.x] = vec4f(uv, 0.0, 0.0);
}
)WGSL";
    const std::vector<float> all = RunMaterialCompute(mat, verts, kTexGenTail);
    if (all.empty()) return {};
    std::vector<float> out(2 * verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        out[2 * i] = all[4 * i];
        out[2 * i + 1] = all[4 * i + 1];
    }
    return out;
}
