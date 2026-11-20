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
// And the projected shadow (projected_blend 1) on the plain path:
// standard.ps 0000110010030011 accumulates the point lights and the six box
// faces in one register and scales that by c0 (1 - 0.75 s c69 sat(c65 . N)),
// adding c1 c0 unscaled (ProjectedShadowScalesTheWholeDiffuseLight). Sabotage:
// scaling only the box term (the form before item 11) fails it -- with the
// factor at 0 the point lights still light the pixel.
//
// And the normal detail map (bit 24, kind 4, item 13 (c)): standard.ps
// 0000030001030035 adds norm_detail_strength (dx, dy, sat(1 - dx^2 - dy^2))
// of the detail texel to the normal map's (x, y, z) before the de_normal
// factor. Sabotages: the detail term removed (the form before item 13, which
// left houseparty's floor without its sheen); its z term removed; the detail
// added after the de_normal factor; the DC3 gate widened to RB3's form
// (Rb3FormKeepsThePlainPath fails).
//
// And the character self-shadow map (option bit 19, item 16 (a)): kinds 5-9
// are five of the permutations above again with bit 19 -- skin, skin without
// rim, hair, plain with rim, and plain without rim or normal map -- whose
// microcode point-samples the 512x512 depth map at four taps, weights them
// bilinearly (getWeights2D) and scales every point light's term (diffuse,
// specular, the rim's back light) by 1 - 0.75 c107 sat(N . -c108) (1 - s).
// The test feeds the engine a 2x2 depth map holding each case's four taps
// and a shadow-camera matrix that lands the pixel on the case's bilinear
// fractions. Sabotages: the factor removed from the diffuse loop; the factor
// on the box light too; the taps without the half-texel shift (floor(uv *
// size) instead of floor(uv * size - 0.5)); the depth compare reversed; the
// skin's specular scaled by m(Nk) instead of m(Nf); the gate on
// scene.selfShadowColor.w dropped (SelfShadowNeedsItsCamera fails).
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
    float detailTex[4], c106[4];   // kind 4 only: the normal detail texel, (strength, tiling, 1, 1)
    // kinds 5-9 only: the shadow map's bilinear fractions, which of its taps
    // (-,-), (+,-), (-,+), (+,+) is lit (1) or in shadow (0), c107 and c108
    float shadowW[2], shadowLit[4], c107[4], c108[4];
};

const CharCase kCases[] = {
#include "retail_char_shading_cases.inc"
};

constexpr int kKinds = 10;
const char *const kKindName[kKinds] = {
    "skin", "skin (no rim)", "hair", "plain + rim", "plain + detail",
    "skin + shadow", "skin (no rim) + shadow", "hair + shadow", "plain + rim + shadow",
    "plain + shadow"};
constexpr int kFirstShadowKind = 5;

bool IsSkin(int kind) { return kind == 0 || kind == 1 || kind == 5 || kind == 6; }
bool IsHair(int kind) { return kind == 2 || kind == 7; }
bool HasRim(int kind) { return kind == 0 || kind == 3 || kind == 5 || kind == 8; }
bool HasNormalMap(int kind) { return !IsHair(kind) && kind != 9; }

// How the self-shadow map is set up for a run of Shade.
enum class ShadowMode {
    AsCase,     // on for kinds 5-9, with each case's taps
    NoCamera,   // the case's taps bound, but colour .w 0: no shadow camera this frame
    AllLit,     // on, every tap lit
};

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
    projMult: vec4f,
    detailSample: vec4f,
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
    px.detailSample = charTestIn.detailSample;
    px.specSample = charTestIn.specSample;
    px.rimSample = vec4f(1.0);
    px.ao = vec4f(1.0, 1.0, 1.0, 0.0);
    px.normalVar = charTestIn.normal.w;
    var proj: Dc3Proj;
    proj.mult = charTestIn.projMult.rgb;
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
        return Shade(retailLighting, kCases, sizeof(kCases) / sizeof(kCases[0]), kNoShadow);
    }
    static constexpr float kNoShadow[3] = {1.0f, 1.0f, 1.0f};
    // RetailPixel.normalVar for every case (specular antialiasing; 0 = off,
    // as retailShade leaves it under the retail look).
    float mNormalVar = 0.0f;

    // `projMult` is the projected light's factor (Dc3Proj.mult) for every case.
    std::vector<float> Shade(float retailLighting, const CharCase *cases, size_t n,
                             const float projMult[3],
                             ShadowMode shadowMode = ShadowMode::AsCase) {
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
            Buffer(10 * 16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
        wgpu::Buffer outBuf =
            Buffer(16, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc);
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
        // The self-shadow map: 2x2, one tap per texel, rewritten per case.
        // depth16unorm because it is the depth format a buffer may be
        // copied into; 0 or 1 against the case's depth 0.5.
        wgpu::TextureDescriptor td{};
        td.label = "RetailCharShadingTest shadow map";
        td.size = {2, 2, 1};
        td.format = wgpu::TextureFormat::Depth16Unorm;
        td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture shadowTex = dev.CreateTexture(&td);
        wgpu::BindGroupEntry shadowEntry{};
        shadowEntry.binding = 1;
        shadowEntry.textureView = shadowTex.CreateView();
        wgpu::BindGroup g0 =
            group(0, {entry(0, sceneBuf, sizeof(SceneUniforms)), shadowEntry});
        wgpu::BindGroup g1 = group(1, {entry(0, matBuf, sizeof(MaterialUniforms))});
        wgpu::BindGroup g2 = group(2, {entry(0, objBuf, sizeof(ObjectUniforms))});
        wgpu::BindGroup g3 = group(3, {entry(1, inBuf, 10 * 16), entry(2, outBuf, 16)});

        for (size_t i = 0; i < n; i++) {
            const CharCase &c = cases[i];
            SceneUniforms s{};
            for (int a = 0; a < 3; a++) s.cameraPos[a] = c.P[a] + c.V[a];
            for (int l = 0; l < 3; l++) {
                Copy4(s.pointLightPos[l], c.lightPos[l]);
                Copy4(s.pointLightColors[l], c.lightCol[l]);
            }
            s.numPointLights = 3.0f;
            s.retailLighting = retailLighting;
            const bool shadowKind = c.kind >= kFirstShadowKind;
            uint16_t taps[4] = {0xffff, 0xffff, 0xffff, 0xffff};
            if (shadowKind) {
                // A constant clip position: the map's texel space t = uv *
                // size - 0.5 lands on (fx, fy), between texel (0,0) and (1,1).
                const float u = (c.shadowW[0] + 0.5f) / 2.0f, v = (c.shadowW[1] + 0.5f) / 2.0f;
                s.selfShadowClip[12] = 2.0f * u - 1.0f;
                s.selfShadowClip[13] = 1.0f - 2.0f * v;
                s.selfShadowClip[14] = 0.5f;
                s.selfShadowClip[15] = 1.0f;
                Copy4(s.selfShadowColor, c.c107);
                s.selfShadowColor[3] = shadowMode == ShadowMode::NoCamera ? 0.0f : 1.0f;
                Copy4(s.selfShadowDir, c.c108);
                if (shadowMode != ShadowMode::AllLit)   // NoCamera: a stale map is still bound
                    for (int t = 0; t < 4; t++) taps[t] = c.shadowLit[t] > 0.5f ? 0xffff : 0;
            }
            wgpu::TexelCopyTextureInfo dst{};
            dst.texture = shadowTex;
            dst.aspect = wgpu::TextureAspect::DepthOnly;
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow = 2 * sizeof(uint16_t);
            layout.rowsPerImage = 2;
            const wgpu::Extent3D extent{2, 2, 1};
            gpu.Queue().WriteTexture(&dst, taps, sizeof(taps), &layout, &extent);
            MaterialUniforms m{};
            Copy4(m.color, c.c0);
            Copy4(m.retailSpec, c.c2);
            if (HasRim(c.kind)) Copy4(m.retailRim, c.c61);
            Copy4(m.specular2Color, c.c19);
            m.deNormal = 1.0f - c.k;
            m.shaderVariation = IsSkin(c.kind) ? 1.0f : IsHair(c.kind) ? 2.0f : 0.0f;
            m.retailFlags[0] = 1.0f;                                // per-pixel
            m.retailFlags[1] = HasNormalMap(c.kind) ? 1.0f : 0.0f;  // normal map
            m.retailFlags[2] = c.kind == 4 ? 0.0f : 1.0f;  // specular map (detail case has none)
            m.retailFlags[3] = 0.0f;                       // no rim map
            if (c.kind == 4) {
                m.hasNormDetailMap = 1.0f;
                m.normDetailStrength = c.c106[0];
                m.normDetailTiling = c.c106[1];
            }
            ObjectUniforms o{};
            for (int f = 0; f < 6; f++) Copy4(o.boxLight[f], c.box[f]);
            float in[10][4] = {};
            Copy3(in[0], c.P, 1.0f);
            Copy3(in[1], c.n, mNormalVar);
            Copy3(in[2], c.t, 0.0f);
            Copy3(in[3], c.b, 0.0f);
            Copy4(in[4], c.normalTex);
            Copy4(in[5], c.specTex);
            Copy4(in[9], c.detailTex);
            for (int a = 0; a < 3; a++) {
                in[6][a] = c.diffTex[a] * c.c0[a];   // texel times material colour
                in[7][a] = c.c1[a];                  // ambient
                in[8][a] = projMult[a];
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
    int bad[kKinds] = {}, total[kKinds] = {};
    float worst[kKinds] = {};
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
    for (int k = 0; k < kKinds; k++) {
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
    int differ[kKinds] = {}, total[kKinds] = {};
    for (size_t i = 0; i < n; i++) {
        const int k = kCases[i].kind;
        total[k]++;
        bool same = true;
        for (int a = 0; a < 3; a++) same = same && Close(rb3[3 * i + a], dc3[3 * i + a]);
        if (!same) differ[k]++;
    }
    // (Kinds 5-9: RB3 has no self-shadow, SelfShadowNeedsItsCamera.)
    // Skin and hair differ everywhere. The plain rim differs only by the
    // back light, which is zero where every light is in front of the pixel.
    for (int k = 0; k < 3; k++)
        EXPECT_EQ(differ[k], total[k]) << kKindName[k] << ": " << total[k] - differ[k]
                                       << " cases shade the same under RB3's form";
    EXPECT_GT(differ[3], 0) << "plain + rim: the back light is not DC3-only";
    // The normal detail is DC3's too: under RB3's form the detail texel is
    // not read, so every detail case shades differently.
    EXPECT_EQ(differ[4], total[4]) << kKindName[4] << ": " << total[4] - differ[4]
                                   << " cases shade the same under RB3's form";
}

// The plain permutation without rim or specular, under a projected_blend 1
// light whose factor is `m` (1 - 0.75 s c69 sat(c65 . N), per channel). The
// microcode's colour is (c1 c0 + (points + box) c0 m) texel, so: m = 0 leaves
// exactly the ambient, and any m scales everything else linearly.
TEST_F(RetailCharShadingTest, ProjectedShadowScalesTheWholeDiffuseLight) {
    std::vector<CharCase> plain;
    for (const CharCase &c : kCases) {
        if (c.kind != 3) continue;
        CharCase p = c;
        for (int a = 0; a < 4; a++) {
            p.c2[a] = 0.0f;    // no specular
            p.c61[a] = 0.0f;   // no rim
        }
        plain.push_back(p);
    }
    ASSERT_GT(plain.size(), 0u);
    const float one[3] = {1.0f, 1.0f, 1.0f}, zero[3] = {0.0f, 0.0f, 0.0f};
    const float m[3] = {0.3f, 0.55f, 0.8f};
    const std::vector<float> lit = Shade(2.0f, plain.data(), plain.size(), one);
    const std::vector<float> dark = Shade(2.0f, plain.data(), plain.size(), zero);
    const std::vector<float> part = Shade(2.0f, plain.data(), plain.size(), m);
    ASSERT_EQ(lit.size(), 3 * plain.size());
    ASSERT_EQ(dark.size(), lit.size());
    ASSERT_EQ(part.size(), lit.size());
    int bad = 0;
    for (size_t i = 0; i < plain.size(); i++) {
        const CharCase &c = plain[i];
        for (int a = 0; a < 3; a++) {
            const float ambient = c.diffTex[a] * c.c0[a] * c.c1[a];
            const float l = lit[3 * i + a], d = dark[3 * i + a], p = part[3 * i + a];
            ASSERT_GT(l - ambient, 1e-3f) << "case " << i << ": the pixel has no diffuse light";
            if (!Close(d, ambient) || !Close(p, d + m[a] * (l - d))) {
                if (bad++ < 3)
                    ADD_FAILURE() << "case " << i << " channel " << a << ": ambient " << ambient
                                  << ", factor 0 gives " << d << ", factor " << m[a] << " gives "
                                  << p << ", unshadowed " << l;
            }
        }
    }
    EXPECT_EQ(bad, 0) << "the projected shadow must scale the point lights with the box";
}

// The self-shadow is on only while the frame has a shadow camera (Rnd_Wgpu
// writes selfShadowColor.w = 1 then) and only in DC3's form of the retail
// model: without the camera, or under RB3's form, a case shades as if every
// tap were lit, even with its map still bound. And the map does something:
// with its taps, the cases it can reach shade differently.
TEST_F(RetailCharShadingTest, SelfShadowNeedsItsCamera) {
    const size_t n = sizeof(kCases) / sizeof(kCases[0]);
    const std::vector<float> asCase = ShadeAll(2.0f);
    const std::vector<float> noCamera = Shade(2.0f, kCases, n, kNoShadow, ShadowMode::NoCamera);
    const std::vector<float> allLit = Shade(2.0f, kCases, n, kNoShadow, ShadowMode::AllLit);
    const std::vector<float> rb3 = Shade(1.0f, kCases, n, kNoShadow, ShadowMode::AsCase);
    const std::vector<float> rb3NoCamera = Shade(1.0f, kCases, n, kNoShadow, ShadowMode::NoCamera);
    for (const auto *v : {&asCase, &noCamera, &allLit, &rb3, &rb3NoCamera})
        ASSERT_EQ(v->size(), 3 * n);
    int total = 0, shadowed = 0, leaks = 0, rb3Leaks = 0;
    for (size_t i = 0; i < n; i++) {
        if (kCases[i].kind < kFirstShadowKind) continue;
        total++;
        bool same = true, differ = false, rb3Same = true;
        for (int a = 0; a < 3; a++) {
            const size_t j = 3 * i + a;
            same = same && Close(noCamera[j], allLit[j]);
            differ = differ || !Close(asCase[j], allLit[j]);
            rb3Same = rb3Same && Close(rb3[j], rb3NoCamera[j]);
        }
        if (!same && leaks++ < 3)
            ADD_FAILURE() << kKindName[kCases[i].kind] << " case " << i
                          << ": shadowed without a shadow camera";
        if (!rb3Same && rb3Leaks++ < 3)
            ADD_FAILURE() << kKindName[kCases[i].kind] << " case " << i
                          << ": shadowed under RB3's form";
        if (differ) shadowed++;
    }
    printf("[ retail char ] self-shadow: %d of %d cases shaded differently by their map\n",
           shadowed, total);
    ASSERT_GT(total, 0);
    EXPECT_EQ(leaks, 0);
    EXPECT_EQ(rb3Leaks, 0);
    // A case changes only where its normal faces the shadow camera, a tap is
    // in shadow and a point light reaches the pixel: 13 of the 60 (RTX 3090).
    EXPECT_GE(shadowed, total / 6) << "the map's taps do not reach the shading";
}

// Specular antialiasing (LookSettings::specularAA, item 18 (c); retailShade
// fills RetailPixel.normalVar from the normal's screen derivatives under the
// enhanced look, specularAAPower widens the exponent). At normalVar 0 every
// case above is still the shipped microcode (PixelShadingIsTheShippedMicrocode
// runs with it zeroed). Here: one point light in front of a surface facing -y
// (z up, so hair's strand direction is x), the view swept over the
// hemisphere, the texel black so only the specular is left. A pixel whose
// normals spread over it (normalVar 0.05: a sphere about 6 px across) gets a
// wider, lower highlight that reflects the same light -- steadier, not dimmer.
TEST_F(RetailCharShadingTest, SpecularAAWidensTheLobeAndKeepsItsLight) {
    struct Kind {
        int kind;
        const char *name;
    };
    const Kind kinds[] = {{4, "plain"}, {1, "skin"}, {2, "hair"}};
    for (const Kind &kd : kinds) {
        std::vector<CharCase> cases;
        std::vector<double> weight;
        CharCase c{};
        c.kind = kd.kind;
        const float n[3] = {0.0f, -1.0f, 0.0f}, t[3] = {1.0f, 0.0f, 0.0f}, b[3] = {0.0f, 0.0f, 1.0f};
        memcpy(c.n, n, sizeof n);
        memcpy(c.t, t, sizeof t);
        memcpy(c.b, b, sizeof b);
        const float neutral[4] = {0.5f, 0.5f, 1.0f, 1.0f}, white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        Copy4(c.normalTex, neutral);
        Copy4(c.detailTex, neutral);
        Copy4(c.specTex, white);
        for (int a = 0; a < 4; a++) {
            c.c0[a] = 1.0f;
            c.c2[a] = 1.0f;
            c.c19[a] = 0.5f;
        }
        c.c2[3] = 64.0f;   // the specular power
        c.k = 1.0f;        // no de_normal
        // 40 degrees off the normal, so the reflected lobe sits where skin's
        // Fresnel factor is not zero (it is at normal incidence).
        c.lightPos[0][1] = -10.0f * std::cos(0.6981317f);
        c.lightPos[0][2] = 10.0f * std::sin(0.6981317f);
        c.lightCol[0][0] = c.lightCol[0][1] = c.lightCol[0][2] = c.lightCol[0][3] = 1.0f;
        constexpr double kPi = 3.14159265358979;
        const double dTh = 1.5 * kPi / 180.0, dPh = 5.0 * kPi / 180.0;
        for (double th = 0.5 * dTh; th < 0.5 * kPi; th += dTh)
            for (double ph = 0.5 * dPh; ph < 2.0 * kPi; ph += dPh) {
                c.V[0] = (float)(std::sin(th) * std::cos(ph));
                c.V[1] = (float)-std::cos(th);
                c.V[2] = (float)(std::sin(th) * std::sin(ph));
                cases.push_back(c);
                weight.push_back(std::sin(th) * dTh * dPh);
            }
        double energy[2] = {}, peak[2] = {};
        for (int on = 0; on < 2; on++) {
            mNormalVar = on ? 0.05f : 0.0f;
            const std::vector<float> got = Shade(2.0f, cases.data(), cases.size(), kNoShadow);
            mNormalVar = 0.0f;
            ASSERT_EQ(got.size(), 3 * cases.size());
            for (size_t i = 0; i < cases.size(); i++) {
                const double l = (got[3 * i] + got[3 * i + 1] + got[3 * i + 2]) / 3.0;
                energy[on] += weight[i] * l;
                peak[on] = std::max(peak[on], l);
            }
        }
        printf("[ retail char ] specular AA, %-5s p 64, normalVar 0.05: light %.4f -> %.4f (x%.3f), "
               "peak %.3f -> %.3f (x%.3f)\n",
               kd.name, energy[0], energy[1], energy[1] / energy[0], peak[0], peak[1],
               peak[1] / peak[0]);
        EXPECT_GT(energy[0], 0.01) << kd.name << ": the sweep reaches the highlight (non-vacuous)";
        // Measured on an RTX 3090: light x1.030 / x1.052 / x0.989 (plain,
        // skin, hair), peak x0.35 / x0.36 / x0.60. Sabotages: the exponent
        // left as it was (every peak stays); hair's band lobe not kept
        // (bandLobe = lobe: hair's light x0.60).
        EXPECT_GT(energy[1] / energy[0], 0.95) << kd.name << ": the highlight keeps its light";
        EXPECT_LT(energy[1] / energy[0], 1.1) << kd.name << ": and adds none";
        EXPECT_LT(peak[1], 0.75 * peak[0]) << kd.name << ": and spreads it";
    }
}

} // namespace
