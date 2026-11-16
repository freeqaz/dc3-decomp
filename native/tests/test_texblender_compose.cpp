// test_texblender_compose.cpp -- a TexBlender's output is the image's two
// steps: the base map over the whole target, then each controller's mesh
// unwrapped at its UVs (PLAN H-117, branch np-texblend).
//
// On the 360, RndTexBlender::DrawShowing binds its output texture, draws the
// base map with DxRnd::DrawRect(0, 0, W, H) -- viewport disabled, so those are
// the target's own pixels (src/system/rnddx9/Rnd.cpp) -- and then draws each
// controller's mesh with the unwrap-UV shader (ShaderType 22) blended at the
// controller's weight. Natively the rect went through DrawRect2D, which maps
// against the 1280x720 virtual screen: the 512^2 base landed squashed into the
// top-left 40% x 71% of the target and the rest stayed transparent black; and
// RndMesh::DrawFacesInRange is a no-op natively, so no layer ever landed. The
// dancers' blended normal maps were partly black. TexBlender.cpp's
// NativeCompose now draws both steps through the engine's composer
// (platform/TexBlendHook.h, gfx/TexBlendPass).
//
// Needs a GPU (skips without one). Compiled only against an engine that can
// read a render target back (MILO_READBACK_GPU_RENDER_TARGET).
//
// Sabotage (np-texblend, 2026-11-16), each red, then restored:
//   - NativeCompose's call removed from DrawShowing (the decomp body draws):
//     all three fail -- 4090 of 4096 texels not the base map's, 0 of 900
//     layer texels landed;
//   - WgpuRnd::ComposeTexBlend passing no BindGroupCache to TexBlendPass::Record:
//     SteadyFrameCreatesNothing fails, 6 bind groups in 3 frames (one per draw);
//   - WgpuRnd::WarmTexBlendPipelines not warming: SteadyFrameCreatesNothing
//     fails, 3 pipelines created at draw time.
// That QueueTexBlender (the load warm) calls WarmTexBlendPipelines is
// NativeRehearse's no-pipeline-after-game_screen guard (test_native_rehearse.cpp).

#include "test_helpers.h"

#include "platform/Rnd_Wgpu.h"

#ifdef MILO_READBACK_GPU_RENDER_TARGET

#include "gfx/BindGroupCount.h"
#include "gfx/PipelineCount.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Cam.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Tex.h"
#include "rndobj/TexBlendController.h"
#include "rndobj/TexBlender.h"

#include <vector>

namespace {

constexpr int kSize = 64;

class TexBlenderComposeTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    // Values are 0 or 255 only (fixed points of sRGB encoding), and red always
    // equals blue, so the checks read the same in an RGBA or a BGRA texture.
    // Base: red/blue on the left half, green on the top half.
    static void BaseTexel(int x, int y, unsigned char *p) {
        const unsigned char rb = x < kSize / 2 ? 255 : 0;
        p[0] = rb;
        p[1] = y < kSize / 2 ? 255 : 0;
        p[2] = rb;
        p[3] = 255;
    }

    static RndTex *MakeTex(bool base) {
        std::vector<unsigned char> px((size_t)kSize * kSize * 4);
        for (int y = 0; y < kSize; y++)
            for (int x = 0; x < kSize; x++) {
                unsigned char *p = &px[((size_t)y * kSize + x) * 4];
                if (base) BaseTexel(x, y, p);
                else { p[0] = 0; p[1] = 255; p[2] = 0; p[3] = 255; }  // the layer: green
            }
        RndTex *tex = Hmx::Object::New<RndTex>();
        RndBitmap bmp;
        bmp.Create(kSize, kSize, 0, 32, 0, nullptr, (void *)px.data(), nullptr);
        tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
        tex->PresyncBitmap();
        return tex;
    }

    // A quad whose UVs span [u0,u1] x [v0,v1]; positions are irrelevant to the
    // unwrap (the vertex's UV is where it lands).
    static RndMesh *MakeUvQuad(float u0, float v0, float u1, float v1) {
        RndMesh *mesh = Hmx::Object::New<RndMesh>();
        mesh->Verts().resize(4);
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        for (int i = 0; i < 4; i++) {
            mesh->Verts()[i].pos.Set(uv[i][0], uv[i][1], 0);
            mesh->Verts()[i].tex = Vector2(uv[i][0], uv[i][1]);
        }
        static const int kFaces[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 2, 1}, {0, 3, 2}};
        mesh->Faces().resize(4);
        for (int i = 0; i < 4; i++)
            mesh->Faces()[i].Set(kFaces[i][0], kFaces[i][1], kFaces[i][2]);
        return mesh;
    }

    // The blender as a character's .milo carries it: base map, a 512^2-style
    // rendered-no-z output, and (optionally) one controller with an override
    // map, which is the custom state at full weight (GetBlendState).
    struct Blender {
        RndTexBlender *blender = nullptr;
        RndTex *output = nullptr;
    };

    static Blender Make(bool withLayer) {
        Blender b;
        b.output = Hmx::Object::New<RndTex>();
        b.output->SetBitmap(kSize, kSize, 32, RndTex::kRenderedNoZ, false, nullptr);
        b.blender = Hmx::Object::New<RndTexBlender>();
        b.blender->SetProperty("base_map", DataNode(MakeTex(true)));
        b.blender->SetProperty("output_texture", DataNode(b.output));
        b.blender->SetProperty("controller_influence", DataNode(1.0f));
        if (withLayer) {
            RndTexBlendController *c = Hmx::Object::New<RndTexBlendController>();
            c->SetProperty("mesh", DataNode(MakeUvQuad(0.25f, 0.25f, 0.75f, 0.75f)));
            c->SetProperty("override_map", DataNode(MakeTex(false)));
            b.blender->InsertProperty(
                DataArrayPtr(DataNode(Symbol("controller_list")), DataNode(0)), DataNode(c)
            );
        }
        return b;
    }

    static void Draw(RndTexBlender *b) {
        TheRnd.BeginDrawing();
        b->DrawShowing();
        TheRnd.EndDrawing();
    }

    struct Texels {
        bool ok = false;
        int w = 0, h = 0;
        std::vector<uint8_t> px;
        const uint8_t *At(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    };

    static Texels Read(RndTex *tex) {
        Texels t;
        t.ok = ReadbackGpuRenderTarget(tex, t.px, t.w, t.h);
        return t;
    }
};

} // namespace

// Base only: every texel of the target is the base map's texel at the same
// place. The defect put the base into the top-left (64*64/1280 x 64*64/720 of
// the virtual screen's mapping) and left the rest at alpha 0.
TEST_F(TexBlenderComposeTest, BaseCoversTheWholeTarget) {
    Blender b = Make(false);
    Draw(b.blender);
    Texels t = Read(b.output);
    ASSERT_TRUE(t.ok) << "the output texture has no GPU render target";
    ASSERT_EQ(t.w, kSize);
    ASSERT_EQ(t.h, kSize);
    int wrong = 0, firstX = -1, firstY = -1;
    for (int y = 0; y < kSize; y++)
        for (int x = 0; x < kSize; x++) {
            unsigned char want[4];
            BaseTexel(x, y, want);
            const uint8_t *got = t.At(x, y);
            bool same = true;
            for (int c = 0; c < 4; c++) same = same && got[c] == want[c];
            if (!same && wrong++ == 0) { firstX = x; firstY = y; }
        }
    EXPECT_EQ(wrong, 0) << "texels that are not the base map's (first at " << firstX << ","
                        << firstY << ")";
}

// A controller at full weight: inside its UV rectangle the target is the
// override map; outside, the base map.
TEST_F(TexBlenderComposeTest, LayerLandsAtItsUvs) {
    Blender b = Make(true);
    Draw(b.blender);
    Texels t = Read(b.output);
    ASSERT_TRUE(t.ok);
    ASSERT_EQ(t.w, kSize);
    int insideGreen = 0, insideTotal = 0, outsideWrong = 0;
    for (int y = 0; y < kSize; y++)
        for (int x = 0; x < kSize; x++) {
            const uint8_t *got = t.At(x, y);
            // [16, 48) is the quad's pixels; keep a texel's margin off its edges.
            const bool inside = x > 16 && x < 47 && y > 16 && y < 47;
            const bool outside = x < 15 || x > 48 || y < 15 || y > 48;
            if (inside) {
                insideTotal++;
                insideGreen += got[0] == 0 && got[1] == 255 && got[2] == 0;
            } else if (outside) {
                unsigned char want[4];
                BaseTexel(x, y, want);
                outsideWrong += got[0] != want[0] || got[1] != want[1] || got[2] != want[2];
            }
        }
    EXPECT_EQ(insideGreen, insideTotal) << "the layer did not land at its UVs";
    EXPECT_EQ(outsideWrong, 0) << "the layer or the base reached outside where they belong";
}

// The blender redraws every frame a controller is active (the image does, as
// the weights follow the pose). Once its pipelines are warmed (QueueTexBlender
// at load, native/src/platform/PipelineWarm.cpp), a frame creates no pipeline;
// from the second frame on it creates no bind group either.
TEST_F(TexBlenderComposeTest, SteadyFrameCreatesNothing) {
    Blender b = Make(true);
    // A frame with nothing in it first: whatever an empty frame creates is the
    // frame's, not the blender's.
    TheRnd.BeginDrawing();
    TheRnd.EndDrawing();
    gWgpuRnd->WarmTexBlendPipelines(b.output);
    const uint64_t pipes0 = GpuPipelineCreateCount();
    const int composed0 = gWgpuRnd->TexBlendsComposed();
    Draw(b.blender);
    EXPECT_EQ(GpuPipelineCreateCount() - pipes0, (uint64_t)0)
        << "a warmed blender created a pipeline at draw time";
    const uint64_t groups1 = GpuBindGroupCreateCount();
    const uint64_t pipes1 = GpuPipelineCreateCount();
    for (int i = 0; i < 3; i++) Draw(b.blender);
    EXPECT_EQ(gWgpuRnd->TexBlendsComposed() - composed0, 4) << "not redrawn every frame";
    EXPECT_EQ(GpuBindGroupCreateCount() - groups1, (uint64_t)0) << "bind groups created per frame";
    EXPECT_EQ(GpuPipelineCreateCount() - pipes1, (uint64_t)0);
}

// The general half of the defect: a rect drawn while a texture is the target
// is in the target's pixels (DxRnd::DrawRect, viewport disabled), not the
// virtual screen's. This is the decomp's own base step -- the default camera
// on the output, SetupMaterial's work material, DrawRect(0, 0, W, H) -- drawn
// through TheNgRnd, as the decomp body of RndTexBlender::DrawShowing does.
// Sabotage: kRectTargetPixels false in RndShape_DC3.h -> fails, 3066 of 4096
// texels wrong (the rect lands in the top-left 64/1280 x 64/720 of the target).
TEST_F(TexBlenderComposeTest, RectIntoATargetIsInTheTargetsPixels) {
    RndTex *output = Hmx::Object::New<RndTex>();
    output->SetBitmap(kSize, kSize, 32, RndTex::kRenderedNoZ, false, nullptr);
    RndTex *base = MakeTex(true);
    TheRnd.BeginDrawing();
    RndCam *cam = TheRnd.GetDefaultCam();
    RndCam *prev = RndCam::Current();
    cam->SetTargetTex(output);
    cam->Select();
    RndMat *work = TheShaderMgr.GetWork();
    work->SetZMode(kZModeDisable);
    work->SetBlend(RndMat::kBlendSrc);
    work->SetCull(kCullNone);
    work->SetTexWrap(kTexWrapClamp);
    work->SetDiffuseTex(base);
    work->SetAlpha(1);
    TheNgRnd.DrawRect(
        Hmx::Rect(0, 0, kSize, kSize), work, (ShaderType)6, Hmx::Color(1, 1, 1), nullptr, nullptr
    );
    cam->SetTargetTex(nullptr);
    if (prev) prev->Select();
    TheRnd.EndDrawing();

    Texels t = Read(output);
    ASSERT_TRUE(t.ok);
    ASSERT_EQ(t.w, kSize);
    int wrong = 0, firstX = -1, firstY = -1;
    for (int y = 0; y < kSize; y++)
        for (int x = 0; x < kSize; x++) {
            unsigned char want[4];
            BaseTexel(x, y, want);
            const uint8_t *got = t.At(x, y);
            if ((got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) && wrong++ == 0) {
                firstX = x;
                firstY = y;
            }
        }
    EXPECT_EQ(wrong, 0) << "rect texels that are not the base map's (first at " << firstX << ","
                        << firstY << ")";
}

#endif // MILO_READBACK_GPU_RENDER_TARGET
