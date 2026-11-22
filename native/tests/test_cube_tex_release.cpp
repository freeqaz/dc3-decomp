// test_cube_tex_release.cpp -- a cube map's GPU copy goes with the cube map,
// and a cube map whose faces change uploads again (np-gfx item 58, branch
// np-cube58, 2026-11-22).
//
// Tex_Wgpu.cpp keeps each RndCubeTex's GPU texture in sCubeTexGpuData, keyed
// on the object's ADDRESS, uploaded lazily on the first draw
// (GetGpuCubeTexView) and, until this item, erased only at shutdown. RndTex
// has the same kind of table and RndTex::~RndTex releases its entry
// (src/system/rndobj/Tex.cpp); RndCubeTex had no destructor, and natively its
// Sync()/Reset() are the empty base versions. So every unloaded venue's or
// character's cube maps stayed on the GPU until exit, a new cube map the
// allocator placed at a dead one's address was handed the dead one's view,
// and a cube map whose faces changed after its first draw kept the old ones.
// This pins:
//  - a cube map drawn once adds one texture and one holder; deleting it takes
//    both back and leaves no entry for its address;
//  - a cube map built at a dead cube map's address (deterministically: placement
//    new into the same storage) uploads its own faces: its own mip count, a
//    view that is not the dead cube map's;
//  - one face resized after the first draw (the faces disagree: the Reset()
//    branch of Update) leaves no GPU copy; all six resized upload again at
//    the new size; one face's pixels changed at the same size (UpdateFace, the
//    Sync() branch of Update) gives a new view.
// The mip count is the observable: under the Retail look a cube map uploads
// every level its faces carry (LookSettings::cubeMipChain), so a 64x64 face
// chain is 7 levels and a 16x16 one 5. View identity is compared while the
// earlier view is still held, so the allocator cannot hand its handle on.
//
// Sabotage (dc3-decomp archive/np-gfx-2026-11-14/lanes/sharp/item58): the
// destructor hook removed; CleanupGpuCubeTex erasing nothing; the cleanup in
// Update() removed. Each red, see the README there.
//
// GPU: needs a device (skips under MILO_NORENDER or with no adapter).

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "platform/LookProfile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/TexGpu.h"
#include "rndobj/Bitmap.h"
#include "rndobj/CubeTex.h"

#include <new>
#include <vector>

namespace {

// A 32 bpp size x size face with its mip chain; `seed` picks the pixels.
void FillFace(RndBitmap &bmp, int size, int seed) {
    std::vector<unsigned char> px(size * size * 4);
    for (int i = 0; i < size * size; i++) {
        px[i * 4 + 0] = (unsigned char)(i * 7 + seed);
        px[i * 4 + 1] = (unsigned char)(i * 13 + seed * 3);
        px[i * 4 + 2] = (unsigned char)(i * 29);
        px[i * 4 + 3] = 0xFF;
    }
    bmp.Create(size, size, 0, 32, 0, nullptr, px.data(), nullptr);
    bmp.GenerateMips();
}

// Six faces through the object's own path (UpdateFace -> Update, as a load or
// a set_bitmap ends).
void SetFaces(RndCubeTex *cube, int size, int seed) {
    for (int f = 0; f < RndCubeTex::kNumCubeFaces; f++) {
        FillFace(cube->GetBitmap((RndCubeTex::CubeFace)f), size, seed + f * 31);
        cube->UpdateFace((RndCubeTex::CubeFace)f);
    }
}

// Levels a cube map's faces carry (base + mips), which the Retail look
// uploads whole (DxCubeTex::Sync: mNumMips + 1).
int Levels(RndCubeTex *cube) { return cube->GetBitmap(RndCubeTex::kCubeFaceRight).NumMips() + 1; }

// RndCubeTex's constructor is protected (objects come from the factory); this
// lets the test build one in storage it owns, so two cube maps share an
// address by construction rather than by the allocator's choice.
struct PlacedCube : public RndCubeTex {
    PlacedCube() {}
};

class CubeTexReleaseTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
};

} // namespace

TEST_F(CubeTexReleaseTest, DeleteReleasesTheGpuCopy) {
    if (IsSkipped()) return;
    milo::ScopedLookOverride look(milo::LookProfile::Retail);
    const size_t tex0 = GpuTexCount(), hold0 = GpuTexHolderCount();
    RndCubeTex *cube = Hmx::Object::New<RndCubeTex>();
    SetFaces(cube, 64, 3);
    ASSERT_TRUE(GetGpuCubeTexView(cube)) << "cube did not upload";
    EXPECT_EQ(GetGpuCubeTexMipLevels(cube), Levels(cube));
    EXPECT_EQ(GpuTexCount(), tex0 + 1) << "one draw is one GPU texture";
    EXPECT_EQ(GpuTexHolderCount(), hold0 + 1);

    RndCubeTex *dead = cube;
    delete cube;
    EXPECT_EQ(GpuTexCount(), tex0) << "the deleted cube map's texture is still on the GPU";
    EXPECT_EQ(GpuTexHolderCount(), hold0) << "the deleted cube map still holds an entry";
    // Only looks the address up; never dereferences it.
    EXPECT_EQ(GetGpuCubeTexMipLevels(dead), -1) << "an entry outlives its cube map";
}

TEST_F(CubeTexReleaseTest, RecycledAddressUploadsItsOwnFaces) {
    if (IsSkipped()) return;
    milo::ScopedLookOverride look(milo::LookProfile::Retail);
    alignas(PlacedCube) static unsigned char storage[sizeof(PlacedCube)];
    const size_t tex0 = GpuTexCount();

    PlacedCube *a = new (storage) PlacedCube();
    SetFaces(a, 64, 5);
    wgpu::TextureView viewA = GetGpuCubeTexView(a); // held: its handle cannot be reused
    ASSERT_TRUE(viewA) << "first cube did not upload";
    const int levelsA = Levels(a);
    ASSERT_EQ(GetGpuCubeTexMipLevels(a), levelsA);
    a->~PlacedCube();
    EXPECT_EQ(GetGpuCubeTexMipLevels((RndCubeTex *)(void *)storage), -1)
        << "the dead cube map's entry is still in the table";

    PlacedCube *b = new (storage) PlacedCube();
    ASSERT_EQ((void *)b, (void *)a);
    SetFaces(b, 16, 77);
    ASSERT_NE(Levels(b), levelsA) << "the two cube maps must be told apart by their levels";
    wgpu::TextureView viewB = GetGpuCubeTexView(b);
    ASSERT_TRUE(viewB) << "second cube did not upload";
    EXPECT_NE(viewB.Get(), viewA.Get()) << "the new cube map was handed the dead one's view";
    EXPECT_EQ(GetGpuCubeTexMipLevels(b), Levels(b))
        << "the new cube map shows the dead one's 64x64 faces";
    EXPECT_EQ(GpuTexCount(), tex0 + 1) << "one live cube map, one texture";
    b->~PlacedCube();
    EXPECT_EQ(GpuTexCount(), tex0);
}

TEST_F(CubeTexReleaseTest, FacesChangedAfterTheFirstDrawUploadAgain) {
    if (IsSkipped()) return;
    milo::ScopedLookOverride look(milo::LookProfile::Retail);
    const size_t tex0 = GpuTexCount();
    RndCubeTex *cube = Hmx::Object::New<RndCubeTex>();
    SetFaces(cube, 64, 9);
    wgpu::TextureView v1 = GetGpuCubeTexView(cube);
    ASSERT_TRUE(v1);
    const int levels1 = Levels(cube);
    ASSERT_EQ(GetGpuCubeTexMipLevels(cube), levels1);

    // One face resized: the faces no longer agree, Update() takes its Reset()
    // branch (retail's DxCubeTex::Reset releases the texture), and the copy is
    // gone before any draw.
    FillFace(cube->GetBitmap(RndCubeTex::kCubeFaceRight), 16, 11);
    cube->UpdateFace(RndCubeTex::kCubeFaceRight);
    EXPECT_EQ(GetGpuCubeTexMipLevels(cube), -1) << "Update's Reset() branch kept the old GPU copy";

    // All six faces resized: the next draw is the new size.
    SetFaces(cube, 16, 11);
    wgpu::TextureView v2 = GetGpuCubeTexView(cube);
    ASSERT_TRUE(v2);
    ASSERT_NE(Levels(cube), levels1);
    EXPECT_EQ(GetGpuCubeTexMipLevels(cube), Levels(cube)) << "the cube map still shows its old faces";
    EXPECT_NE(v2.Get(), v1.Get());

    // One face's pixels changed at the same size: Update() takes its Sync()
    // branch (the properties still agree) and the next draw is a new upload.
    FillFace(cube->GetBitmap(RndCubeTex::kCubeFaceTop), 16, 200);
    cube->UpdateFace(RndCubeTex::kCubeFaceTop);
    wgpu::TextureView v3 = GetGpuCubeTexView(cube);
    ASSERT_TRUE(v3);
    EXPECT_NE(v3.Get(), v2.Get()) << "a changed face did not reach the GPU";
    EXPECT_EQ(GpuTexCount(), tex0 + 1) << "re-uploads replace the texture, they do not add one";
    delete cube;
    EXPECT_EQ(GpuTexCount(), tex0);
}
