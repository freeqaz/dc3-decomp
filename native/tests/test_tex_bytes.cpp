// test_tex_bytes.cpp -- [GPUCOUNT] texBytes / texBytesPeak, the live GPU
// memory of the bitmap textures and its peak (np-gfx spotperf round 6, branch
// np-texwarm, PLAN H-116; Tex_Wgpu.cpp GpuTexBitmapBytes /
// GpuTexBitmapBytesPeak).
//
// Round 6 measured what creating textures at load instead of at their first
// draw would cost in GPU memory (desktop and web) with these two numbers, and
// they stay for the next lane. This pins what they count:
//  - a created texture adds every mip level it has, at its format's size (a
//    64x64 32 bpp texture after RndBitmap::GenerateMips, which stops at 16x16:
//    4 x (64^2 + 32^2 + 16^2) bytes);
//  - a byte-identical twin (a share hit, test_tex_share.cpp) adds nothing:
//    a shared texture is counted once;
//  - the texture's bytes stay while any holder is left and go with the last;
//  - the peak is the most the live count has been, so it does not drop when
//    textures go.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/spotperf/round6):
//  - a share hit counting its own bytes -> the twin adds, red;
//  - only the top mip counted -> 16384 bytes, red;
//  - the bytes left in place when the last holder goes -> red;
//  - the peak taken from the live count when it is read -> drops, red.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/Tex.h"

#include <vector>

namespace {

const int kSize = 64;
// 4 bytes a texel over the mip chain GenerateMips makes: 64x64, 32x32, 16x16.
const unsigned long long kChainBytes = 4ull * (64 * 64 + 32 * 32 + 16 * 16);

RndTex *Make(int seed) {
    std::vector<unsigned char> px(kSize * kSize * 4);
    for (int i = 0; i < kSize * kSize; i++) {
        px[i * 4 + 0] = (unsigned char)(i * 3 + seed);
        px[i * 4 + 1] = (unsigned char)(i * 17 + seed * 5);
        px[i * 4 + 2] = (unsigned char)(i * 23 + seed * 11);
        px[i * 4 + 3] = 0xFF;
    }
    RndBitmap bmp;
    bmp.Create(kSize, kSize, 0, 32, 0, nullptr, (void *)px.data(), nullptr);
    bmp.GenerateMips();
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(bmp, nullptr, true, RndTex::kRegular);
    tex->PresyncBitmap();
    return tex;
}

class TexBytesTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
};

} // namespace

TEST_F(TexBytesTest, ATextureCountsItsMipChainAndATwinNothing) {
    const unsigned long long base = GpuTexBitmapBytes();
    RndTex *a = Make(41);
    ASSERT_EQ(GpuTexBitmapBytes() - base, kChainBytes) << "one 64x64 texture with its mips";
    RndTex *twin = Make(41);
    EXPECT_EQ(GpuTexBitmapBytes() - base, kChainBytes) << "a share hit is the same texture";
    RndTex *other = Make(42);
    EXPECT_EQ(GpuTexBitmapBytes() - base, 2 * kChainBytes);
    EXPECT_GE(GpuTexBitmapBytesPeak(), GpuTexBitmapBytes());
    delete other;
    delete twin;
    delete a;
    EXPECT_EQ(GpuTexBitmapBytes(), base);
}

TEST_F(TexBytesTest, BytesStayWhileAHolderIsLeftAndThePeakDoesNotDrop) {
    const unsigned long long base = GpuTexBitmapBytes();
    RndTex *a = Make(43);
    RndTex *twin = Make(43);
    RndTex *b = Make(44);
    const unsigned long long top = GpuTexBitmapBytes();
    ASSERT_EQ(top - base, 2 * kChainBytes);
    const unsigned long long peak = GpuTexBitmapBytesPeak();
    ASSERT_GE(peak, top);
    delete a;  // the twin still holds the shared texture
    EXPECT_EQ(GpuTexBitmapBytes() - base, 2 * kChainBytes);
    delete twin;
    EXPECT_EQ(GpuTexBitmapBytes() - base, kChainBytes);
    delete b;
    EXPECT_EQ(GpuTexBitmapBytes(), base);
    EXPECT_EQ(GpuTexBitmapBytesPeak(), peak) << "the peak keeps the most there has been";
}
