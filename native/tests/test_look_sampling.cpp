// test_look_sampling.cpp -- the DC3_LOOK texture-sampling and supersampling
// settings (engine platform/LookProfile.h), each against the original.
//
// Retail is the original hardware: DxTex::SyncBitmap creates a regular texture
// with min(NumMips()+1, 2) levels (test_bitmap_mip_levels.cpp), its fetch
// constants carry no anisotropy and LOD bias 0, DxCubeTex::Sync creates a cube
// with its whole chain (numMips = mNumMips + 1), and the world draws one
// sample per pixel. Enhanced (the default) uploads whole chains filtered 16x
// anisotropically with a -1 LOD bias on world textures, keeps text and HUD at
// the original's two levels and unbiased, and draws 2x2 supersampled on a
// discrete GPU when it presents to a window (not on an integrated one, not
// headless unless MILO_RENDER_SCALE asks), text and HUD still sampled once per
// output pixel.

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/PipelineManager.h"
#include "gfx/PresentScale.h"
#include "platform/LookProfile.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/CubeTex.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"

#include <algorithm>
#include <string>
#include <vector>

// Declared here, not via the engine's MaterialSetup.h alone: dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy, compiled into no
// engine-linked target) is earlier on the include path and shadows it; its
// MaterialParams is the engine's. Same convention as
// test_native_ui_material_terms.cpp.
struct UiDrawTerms {
    bool gammaShading = false;
    bool retailMaterialTerms = false;
};
UiDrawTerms UiDrawTermsFor(bool frameInOverlay);
MaterialParams BuildMaterialParams(RndMat *mat, bool isTextMesh, const UiDrawTerms &uiTerms);
void ApplyLookSampling(SamplerDesc &desc, bool uiClass, const milo::LookSettings &look);
bool MaterialDrawTakesLodBias(bool uiClass, float materialLodBias);
bool MaterialDrawSamplesAtOutputPixels(bool uiClass, bool toTextureTarget, float outputPixelScale);

using milo::Look;
using milo::LookProfile;
using milo::ScopedLookOverride;

namespace {

void FillBitmap(RndBitmap &bmp, int size, int seed) {
    std::vector<unsigned char> px(size * size * 4);
    for (int i = 0; i < size * size; i++) {
        px[i * 4 + 0] = (unsigned char)(i * 7 + seed);
        px[i * 4 + 1] = (unsigned char)(i * 13 + seed);
        px[i * 4 + 2] = (unsigned char)(i * 29 + seed);
        px[i * 4 + 3] = 0xFF;
    }
    bmp.Create(size, size, 0, 32, 0, nullptr, px.data(), nullptr);
    bmp.GenerateMips();
}

class LookSamplingTest : public EngineTestFixture {
protected:
    bool HaveGpu() const { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

} // namespace

// The Retail profile's numbers are the original's, field by field; Enhanced
// departs only where it says it does. Sabotage: Retail given anisotropy 16,
// or Enhanced given bitmapMipLevels 2 -- red.
TEST_F(LookSamplingTest, RetailIsTheOriginalAndEnhancedDepartsAsDocumented) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_EQ(Look().bitmapMipLevels, 2) << "DxTex::SyncBitmap: min(NumMips()+1, 2)";
        EXPECT_EQ(Look().maxAnisotropy, 1) << "fetch constant aniso_filter 0";
        EXPECT_EQ(Look().lodBias, 0.0f) << "fetch constant lod_bias 0";
        EXPECT_EQ(Look().uiMipLevels, 0) << "text gets the regular rule, no extra clamp";
        EXPECT_TRUE(Look().cubeMipChain) << "DxCubeTex::Sync: numMips = mNumMips + 1";
        EXPECT_EQ(Look().renderScale, 1.0f) << "one sample per pixel";
        EXPECT_EQ(Look().renderScaleIntegrated, 1.0f);
    }
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        EXPECT_EQ(Look().bitmapMipLevels, 0) << "whole authored chain";
        EXPECT_EQ(Look().maxAnisotropy, 16);
        EXPECT_EQ(Look().uiMipLevels, 2) << "text/HUD keep the original's two levels";
        EXPECT_TRUE(Look().cubeMipChain);
        EXPECT_EQ(Look().lodBias, -1.0f) << "world one level sharper (text/HUD exempt)";
        EXPECT_EQ(Look().renderScale, 2.0f) << "2x2 supersampling on a discrete GPU (windowed)";
        // A handheld's iGPU spends ~2 ms on the 1x main pass; a render scale
        // multiplies that by its square, so it stays off there.
        EXPECT_EQ(Look().renderScaleIntegrated, 1.0f);
    }
}

// Per texture class: world draws take the anisotropy, text/HUD draws the mip
// clamp, and Retail touches neither. Sabotage: ApplyLookSampling's class test
// inverted, or the clamp written as uiMipLevels instead of uiMipLevels - 1.
TEST_F(LookSamplingTest, WorldDrawsFilterAnisotropicallyTextKeepsTwoLevels) {
    if (IsSkipped()) return;
    {
        ScopedLookOverride enhanced(LookProfile::Enhanced);
        SamplerDesc world, ui;
        ApplyLookSampling(world, false, Look());
        ApplyLookSampling(ui, true, Look());
        EXPECT_EQ(world.maxAnisotropy, 16);
        EXPECT_EQ(world.lodMaxClamp, 16.0f) << "world samples the whole chain";
        EXPECT_EQ(ui.maxAnisotropy, 1);
        EXPECT_EQ(ui.lodMaxClamp, 1.0f) << "base + one mip, as the original's two-level texture";
    }
    {
        ScopedLookOverride retail(LookProfile::Retail);
        SamplerDesc world, ui;
        ApplyLookSampling(world, false, Look());
        ApplyLookSampling(ui, true, Look());
        EXPECT_EQ(world.maxAnisotropy, 1);
        EXPECT_EQ(ui.maxAnisotropy, 1);
        EXPECT_EQ(world.lodMaxClamp, 16.0f);
        EXPECT_EQ(ui.lodMaxClamp, 16.0f);
    }
    // WebGPU allows anisotropy only with every filter linear.
    SamplerDesc d;
    d.maxAnisotropy = 16;
    EXPECT_EQ(d.ToWgpu().maxAnisotropy, 16);
    d.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    EXPECT_EQ(d.ToWgpu().maxAnisotropy, 1);
}

// The look's LOD bias reaches world pipelines only: a text or HUD-overlay
// draw keys an unbiased pipeline whatever the bias, as the original's samples
// (lod_bias 0), and no bias set means no draw keys a biased one. Sabotage:
// MaterialDrawTakesLodBias ignoring uiClass, or LodBiasFor ignoring the key --
// red.
TEST_F(LookSamplingTest, TheLodBiasIsForWorldDrawsOnly) {
    if (IsSkipped()) return;
    const float bias = -1.0f;
    EXPECT_TRUE(MaterialDrawTakesLodBias(false, bias));
    EXPECT_FALSE(MaterialDrawTakesLodBias(true, bias)) << "text/HUD stay unbiased";
    EXPECT_FALSE(MaterialDrawTakesLodBias(false, 0.0f));
    EXPECT_FALSE(MaterialDrawTakesLodBias(true, 0.0f));

    PipelineManager pm;
    pm.SetMaterialLodBias(bias);
    PipelineKey world{}, text{};
    world.lodBias = MaterialDrawTakesLodBias(false, pm.MaterialLodBias());
    text.lodBias = MaterialDrawTakesLodBias(true, pm.MaterialLodBias());
    EXPECT_EQ(pm.LodBiasFor(world), bias);
    EXPECT_EQ(pm.LodBiasFor(text), 0.0f);
    EXPECT_FALSE(world == text) << "the two must be distinct cached pipelines";
    EXPECT_NE(PipelineKeyHash{}(world), PipelineKeyHash{}(text));

    {
        ScopedLookOverride retail(LookProfile::Retail);
        EXPECT_FALSE(MaterialDrawTakesLodBias(false, Look().lodBias));
    }
}

// A render-scaled frame keeps text and HUD art as an unscaled frame draws it:
// their draws into the frame sample at output-pixel centres, world draws and
// draws into texture targets (drawn at the target's own size) do not, and an
// unscaled frame -- the original's -- never does. Both standard pipelines
// must build. Sabotage: the rule ignoring uiClass, or OutputPixelScaleFor
// ignoring the key -- red.
TEST_F(LookSamplingTest, ARenderScaledFrameSamplesTextAtOutputPixels) {
    if (IsSkipped()) return;
    EXPECT_TRUE(MaterialDrawSamplesAtOutputPixels(true, false, 2.0f));
    EXPECT_FALSE(MaterialDrawSamplesAtOutputPixels(false, false, 2.0f)) << "world draws keep the samples";
    EXPECT_FALSE(MaterialDrawSamplesAtOutputPixels(true, true, 2.0f)) << "texture targets are not scaled";
    EXPECT_FALSE(MaterialDrawSamplesAtOutputPixels(true, false, 1.0f)) << "unscaled: the original's samples";

    PipelineManager pm;
    pm.SetOutputPixelScale(2.0f);
    PipelineKey world{}, text{};
    text.outputPixelSampling = MaterialDrawSamplesAtOutputPixels(true, false, pm.OutputPixelScale());
    EXPECT_EQ(pm.OutputPixelScaleFor(text), 2.0f);
    EXPECT_EQ(pm.OutputPixelScaleFor(world), 1.0f);
    EXPECT_FALSE(world == text);

    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    PipelineManager &live = gWgpuRnd->Pipelines();
    const float prevScale = live.OutputPixelScale();
    live.SetOutputPixelScale(2.0f);
    for (int sampling = 0; sampling < 2; sampling++) {
        PipelineKey key{};
        key.targetFormat = gpu.SurfaceFormat();
        key.sampleCount = 1;
        key.hasDepth = false;
        key.outputPixelSampling = sampling != 0;
        gpu.Device().PushErrorScope(wgpu::ErrorFilter::Validation);
        wgpu::RenderPipeline pipe = live.GetPipeline(key);
        std::string err;
        wgpu::Future f = gpu.Device().PopErrorScope(
            wgpu::CallbackMode::WaitAnyOnly,
            [&err](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
                if (type != wgpu::ErrorType::NoError) err.assign(msg.data, msg.length);
            });
        gpu.Instance().WaitAny(f, UINT64_MAX);
        EXPECT_TRUE(pipe) << "outputPixelSampling " << sampling;
        EXPECT_TRUE(err.empty()) << "outputPixelSampling " << sampling << ": " << err;
    }
    live.SetOutputPixelScale(prevScale);
}

// Through the material path: a text mesh drawn under Enhanced clamps its
// sampler to mip 1.
TEST_F(LookSamplingTest, ATextMaterialClampsItsSamplerToMipOne) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndMat *mat = Hmx::Object::New<RndMat>();
    MaterialParams p = BuildMaterialParams(mat, true, UiDrawTermsFor(false));
    EXPECT_EQ(p.samplerDesc.lodMaxClamp, 1.0f);
    EXPECT_EQ(p.mapSamplerDesc.lodMaxClamp, 1.0f);
    EXPECT_EQ(p.samplerDesc.maxAnisotropy, 1);
    delete mat;
}

// Regular textures: Enhanced uploads the whole authored chain (Retail's cap
// is test_bitmap_mip_levels.cpp). Sabotage: Enhanced bitmapMipLevels 2 -- red.
TEST_F(LookSamplingTest, EnhancedUploadsTheWholeAuthoredChain) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    RndBitmap bmp;
    FillBitmap(bmp, 256, 0);
    const int authored = bmp.NumMips();
    ASSERT_GE(authored, 2);
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
    tex->PresyncBitmap();
    GpuTexDebugInfo info = GetGpuTexDebugInfo(tex);
    ASSERT_TRUE(info.uploaded);
    EXPECT_EQ(info.mipLevels, authored + 1);
    delete tex;
}

// Cube maps: both DC3 profiles create every level the faces carry, as
// DxCubeTex::Sync does (numMips = mNumMips + 1); native used to upload the
// base only. Sabotage: cubeMipChain false, or CreateCubeFromBitmaps ignoring
// it -- 1 level, red.
TEST_F(LookSamplingTest, CubeMapsUploadTheirWholeChain) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    for (LookProfile prof : {LookProfile::Retail, LookProfile::Enhanced}) {
        ScopedLookOverride look(prof);
        RndCubeTex *cube = Hmx::Object::New<RndCubeTex>();
        int authored = -1;
        for (int f = 0; f < 6; f++) {
            FillBitmap(cube->GetBitmap((RndCubeTex::CubeFace)f), 64, f * 31);
            authored = cube->GetBitmap((RndCubeTex::CubeFace)f).NumMips();
        }
        ASSERT_GE(authored, 1);
        ASSERT_TRUE(GetGpuCubeTexView(cube)) << "cube did not upload";
        EXPECT_EQ(GetGpuCubeTexMipLevels(cube), authored + 1)
            << milo::LookProfileName(prof) << ": DxCubeTex::Sync creates mNumMips + 1 levels";
        delete cube;
    }
}

// Render scale: the box filter a scaled frame leaves the renderer through
// averages each output pixel's whole footprint. An 8x8 frame of 4x4 blocks,
// each a bright ring around a dark 2x2 centre, read at 2x2: the box gives the
// block mean (12 bright of 16); one bilinear tap would see only the dark
// centre. Sabotage: SetBoxFilter(true) not honoured -- red.
TEST_F(LookSamplingTest, RenderScaleBoxFiltersTheWholeFootprint) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    GpuDevice &gpu = gWgpuRnd->Gpu();
    wgpu::Device dev = gpu.Device();
    wgpu::TextureDescriptor td{};
    td.size = {8, 8, 1};
    td.format = wgpu::TextureFormat::RGBA8Unorm;
    td.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    wgpu::Texture frame = dev.CreateTexture(&td);
    std::vector<uint8_t> px(8 * 8 * 4);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const int bx = x % 4, by = y % 4;
            const bool centre = (bx == 1 || bx == 2) && (by == 1 || by == 2);
            uint8_t *p = &px[(y * 8 + x) * 4];
            p[0] = p[1] = p[2] = centre ? 0 : 255;
            p[3] = 255;
        }
    wgpu::TexelCopyTextureInfo dst{};
    dst.texture = frame;
    wgpu::TexelCopyBufferLayout layout{};
    layout.bytesPerRow = 8 * 4;
    wgpu::Extent3D ext = {8, 8, 1};
    gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);

    td.size = {2, 2, 1};
    td.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    wgpu::Texture out = dev.CreateTexture(&td);
    PresentScale scale;
    scale.SetBoxFilter(true);
    wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
    scale.Encode(dev, enc, frame, out.CreateView(), wgpu::TextureFormat::RGBA8Unorm, 2, 2);
    wgpu::CommandBuffer cmd = enc.Finish();
    gpu.Queue().Submit(1, &cmd);
    std::vector<uint8_t> got(2 * 2 * 4);
    ASSERT_TRUE(gpu.ReadbackTexture(out, 2, 2, got.data(), got.size()));
    for (int i = 0; i < 4; i++)
        EXPECT_NEAR(got[i * 4], 255 * 12 / 16, 3) << "output pixel " << i
            << ": not the mean of its 4x4 footprint";
}

// The render scale rule (ResolveRenderScale, gfx/GpuDevice.h), as a table.
// The DEFAULT is the adapter class's (renderScale discrete, renderScaleIntegrated
// otherwise), applies only to a frame presented to a window -- a headless run
// (gates, flow tests, readback-only captures) draws 1x -- and is capped so the
// drawn frame fits 3840x2160, rounded down to a whole scale. An EXPLICIT scale
// (MILO_RENDER_SCALE) is clamped to [1, 2] and wins everywhere, cap included.
// Sabotages, each red here: the headless rule removed; the adapter classes
// swapped; the cap removed; the cap applied to an explicit scale; the
// whole-number rounding removed; the explicit clamp removed.
TEST(LookRenderScale, TheRuleAsATable) {
    struct Row {
        float discrete, integrated, asked;
        bool isDiscrete, windowed;
        int w, h;
        float want;
        const char *why;
    };
    const Row rows[] = {
        {2, 1, 0, true, false, 1280, 720, 1, "headless: the default does not apply"},
        {2, 2, 0, false, false, 1280, 720, 1, "headless on any adapter"},
        {2, 1, 0, true, true, 1280, 720, 2, "windowed 720p frame, discrete: 2560x1440"},
        {2, 1, 0, false, true, 1280, 720, 1, "windowed, integrated: renderScaleIntegrated"},
        {1, 2, 0, false, true, 1280, 720, 2, "the integrated default is the integrated one"},
        {2, 1, 0, true, true, 1920, 1080, 2, "1080p frame: 3840x2160, at the cap"},
        {2, 1, 0, true, true, 2560, 1440, 1, "1440p frame: the cap is 1.5, whole scales only"},
        {2, 1, 0, true, true, 3840, 2160, 1, "4K frame: no supersampling past 3840x2160"},
        {2, 1, 0, true, true, 2560, 1080, 1, "ultrawide 2560x1080: the width caps it (1.5)"},
        {3, 1, 0, true, true, 640, 360, 2, "a default above 2 clamps to 2"},
        {1, 1, 0, true, true, 1280, 720, 1, "retail / native: off"},
        {2, 1, 1.5f, true, false, 1280, 720, 1.5f, "asked for: headless too, fractional kept"},
        {1, 1, 2, false, false, 1280, 720, 2, "asked for: any adapter, any profile default"},
        {2, 1, 2, true, true, 3840, 2160, 2, "asked for: may exceed the cap, up to 2"},
        {2, 1, 1, true, true, 1280, 720, 1, "asked for 1: the default is turned off"},
        {2, 1, 3, true, true, 1280, 720, 2, "asked above 2: clamped to 2"},
        {2, 1, 0.5f, true, true, 1280, 720, 1, "asked below 1: clamped to 1"},
    };
    for (const Row &r : rows) {
        GpuDeviceDesc d;
        d.renderScale = r.discrete;
        d.renderScaleIntegrated = r.integrated;
        d.renderScaleExplicit = r.asked;
        EXPECT_FLOAT_EQ(ResolveRenderScale(d, r.isDiscrete, r.windowed, r.w, r.h), r.want)
            << r.why;
    }
    // The cap holds wherever the default applies: a scaled frame never
    // exceeds 3840x2160 drawn, the scale is a whole number >= 1, and an output
    // already past the cap is simply not supersampled.
    GpuDeviceDesc d;
    d.renderScale = 2.0f;
    for (int h = 360; h <= 2880; h += 36) {
        const int w = h * 16 / 9;
        const float s = ResolveRenderScale(d, true, true, w, h);
        if (s > 1.0f) {
            EXPECT_LE(s * w, 3840.0f + 0.5f) << w << "x" << h;
            EXPECT_LE(s * h, 2160.0f + 0.5f) << w << "x" << h;
        }
        EXPECT_EQ(s, (float)(int)s) << w << "x" << h;
        EXPECT_GE(s, 1.0f);
        EXPECT_EQ(s, h <= 1080 ? 2.0f : 1.0f) << w << "x" << h;
    }
}

// MILO_RENDER_SCALE's parse (milo::ParseRenderScaleKnob): unset/empty = 0 (the
// profile default), a number clamps to [1, 2], anything else is reported bad
// and ignored. Sabotage: the [1, 2] clamp removed, or "2x" accepted -- red.
TEST(LookRenderScale, TheKnobParses) {
    struct Row { const char *v; float want; bool bad; };
    const Row rows[] = {
        {nullptr, 0, false}, {"", 0, false}, {"1", 1, false}, {"1.5", 1.5f, false},
        {"2", 2, false}, {"4", 2, false}, {"0.5", 1, false}, {"0", 0, true},
        {"-1", 0, true}, {"abc", 0, true}, {"2x", 0, true}, {"nan", 0, true},
    };
    for (const Row &r : rows) {
        bool bad = !r.bad;
        EXPECT_FLOAT_EQ(milo::ParseRenderScaleKnob(r.v, &bad), r.want) << (r.v ? r.v : "(unset)");
        EXPECT_EQ(bad, r.bad) << (r.v ? r.v : "(unset)");
    }
    // A profile override does not carry an explicit scale: the knob is the
    // process's, read once with DC3_LOOK.
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    EXPECT_EQ(Look().renderScaleExplicit, 0.0f);
}

// GpuDevice::Init applies the rule: a headless device given a render-scale
// DEFAULT (both adapter classes, so whichever this box has) draws 1x; asked
// explicitly it draws 2x and reads back at its output size. Sabotage:
// applyRenderScale reading desc.renderScale directly (the old rule) -- red on
// the first device; ignoring renderScaleExplicit -- red on the second.
TEST_F(LookSamplingTest, AHeadlessDeviceDrawsOneXUnlessAsked) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    for (int asked = 0; asked < 2; asked++) {
        GpuDevice d;
        GpuDeviceDesc desc;
        desc.headless = true;
        desc.width = 64;
        desc.height = 36;
        desc.renderScale = 2.0f;
        desc.renderScaleIntegrated = 2.0f;
        desc.renderScaleExplicit = asked ? 2.0f : 0.0f;
        if (!d.Init(desc)) GTEST_SKIP() << "no second headless device";
        EXPECT_EQ(d.RenderScaled(), asked != 0)
            << (asked ? "asked for 2x" : "a default only") << " on a headless device";
        EXPECT_EQ(d.OutputWidth(), 64);
        EXPECT_EQ(d.WindowWidth(), asked ? 128 : 64);
        d.Shutdown();
    }
}
