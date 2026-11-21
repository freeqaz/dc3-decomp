// test_look_text_sampling.cpp -- np-look34 (item 34b): the enhanced look's
// text and HUD sampling rule, two numbers: a LOD bias for text and HUD-overlay
// (uiClass) draws (LookSettings::uiLodBias) and the level clamp that already
// existed (uiMipLevels). Retail is the original: unbiased text read from its
// two levels, and Native (rb3, rb3-xenon) samples as it always has. The bias is
// a uniform (SceneUniforms::uiLodBias / uiArtLodBias); a pipeline constant
// (kTakesUiBias, PipelineKey::uiBias) only says whether a draw reads it, so an
// inspection token moves it on a held frame and compiles nothing. At render
// scale above 1 text samples with textureSampleGrad at output-pixel centres,
// which takes no bias, so the gradients carry it (x 2^bias).
//
// Sabotages run once each and restored (logs in the item 34b evidence folder,
// archive/np-gfx-2026-11-14/lanes/sharp/item34b/sabotage/).

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineCount.h"
#include "gfx/PipelineManager.h"
#include "gfx/ShippedWgsl.h"
#include "platform/LookProfile.h"
#include "platform/MeshPipelineKey.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Rnd.h"

#include <cstring>
#include <string>
#include <vector>

// Declared here, not via MaterialSetup.h (dc3's pre-engine copy shadows the
// engine's on the include path; see test_look_sampling.cpp).
void ApplyLookSampling(SamplerDesc &desc, bool uiClass, const milo::LookSettings &look);
uint8_t MaterialDrawUiBias(bool uiClass, bool glyphPage, bool lookTakesUiLodBias);
bool MaterialDrawTakesLodBias(bool uiClass, bool lookTakesLodBias);

using milo::Look;
using milo::LookProfile;
using milo::ScopedLookOverride;

namespace {

class LookTextSamplingTest : public EngineTestFixture {
protected:
    bool HaveGpu() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

// One WGSL item of the shipped standard shader, verbatim: "override NAME..."
// to its ';', or "fn NAME(" to its closing brace.
std::string ShippedItem(const std::string &src, const std::string &head) {
    const size_t at = src.find(head);
    if (at == std::string::npos) return std::string();
    if (head.rfind("override", 0) == 0) return src.substr(at, src.find(';', at) - at + 1) + "\n";
    size_t i = src.find('{', at);
    int depth = 0;
    for (; i < src.size(); i++) {
        if (src[i] == '{') depth++;
        else if (src[i] == '}' && --depth == 0) break;
    }
    return src.substr(at, i - at + 1) + "\n";
}

} // namespace

// The rule's two numbers per profile. Sabotage: Retail given uiTakesLodBias,
// or Enhanced's uiTakesLodBias dropped -- red.
TEST_F(LookTextSamplingTest, EnhancedHasATextRuleRetailAndNativeHaveNone) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_FALSE(milo::LookTakesUiLodBias(Look())) << "the original's text samples are unbiased";
        EXPECT_EQ(milo::UiGlyphLodBias(Look()), 0.0f);
        EXPECT_EQ(milo::UiArtLodBias(Look()), 0.0f);
        EXPECT_EQ(Look().uiMipLevels, 0) << "retail text reads what it uploaded (two levels)";
    }
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        EXPECT_TRUE(milo::LookTakesUiLodBias(Look())) << "text pipelines read the bias, so tokens move it";
        EXPECT_EQ(Look().uiMipLevels, 2) << "item 34b: the clamp ships unchanged";
        EXPECT_EQ(milo::UiGlyphLodBias(Look()), Look().uiLodBias);
        EXPECT_TRUE(Look().uiLodBiasGlyphsOnly) << "item 34b: HUD art keeps unbiased samples";
        EXPECT_EQ(milo::UiArtLodBias(Look()), 0.0f);
    }
    // A Native-shaped settings block (the default-constructed one): no rule.
    milo::LookSettings native;
    EXPECT_FALSE(milo::LookTakesUiLodBias(native));
    EXPECT_EQ(milo::UiGlyphLodBias(native), 0.0f);
    EXPECT_EQ(milo::UiArtLodBias(native), 0.0f);
}

// Which draws read which bias: a glyph page the text bias, other uiClass art
// the art bias, a world draw neither (it keeps the world bias), and nothing
// under a look without a text rule. Sabotage: MaterialDrawUiBias ignoring
// uiClass, or the glyph/art classes swapped -- red.
TEST_F(LookTextSamplingTest, TextDrawsTakeTheTextBiasWorldDrawsDoNot) {
    if (IsSkipped()) return;
    EXPECT_EQ(MaterialDrawUiBias(true, true, true), 1) << "a glyph page reads scene.uiLodBias";
    EXPECT_EQ(MaterialDrawUiBias(true, false, true), 2) << "HUD art reads scene.uiArtLodBias";
    EXPECT_EQ(MaterialDrawUiBias(false, false, true), 0) << "a world draw never takes the text bias";
    EXPECT_EQ(MaterialDrawUiBias(true, true, false), 0) << "retail / Native: no text bias";
    EXPECT_EQ(MaterialDrawUiBias(true, false, false), 0);
    EXPECT_FALSE(MaterialDrawTakesLodBias(true, true)) << "and text still never takes the world bias";
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_EQ(MaterialDrawUiBias(true, true, milo::LookTakesUiLodBias(Look())), 0);
    }
    PipelineKey glyph{}, art{}, none{};
    glyph.uiBias = 1;
    art.uiBias = 2;
    EXPECT_FALSE(glyph == none);
    EXPECT_FALSE(glyph == art);
    EXPECT_NE(PipelineKeyHash{}(glyph), PipelineKeyHash{}(none));
    EXPECT_NE(PipelineKeyHash{}(glyph), PipelineKeyHash{}(art));
}

// The tokens move the values, and the _rule tokens give the look's back; the
// scene block carries what is in force. Sabotage: text_bias_rule restoring 0
// instead of the look's value, or text_levels_all leaving the clamp -- red.
TEST_F(LookTextSamplingTest, TheTokensMoveTheRuleAndTheRuleTokensRestoreIt) {
    if (IsSkipped()) return;
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    const float lookBias = Look().uiLodBias;
    ASSERT_TRUE(milo::SetLookUiLodBiasForInspection(false, -0.5f));
    EXPECT_FLOAT_EQ(milo::UiGlyphLodBias(Look()), -0.5f);
    EXPECT_FLOAT_EQ(milo::UiArtLodBias(Look()), 0.0f) << "glyphs only by default";
    ASSERT_TRUE(milo::SetLookUiLodBiasScopeForInspection(0));
    EXPECT_FLOAT_EQ(milo::UiArtLodBias(Look()), -0.5f) << "text_bias_all_ui";
    ASSERT_TRUE(milo::SetLookUiLodBiasScopeForInspection(-1));
    EXPECT_TRUE(Look().uiLodBiasGlyphsOnly) << "text_bias_scope_rule";
    ASSERT_TRUE(milo::SetLookUiLodBiasForInspection(true, 0.0f));
    EXPECT_FLOAT_EQ(Look().uiLodBias, lookBias) << "text_bias_rule gives the look's value back";

    SamplerDesc ui;
    ASSERT_TRUE(milo::SetLookUiMipLevelsForInspection(0));
    ApplyLookSampling(ui, true, Look());
    EXPECT_EQ(ui.lodMaxClamp, SamplerDesc{}.lodMaxClamp) << "text_levels_all: no clamp";
    ASSERT_TRUE(milo::SetLookUiMipLevelsForInspection(-1));
    SamplerDesc rule;
    ApplyLookSampling(rule, true, Look());
    EXPECT_EQ(rule.lodMaxClamp, 1.0f) << "text_levels_rule: the look's two levels";

    float written = 99.0f, writtenArt = 99.0f, writtenRule = 99.0f;
    if (HaveGpu()) {
        RndCam *prev = RndCam::Current();
        RndCam *cam = Hmx::Object::New<RndCam>();
        cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
        TheRnd.BeginDrawing();
        cam->Select();
        milo::SetLookUiLodBiasForInspection(false, -0.5f);
        milo::SetLookUiLodBiasScopeForInspection(0);
        gWgpuRnd->EnsureSceneUniformsCurrent();
        written = gWgpuRnd->LastSceneUiLodBias();
        writtenArt = gWgpuRnd->LastSceneUiArtLodBias();
        milo::SetLookUiLodBiasForInspection(true, 0.0f);
        milo::SetLookUiLodBiasScopeForInspection(-1);
        gWgpuRnd->EnsureSceneUniformsCurrent();
        writtenRule = gWgpuRnd->LastSceneUiLodBias();
        if (prev) prev->Select(); else RndCam::ClearCurrent();
        TheRnd.EndDrawing();
        delete cam;
    }
    milo::SetLookUiLodBiasForInspection(true, 0.0f);
    milo::SetLookUiLodBiasScopeForInspection(-1);
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (the pure rule above ran)";
    EXPECT_FLOAT_EQ(written, -0.5f) << "the inspected text bias reached the scene block";
    EXPECT_FLOAT_EQ(writtenArt, -0.5f) << "and the art bias, scope all_ui";
    EXPECT_FLOAT_EQ(writtenRule, lookBias) << "and the rule's, on the next write";
}

// A token switch compiles no pipeline: the key a text material draws with does
// not depend on the bias's value or the clamp. Sabotage: MaterialPipelineKey
// keying uiBias on the value (uiLodBias != 0) -- red.
TEST_F(LookTextSamplingTest, ATokenSwitchCompilesNoPipeline) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndMat *mat = Hmx::Object::New<RndMat>();
    MeshPassContext ctx;
    ctx.targetFormat = gWgpuRnd->Gpu().SurfaceFormat();
    ctx.hasDepth = false;
    PipelineManager &pm = gWgpuRnd->Pipelines();
    const PipelineKey before = MaterialPipelineKey(mat, false, true, true, 0, ctx);
    EXPECT_EQ(before.uiBias, pm.UiTakesLodBias() ? 1 : 0);
    ASSERT_TRUE(pm.GetPipeline(before));
    const uint64_t creates = GpuPipelineCreateCount();
    for (int i = 0; i < 4; i++) {
        milo::SetLookUiLodBiasForInspection(false, i == 0 ? 0.0f : i == 1 ? -0.5f : -1.0f);
        milo::SetLookUiMipLevelsForInspection(i == 3 ? 0 : 2);
        const PipelineKey after = MaterialPipelineKey(mat, false, true, true, 0, ctx);
        EXPECT_TRUE(after == before) << "token step " << i << " changed the text key";
        EXPECT_TRUE(pm.GetPipeline(after));
    }
    EXPECT_EQ(GpuPipelineCreateCount(), creates) << "a token switch compiled a pipeline";
    milo::SetLookUiLodBiasForInspection(true, 0.0f);
    milo::SetLookUiMipLevelsForInspection(-1);
    delete mat;
}

// The shipped sampling code, on the GPU: the standard shader's own
// diffuseSample, uiLodBias and materialLodBias (cut verbatim from
// StandardWgslSource()) draw a 32x32 texture whose level 0 is red, level 1
// green, level 2 blue, at a minification that reads level 1 unbiased. Each case
// reads which level arrived. At kOutputPixelScale 2 (the render-scaled path,
// textureSampleGrad) the same bias must arrive through the gradients.
// Sabotage: the grad path's exp2(uiLodBias()) dropped, or uiLodBias() reading
// uiArtLodBias for class 1 -- red.
TEST_F(LookTextSamplingTest, TheShippedTextSampleReadsTheBiasedLevel) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    wgpu::Device dev = gpu.Device();
    const std::string src = StandardWgslSource();
    std::string items;
    for (const char *h : {"override kTakesLodBias", "override kOutputPixelScale", "override kTakesUiBias",
                          "fn materialLodBias(", "fn uiLodBias(", "fn diffuseSample("}) {
        const std::string it = ShippedItem(src, h);
        ASSERT_FALSE(it.empty()) << "the shipped shader has no " << h;
        items += it;
    }
    const std::string wgsl = std::string(R"(
struct Scene { materialLodBias: f32, uiLodBias: f32, uiArtLodBias: f32, uvPerPixel: f32 };
@group(0) @binding(0) var<uniform> scene: Scene;
@group(0) @binding(1) var diffuseTex: texture_2d<f32>;
@group(0) @binding(2) var diffuseSampler: sampler;
)") + items + R"(
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((i << 1u) & 2u), f32(i & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}
@fragment fn fs(@builtin(position) pos: vec4f) -> @location(0) vec4f {
    return diffuseSample(pos.xy * scene.uvPerPixel, pos.xy);
}
)";
    wgpu::ShaderSourceWGSL w{};
    w.code = {wgsl.c_str(), wgsl.size()};
    wgpu::ShaderModuleDescriptor smd{};
    smd.nextInChain = &w;
    wgpu::ShaderModule mod = dev.CreateShaderModule(&smd);

    // 32x32, three levels: red, green, blue.
    wgpu::TextureDescriptor td{};
    td.size = {32, 32, 1};
    td.mipLevelCount = 3;
    td.format = wgpu::TextureFormat::RGBA8Unorm;
    td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture tex = dev.CreateTexture(&td);
    for (uint32_t l = 0; l < 3; l++) {
        const uint32_t s = 32 >> l;
        std::vector<uint8_t> px(s * s * 4, 0);
        for (uint32_t i = 0; i < s * s; i++) { px[i * 4 + l] = 255; px[i * 4 + 3] = 255; }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = tex;
        dst.mipLevel = l;
        wgpu::TexelCopyBufferLayout lay{};
        lay.bytesPerRow = s * 4;
        wgpu::Extent3D ext = {s, s, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &lay, &ext);
    }
    wgpu::TextureDescriptor od{};
    od.size = {16, 16, 1};
    od.format = wgpu::TextureFormat::RGBA8Unorm;
    od.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture out = dev.CreateTexture(&od);
    wgpu::BufferDescriptor bd{};
    bd.size = 16;
    bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer ubo = dev.CreateBuffer(&bd);

    struct Case {
        const char *what;
        double takesUi, opx;
        float ui, art, uvPerPixel, lodMax;
        int level;   // the level that must arrive (0 red, 1 green, 2 blue)
    };
    // uvPerPixel 2/32: 2 texels per pixel, LOD 1. 1/32: LOD 0, but at opx 2
    // the gradients double (one output pixel = 2 frame pixels): LOD 1 again.
    const Case cases[] = {
        {"glyph, bias 0", 1, 1, 0.0f, 0.0f, 2.0f / 32, 16, 1},
        {"glyph, bias -1", 1, 1, -1.0f, 0.0f, 2.0f / 32, 16, 0},
        {"world draw, text bias -1 not taken", 0, 1, -1.0f, -1.0f, 2.0f / 32, 16, 1},
        {"HUD art reads the art bias", 2, 1, 0.0f, -1.0f, 2.0f / 32, 16, 0},
        {"HUD art ignores the glyph bias", 2, 1, -1.0f, 0.0f, 2.0f / 32, 16, 1},
        {"glyph, scaled path, bias 0", 1, 2, 0.0f, 0.0f, 1.0f / 32, 16, 1},
        {"glyph, scaled path, bias -1", 1, 2, -1.0f, 0.0f, 1.0f / 32, 16, 0},
        {"glyph, LOD 2, two-level clamp", 1, 1, 0.0f, 0.0f, 4.0f / 32, 1, 1},
        {"glyph, LOD 2, no clamp (text_levels_all)", 1, 1, 0.0f, 0.0f, 4.0f / 32, 16, 2},
    };
    for (const Case &c : cases) {
        wgpu::ConstantEntry k[3];
        k[0].key = "kTakesLodBias"; k[0].value = 0.0;
        k[1].key = "kOutputPixelScale"; k[1].value = c.opx;
        k[2].key = "kTakesUiBias"; k[2].value = c.takesUi;
        wgpu::ColorTargetState ct{};
        ct.format = wgpu::TextureFormat::RGBA8Unorm;
        wgpu::FragmentState fs{};
        fs.module = mod;
        fs.entryPoint = "fs";
        fs.targetCount = 1;
        fs.targets = &ct;
        fs.constantCount = 3;
        fs.constants = k;
        wgpu::RenderPipelineDescriptor pd{};
        pd.vertex.module = mod;
        pd.vertex.entryPoint = "vs";
        pd.fragment = &fs;
        dev.PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipe = dev.CreateRenderPipeline(&pd);
        std::string err;
        wgpu::Future f = dev.PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&err](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) err.assign(msg.data, msg.length);
            });
        gpu.Instance().WaitAny(f, UINT64_MAX);
        ASSERT_TRUE(err.empty()) << c.what << ": " << err;

        SamplerDesc sd;
        sd.lodMaxClamp = c.lodMax;
        sd.addressU = sd.addressV = wgpu::AddressMode::Repeat;
        wgpu::SamplerDescriptor wsd = sd.ToWgpu();
        wgpu::Sampler smp = dev.CreateSampler(&wsd);
        const float u[4] = {0.0f, c.ui, c.art, c.uvPerPixel};
        gpu.Queue().WriteBuffer(ubo, 0, u, sizeof(u));
        wgpu::BindGroupEntry be[3] = {};
        be[0].binding = 0; be[0].buffer = ubo; be[0].size = 16;
        be[1].binding = 1; be[1].textureView = tex.CreateView();
        be[2].binding = 2; be[2].sampler = smp;
        wgpu::BindGroupDescriptor bgd{};
        bgd.layout = pipe.GetBindGroupLayout(0);
        bgd.entryCount = 3;
        bgd.entries = be;
        wgpu::BindGroup bg = dev.CreateBindGroup(&bgd);

        wgpu::RenderPassColorAttachment ca{};
        ca.view = out.CreateView();
        ca.loadOp = wgpu::LoadOp::Clear;
        ca.storeOp = wgpu::StoreOp::Store;
        ca.clearValue = {0, 0, 0, 0};
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
        pass.SetPipeline(pipe);
        pass.SetBindGroup(0, bg);
        pass.Draw(3);
        pass.End();
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        std::vector<uint8_t> got(16 * 16 * 4);
        ASSERT_TRUE(gpu.ReadbackTexture(out, 16, 16, got.data(), got.size())) << c.what;
        const uint8_t *p = &got[(8 * 16 + 8) * 4];
        for (int ch = 0; ch < 3; ch++)
            EXPECT_NEAR(p[ch], ch == c.level ? 255 : 0, 8)
                << c.what << ": channel " << ch << " (want level " << c.level << "), got "
                << (int)p[0] << "," << (int)p[1] << "," << (int)p[2];
    }
}
