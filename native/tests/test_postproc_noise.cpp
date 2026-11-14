// test_postproc_noise.cpp -- the "awesome" post-process noise (PLAN G-14).
//
// The image's NgPostProc::CheckNoise hands postprocess.ps a noise TEXTURE
// (mNoiseMap, sampled at (uv + seed) * mNoiseBaseScale, twice, the samples
// multiplied) and the shader overlay-blends it into the scene at
// mNoiseIntensity (option bit 0x4; disassembled from the shipped xbox_shaders,
// permutation postprocess_0000000000200014):
//
//     S < 0.5:  out = (1-I) S + I * 2 n S
//     S >= 0.5: out = (1-I) S + I * (1 - 2 (1-n)(1-S))
//
// dci/dclive_performance_awesome.pp author I = 1, stationary, base scale (1, 3)
// and noise_mono.tex, an 8x256 DXT1 bitmap of 1-texel horizontal stripes
// whose two colours are light greys (~0.81 and ~0.85 per channel). With I = 1
// and stationary seeds (both layers on the same texel) n = t^2 with t in about
// [0.80, 0.89], so the look is a static brightening with faint scanlines.
//
// The engine used to add (hash - 0.5) * I per pixel, re-rolled every frame:
// +/-0.5 of animated static over the whole frame. This test runs the engine's
// PostProcPass on a flat grey scene twice -- noise off, noise on -- and checks
// every pixel against the overlay above for some t in the texture's range, and
// that stationary noise does not change between frames.
//
// Needs a GPU (skips without one) and the game data (camera.milo), and an
// engine that carries the noise-map seam (milo-native-engine "PostProcPass:
// the noise is the post-process's noise MAP", which defines
// MILO_POSTPROC_NOISE_MAP). The pinned engine has it, so the test is always
// compiled: an engine without the seam is a build error here, not a test that
// silently disappears.

#include "test_helpers.h"

#include "gfx/PostProcPass.h"

#ifndef MILO_POSTPROC_NOISE_MAP
#error "milo-native-engine lacks the post-process noise-map seam (MILO_POSTPROC_NOISE_MAP); MILO_ENGINE_PATH/MILO_ENGINE_PIN predate it"
#endif

#include "gfx/GpuDevice.h"
#include "platform/rndshape/RndShape.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "os/File.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/PostProc.h"
#include "rndobj/Tex.h"
#include "utl/FilePath.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

static_assert(rndshape::HasPostProcNoiseMap<RndPostProc>::value,
              "dc3's RndPostProc must expose the noise map to the engine "
              "(GetNoiseMap/GetNoiseBaseScale/GetNoiseTopScale)");

namespace {

constexpr int kW = 160;
constexpr int kH = 120;
// Flat greys either side of the overlay's 0.5 branch.
constexpr uint8_t kDark = 64;    // 0.251
constexpr uint8_t kLight = 191;  // 0.749

// noise_mono.tex: every DXT1 block is c0 = 0xDF1C, c1 = 0xCE9A, rows
// alternating index 1 (c1) and index 2 ((2 c0 + c1) / 3). Per channel the
// texel is in [c1, (2c0+c1)/3]; bilinear filtering stays inside that range.
// R5 25..(2*27+25)/3, G6 52..(2*56+52)/3, B5 26..(2*28+26)/3, widened by 0.02.
constexpr float kTexLo[3] = {25.0f / 31.0f - 0.02f, 52.0f / 63.0f - 0.02f, 26.0f / 31.0f - 0.02f};
constexpr float kTexHi[3] = {(2 * 27 + 25) / 93.0f + 0.02f, (2 * 56 + 52) / 189.0f + 0.02f,
                             (2 * 28 + 26) / 93.0f + 0.02f};

struct Readback {
    std::vector<uint8_t> px;
    float At(int x, int y, int c) const { return px[(y * kW + x) * 4 + c] / 255.0f; }
};

class PostProcNoiseTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    static ObjectDir *LoadCameraDir() {
        static ObjectDir *sDir = nullptr;
        if (!sDir)
            sDir = DirLoader::LoadObjects(
                FilePath(FileRoot(), "world/shared/camera.milo"), nullptr, nullptr
            );
        return sDir;
    }

    // One PostProcPass::Run over a flat scene: left half kDark, right half kLight.
    Readback Run(PostProcPass &pass) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        auto &dev = gpu.Device();
        if (!mScene) {
            wgpu::TextureDescriptor d{};
            d.size = {(uint32_t)kW, (uint32_t)kH, 1};
            d.format = wgpu::TextureFormat::RGBA8Unorm;
            d.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst |
                wgpu::TextureUsage::RenderAttachment;
            mScene = dev.CreateTexture(&d);
            std::vector<uint8_t> px(kW * kH * 4);
            for (int y = 0; y < kH; y++)
                for (int x = 0; x < kW; x++) {
                    uint8_t g = x < kW / 2 ? kDark : kLight;
                    uint8_t *p = &px[(y * kW + x) * 4];
                    p[0] = p[1] = p[2] = g;
                    // Alpha is DC3's pseudo-HDR bloom mask (np-gfx-post's
                    // retail chain): 0, so this pp's bloom adds nothing and
                    // the composite is the noise overlay alone.
                    p[3] = 0;
                }
            wgpu::TexelCopyTextureInfo dst{};
            dst.texture = mScene;
            wgpu::TexelCopyBufferLayout layout{};
            layout.bytesPerRow = kW * 4;
            wgpu::Extent3D ext = {(uint32_t)kW, (uint32_t)kH, 1};
            gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);

            d.format = gpu.SurfaceFormat();
            d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
            mFrame = dev.CreateTexture(&d);

            wgpu::TextureDescriptor b{};
            b.size = {1, 1, 1};
            b.format = wgpu::TextureFormat::RGBA8Unorm;
            b.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
            mBlack = dev.CreateTexture(&b);
            const uint8_t black[4] = {0, 0, 0, 255};
            dst.texture = mBlack;
            layout.bytesPerRow = 4;
            wgpu::Extent3D one = {1, 1, 1};
            gpu.Queue().WriteTexture(&dst, black, 4, &layout, &one);
        }
        wgpu::TextureView sceneView = mScene.CreateView();
        wgpu::TextureView frameView = mFrame.CreateView();
        wgpu::TextureView blackView = mBlack.CreateView();
        wgpu::TextureView noDepth;
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        pass.Run(enc, sceneView, mScene, kW, kH, noDepth, 1, frameView, blackView, gpu);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        Readback r;
        r.px.resize(kW * kH * 4);
        EXPECT_TRUE(gpu.ReadbackTexture(mFrame, kW, kH, r.px.data(), r.px.size()));
        // A BGRA frame: swap to RGB order (the scene is grey, the texel is not).
        if (gpu.SurfaceFormat() == wgpu::TextureFormat::BGRA8Unorm)
            for (size_t i = 0; i < r.px.size(); i += 4) std::swap(r.px[i], r.px[i + 2]);
        return r;
    }

    wgpu::Texture mScene, mFrame, mBlack;
};

// The overlay the image applies at intensity 1, for texel value t (n = t*t).
float Overlay(float s, float t) {
    const float n = t * t;
    return s < 0.5f ? 2.0f * n * s : 1.0f - 2.0f * (1.0f - n) * (1.0f - s);
}

} // namespace

TEST_F(PostProcNoiseTest, AwesomeNoiseIsTheImagesStationaryTextureOverlay) {
    ObjectDir *dir = LoadCameraDir();
    if (!dir) GTEST_SKIP() << "world/shared/camera.milo not loadable";
    RndPostProc *pp = dir->Find<RndPostProc>("dci_performance_awesome.pp", false);
    ASSERT_NE(pp, nullptr) << "dci_performance_awesome.pp missing from camera.milo";

    // The authored values this test is about (read live 2026-11-13 too).
    ASSERT_FLOAT_EQ(pp->GetNoiseIntensity(), 1.0f);
    ASSERT_TRUE(pp->GetNoiseStationary());
    ASSERT_FALSE(pp->GetNoiseMidtone());
    ASSERT_NE(pp->GetNoiseMap(), nullptr);
    EXPECT_STREQ(pp->GetNoiseMap()->Name(), "noise_mono.tex");
    EXPECT_FLOAT_EQ(pp->GetNoiseBaseScale().x, 1.0f);
    EXPECT_FLOAT_EQ(pp->GetNoiseBaseScale().y, 3.0f);
    pp->GetNoiseMap()->PresyncBitmap();
    ASSERT_TRUE(GetGpuTexView(pp->GetNoiseMap())) << "noise_mono.tex has no GPU texture";

    RndPostProc *prev = RndPostProc::Current();
    pp->Select();
    PostProcPass pass;
    pass.Init(gWgpuRnd->Gpu());

    pp->SetProperty("noise_intensity", DataNode(0.0f));
    Readback base = Run(pass);
    pp->SetProperty("noise_intensity", DataNode(1.0f));
    Readback noisy = Run(pass);
    Readback noisy2 = Run(pass);

    pass.Terminate();
    if (prev) prev->Select(); else pp->Unselect();

    int checked = 0, outside = 0, moved = 0;
    float maxDev = 0.0f;
    for (int y = 0; y < kH; y++)
        for (int x = 0; x < kW; x++) {
            if (x == kW / 2 - 1 || x == kW / 2) continue;  // the seam is filtered
            for (int c = 0; c < 3; c++) {
                const float s = base.At(x, y, c), o = noisy.At(x, y, c);
                if (s < 0.05f || s > 0.95f || std::fabs(s - 0.5f) < 0.05f) continue;
                checked++;
                // The overlay is monotonic in t: the output must lie between
                // the texture's two extremes (plus two codes of 8-bit rounding).
                const float lo = Overlay(s, kTexLo[c]) - 2.0f / 255.0f;
                const float hi = Overlay(s, kTexHi[c]) + 2.0f / 255.0f;
                if (o < lo || o > hi) {
                    outside++;
                    maxDev = std::max(maxDev, o < lo ? lo - o : o - hi);
                }
                if (noisy2.px[(y * kW + x) * 4 + c] != noisy.px[(y * kW + x) * 4 + c]) moved++;
            }
        }
    printf("PostProcNoise: %d channel samples, %d outside the overlay band (max %.3f), "
           "%d changed between two frames\n", checked, outside, maxDev, moved);
    ASSERT_GT(checked, kW * kH * 2) << "the scene did not reach the composite";
    EXPECT_EQ(outside, 0) << "noise is not the image's texture overlay";
    EXPECT_EQ(moved, 0) << "stationary noise changed between frames";
}
