// test_texrenderer_clear.cpp -- a TexRenderer's clear reaches its target
// (PLAN P-4, photos; branch np-media).
//
// RndTexRenderer::DrawToTexture selects its camera on the output texture and,
// with clear_buffer set, calls TheRnd.Clear(1, clear_color) -- alpha from
// clear_alpha -- before drawing.  On the 360 that is DxRnd::Clear, an
// immediate D3DDevice_Clear of the bound target (DxRnd::Clear and DxCam::Select
// are 100% under name_check; a rendered DxTex is A8R8G8B8 whatever bpp it was
// made with, DxTex::SyncBitmap).  DxCam::Select's own clear of a rendered
// target is colour 0x00000000, so the TexRenderer's clear is what decides the
// alpha of anything its drawable does not write alpha over.
//
// That is every photo the game takes: ui/augmented_photo.milo's
// TexRenderer.rndtex has clear_buffer 1, clear_alpha 1, and all six of its
// materials have alpha_write 0.  The results screen shows the snapshots through
// result_photoshare_photo1..3.mat, blend kBlendSrcAlpha -- so a snapshot with
// alpha 0 is an invisible photo.  WgpuRnd::Clear used to only remember the
// colour for the next frame pass, leaving every texture target at the
// transparent black its pass was opened with: the snapshot target measured
// alpha 0 everywhere (POSE_INPUT.md, Player on screen).
//
// Needs a GPU (skips without one) and the game data.  Compiled only against an
// engine that clears bound targets and can read one back
// (MILO_READBACK_GPU_RENDER_TARGET, ~/tmp/np-media-engine.patch).
//
// Sabotage (np-media, scratch engine): WgpuRnd::Clear back to only storing the
// colour -> both tests fail (alpha 0, colour 0).

#include "test_helpers.h"

#include "platform/Rnd_Wgpu.h"

#ifdef MILO_READBACK_GPU_RENDER_TARGET

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "rndobj/Cam.h"
#include "rndobj/Dir.h"
#include "rndobj/Group.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/TexRenderer.h"
#include "utl/FilePath.h"

#include <vector>

namespace {

class TexRendererClearTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    // The snapshot target exactly as LiveCameraInput::InitSnapshots makes it.
    static RndTex *NewTarget(int w, int h) {
        RndTex *tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(w, h, 16, RndTex::kRendered, false, nullptr);
        return tex;
    }

    struct Texels {
        bool ok = false;
        int w = 0, h = 0;
        std::vector<uint8_t> px;
        int Count(int channel, uint8_t value) const {
            int n = 0;
            for (size_t i = channel; i < px.size(); i += 4) n += px[i] == value;
            return n;
        }
    };

    static Texels Read(RndTex *tex) {
        Texels t;
        t.ok = ReadbackGpuRenderTarget(tex, t.px, t.w, t.h);
        return t;
    }
};

} // namespace

// A TexRenderer whose drawable draws nothing: the target is exactly its clear.
// Magenta at alpha 1 reads the same in an RGBA or a BGRA target and is not
// moved by sRGB encoding (0 and 1 are fixed points).
TEST_F(TexRendererClearTest, ClearBufferFillsTheTargetWithClearColourAndAlpha) {
    RndTex *tex = NewTarget(64, 48);
    RndCam *cam = Hmx::Object::New<RndCam>();
    RndGroup *nothing = Hmx::Object::New<RndGroup>();
    RndTexRenderer *tr = Hmx::Object::New<RndTexRenderer>();
    tr->SetProperty("output_texture", DataNode(tex));
    tr->SetProperty("draw", DataNode(nothing));
    tr->SetProperty("cam", DataNode(cam));
    tr->SetProperty("draw_pre_clear", DataNode(0));
    tr->SetProperty("clear_buffer", DataNode(1));
    tr->SetProperty("clear_color", DataNode(Hmx::Color(1.0f, 0.0f, 1.0f).Pack()));
    tr->SetProperty("clear_alpha", DataNode(1.0f));

    TheRnd.BeginDrawing();
    tr->DrawShowing();
    TheRnd.EndDrawing();

    Texels t = Read(tex);
    ASSERT_TRUE(t.ok) << "the output texture has no GPU render target";
    const int n = t.w * t.h;
    ASSERT_EQ(n, 64 * 48);
    EXPECT_EQ(t.Count(3, 255), n) << "clear_alpha 1 did not reach the target's alpha";
    EXPECT_EQ(t.Count(0, 255), n) << "clear_color red did not reach the target";
    EXPECT_EQ(t.Count(1, 0), n) << "clear_color green did not reach the target";
    EXPECT_EQ(t.Count(2, 255), n) << "clear_color blue did not reach the target";

    // clear_buffer off: the target keeps the select's transparent black (what
    // impostor/billboard targets alpha-cut against).
    tr->SetProperty("clear_buffer", DataNode(0));
    tr->SetProperty("prime_draw", DataNode(0)); // re-dirty (output_texture does not)
    TheRnd.BeginDrawing();
    tr->DrawShowing();
    TheRnd.EndDrawing();
    Texels off = Read(tex);
    ASSERT_TRUE(off.ok);
    EXPECT_EQ(off.Count(3, 0), n) << "without clear_buffer the target should stay transparent";

    delete tr;
    delete nothing;
    delete cam;
    delete tex;
}

// The game's own photo: ui/augmented_photo.milo rendered into a snapshot-shaped
// target the way HamUI::Draw does it (Poll + DrawShowing of the dir) must come
// out opaque everywhere, because result_photoshare_photo*.mat show it with
// SrcAlpha blending.
TEST_F(TexRendererClearTest, TheAugmentedPhotoIsOpaque) {
    ObjectDir *dir = DirLoader::LoadObjects(FilePath(FileRoot(), "ui/augmented_photo.milo"),
                                            nullptr, nullptr);
    RndDir *photo = dynamic_cast<RndDir *>(dir);
    ASSERT_NE(photo, nullptr) << "ui/augmented_photo.milo did not load as a RndDir";
    RndTexRenderer *tr = photo->Find<RndTexRenderer>("TexRenderer.rndtex", false);
    ASSERT_NE(tr, nullptr);
    // The authored state this test is about.
    ASSERT_EQ(tr->Property("clear_buffer", true)->Int(), 1);
    ASSERT_FLOAT_EQ(tr->Property("clear_alpha", true)->Float(), 1.0f);

    RndTex *snapshot = NewTarget(640, 480);
    tr->SetProperty("output_texture", DataNode(snapshot));
    tr->SetProperty("prime_draw", DataNode(0)); // dirty, as a Poll would leave it
    TheRnd.BeginDrawing();
    photo->Poll();
    photo->DrawShowing();
    TheRnd.EndDrawing();

    Texels t = Read(snapshot);
    ASSERT_TRUE(t.ok) << "the snapshot has no GPU render target";
    const int n = t.w * t.h;
    ASSERT_EQ(n, 640 * 480);
    const int opaque = t.Count(3, 255);
    printf("[texrenderer-clear] augmented photo: %d of %d texels opaque\n", opaque, n);
    EXPECT_EQ(opaque, n) << "the photo has transparent texels; the results screen's SrcAlpha "
                            "photo materials would not show them";

    tr->SetProperty("output_texture", DataNode((Hmx::Object *)nullptr));
    delete snapshot;
    delete photo;
}

#endif // MILO_READBACK_GPU_RENDER_TARGET
