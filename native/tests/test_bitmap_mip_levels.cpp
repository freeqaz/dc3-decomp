// test_bitmap_mip_levels.cpp -- a regular texture reaches the GPU with at most
// two mip levels, as DC3's DxTex::SyncBitmap creates it.
//
// The original (dc3 src/system/rnddx9/Tex.cpp, DxTex::SyncBitmap, regular
// branch; target asm at 0x82614738):
//
//     bl     RndBitmap::NumMips
//     addi   r24, r3, 0x1          ; numLevels = NumMips() + 1
//     cmpwi  cr6, r24, 0x2
//     ble    cr6, +8
//     li     r24, 0x2              ; if (numLevels > 2) numLevels = 2
//
// and numLevels is what both D3DDevice_CreateTexture and
// TheDxTexMgr.CreateSurface receive, and what the XGTileTextureLevel loop
// uploads. (The other clamp on the same lines, MemUseLowestMip, keeps only the
// smallest mip; retail config/mem.dta sets use_lowest_mip FALSE, so it never
// runs.) The live fetch constants of the running original under Xenia agree:
// every bitmap sampler in a dci song reads mip_max_level <= 1.
//
// The engine used to upload the whole authored chain, so a minified texture
// blended in mips 2.. the original never has: props read ~0.65-0.8 of the
// original's sharpness and the 1024x1024 song-select font atlas (6 authored
// mips, drawn ~2.5x minified) lost half its 1-px stroke intensity.
//
// Expected levels are min(NumMips() + 1, 2) -- the original's rule, not a
// number read off native. It is the DC3_LOOK=retail rule (LookSettings
// bitmapMipLevels 2), so both tests run under that profile; the enhanced
// look's whole-chain upload is test_look_sampling.cpp. Needs a GPU (skips
// without one); the asset half needs the game data.

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "obj/Dir.h"
#include "obj/DirLoader.h"
#include "os/File.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"
#include "utl/FilePath.h"

#include <algorithm>
#include <vector>

namespace {

int OriginalLevels(const RndBitmap &bmp) { return std::min(bmp.NumMips() + 1, 2); }

class BitmapMipLevelsTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    // A kRegular texture whose bitmap is `size` square, 32 bpp, with the mip
    // chain RndBitmap::GenerateMips authors (halving down to 16).
    static RndTex *MakeRegular(int size, bool mips, int &authoredMips) {
        std::vector<unsigned char> px(size * size * 4);
        for (int i = 0; i < size * size; i++) {
            px[i * 4 + 0] = (unsigned char)(i * 7);
            px[i * 4 + 1] = (unsigned char)(i * 13);
            px[i * 4 + 2] = (unsigned char)(i * 29);
            px[i * 4 + 3] = 0xFF;
        }
        RndBitmap bmp;
        bmp.Create(size, size, 0, 32, 0, nullptr, px.data(), nullptr);
        if (mips) bmp.GenerateMips();
        authoredMips = bmp.NumMips();
        RndTex *tex = Hmx::Object::New<RndTex>();
        tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
        tex->PresyncBitmap();
        return tex;
    }
};

} // namespace

// Sabotage: CreateFromBitmap's maxLevels clamp removed (or the DC3 shape's
// kBitmapMaxMipLevels set to 0) -- the 256 texture uploads 5 levels, red.
TEST_F(BitmapMipLevelsTest, ARegularTextureGetsBaseAndOneMipAtMost) {
    if (IsSkipped()) return;
    milo::ScopedLookOverride retail(milo::LookProfile::Retail);
    struct Case {
        int size;
        bool mips;
    } cases[] = {{256, true}, {64, true}, {32, true}, {64, false}};
    for (const Case &c : cases) {
        int authored = -1;
        RndTex *tex = MakeRegular(c.size, c.mips, authored);
        GpuTexDebugInfo info = GetGpuTexDebugInfo(tex);
        ASSERT_TRUE(info.uploaded) << c.size << "x" << c.size << " did not upload";
        if (c.mips) ASSERT_GE(authored, 1) << "GenerateMips authored no chain";
        EXPECT_EQ(info.mipLevels, std::min(authored + 1, 2))
            << c.size << "x" << c.size << " with " << authored
            << " authored mips: the original creates min(NumMips()+1, 2) levels";
        delete tex;
    }
}

// The same rule over every texture the game ships in two directories that
// carry authored chains: the song-select font atlases and the shared camera
// set. At least one texture must have 2+ authored mips, or the test proves
// nothing.
TEST_F(BitmapMipLevelsTest, ShippedTexturesKeepTheOriginalsLevelCount) {
    if (IsSkipped()) return;
    milo::ScopedLookOverride retail(milo::LookProfile::Retail);
    const char *milos[] = {"ui/resource/fonts/default.milo", "world/shared/camera.milo"};
    int checked = 0, deep = 0;
    for (const char *path : milos) {
        ObjectDir *dir = DirLoader::LoadObjects(FilePath(FileRoot(), path), nullptr, nullptr);
        if (!dir) continue;
        for (ObjDirItr<RndTex> it(dir, true); it; ++it) {
            RndTex *tex = it;
            if (tex->GetType() != RndTex::kRegular) continue;
            const RndBitmap &bmp = tex->Bitmap();
            if (!bmp.Pixels() || bmp.Width() <= 0) continue;
            int want = OriginalLevels(bmp);
            int authored = bmp.NumMips();
            tex->PresyncBitmap();
            GpuTexDebugInfo info = GetGpuTexDebugInfo(tex);
            if (!info.uploaded) continue;
            checked++;
            if (authored >= 2) deep++;
            EXPECT_EQ(info.mipLevels, want)
                << path << ":" << tex->Name() << " " << bmp.Width() << "x" << bmp.Height()
                << " with " << authored << " authored mips";
        }
    }
    if (checked == 0) GTEST_SKIP() << "no shipped texture loadable (game data missing?)";
    EXPECT_GT(deep, 0) << "no shipped texture with 2+ authored mips was examined; "
                          "the cap was not exercised";
}
