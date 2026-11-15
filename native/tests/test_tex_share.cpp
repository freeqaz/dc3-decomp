// test_tex_share.cpp -- RndTexes with byte-identical bitmaps share one GPU
// texture, and sharing never changes what a holder samples (np-gfx spotperf
// lane round 2, branch np-framecost, 2026-11-15).
//
// A flashcard, a results row, a dancer's eyes are copies of one object, each
// carrying its own copy of the same pixels, and each used to become its own
// GPU texture on first draw (milo-native-engine
// docs/native/dc3-backend-for-rb3-wii.md section 38.3). Tex_Wgpu.cpp now keys
// kRegular bitmap textures by everything CreateFromBitmap is given. This pins:
//  - a twin shares the first texture: no create, the same view;
//  - one differing byte gets its own texture;
//  - the first holder going away leaves the twin's texture alive, and a later
//    twin still finds it; when the last holder goes the entry goes;
//  - a holder whose bitmap changes gets a texture of its own, and its twin's
//    texture, READ BACK FROM THE GPU, is unchanged;
//  - a texture of another type (kRegularLinear) with the same bytes does not
//    share.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round2):
//  - the hit disabled -> the twin creates, red;
//  - the owner hand-off in ReleaseBitmapTexture removed -> the twin made
//    after the first holder's delete creates, red;
//  - write-through: a re-synced holder copies its new texture into the
//    shared one -> the twin's read-back changes, red;
//  - the type check and the type in the key removed -> the kRegularLinear
//    twin shares, red.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/TextureConvert.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"

#include <vector>

namespace {

const int kSize = 64;

std::vector<unsigned char> Pixels(int seed, int poke = -1) {
    std::vector<unsigned char> px(kSize * kSize * 4);
    for (int i = 0; i < kSize * kSize; i++) {
        px[i * 4 + 0] = (unsigned char)(i * 7 + seed);
        px[i * 4 + 1] = (unsigned char)(i * 13 + seed * 3);
        px[i * 4 + 2] = (unsigned char)(i * 29);
        px[i * 4 + 3] = 0xFF;
    }
    if (poke >= 0) px[poke] ^= 0x01;
    return px;
}

void SetPixels(RndTex *tex, const std::vector<unsigned char> &px, bool mips,
               RndTex::Type type = RndTex::kRegular) {
    RndBitmap bmp;
    bmp.Create(kSize, kSize, 0, 32, 0, nullptr, (void *)px.data(), nullptr);
    if (mips) bmp.GenerateMips();
    tex->SetBitmap(bmp, nullptr, true, type);
    tex->PresyncBitmap();
}

// A 64x64 32 bpp texture; `seed` picks the pixels, `poke` flips one byte.
RndTex *Make(int seed, int poke = -1, bool mips = true, RndTex::Type type = RndTex::kRegular) {
    RndTex *tex = Hmx::Object::New<RndTex>();
    SetPixels(tex, Pixels(seed, poke), mips, type);
    return tex;
}

unsigned long long Creates() { return GetGpuTexDebugInfo(nullptr).createCount; }
unsigned long long Shares() { return GetGpuTexDebugInfo(nullptr).shareCount; }

class TexShareTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
        TextureConvert::SetBitmapTexturesCopyableForTests(true);
    }
    void TearDown() override {
        TextureConvert::SetBitmapTexturesCopyableForTests(false);
        EngineTestFixture::TearDown();
    }

    // Base level of the texture behind `tex`, as the GPU holds it.
    static std::vector<unsigned char> ReadBack(RndTex *tex) {
        std::vector<unsigned char> out(kSize * kSize * 4);
        wgpu::Texture t = GetGpuTexTexture(tex);
        EXPECT_TRUE(t) << "no GPU texture";
        if (!t) return {};
        EXPECT_TRUE(gWgpuRnd->Gpu().ReadbackTexture(t, kSize, kSize, out.data(), out.size()));
        return out;
    }
};

} // namespace

TEST_F(TexShareTest, ByteIdenticalBitmapsShareOneTexture) {
    if (IsSkipped()) return;
    const int seed = 41; // pixels no other test in the process uploads
    const unsigned long long c0 = Creates(), s0 = Shares();
    RndTex *a = Make(seed);
    GpuTexDebugInfo ia = GetGpuTexDebugInfo(a);
    ASSERT_TRUE(ia.uploaded);
    EXPECT_EQ(Creates() - c0, 1u) << "the first copy creates";

    RndTex *b = Make(seed);
    GpuTexDebugInfo ib = GetGpuTexDebugInfo(b);
    ASSERT_TRUE(ib.uploaded);
    EXPECT_EQ(Creates() - c0, 1u) << "the twin created its own texture";
    EXPECT_EQ(Shares() - s0, 1u);
    EXPECT_EQ(ib.view, ia.view) << "the twin does not draw from the first copy's view";
    EXPECT_EQ(ib.texture, ia.texture);

    RndTex *c = Make(seed, 1000);
    GpuTexDebugInfo ic = GetGpuTexDebugInfo(c);
    ASSERT_TRUE(ic.uploaded);
    EXPECT_EQ(Creates() - c0, 2u) << "one differing byte must get its own texture";
    EXPECT_NE(ic.view, ia.view);

    // The first holder goes; the twin keeps the texture, and a later copy
    // still finds it.
    delete a;
    GpuTexDebugInfo ib2 = GetGpuTexDebugInfo(b);
    EXPECT_TRUE(ib2.uploaded);
    EXPECT_EQ(ib2.view, ib.view) << "deleting the first copy took the twin's texture";
    RndTex *d = Make(seed);
    EXPECT_EQ(Creates() - c0, 2u) << "a copy made after the first holder's delete created";
    EXPECT_EQ(GetGpuTexDebugInfo(d).view, ib.view);

    // The last holders go: the entry goes with them, so the next copy creates.
    delete b;
    delete d;
    const unsigned long long c1 = Creates();
    RndTex *e = Make(seed);
    EXPECT_TRUE(GetGpuTexDebugInfo(e).uploaded);
    EXPECT_EQ(Creates() - c1, 1u) << "a copy made after every holder's delete did not create";

    delete c;
    delete e;
}

TEST_F(TexShareTest, AHolderWhoseBitmapChangesLeavesItsTwinsPixelsAlone) {
    if (IsSkipped()) return;
    const int seed = 77;
    RndTex *a = Make(seed);
    RndTex *b = Make(seed);
    ASSERT_EQ(GetGpuTexDebugInfo(a).texture, GetGpuTexDebugInfo(b).texture)
        << "the twins do not share; nothing below would be tested";
    const std::vector<unsigned char> before = ReadBack(b);
    ASSERT_FALSE(before.empty());

    // Each holder in turn takes new pixels of the same size and format: the
    // reference holder (a), then, after a fresh twin joins, the other one.
    for (int round = 0; round < 2; round++) {
        RndTex *changer = round == 0 ? a : b;
        RndTex *keeper = round == 0 ? b : a;
        if (round == 1) {
            // Re-pair: a takes the original pixels back and shares with b.
            SetPixels(a, Pixels(seed), true);
            ASSERT_EQ(GetGpuTexDebugInfo(a).texture, GetGpuTexDebugInfo(b).texture);
        }
        const std::vector<unsigned char> keeperBefore = ReadBack(keeper);
        EXPECT_EQ(keeperBefore, before);
        SetPixels(changer, Pixels(seed + 100), true);
        EXPECT_NE(GetGpuTexDebugInfo(changer).texture, GetGpuTexDebugInfo(keeper).texture)
            << "round " << round << ": the changed holder still shares";
        const std::vector<unsigned char> changed = ReadBack(changer);
        EXPECT_NE(changed, before) << "round " << round
                                   << ": the new pixels never reached the GPU (vacuous)";
        EXPECT_EQ(ReadBack(keeper), before)
            << "round " << round << ": the twin's texture changed when its partner re-synced";
    }
    delete a;
    delete b;
}

TEST_F(TexShareTest, OnlyRegularTexturesShare) {
    if (IsSkipped()) return;
    const int seed = 93;
    const unsigned long long c0 = Creates();
    RndTex *regular = Make(seed, -1, false, RndTex::kRegular);
    RndTex *linear = Make(seed, -1, false, RndTex::kRegularLinear);
    ASSERT_TRUE(GetGpuTexDebugInfo(regular).uploaded);
    ASSERT_TRUE(GetGpuTexDebugInfo(linear).uploaded);
    EXPECT_EQ(Creates() - c0, 2u) << "a kRegularLinear texture shared a kRegular one";
    EXPECT_NE(GetGpuTexDebugInfo(regular).texture, GetGpuTexDebugInfo(linear).texture);
    RndTex *twin = Make(seed, -1, false, RndTex::kRegular);
    EXPECT_EQ(GetGpuTexDebugInfo(twin).texture, GetGpuTexDebugInfo(regular).texture)
        << "control: the kRegular twin should share";
    delete regular;
    delete linear;
    delete twin;
}
