// test_retail_char_shading.cpp -- DC3's skin, hair and rim pixel shading
// against the shipped standard.ps microcode.
//
// DC3 shades a material per pixel with one of the standard.ps permutations in
// the shader cache. Skin (RndMat shader_variation 1) and hair (2) are their
// own permutations, not the plain one with a different colour: skin wraps the
// point lights per channel (red most), adds a rim-lit box term and a
// back-light sheen, and puts a Fresnel-weighted lobe on the full-strength
// normal; hair has a horizontal-strand anisotropic lobe tinted by the second
// specular colour. The plain permutation with a rim also lights the rim from
// behind with each point light. The models are written out in the engine's
// standard_wgsl.inc (dc3SkinLight, dc3HairLight, retailPixelShade) and in
// docs/native/dc3-retail-lighting.md.
//
// The expected colours in retail_char_shading_cases.inc are what the shipped
// MICROCODE computes for random inputs: scripts/gpu/xenos_ucode/ucode_emu.py
// executes xenia's ucode listing of each cached permutation
// (gen_char_shading_cases.py writes the table). This test runs the engine's
// own WGSL -- retailPixelShade, the per-pixel half of retailShade after the
// texture fetches -- in a compute shader on the same inputs, with the scene,
// material and object uniform blocks filled as the draw path fills them, and
// compares.
//
// Sabotages tried (each fails this test; see the lane notes):
//   skin wrap 0.45 -> 0.5; skin back-light sheen removed; skin specular on the
//   de_normal'd normal instead of the full-strength one; hair second-colour
//   lobe tinted by c2; plain rim without the point-light back term (the
//   round-1 form); the skin/hair branch disabled (everything through the
//   plain path).
//
// Needs a GPU (skips without one). No game data.

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/ShippedWgsl.h"
#include "gfx/UniformStructs.h"
#include "platform/Rnd_Wgpu.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct CharCase {
    int kind;   // 0 skin, 1 skin without rim, 2 hair, 3 plain with rim
    float P[3], V[3], n[3], t[3], b[3];
    float diffTex[4], normalTex[4], specTex[4];
    float c0[4], c1[4], c2[4], k, c19[4], c61[4];
    float lightPos[3][4], lightCol[3][4];
    float box[6][4];
    float want[3];
};

const CharCase kCases[] = {
#include "retail_char_shading_cases.inc"
};

const char *const kKindName[] = {"skin", "skin (no rim)", "hair", "plain + rim"};

// Appended to the standard shader: one pixel per dispatch.
const char *kComputeTail = R"WGSL(
struct CharTestIn {
    worldPos: vec4f,
    normal: vec4f,
    tangent: vec4f,
    bitangent: vec4f,
    normalSample: vec4f,
    specSample: vec4f,
    base: vec4f,
    ambient: vec4f,
}
@group(3) @binding(1) var<storage, read> charTestIn: CharTestIn;
@group(3) @binding(2) var<storage, read_write> charTestOut: vec4f;

@compute @workgroup_size(1)
fn char_test_main() {
    var px: RetailPixel;
    px.worldPos = charTestIn.worldPos.xyz;
    px.normal = charTestIn.normal.xyz;
    px.tangent = charTestIn.tangent.xyz;
    px.bitangent = charTestIn.bitangent.xyz;
    px.normalSample = charTestIn.normalSample;
    px.specSample = charTestIn.specSample;
    px.rimSample = vec4f(1.0);
    px.ao = vec4f(1.0, 1.0, 1.0, 0.0);
    var proj: Dc3Proj;
    proj.mult = vec3f(1.0);
    proj.add = vec3f(0.0);
    charTestOut = vec4f(retailPixelShade(px, charTestIn.base.rgb, charTestIn.ambient.rgb, proj),
                        1.0);
}
)WGSL";

void Copy4(float *dst, const float *src) { memcpy(dst, src, 4 * sizeof(float)); }
void Copy3(float *dst, const float *src, float w) {
    memcpy(dst, src, 3 * sizeof(float));
    dst[3] = w;
}

class RetailCharShadingTest : public EngineTestFixture {
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

    // Shade every case through the engine's WGSL; returns rgb per case, or an
    // empty vector (with a gtest failure) if the shader does not compile.
    std::vector<float> ShadeAll(float retailLighting) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        wgpu::Device &dev = gpu.Device();
        const std::string src = std::string(StandardWgslSource()) + kComputeTail;
        wgpu::ShaderSourceWGSL wgsl;
        wgsl.code = src.c_str();
        wgpu::ShaderModuleDescriptor md{};
        md.label = "RetailCharShadingTest";
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
        cd.compute.entryPoint = "char_test_main";
        wgpu::ComputePipeline pipe = dev.CreateComputePipeline(&cd);

        const auto U = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        wgpu::Buffer sceneBuf = Buffer(sizeof(SceneUniforms), U);
        wgpu::Buffer matBuf = Buffer(sizeof(MaterialUniforms), U);
        wgpu::Buffer objBuf = Buffer(sizeof(ObjectUniforms), U);
        wgpu::Buffer inBuf =
            Buffer(8 * 16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
        wgpu::Buffer outBuf =
            Buffer(16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
        const size_t n = sizeof(kCases) / sizeof(kCases[0]);
        wgpu::Buffer staging =
            Buffer(16 * n, wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst);

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
        wgpu::BindGroup g2 = group(2, {entry(0, objBuf, sizeof(ObjectUniforms))});
        wgpu::BindGroup g3 = group(3, {entry(1, inBuf, 8 * 16), entry(2, outBuf, 16)});

        for (size_t i = 0; i < n; i++) {
            const CharCase &c = kCases[i];
            SceneUniforms s{};
            for (int a = 0; a < 3; a++) s.cameraPos[a] = c.P[a] + c.V[a];
            for (int l = 0; l < 3; l++) {
                Copy4(s.pointLightPos[l], c.lightPos[l]);
                Copy4(s.pointLightColors[l], c.lightCol[l]);
            }
            s.numPointLights = 3.0f;
            s.retailLighting = retailLighting;
            MaterialUniforms m{};
            Copy4(m.color, c.c0);
            Copy4(m.retailSpec, c.c2);
            const bool rim = c.kind == 0 || c.kind == 3;
            if (rim) Copy4(m.retailRim, c.c61);
            Copy4(m.specular2Color, c.c19);
            m.deNormal = 1.0f - c.k;
            m.shaderVariation = c.kind <= 1 ? 1.0f : c.kind == 2 ? 2.0f : 0.0f;
            m.retailFlags[0] = 1.0f;                       // per-pixel
            m.retailFlags[1] = c.kind == 2 ? 0.0f : 1.0f;  // normal map (hair has none)
            m.retailFlags[2] = 1.0f;                       // specular map
            m.retailFlags[3] = 0.0f;                       // no rim map
            ObjectUniforms o{};
            for (int f = 0; f < 6; f++) Copy4(o.boxLight[f], c.box[f]);
            float in[8][4] = {};
            Copy3(in[0], c.P, 1.0f);
            Copy3(in[1], c.n, 0.0f);
            Copy3(in[2], c.t, 0.0f);
            Copy3(in[3], c.b, 0.0f);
            Copy4(in[4], c.normalTex);
            Copy4(in[5], c.specTex);
            for (int a = 0; a < 3; a++) {
                in[6][a] = c.diffTex[a] * c.c0[a];   // texel times material colour
                in[7][a] = c.c1[a];                  // ambient
            }
            gpu.Queue().WriteBuffer(sceneBuf, 0, &s, sizeof(s));
            gpu.Queue().WriteBuffer(matBuf, 0, &m, sizeof(m));
            gpu.Queue().WriteBuffer(objBuf, 0, &o, sizeof(o));
            gpu.Queue().WriteBuffer(inBuf, 0, in, sizeof(in));
            wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
            wgpu::ComputePassEncoder pass = enc.BeginComputePass();
            pass.SetPipeline(pipe);
            pass.SetBindGroup(0, g0);
            pass.SetBindGroup(1, g1);
            pass.SetBindGroup(2, g2);
            pass.SetBindGroup(3, g3);
            pass.DispatchWorkgroups(1);
            pass.End();
            enc.CopyBufferToBuffer(outBuf, 0, staging, 16 * i, 16);
            wgpu::CommandBuffer cmd = enc.Finish();
            gpu.Queue().Submit(1, &cmd);
        }
        bool mapped = false;
        gpu.Instance().WaitAny(
            staging.MapAsync(wgpu::MapMode::Read, 0, 16 * n, wgpu::CallbackMode::WaitAnyOnly,
                             [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                 mapped = st == wgpu::MapAsyncStatus::Success;
                             }),
            UINT64_MAX);
        if (!mapped) {
            ADD_FAILURE() << "readback map failed";
            return {};
        }
        const float *p = static_cast<const float *>(staging.GetConstMappedRange(0, 16 * n));
        std::vector<float> out(3 * n);
        for (size_t i = 0; i < n; i++)
            for (int a = 0; a < 3; a++) out[3 * i + a] = p[4 * i + a];
        staging.Unmap();
        return out;
    }
};

// Relative to the colour, with a floor for near-black channels. On an RTX
// 3090 the WGSL lands within 8e-7 of the emulated microcode; 2e-5 leaves room
// for other GPUs' pow/normalize and is far below what any sabotage above moves.
bool Close(float got, float want) {
    return std::fabs(got - want) <= 2e-5f * std::fmax(1.0f, std::fabs(want));
}

TEST_F(RetailCharShadingTest, PixelShadingIsTheShippedMicrocode) {
    const std::vector<float> got = ShadeAll(2.0f);
    ASSERT_FALSE(got.empty());
    const size_t n = sizeof(kCases) / sizeof(kCases[0]);
    int bad[4] = {}, total[4] = {};
    float worst[4] = {};
    for (size_t i = 0; i < n; i++) {
        const CharCase &c = kCases[i];
        total[c.kind]++;
        bool ok = true;
        for (int a = 0; a < 3; a++) {
            const float g = got[3 * i + a];
            worst[c.kind] = std::fmax(worst[c.kind], std::fabs(g - c.want[a]) /
                                                         std::fmax(1.0f, std::fabs(c.want[a])));
            ok = ok && std::isfinite(g) && Close(g, c.want[a]);
        }
        if (!ok) {
            if (bad[c.kind]++ < 3)
                ADD_FAILURE() << kKindName[c.kind] << " case " << i << ": got (" << got[3 * i]
                              << ", " << got[3 * i + 1] << ", " << got[3 * i + 2]
                              << ") want (" << c.want[0] << ", " << c.want[1] << ", "
                              << c.want[2] << ")";
        }
    }
    for (int k = 0; k < 4; k++) {
        printf("[ retail char ] %-14s %d/%d within tolerance, worst rel err %.2e\n", kKindName[k],
               total[k] - bad[k], total[k], worst[k]);
        EXPECT_EQ(bad[k], 0) << kKindName[k];
        EXPECT_GT(total[k], 0) << kKindName[k] << ": no cases (table regenerated wrongly?)";
    }
}

// The skin/hair maths and the rim back light are DC3's (scene.retailLighting
// 2). Under RB3's form of the retail model (1) the same material must take
// the plain path and the round-1 rim, so the colours differ.
TEST_F(RetailCharShadingTest, Rb3FormKeepsThePlainPath) {
    const std::vector<float> dc3 = ShadeAll(2.0f);
    const std::vector<float> rb3 = ShadeAll(1.0f);
    ASSERT_FALSE(dc3.empty());
    ASSERT_FALSE(rb3.empty());
    const size_t n = sizeof(kCases) / sizeof(kCases[0]);
    int differ[4] = {}, total[4] = {};
    for (size_t i = 0; i < n; i++) {
        const int k = kCases[i].kind;
        total[k]++;
        bool same = true;
        for (int a = 0; a < 3; a++) same = same && Close(rb3[3 * i + a], dc3[3 * i + a]);
        if (!same) differ[k]++;
    }
    // Skin and hair differ everywhere. The plain rim differs only by the
    // back light, which is zero where every light is in front of the pixel.
    for (int k = 0; k < 3; k++)
        EXPECT_EQ(differ[k], total[k]) << kKindName[k] << ": " << total[k] - differ[k]
                                       << " cases shade the same under RB3's form";
    EXPECT_GT(differ[3], 0) << "plain + rim: the back light is not DC3-only";
}

} // namespace
