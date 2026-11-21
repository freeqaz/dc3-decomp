// test_crowd_contact_shadow.cpp -- the enhanced look's soft contact shadows
// under the 3D crowd (np-look32; engine platform/ContactShadowHook.h,
// gfx/ContactShadowPass.h, WgpuRnd::QueueContactShadow; dc3
// src/system/world/Crowd.cpp NativeCrowdContactShadow).
//
// The original draws no shadow of any kind for the crowd, so this is new work
// beyond it, and the retail look must draw none. What is pinned here:
//   * the pass darkens: a quad over the whole target darkens its centre to
//     1 - 0.45 of the colour and leaves its rim and the frame alpha alone;
//   * it writes no depth: a second shadow behind the first still darkens;
//   * it draws under nothing nearer: a surface in front of the shadow (a crowd
//     member's legs, drawn before the shadows flush) is left untouched;
//   * the look gates it: the renderer is registered, and under the retail
//     look (and Native, rb3 / rb3-xenon) it takes no shadow, while under the
//     enhanced look a shadow queued in the frame pass is drawn by the frame.
// That the crowd asks for one per 3D member is pinned end to end by
// test_native_crowd.cpp (DC3_CROWD_STATS shadows=).
//
// Sabotages run (np-look32, each: edit, rebuild, the named test fails, revert;
// table in archive/np-gfx-2026-11-14/lanes/sharp/item32/README.md):
//   kStrength 0 in the shader (the shadow removed)  -> DarkensCentreLeavesRimAndAlpha
//   crowdContactShadow on for every profile         -> RetailAndNativeTakeNone
//   depthWriteEnabled True                          -> WritesNoDepth
//   depthCompare Always                             -> NearerSurfaceStaysClean
//   the Crowd.cpp call removed                      -> CrowdTest (test_native_crowd.cpp)

#include "test_helpers.h"

#include "gfx/ContactShadowPass.h"
#include "gfx/GpuDevice.h"
#include "platform/ContactShadowHook.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"

#include "obj/Object.h"
#include "rndobj/Cam.h"
#include "rndobj/Rnd.h"

#include <cmath>
#include <vector>

namespace {

class ContactShadowGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    static constexpr int W = 64, H = 64;
    static constexpr int kGrey = 200, kAlpha = 77;

    // A W x H RGBA8Unorm target filled with kGrey (alpha kAlpha), a depth
    // attachment cleared to `clearDepth`; one shadow per entry of `zs`, each a
    // quad over the whole target at that clip depth. Returns RGBA bytes.
    std::vector<uint8_t> Run(float clearDepth, const std::vector<float> &zs) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        auto &dev = gpu.Device();
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)W, (uint32_t)H, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
            wgpu::TextureUsage::CopyDst;
        wgpu::Texture color = dev.CreateTexture(&d);
        std::vector<uint8_t> px((size_t)W * H * 4);
        for (size_t i = 0; i < px.size(); i += 4) {
            px[i] = px[i + 1] = px[i + 2] = kGrey;
            px[i + 3] = kAlpha;
        }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = color;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = W * 4;
        wgpu::Extent3D ext = {(uint32_t)W, (uint32_t)H, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);

        wgpu::TextureDescriptor dd{};
        dd.size = {(uint32_t)W, (uint32_t)H, 1};
        dd.format = wgpu::TextureFormat::Depth24PlusStencil8;
        dd.usage = wgpu::TextureUsage::RenderAttachment;
        wgpu::Texture depth = dev.CreateTexture(&dd);

        std::vector<ContactShadowPass::Vertex> verts;
        for (float z : zs) {
            const float c[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            const int tri[6] = {0, 1, 2, 0, 2, 3};
            for (int i : tri) {
                ContactShadowPass::Vertex v{};
                v.clip[0] = c[i][0];
                v.clip[1] = c[i][1];
                v.clip[2] = z;
                v.clip[3] = 1.0f;
                v.uv[0] = c[i][0];
                v.uv[1] = c[i][1];
                verts.push_back(v);
            }
        }

        wgpu::RenderPassColorAttachment ca{};
        ca.view = color.CreateView();
        ca.loadOp = wgpu::LoadOp::Load;
        ca.storeOp = wgpu::StoreOp::Store;
        wgpu::RenderPassDepthStencilAttachment da{};
        da.view = depth.CreateView();
        da.depthLoadOp = wgpu::LoadOp::Clear;
        da.depthStoreOp = wgpu::StoreOp::Store;
        da.depthClearValue = clearDepth;
        da.stencilLoadOp = wgpu::LoadOp::Clear;
        da.stencilStoreOp = wgpu::StoreOp::Store;
        da.stencilClearValue = 0;
        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.depthStencilAttachment = &da;

        // One shadow per Draw, as two flushes in a frame would record them.
        mPass.BeginFrame();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
        int drawn = 0;
        for (size_t s = 0; s < zs.size(); s++) {
            std::vector<ContactShadowPass::Vertex> one(verts.begin() + s * 6, verts.begin() + s * 6 + 6);
            drawn += mPass.Draw(gpu, pass, d.format, 1, one);
        }
        pass.End();
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        EXPECT_EQ(drawn, (int)zs.size());
        std::vector<uint8_t> out((size_t)W * H * 4);
        EXPECT_TRUE(gpu.ReadbackTexture(color, W, H, out.data(), out.size()));
        return out;
    }
    static int At(const std::vector<uint8_t> &px, int x, int y, int c) {
        return px[((size_t)y * W + x) * 4 + c];
    }
    static int AlphaChanged(const std::vector<uint8_t> &px) {
        int n = 0;
        for (size_t i = 3; i < px.size(); i += 4) n += px[i] != kAlpha;
        return n;
    }
    ContactShadowPass mPass;
    void TearDown() override { mPass.Terminate(); }
};

// The factor at the quad's centre, display-referred, on a unorm target.
int Expected(double times) {
    return (int)std::lround(200.0 * std::pow(1.0 - ContactShadowPass::kStrength, times));
}

} // namespace

TEST_F(ContactShadowGpuTest, DarkensCentreLeavesRimAndAlpha) {
    std::vector<uint8_t> px = Run(1.0f, {0.5f});
    const int c = At(px, W / 2, H / 2, 0);
    EXPECT_NEAR(c, Expected(1), 3) << "the shadow's centre is not 45 % darker";
    EXPECT_EQ(At(px, 0, 0, 0), kGrey) << "the quad's corner (r > 1) is darkened";
    // Outside the ellipse (r > 1) but inside the quad.
    EXPECT_EQ(At(px, W - 1, 12, 0), kGrey) << "a pixel outside the ellipse (r > 1) is darkened";
    EXPECT_LE(At(px, W - 1, H / 2, 0) - c, kGrey - c) << "the edge is darker than the centre";
    EXPECT_GE(At(px, W - 1, H / 2, 0), kGrey - 2) << "the falloff does not reach ~0 at the edge";
    // Monotone falloff from the centre out along a row.
    int last = c, rises = 0;
    for (int x = W / 2; x < W; x++) {
        rises += At(px, x, H / 2, 0) < last;
        last = At(px, x, H / 2, 0);
    }
    EXPECT_EQ(rises, 0) << "the falloff is not monotone";
    EXPECT_EQ(AlphaChanged(px), 0) << "the shadow wrote the frame alpha (the bloom mask)";
}

TEST_F(ContactShadowGpuTest, WritesNoDepth) {
    // Two overlapping shadows, the second further away: with no depth write
    // both darken (members standing close together), so the centre is the
    // factor squared.
    std::vector<uint8_t> px = Run(1.0f, {0.5f, 0.7f});
    EXPECT_NEAR(At(px, W / 2, H / 2, 0), Expected(2), 3)
        << "the second shadow was hidden by the first: the shadow writes depth";
}

TEST_F(ContactShadowGpuTest, NearerSurfaceStaysClean) {
    // Depth already holds a surface nearer than the shadow (a character's legs).
    std::vector<uint8_t> px = Run(0.3f, {0.5f});
    int changed = 0;
    for (size_t i = 0; i < px.size(); i += 4) changed += px[i] != kGrey;
    EXPECT_EQ(changed, 0) << changed << " pixels darkened over a nearer surface";
}

TEST_F(ContactShadowGpuTest, RetailAndNativeTakeNone) {
    NativeContactShadowRenderer *r = GetNativeContactShadowRenderer();
    ASSERT_NE(r, nullptr) << "WgpuRnd registered no contact shadow renderer";
    RndCam *cam = Hmx::Object::New<RndCam>();
    NativeContactShadow s{};
    // In front of a default camera (Milo cameras look down +y).
    s.centre[1] = 100.0f;
    s.axisA[0] = 10.0f;
    s.axisB[1] = 7.0f;
    auto frame = [&](milo::LookProfile p, bool &wants, bool &queued) {
        milo::ScopedLookOverride look(p);
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        wants = r->WantsContactShadows();
        queued = r->QueueContactShadow(s);
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        return gWgpuRnd->ContactShadowsDrawnThisFrame();
    };
    bool wants = true, queued = true;
    EXPECT_EQ(frame(milo::LookProfile::Retail, wants, queued), 0);
    EXPECT_FALSE(wants) << "the retail look asks for crowd contact shadows";
    EXPECT_FALSE(queued) << "the retail look queued a crowd contact shadow";
    EXPECT_EQ(frame(milo::LookProfile::Native, wants, queued), 0);
    EXPECT_FALSE(wants) << "the Native profile (rb3, rb3-xenon) asks for crowd contact shadows";
    EXPECT_FALSE(queued);
    // The positive control: the same frame under the enhanced look draws it.
    EXPECT_EQ(frame(milo::LookProfile::Enhanced, wants, queued), 1)
        << "a shadow queued in the enhanced frame pass was not drawn";
    EXPECT_TRUE(wants);
    EXPECT_TRUE(queued);
    delete cam;
}
