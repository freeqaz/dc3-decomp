// test_screen_aligned_texgen.cpp -- DC3's screen-aligned texture coordinates
// (np-gfx-light round 5, docs/native/dc3-retail-lighting.md section 10 in the
// engine).
//
// A material with screen_aligned set (BaseMaterial::mScreenAligned) draws with
// ShaderOptions bit 13 (RndShaderStandard::CalcShaderOpts). The shipped
// permutation standard_0000000000002010.vs (xenia-gpu-shader-compiler ucode)
// replaces the vertex UV with the vertex's screen position before the texgen
// rows c20/c21:
//
//   r0.xy = oPos.xy * rcp(oPos.w) * 0.5 + 0.5;  r0.z = 1 - r0.y
//   o0.x  = dp2add(r0.zx, c20.yx, c20.w);  o0.y = dp2add(r0.zx, c21.yx, c21.w)
//
// The plain permutation (0x10) feeds the vertex UV to the same dp2adds.
// Streetside's peak sky (sky_peak_1..3.mat, a cylinder at radius ~7000 with
// tex_gen Xfm, tex_xfm y 0.2) is one: on Xenia its searchlight fan spans the
// screen and the mirror floor reflects the texture's navy bottom, while the
// native port mapped the cylinder's own UVs and reflected stars.
//
// These tests pin the material flag (rndshape::MatScreenAligned through
// BuildMaterialParams / BuildPassMaterialParams, colorMod.w bit 9) and then
// run the engine's own WGSL -- texGenSourceUV and computeTexGenUV, the two
// calls vs_main makes -- in a compute shader on the uniforms the builder
// wrote, against the microcode's arithmetic above.
//
// Sabotages tried (each fails a test here): texGenSourceUV returning the
// vertex UV unconditionally; FillScreenAligned not called for the primary
// material; not called for a NextPass; v not flipped (ndc.y * 0.5 + 0.5);
// rndshape::MatScreenAligned returning false.
//
// The GPU half needs a device (skips without one). No game data.

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/ShippedWgsl.h"
#include "gfx/UniformStructs.h"
#include "math/Mtx.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Declared here as test_native_ui_material_terms.cpp does: dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
struct UiDrawTerms {
    bool gammaShading = false;
    bool retailMaterialTerms = false;
};
MaterialParams BuildMaterialParams(RndMat *mat, bool isTextMesh, const UiDrawTerms &uiTerms);
MaterialParams BuildPassMaterialParams(BaseMaterial *pass, const UiDrawTerms &uiTerms);

namespace {

const UiDrawTerms kWorld{false, false};

// The sky's material as streetside authors it, less its textures:
// tex_gen Xfm, tex_xfm translated by (0, 0.2), screen_aligned as given.
RndMat *MakeSkyMat(bool screenAligned) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetTexGen(kTexGenXfm);
    Transform xfm;
    xfm.Reset();
    xfm.v.y = 0.2f;
    mat->SetTexXfm(xfm);
    mat->SetProperty("screen_aligned", DataNode(screenAligned ? 1 : 0));
    return mat;
}

bool ScreenAlignedBit(const MaterialUniforms &u) { return ((u.colorMod[3] >> 9) & 1u) != 0; }

// One vertex per invocation: in = (clip xyzw), (vertex uv, 0, 0); out = uv.
const char *kComputeTail = R"WGSL(
struct TexGenTestIn {
    clip: vec4f,
    uv: vec4f,
}
@group(3) @binding(1) var<storage, read> texGenTestIn: array<TexGenTestIn>;
@group(3) @binding(2) var<storage, read_write> texGenTestOut: array<vec4f>;

@compute @workgroup_size(1)
fn texgen_test_main(@builtin(global_invocation_id) id: vec3u) {
    let v = texGenTestIn[id.x];
    let uv = computeTexGenUV(texGenSourceUV(v.uv.xy, v.clip), vec3f(0.0), vec3f(0.0, 0.0, 1.0),
                             vec3f(0.0, 0.0, 1.0));
    texGenTestOut[id.x] = vec4f(uv, 0.0, 0.0);
}
)WGSL";

struct Vertex {
    float clip[4];
    float uv[2];
};

// Clip positions with w far from 1 (the sky sits ~7000 units out) and both
// signs of x and y, plus one off screen; vertex UVs as the cylinder carries
// them (u in -1..1 around, v 0..1 down).
const Vertex kVerts[] = {
    {{-3500.0f, 1800.0f, 6900.0f, 7000.0f}, {-0.91f, 0.405f}},
    {{2100.0f, -900.0f, 6950.0f, 7050.0f}, {0.95f, 0.596f}},
    {{0.0f, 0.0f, 10.0f, 11.0f}, {0.5f, 0.21f}},
    {{-12.5f, 30.0f, 19.0f, 20.0f}, {0.0f, 1.0f}},
};
const size_t kNumVerts = sizeof(kVerts) / sizeof(kVerts[0]);

// The shipped microcode for the material MakeSkyMat builds. MakeTex3 of an
// identity rotation translated by (0, 0.2) uploads c20 = (1, 0, -, 0) and
// c21 = (0, 1, -, 0.2): o0 = (r.x, r.y + 0.2) for whichever r the
// permutation feeds it.
void Expected(const Vertex &v, bool screenAligned, float out[2]) {
    float r[2] = {v.uv[0], v.uv[1]};
    if (screenAligned) {
        const float rw = 1.0f / v.clip[3];
        const float sx = v.clip[0] * rw * 0.5f + 0.5f;
        const float sy = v.clip[1] * rw * 0.5f + 0.5f;
        r[0] = sx;
        r[1] = 1.0f - sy;
    }
    out[0] = r[0];
    out[1] = r[1] + 0.2f;
}

class ScreenAlignedTexGenTest : public EngineTestFixture {};

class ScreenAlignedTexGenGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    wgpu::Buffer Buffer(uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor d{};
        d.size = size;
        d.usage = usage;
        return gWgpuRnd->Gpu().Device().CreateBuffer(&d);
    }

    // Every vertex through the engine's WGSL with the given material block;
    // returns uv per vertex, or an empty vector (with a failure).
    std::vector<float> Run(const MaterialUniforms &mat) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        wgpu::Device &dev = gpu.Device();
        const std::string src = std::string(StandardWgslSource()) + kComputeTail;
        wgpu::ShaderSourceWGSL wgsl;
        wgsl.code = src.c_str();
        wgpu::ShaderModuleDescriptor md{};
        md.label = "ScreenAlignedTexGenTest";
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

        const auto U = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        const uint64_t inSize = kNumVerts * 32, outSize = kNumVerts * 16;
        wgpu::Buffer sceneBuf = Buffer(sizeof(SceneUniforms), U);
        wgpu::Buffer matBuf = Buffer(sizeof(MaterialUniforms), U);
        wgpu::Buffer inBuf =
            Buffer(inSize, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
        wgpu::Buffer outBuf =
            Buffer(outSize, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
        wgpu::Buffer staging =
            Buffer(outSize, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);

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
        wgpu::BindGroup g2 = group(2, {}); // the texgen path reads no object uniforms
        wgpu::BindGroup g3 = group(3, {entry(1, inBuf, inSize), entry(2, outBuf, outSize)});

        SceneUniforms s{};
        std::vector<float> in(kNumVerts * 8, 0.0f);
        for (size_t i = 0; i < kNumVerts; i++) {
            memcpy(&in[8 * i], kVerts[i].clip, 4 * sizeof(float));
            memcpy(&in[8 * i + 4], kVerts[i].uv, 2 * sizeof(float));
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
        pass.DispatchWorkgroups(kNumVerts);
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
        std::vector<float> out(2 * kNumVerts);
        for (size_t i = 0; i < kNumVerts; i++) {
            out[2 * i] = p[4 * i];
            out[2 * i + 1] = p[4 * i + 1];
        }
        staging.Unmap();
        return out;
    }

    void Check(bool screenAligned) {
        RndMat *mat = MakeSkyMat(screenAligned);
        const MaterialParams params = BuildMaterialParams(mat, false, kWorld);
        delete mat;
        const std::vector<float> got = Run(params.uniforms);
        ASSERT_EQ(got.size(), 2 * kNumVerts);
        for (size_t i = 0; i < kNumVerts; i++) {
            float want[2];
            Expected(kVerts[i], screenAligned, want);
            for (int a = 0; a < 2; a++)
                EXPECT_NEAR(got[2 * i + a], want[a], 1e-5f)
                    << (screenAligned ? "screen-aligned" : "vertex-UV") << " vertex " << i
                    << (a ? " v" : " u");
        }
    }
};

} // namespace

// The flag is DC3's material state, carried by both builders.
TEST_F(ScreenAlignedTexGenTest, BuildersCarryTheMaterialFlag) {
    ASSERT_NE(gWgpuRnd, nullptr);
    RndMat *on = MakeSkyMat(true);
    RndMat *off = MakeSkyMat(false);
    ASSERT_TRUE(on->GetScreenAligned());
    ASSERT_FALSE(off->GetScreenAligned());
    EXPECT_TRUE(rndshape::MatScreenAligned(on));
    EXPECT_FALSE(rndshape::MatScreenAligned(off));
    EXPECT_TRUE(ScreenAlignedBit(BuildMaterialParams(on, false, kWorld).uniforms));
    EXPECT_FALSE(ScreenAlignedBit(BuildMaterialParams(off, false, kWorld).uniforms));
    EXPECT_TRUE(ScreenAlignedBit(BuildPassMaterialParams(on, kWorld).uniforms));
    EXPECT_FALSE(ScreenAlignedBit(BuildPassMaterialParams(off, kWorld).uniforms));
    // The flag lives above the colour-mod mode byte: the mode stays none.
    EXPECT_EQ(BuildMaterialParams(on, false, kWorld).uniforms.colorMod[3] & 0xFFu, 0u);
    delete on;
    delete off;
}

// The vertex shader's UV for a screen-aligned material: the microcode's.
TEST_F(ScreenAlignedTexGenGpuTest, ScreenAlignedUvIsTheVertexScreenPosition) {
    Check(true);
}

// And for every other material, the vertex UV through the same rows.
TEST_F(ScreenAlignedTexGenGpuTest, OtherMaterialsKeepTheVertexUv) {
    Check(false);
}
