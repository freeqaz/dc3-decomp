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
        EXPECT_FALSE(Look().outputFollowsTarget)
            << "the original draws a 1280x720 frame, scaled to the display";
        EXPECT_EQ(Look().outputMaxRowsIntegrated, 0);
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
        EXPECT_TRUE(Look().outputFollowsTarget) << "the frame at the window's resolution";
        EXPECT_EQ(Look().outputMaxRowsIntegrated, 1080) << "a handheld draws at most 1080 rows";
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

// The frame's sizes (ResolveFrameSizes, gfx/GpuDevice.h): ONE rule for every
// window size. DC3 draws its frame at the window's size (outputFollowsTarget):
// output = the logical 1280x720 fitted into the target, pillar- or letterboxed
// for a target that is not 16:9; scale = ResolveRenderScale(output); drawn =
// output * scale. With the Enhanced profile a 720p window must give exactly
// round 3's frame (2560x1440 drawn, boxed down to 1280x720); 1080p supersamples
// 2x to the 3840x2160 cap; 1440p and 4K draw 1x. Headless draws its own size
// (MILO_WIDTH x MILO_HEIGHT) at 1x. A shape that does not follow (rb3 /
// rb3-xenon: the fixed frameWidth) keeps the old frame. Sabotages, each red
// here: output = the target, unfitted (the bars); output = the logical size
// (the old fixed frame); the scale resolved from the target instead of the
// output; drawn not multiplied by the scale.
TEST(OutputResolution, TheOneRuleForEveryWindowSize) {
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    struct Row {
        bool follows, windowed, discrete;
        float asked;
        int targetW, targetH;
        int outW, outH;
        float scale;
        int drawnW, drawnH;
        const char *why;
    };
    const Row rows[] = {
        {true, true, true, 0, 1280, 720, 1280, 720, 2, 2560, 1440, "720p window: round 3's 2x box-down"},
        {true, true, true, 0, 1920, 1080, 1920, 1080, 2, 3840, 2160, "1080p window: 2x, at the cap"},
        {true, true, true, 0, 2560, 1440, 2560, 1440, 1, 2560, 1440, "1440p window: 1x (whole scales)"},
        {true, true, true, 0, 3840, 2160, 3840, 2160, 1, 3840, 2160, "4K window: 1x"},
        {true, true, true, 0, 2560, 1080, 1920, 1080, 2, 3840, 2160, "ultrawide: 16:9 pillarboxed, 2x"},
        {true, true, true, 0, 1920, 1200, 1920, 1080, 2, 3840, 2160, "16:10: letterboxed"},
        {true, true, true, 0, 1000, 1000, 1000, 563, 2, 2000, 1126, "square: letterboxed, any size"},
        {true, true, false, 0, 1920, 1080, 1920, 1080, 1, 1920, 1080, "integrated GPU: 1x"},
        {true, false, true, 0, 1280, 720, 1280, 720, 1, 1280, 720, "headless default: 1280x720 at 1x"},
        {true, false, true, 0, 1920, 1080, 1920, 1080, 1, 1920, 1080, "headless MILO_WIDTH/HEIGHT 1080p"},
        {true, false, true, 2, 1280, 720, 1280, 720, 2, 2560, 1440, "headless, asked for 2x"},
        {true, true, true, 2, 3840, 2160, 3840, 2160, 2, 7680, 4320, "asked for 2x at 4K: not capped"},
        {false, true, true, 0, 1920, 1080, 1280, 720, 2, 2560, 1440, "not following: the fixed frame"},
        {false, false, true, 0, 1920, 1080, 1920, 1080, 1, 1920, 1080, "not following, headless: target"},
    };
    for (const Row &r : rows) {
        GpuDeviceDesc d;
        d.renderScale = Look().renderScale;
        d.renderScaleIntegrated = Look().renderScaleIntegrated;
        d.renderScaleExplicit = r.asked;
        d.frameWidth = 1280;
        d.frameHeight = 720;
        if (r.follows) {
            d.outputFollowsTarget = true;
            d.logicalWidth = 1280;
            d.logicalHeight = 720;
        }
        const FrameSizes s = ResolveFrameSizes(d, r.discrete, r.windowed, r.targetW, r.targetH);
        EXPECT_EQ(s.outputW, r.outW) << r.why;
        EXPECT_EQ(s.outputH, r.outH) << r.why;
        EXPECT_FLOAT_EQ(s.scale, r.scale) << r.why;
        EXPECT_EQ(s.drawnW, r.drawnW) << r.why;
        EXPECT_EQ(s.drawnH, r.drawnH) << r.why;
    }
    // Retail is the original: one sample per output pixel at every size.
    ScopedLookOverride retail(LookProfile::Retail);
    GpuDeviceDesc d;
    d.renderScale = Look().renderScale;
    d.renderScaleIntegrated = Look().renderScaleIntegrated;
    d.outputFollowsTarget = true;
    d.logicalWidth = 1280;
    d.logicalHeight = 720;
    for (int h : {720, 1080, 1440}) {
        const FrameSizes s = ResolveFrameSizes(d, true, true, h * 16 / 9, h);
        EXPECT_EQ(s.drawnH, h) << "retail at " << h << "p";
        EXPECT_FLOAT_EQ(s.scale, 1.0f) << "retail at " << h << "p";
    }
}

// GpuDevice::Init applies the frame rule: a headless device whose frame follows
// its target, at 1920x1080 with a logical 1280x720, draws and reads back
// 1920x1080 (not a 1280x720 frame in the corner), viewports scale 1.5x and the
// frame is not supersampled; asked for 2x, it draws 3840x2160 and reads back
// 1920x1080. Sabotages: ApplyFrameSizes ignoring the output (drawn = logical) --
// red on WindowWidth; FrameScaleY taken from the output instead of the logical
// size -- red on 1.5; SupersampleScale from drawn/logical -- red on 1.
TEST_F(LookSamplingTest, AFollowingDeviceDrawsItsTargetsSize) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device";
    for (int asked = 0; asked < 2; asked++) {
        GpuDevice d;
        GpuDeviceDesc desc;
        desc.headless = true;
        desc.width = 1920;
        desc.height = 1080;
        desc.outputFollowsTarget = true;
        desc.logicalWidth = 1280;
        desc.logicalHeight = 720;
        desc.renderScale = 2.0f;
        desc.renderScaleIntegrated = 2.0f;
        desc.renderScaleExplicit = asked ? 2.0f : 0.0f;
        if (!d.Init(desc)) GTEST_SKIP() << "no second headless device";
        const char *why = asked ? "asked for 2x" : "headless default";
        EXPECT_EQ(d.OutputWidth(), 1920) << why;
        EXPECT_EQ(d.OutputHeight(), 1080) << why;
        EXPECT_EQ(d.WindowWidth(), asked ? 3840 : 1920) << why;
        EXPECT_EQ(d.WindowHeight(), asked ? 2160 : 1080) << why;
        EXPECT_EQ(d.LogicalWidth(), 1280) << why;
        EXPECT_EQ(d.LogicalHeight(), 720) << why;
        EXPECT_TRUE(d.FrameScaled()) << why;
        EXPECT_FLOAT_EQ(d.FrameScaleX(), asked ? 3.0f : 1.5f) << why;
        EXPECT_FLOAT_EQ(d.FrameScaleY(), asked ? 3.0f : 1.5f) << why;
        EXPECT_FLOAT_EQ(d.SupersampleScale(), asked ? 2.0f : 1.0f) << why;
        EXPECT_EQ(d.RenderScaled(), asked != 0) << why;
        EXPECT_FLOAT_EQ(d.RenderScaleFor(d.WindowWidth()), asked ? 3.0f : 1.5f) << why;
        EXPECT_FLOAT_EQ(d.RenderScaleFor(512), 1.0f) << why << ": a 512 render target";
        d.Shutdown();
    }
}

// The output's row cap (OutputRowCap / ResolveFrameSizes, gfx/GpuDevice.h).
// Enhanced on a NON-discrete adapter presenting to a window holds the output
// to 1080 rows at its 16:9 shape (a handheld's 2560x1600 panel gets a
// 1920x1080 frame, scaled up into the window by the present path); discrete
// adapters and headless frames are not capped by default. MILO_OUTPUT_MAX_ROWS
// (outputMaxRowsExplicit) overrides it both ways on any adapter, headless
// included: 0 = no cap, 720 / 1440 = that many rows. The render scale is then
// resolved from the capped output, under the 3840x2160 drawn cap as before.
// Sabotages, each red here: the cap applied on a discrete adapter; the cap
// ignored on an integrated one; the knob ignored, or its 0 read as "unset";
// the knob limited to integrated adapters; the capped width not rescaled to
// the shape; the render scale resolved before the cap.
TEST(OutputResolution, AnIntegratedAdapterIsCappedAt1080Rows) {
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    struct Row {
        bool discrete, windowed;
        int knob;
        float asked;
        int targetW, targetH;
        int outW, outH;
        float scale;
        int drawnW, drawnH;
        const char *why;
    };
    const Row rows[] = {
        // Discrete: unchanged (no cap).
        {true, true, -1, 0, 1920, 1080, 1920, 1080, 2, 3840, 2160, "discrete 1080p window: 2x"},
        {true, true, -1, 0, 2560, 1440, 2560, 1440, 1, 2560, 1440, "discrete 1440p window: 1x"},
        {true, true, -1, 0, 3840, 2160, 3840, 2160, 1, 3840, 2160, "discrete 4K window: 1x"},
        // Integrated: at most 1080 rows.
        {false, true, -1, 0, 1280, 720, 1280, 720, 1, 1280, 720, "integrated 720p window: under the cap"},
        {false, true, -1, 0, 1920, 1080, 1920, 1080, 1, 1920, 1080, "integrated 1080p window: at the cap"},
        {false, true, -1, 0, 2560, 1600, 1920, 1080, 1, 1920, 1080, "integrated 2560x1600 panel: 1080 rows"},
        {false, true, -1, 0, 3840, 2160, 1920, 1080, 1, 1920, 1080, "integrated 4K window: 1080 rows"},
        {false, false, -1, 0, 2560, 1440, 2560, 1440, 1, 2560, 1440, "integrated headless: not capped"},
        {false, true, -1, 2, 2560, 1600, 1920, 1080, 2, 3840, 2160, "integrated, asked for 2x: 1080 rows drawn 2x"},
        // The knob, both directions, any adapter.
        {false, true, 0, 0, 2560, 1600, 2560, 1440, 1, 2560, 1440, "knob 0 on the handheld: no cap"},
        {false, true, 1440, 0, 2560, 1600, 2560, 1440, 1, 2560, 1440, "knob 1440 on the handheld"},
        {false, true, 720, 0, 2560, 1600, 1280, 720, 1, 1280, 720, "knob 720 on the handheld"},
        {true, true, 720, 0, 3840, 2160, 1280, 720, 2, 2560, 1440, "knob 720 on a discrete 4K window: 2x"},
        {true, true, 1440, 0, 3840, 2160, 2560, 1440, 1, 2560, 1440, "knob 1440 on a discrete 4K window"},
        {true, true, 0, 0, 1920, 1080, 1920, 1080, 2, 3840, 2160, "knob 0 on a discrete 1080p window"},
        {true, false, 720, 0, 1920, 1080, 1280, 720, 1, 1280, 720, "knob 720 headless"},
        {true, true, 1440, 0, 1920, 1080, 1920, 1080, 2, 3840, 2160, "a cap above the window: no effect"},
        // The 3840x2160 drawn cap is unchanged by the row cap.
        {true, true, 2160, 0, 3840, 2160, 3840, 2160, 1, 3840, 2160, "knob 2160 at 4K: still 1x"},
        {true, true, 1080, 0, 3840, 2160, 1920, 1080, 2, 3840, 2160, "knob 1080 at 4K: 2x, at the cap"},
        {true, true, 900, 0, 3840, 2160, 1600, 900, 2, 3200, 1800, "knob 900 at 4K: 1600x900, 2x"},
    };
    for (const Row &r : rows) {
        GpuDeviceDesc d;
        d.renderScale = Look().renderScale;
        d.renderScaleIntegrated = Look().renderScaleIntegrated;
        d.renderScaleExplicit = r.asked;
        d.outputFollowsTarget = true;
        d.logicalWidth = 1280;
        d.logicalHeight = 720;
        d.outputMaxRowsIntegrated = Look().outputMaxRowsIntegrated;
        d.outputMaxRowsExplicit = r.knob;
        const FrameSizes s = ResolveFrameSizes(d, r.discrete, r.windowed, r.targetW, r.targetH);
        EXPECT_EQ(s.outputW, r.outW) << r.why;
        EXPECT_EQ(s.outputH, r.outH) << r.why;
        EXPECT_FLOAT_EQ(s.scale, r.scale) << r.why;
        EXPECT_EQ(s.drawnW, r.drawnW) << r.why;
        EXPECT_EQ(s.drawnH, r.drawnH) << r.why;
    }
    // Not following (Retail, rb3, rb3-xenon): no row cap applies to the fixed
    // frame, knob or not.
    GpuDeviceDesc d;
    d.frameWidth = 1280;
    d.frameHeight = 720;
    d.outputMaxRowsIntegrated = 1080;
    d.outputMaxRowsExplicit = 360;
    const FrameSizes s = ResolveFrameSizes(d, false, true, 2560, 1600);
    EXPECT_EQ(s.outputW, 1280);
    EXPECT_EQ(s.outputH, 720);
}

// MILO_OUTPUT_MAX_ROWS's parse (milo::ParseOutputRowsKnob): unset/empty = -1
// (the profile default), a whole number >= 0 is taken as is (0 = no cap),
// anything else is reported bad and ignored. Sabotage: "0" read as unset, or
// "1080p" accepted -- red.
TEST(OutputResolution, TheRowsKnobParses) {
    struct Row { const char *v; int want; bool bad; };
    const Row rows[] = {
        {nullptr, -1, false}, {"", -1, false}, {"0", 0, false}, {"720", 720, false},
        {"1080", 1080, false}, {"1440", 1440, false}, {"-1", -1, true}, {"1080p", -1, true},
        {"abc", -1, true}, {"1.5", -1, true}, {"99999", -1, true},
    };
    for (const Row &r : rows) {
        bool bad = !r.bad;
        EXPECT_EQ(milo::ParseOutputRowsKnob(r.v, &bad), r.want) << (r.v ? r.v : "(unset)");
        EXPECT_EQ(bad, r.bad) << (r.v ? r.v : "(unset)");
    }
    // A profile override does not carry the knob: it is the process's, read
    // once with DC3_LOOK.
    ScopedLookOverride enhanced(LookProfile::Enhanced);
    EXPECT_EQ(Look().outputMaxRowsExplicit, -1);
}
