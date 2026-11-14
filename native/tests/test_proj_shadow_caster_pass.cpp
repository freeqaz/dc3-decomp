// test_proj_shadow_caster_pass.cpp -- the DC3 projected-shadow caster pass
// (milo-native-engine src/gfx/ShadowPass.cpp, ShadowPass::RenderCasters).
//
// The receivers of a projected_blend 1 light read a coverage map: each caster
// drawn at depth 0 over a cleared depth of 1, through the light's clip
// transform (test_retail_lighting.cpp pins which meshes and which transform).
// Round 3 of np-gfx-light made the pass self-contained: its own dynamic-offset
// layouts, one upload per frame for every caster, and bind groups created
// once, instead of Mesh_Wgpu's DrawMeshShadow creating one or two bind groups
// per caster per frame. These tests pin both halves on the GPU:
//
//   * the map: a caster quad covers the texels under it and nothing else, and
//     a caster with a world transform lands where the transform puts it (so
//     the per-caster object slot reaches the shader at its own offset);
//   * the atlas: two lights in one frame each draw into their own quarter of
//     the depth texture with their own clip transform (houseparty);
//   * the cost: a second frame with the same casters creates no bind group.
//
// Sabotages tried, each fails a test here: creating the object bind group
// per frame (EnsureCasterBuffer ignoring `capacity`) -> SecondFrameCreatesNo-
// BindGroups; every draw using dynamic offset 0 -> the moved caster lands on
// the first caster's place (CasterTransformsReachTheShader); not drawing the
// casters -> CasterCoversTheTexelsUnderIt; one viewport for every tile, or
// clip offset 0 for every tile -> TwoLightsDrawIntoTheirOwnTiles.
//
// And the gobo of a projected_blend 0 light: textures reach the GPU when a
// material binds them, and a gobo is bound by a light only, so the scene's
// slot 3 must upload it itself (GoboOnlyALightBindsReachesSlot3). Sabotage:
// dropping the PresyncBitmap call in WgpuRnd::WriteSceneUniforms fails it --
// slot 3 falls back to white and the light lights its whole cone flat, which
// is what the rink floor did (dc3-retail-lighting.md 8.1).

#include "test_helpers.h"

#include "gfx/GpuDevice.h"
#include "gfx/ShadowPass.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/Env.h"
#include "rndobj/Lit.h"
#include "rndobj/Mesh.h"
#include "rndobj/Tex.h"

#include <vector>


namespace {

// A flat quad in the z = 0 plane, both windings, so back-face culling cannot
// hide it whichever way the clip transform turns it.
RndMesh *MakeQuad(float x0, float y0, float x1, float y1) {
    RndMesh *mesh = Hmx::Object::New<RndMesh>();
    mesh->Verts().resize(4);
    mesh->Verts()[0].pos.Set(x0, y0, 0);
    mesh->Verts()[1].pos.Set(x1, y0, 0);
    mesh->Verts()[2].pos.Set(x1, y1, 0);
    mesh->Verts()[3].pos.Set(x0, y1, 0);
    static const int kFaces[4][3] = {{0, 1, 2}, {0, 2, 3}, {0, 2, 1}, {0, 3, 2}};
    mesh->Faces().resize(4);
    for (int i = 0; i < 4; i++)
        mesh->Faces()[i].Set(kFaces[i][0], kFaces[i][1], kFaces[i][2]);
    return mesh;
}

// clip = (x, y, 0, 1) from world (x, y, z): column-major, as the shape hands
// RenderCasters (out[c * 4 + row]).
void FlatClip(float out[16]) {
    for (int i = 0; i < 16; i++) out[i] = 0.0f;
    out[0] = 1.0f;    // row 0 <- x
    out[5] = 1.0f;    // row 1 <- y
    out[15] = 1.0f;   // row 3 <- 1
}

} // namespace

class ProjShadowCasterPassTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    void Render(ShadowPass &sp, std::vector<ShadowPass::CasterSet> sets) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        sp.RenderCasters(enc, gpu, sets.data(), (int)sets.size());
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
    }
    void Render(ShadowPass &sp, std::vector<RndMesh *> &meshes) {
        float clip[16];
        FlatClip(clip);
        Render(sp, {{clip, meshes.data(), (int)meshes.size(), 0}});
    }

    // The whole map, row-major floats.
    std::vector<float> ReadMap(ShadowPass &sp) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        const uint32_t n = ShadowPass::kShadowMapSize;
        wgpu::BufferDescriptor bd{};
        bd.size = (uint64_t)n * n * 4;
        bd.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        wgpu::Buffer staging = gpu.Device().CreateBuffer(&bd);
        wgpu::TexelCopyTextureInfo src{};
        src.texture = sp.DepthTexture();
        src.aspect = wgpu::TextureAspect::DepthOnly;
        wgpu::TexelCopyBufferInfo dst{};
        dst.buffer = staging;
        dst.layout.bytesPerRow = n * 4;
        dst.layout.rowsPerImage = n;
        wgpu::Extent3D size{n, n, 1};
        wgpu::CommandEncoder enc = gpu.Device().CreateCommandEncoder();
        enc.CopyTextureToBuffer(&src, &dst, &size);
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        bool mapped = false;
        gpu.Instance().WaitAny(
            staging.MapAsync(wgpu::MapMode::Read, 0, bd.size, wgpu::CallbackMode::WaitAnyOnly,
                             [&](wgpu::MapAsyncStatus st, wgpu::StringView) {
                                 mapped = st == wgpu::MapAsyncStatus::Success;
                             }),
            UINT64_MAX);
        if (!mapped) {
            ADD_FAILURE() << "readback map failed";
            return {};
        }
        const float *p = static_cast<const float *>(staging.GetConstMappedRange(0, bd.size));
        std::vector<float> out(p, p + (size_t)n * n);
        staging.Unmap();
        return out;
    }

    // Depth at a light's NDC (x, y) in atlas tile `tile` (2x2, row-major from
    // the top left): +y is up, the texture's row 0 is the top.
    static float At(const std::vector<float> &map, float x, float y, int tile = 0) {
        const int n = ShadowPass::kShadowMapSize;
        const int half = n / 2;
        int col = (tile % 2) * half + (int)((x * 0.5f + 0.5f) * half);
        int row = (tile / 2) * half + (int)((0.5f - y * 0.5f) * half);
        return map[(size_t)row * n + col];
    }
};

TEST_F(ProjShadowCasterPassTest, CasterCoversTheTexelsUnderIt) {
    ShadowPass sp;
    std::vector<RndMesh *> meshes{MakeQuad(-0.5f, -0.5f, 0.5f, 0.5f)};
    Render(sp, meshes);
    std::vector<float> map = ReadMap(sp);
    ASSERT_FALSE(map.empty());
    EXPECT_LT(At(map, 0.0f, 0.0f), 0.01f) << "the caster's own texels are covered";
    EXPECT_LT(At(map, 0.4f, -0.4f), 0.01f);
    EXPECT_EQ(At(map, 0.8f, 0.8f), 1.0f) << "outside the caster the map stays clear";
    EXPECT_EQ(At(map, -0.8f, 0.0f), 1.0f);
    EXPECT_EQ(sp.GetCasterStats().lastDraws, 1u);
    for (RndMesh *m : meshes) delete m;
}

TEST_F(ProjShadowCasterPassTest, CasterTransformsReachTheShader) {
    // Two casters: the second's geometry is at the origin, its world transform
    // moves it to (+0.6, +0.6). Each draw reads its own 256-byte slot.
    ShadowPass sp;
    RndMesh *fixed = MakeQuad(-0.7f, -0.7f, -0.5f, -0.5f);
    RndMesh *moved = MakeQuad(-0.1f, -0.1f, 0.1f, 0.1f);
    Transform xfm;
    xfm.Reset();
    xfm.v.Set(0.6f, 0.6f, 0.0f);
    moved->SetLocalXfm(xfm);
    std::vector<RndMesh *> meshes{fixed, moved};
    Render(sp, meshes);
    std::vector<float> map = ReadMap(sp);
    ASSERT_FALSE(map.empty());
    EXPECT_LT(At(map, -0.6f, -0.6f), 0.01f) << "first caster in place";
    EXPECT_LT(At(map, 0.6f, 0.6f), 0.01f) << "second caster moved by its world transform";
    EXPECT_EQ(At(map, 0.0f, 0.0f), 1.0f) << "the second caster's untransformed place is clear";
    delete fixed;
    delete moved;
}

TEST_F(ProjShadowCasterPassTest, SecondFrameCreatesNoBindGroups) {
    ShadowPass sp;
    std::vector<RndMesh *> meshes;
    for (int i = 0; i < 8; i++) {
        const float x = -0.9f + 0.2f * i;
        meshes.push_back(MakeQuad(x, -0.1f, x + 0.1f, 0.1f));
    }
    Render(sp, meshes);
    const uint32_t afterFirst = sp.GetCasterStats().bindGroupsCreated;
    EXPECT_LE(afterFirst, 3u) << "scene + object + bone groups, not one per caster";
    Render(sp, meshes);
    Render(sp, meshes);
    EXPECT_EQ(sp.GetCasterStats().bindGroupsCreated, afterFirst)
        << "the same casters on a later frame must reuse the bind groups";
    EXPECT_EQ(sp.GetCasterStats().frames, 3u);
    EXPECT_EQ(sp.GetCasterStats().lastDraws, 8u);
    // 8 object slots of 256 bytes, no bones, plus the four 256-byte clip slots.
    EXPECT_EQ(sp.GetCasterStats().lastUploadBytes, 8u * 256u + 4u * 256u);
    for (RndMesh *m : meshes) delete m;
}

TEST_F(ProjShadowCasterPassTest, TwoLightsDrawIntoTheirOwnTiles) {
    // Light A (tile 0): the flat clip, a quad at the centre. Light B (tile 3):
    // a clip that mirrors x, the same kind of quad on the left -> lands right.
    ShadowPass sp;
    RndMesh *a = MakeQuad(-0.2f, -0.2f, 0.2f, 0.2f);
    RndMesh *b = MakeQuad(-0.8f, -0.1f, -0.6f, 0.1f);
    float clipA[16], clipB[16];
    FlatClip(clipA);
    FlatClip(clipB);
    clipB[0] = -1.0f;
    RndMesh *setA[] = {a};
    RndMesh *setB[] = {b};
    Render(sp, {{clipA, setA, 1, 0}, {clipB, setB, 1, 3}});
    std::vector<float> map = ReadMap(sp);
    ASSERT_FALSE(map.empty());
    EXPECT_LT(At(map, 0.0f, 0.0f, 0), 0.01f) << "A in its tile";
    EXPECT_EQ(At(map, 0.7f, 0.0f, 0), 1.0f) << "B is not in A's tile";
    EXPECT_LT(At(map, 0.7f, 0.0f, 3), 0.01f) << "B in its tile, through its own clip";
    EXPECT_EQ(At(map, -0.7f, 0.0f, 3), 1.0f) << "B drawn with A's clip would land here";
    EXPECT_EQ(At(map, 0.0f, 0.0f, 3), 1.0f) << "A is not in B's tile";
    EXPECT_EQ(At(map, 0.0f, 0.0f, 1), 1.0f) << "unused tiles stay clear";
    EXPECT_EQ(At(map, 0.0f, 0.0f, 2), 1.0f);
    EXPECT_EQ(sp.GetCasterStats().lastTiles, 2u);
    delete a;
    delete b;
}

extern wgpu::TextureView GetGpuTexView(RndTex *tex);

TEST_F(ProjShadowCasterPassTest, GoboOnlyALightBindsReachesSlot3) {
    RndEnviron *env = Hmx::Object::New<RndEnviron>();
    RndLight *spot = Hmx::Object::New<RndLight>();
    spot->SetLightType(RndLight::kFakeSpot);
    spot->SetColor(Hmx::Color(1.0f, 1.0f, 1.0f));
    spot->SetProjectedBlend(0);
    RndTex *gobo = Hmx::Object::New<RndTex>();
    gobo->SetBitmap(4, 4, 32, RndTex::kRegular, false, nullptr);
    spot->SetProperty("texture", DataNode(gobo));
    env->LightsReal().push_back(spot);
    env->RndEnviron::Select(nullptr);
    ASSERT_FALSE(GetGpuTexView(gobo)) << "precondition: nothing has uploaded the gobo";

    gWgpuRnd->WriteSceneUniforms();
    ASSERT_EQ(rndshape::SceneProjTexture(), gobo) << "the light is the scene's projected light";
    wgpu::TextureView view = gWgpuRnd->ProjLightTexView();
    EXPECT_TRUE(view) << "slot 3 fell back to white: the gobo was never uploaded";
    EXPECT_EQ(view.Get(), GetGpuTexView(gobo).Get());

    env->LightsReal().clear();
    delete env;   // clears the current environ
    gWgpuRnd->WriteSceneUniforms();
    spot->SetProperty("texture", DataNode((Hmx::Object *)nullptr));
    delete spot;
    delete gobo;
}
