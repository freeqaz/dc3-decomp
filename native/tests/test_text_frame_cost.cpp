// test_text_frame_cost.cpp -- what one frame of unchanged text costs the
// engine (np-gfx spotperf lane round 2, branch np-framecost, 2026-11-15).
//
// RndText builds one mesh per glyph page (FontMap::AllocateMeshes: unnamed,
// marked with MarkTextGlyphMesh) and writes its vertices only when the text is
// rebuilt: ConstructMeshes -> FontMap::CleanupSyncMeshes -> RndMesh::Sync, and
// Sync -> OnSync -> InvalidateGpuMesh (src/system/rndobj/Mesh.cpp, HX_NATIVE).
// A scroll or a marquee moves only the page's transform (UpdateScrolling,
// DrawMesh's offset copies). The engine's EnsureMeshUploaded nevertheless took
// every unnamed mesh for a glyph page that had to be re-read on every call, so
// every page of every visible text was unpacked and re-uploaded -- a new
// vertex and index buffer, the old pair released -- every frame: ~5.7 a frame
// in rollerrink gameplay, ~13 a frame on average over menus, song select, a
// song and results (milo-native-engine docs/native/dc3-backend-for-rb3-wii.md
// section 38).
//
// This pins it: N frames of unchanged text upload nothing after the first;
// moving a text uploads nothing; changing one text's string uploads exactly
// that text's pages, on the next frame only.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round2):
//  - the per-call re-read of unnamed meshes put back in UploadMesh -> the
//    steady frames upload every page of every text;
//  - InvalidateGpuMesh removed from RndMesh::OnSync -> the changed string
//    uploads nothing (the GPU keeps drawing the old string).
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter), and the
// archive's UI fonts (skips without them).

#include "test_helpers.h"

#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "obj/Object.h"
#include "platform/MeshGpuCache.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Cam.h"
#include "rndobj/Font.h"
#include "rndobj/Mesh.h"
#include "rndobj/Rnd.h"
#include "rndobj/Tex.h"
#include "rndobj/Text.h"
#include "utl/FilePath.h"

#include <list>
#include <string>
#include <vector>

namespace {

class TextFrameCostTest : public EngineTestFixture {
protected:
    static bool HaveGpu() { return gWgpuRnd && gWgpuRnd->Gpu().IsReady(); }
};

int PageMeshes(RndText *text) {
    int n = 0;
    for (std::list<RndTransformable *>::const_iterator it = text->Children().begin();
         it != text->Children().end(); ++it) {
        RndMesh *mesh = dynamic_cast<RndMesh *>(*it);
        if (mesh && mesh->Showing()) n++;
    }
    return n;
}

} // namespace

TEST_F(TextFrameCostTest, UnchangedTextUploadsNothingAfterItsFirstFrame) {
    if (IsSkipped()) return;
    if (!HaveGpu()) GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    ObjectDir *fonts =
        DirLoader::LoadObjects(FilePath("ui/resource/fonts/gen/default.milo_xbox"), nullptr, nullptr);
    if (!fonts) GTEST_SKIP() << "no archive UI fonts (ui/resource/fonts/gen/default.milo_xbox)";
    RndFont *font = nullptr;
    for (ObjDirItr<RndFont> it(fonts, true); it != nullptr; ++it) {
        font = it;
        break;
    }
    ASSERT_NE(font, nullptr) << "no RndFont in " << fonts->Name();

    RndTex *target = Hmx::Object::New<RndTex>();
    target->SetBitmap(128, 64, 32, RndTex::kRendered, false, nullptr);
    RndCam *cam = Hmx::Object::New<RndCam>();
    cam->SetFrustum(1.0f, 1000.0f, 1.0f, 0.75f);
    cam->SetTargetTex(target);

    // Three texts, as a HUD has: a label, a score counter, a name.
    const char *strings[3] = {"Ab", "0123", "Flashcard"};
    std::vector<RndText *> texts;
    for (int i = 0; i < 3; i++) {
        RndText *t = Hmx::Object::New<RndText>();
        DataArrayPtr fontProp(Symbol("styles"), 0, Symbol("font"));
        t->SetProperty(fontProp, DataNode(font));
        t->SetTextASCII(strings[i]);
        t->UpdateText(); // Load/Copy/the update_text handler do this; SetText does not
        t->SetLocalPos(Vector3(-40.0f + 30.0f * i, 200.0f, 0.0f));
        texts.push_back(t);
    }
    std::vector<int> pages;
    int allPages = 0;
    for (RndText *t : texts) {
        pages.push_back(PageMeshes(t));
        allPages += pages.back();
        ASSERT_GT(pages.back(), 0) << "the text built no page mesh (font " << font->Name() << ")";
    }

    struct Cost {
        uint64_t meshUploads = 0;
        uint64_t texCreates = 0;
    };
    auto frame = [&]() {
        Cost c;
        const uint64_t up0 = GpuMeshUploadCount();
        const uint64_t tex0 = GetGpuTexDebugInfo(nullptr).createCount;
        TheRnd.BeginDrawing();
        RndCam *prev = RndCam::Current();
        cam->Select();
        for (RndText *t : texts) t->DrawShowing();
        if (prev) prev->Select();
        TheRnd.EndDrawing();
        c.meshUploads = GpuMeshUploadCount() - up0;
        c.texCreates = GetGpuTexDebugInfo(nullptr).createCount - tex0;
        return c;
    };
    std::string seq;
    auto note = [&](const Cost &c) { seq += std::to_string(c.meshUploads) + " "; };

    // Non-vacuous: the first frame reaches the GPU mesh cache, once per page.
    const Cost first = frame();
    note(first);
    EXPECT_EQ(first.meshUploads, (uint64_t)allPages)
        << "the first frame uploads each glyph page once (" << allPages << " pages)";
    for (int f = 1; f < 10; f++) {
        const Cost c = frame();
        note(c);
        EXPECT_EQ(c.meshUploads, 0u) << "steady frame " << f << " re-uploaded "
                                     << c.meshUploads << " glyph page(s); per frame: " << seq;
        EXPECT_EQ(c.texCreates, 0u) << "steady frame " << f << " created a texture";
    }

    // A scroll or a marquee moves the text, not its glyphs.
    texts[0]->SetLocalPos(Vector3(-35.0f, 200.0f, 5.0f));
    const Cost moved = frame();
    note(moved);
    EXPECT_EQ(moved.meshUploads, 0u) << "moving a text re-uploaded its pages; per frame: " << seq;

    // A changed string reaches the GPU: exactly the changed text's pages, once.
    texts[1]->SetTextASCII("4567");
    texts[1]->UpdateText();
    const Cost changed = frame();
    note(changed);
    EXPECT_EQ(changed.meshUploads, (uint64_t)pages[1])
        << "the changed score text has " << pages[1] << " page(s); per frame: " << seq;
    const Cost after = frame();
    note(after);
    EXPECT_EQ(after.meshUploads, 0u) << "and not again; per frame: " << seq;
    printf("[TextFrameCost] pages %d+%d+%d; uploads/frame: %s\n", pages[0], pages[1], pages[2],
           seq.c_str());

    for (RndText *t : texts) delete t;
    delete cam;
    delete target;
    delete fonts;
}
