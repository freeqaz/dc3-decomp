// test_retail_flip_normal.cpp -- DC3's flip_normal (np-gfx-light round 6,
// docs/native/dc3-retail-lighting.md section 11 in the engine).
//
// RndShaderStandard::CalcShaderOpts (src/system/rndobj/Shader.cpp) sets
// ShaderOptions bit 54, mFlipNormal, inside its per-pixel block -- a material
// that uses the environment, is per-pixel lit, and has lights -- when
// GetCull() == kCullBackwards. In the shipped cache the bit changes only the
// vertex shader (standard_0040000080020015.vs against ..._0000000080020015.vs,
// standard_0040000000020037.vs against ..._0000000000020037.vs, xenia ucode):
// the world normal is negated, and with a normal map the bitangent crossed
// from it flips too while the tangent does not. A backwards-culled surface is
// one whose back faces are drawn; without the flip its lit side faces away
// from the camera. The native port had no flip_normal at all.
//
// The tests check (a) the builders put the flag in bit 10 of colorMod.w for
// exactly retail's condition, for the primary material and a NextPass, and
// leave the other bits as they were; (b) on the GPU, the engine's own
// vsOrientNormal on those uniforms negates the normal, and only then; (c) both
// vertex shaders hand on vsOrientNormal's result and cross the bitangent from
// it (a check of the WGSL text: the entry points cannot be called from a
// compute shader).
//
// Sabotages tried, 8 of 8 caught (np-gfx-light round 6, sabotage_flip.py):
// the shape never setting flipNormal; the shape setting it outside the
// per-pixel block; the shape testing kCullRegular; FillFlipNormal not called
// for the NextPass; FillFlipNormal called before FillColorMod (which assigns
// w for a colour-modulated material -- caught only by the colour-modulated
// case, added after a first version of this sabotage survived);
// vsOrientNormal returning the normal unflipped; vs_skinned not calling
// vsOrientNormal; the bit read from bit 9.
//
// Shipped use (round 6 census, milo-mat-export over every char/world/ui milo):
// 87 materials, all in char/main -- every dancer's and backup dancer's
// *_hair_backface(s).mat, plus a few feather / visor / no-cull backfaces (19
// with a normal map). No world or UI material qualifies.
//
// The GPU half needs a device (skips without one). No game data.

#include "test_helpers.h"
#include "texgen_compute_harness.h"

#include "gfx/ShippedWgsl.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"

#include <string>
#include <vector>

// See test_screen_aligned_texgen.cpp: dc3's native/src/platform/MaterialSetup.h
// shadows the engine's, so the engine's entry points are declared here.
struct UiDrawTerms {
    bool gammaShading = false;
    bool retailMaterialTerms = false;
};
MaterialParams BuildMaterialParams(RndMat *mat, bool isTextMesh, const UiDrawTerms &uiTerms);
MaterialParams BuildPassMaterialParams(BaseMaterial *pass, const UiDrawTerms &uiTerms);

namespace {

const UiDrawTerms kWorld{false, false};
const uint32_t kFlipBit = 1u << 10;

struct FlipCase {
    const char *name;
    bool useEnviron;
    bool perPixel;
    Cull cull;
    bool screenAligned; // another colorMod.w bit, which must survive
    bool colorMod;      // kColorModModulate: FillColorMod assigns colorMod.w
    bool want;
};

const FlipCase kCases[] = {
    {"lit, per-pixel, culled backwards", true, true, kCullBackwards, false, false, true},
    {"lit, per-pixel, culled backwards, screen aligned", true, true, kCullBackwards, true,
     false, true},
    // a crowd member's hair backfaces: WorldCrowd tints them with three colours
    {"lit, per-pixel, culled backwards, colour-modulated", true, true, kCullBackwards, false,
     true, true},
    {"lit, per-pixel, culled regular", true, true, kCullRegular, false, false, false},
    {"lit, per-pixel, not culled", true, true, kCullNone, false, false, false},
    {"lit, per-vertex, culled backwards", true, false, kCullBackwards, false, false, false},
    {"unlit, per-pixel, culled backwards", false, true, kCullBackwards, false, false, false},
};

RndMat *MakeMat(const FlipCase &c, Cull cull) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetUseEnv(c.useEnviron);
    mat->SetPerPixelLit(c.perPixel);
    mat->SetCull(cull);
    if (c.colorMod) {
        mat->SetColorModFlags(BaseMaterial::kColorModModulate);
        for (int i = 0; i < 3; i++) mat->SetColorMod(Hmx::Color(0.2f * i, 0.5f, 0.9f), i);
    }
    mat->SetProperty("screen_aligned", DataNode(c.screenAligned ? 1 : 0));
    return mat;
}

class RetailFlipNormalTest : public EngineTestFixture {};

class RetailFlipNormalGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
};

size_t Count(const std::string &s, const std::string &needle) {
    size_t n = 0;
    for (size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + 1)) n++;
    return n;
}

} // namespace

// Both builders set bit 10 for retail's condition and nothing else: the rest
// of colorMod.w is what the same material culled regular gets.
TEST_F(RetailFlipNormalTest, BuildersCarryRetailsCondition) {
    ASSERT_NE(gWgpuRnd, nullptr);
    for (const FlipCase &c : kCases) {
        RndMat *mat = MakeMat(c, c.cull);
        RndMat *twin = MakeMat(c, kCullRegular);
        const uint32_t prim = BuildMaterialParams(mat, false, kWorld).uniforms.colorMod[3];
        const uint32_t pass = BuildPassMaterialParams(mat, kWorld).uniforms.colorMod[3];
        const uint32_t twinPrim = BuildMaterialParams(twin, false, kWorld).uniforms.colorMod[3];
        const uint32_t twinPass = BuildPassMaterialParams(twin, kWorld).uniforms.colorMod[3];
        delete mat;
        delete twin;
        EXPECT_EQ((prim & kFlipBit) != 0, c.want) << c.name << ", primary";
        EXPECT_EQ((pass & kFlipBit) != 0, c.want) << c.name << ", NextPass";
        EXPECT_EQ(twinPrim & kFlipBit, 0u) << c.name << ", culled regular";
        EXPECT_EQ(prim & ~kFlipBit, twinPrim) << c.name << ", primary: the other bits of colorMod.w";
        EXPECT_EQ(pass & ~kFlipBit, twinPass) << c.name << ", NextPass: the other bits of colorMod.w";
        if (c.screenAligned) EXPECT_NE(twinPrim & (1u << 9), 0u) << c.name;
        if (c.colorMod)
            EXPECT_EQ(twinPrim & 0xFFu, (uint32_t)BaseMaterial::kColorModModulate) << c.name;
    }
}

// The engine's vsOrientNormal, on the builder's uniforms: negated when the
// flag is set, untouched otherwise.
TEST_F(RetailFlipNormalGpuTest, ShaderNegatesTheNormalExactlyWhenFlagged) {
    static const char *kTail = R"WGSL(
@compute @workgroup_size(1)
fn texgen_test_main(@builtin(global_invocation_id) id: vec3u) {
    _ = scene.cameraPos; // the harness binds group 0; keep it in the layout
    let v = texGenTestIn[id.x];
    texGenTestOut[id.x] = vec4f(vsOrientNormal(v.clip.xyz), 0.0);
}
)WGSL";
    const float normals[][3] = {{0, 0, 1}, {0.6f, -0.8f, 0}, {-0.267f, 0.535f, 0.802f}};
    std::vector<TexGenVertex> verts;
    for (const auto &n : normals) verts.push_back({{n[0], n[1], n[2], 1.0f}, {0.0f, 0.0f}});
    for (const FlipCase &c : kCases) {
        RndMat *mat = MakeMat(c, c.cull);
        const MaterialParams params = BuildMaterialParams(mat, false, kWorld);
        delete mat;
        const std::vector<float> got = RunMaterialCompute(params.uniforms, verts, kTail);
        ASSERT_EQ(got.size(), 4 * verts.size()) << c.name;
        const float sign = c.want ? -1.0f : 1.0f;
        for (size_t i = 0; i < verts.size(); i++)
            for (int a = 0; a < 3; a++)
                EXPECT_NEAR(got[4 * i + a], sign * normals[i][a], 1e-6f)
                    << c.name << ", normal " << i << " axis " << a;
    }
}

// Both vertex shaders hand on vsOrientNormal's result as worldNormal, and the
// bitangent is crossed from that (so it flips with it) while the tangent is not
// derived from the normal.
TEST_F(RetailFlipNormalTest, BothVertexShadersOrientTheNormal) {
    const std::string src = StandardWgslSource();
    EXPECT_EQ(Count(src, "let worldNormal =\n        vsOrientNormal(normalize((object.worldInvTranspose"), 2u)
        << "vs_main and vs_skinned each orient their world normal";
    EXPECT_EQ(Count(src, "vertexOutput.worldBitangent = cross(worldNormal, worldTangent)"), 2u);
    EXPECT_EQ(Count(src, "vertexOutput.worldNormal = worldNormal;"), 2u);
    EXPECT_EQ(Count(src, "@vertex"), 2u) << "a new vertex entry point must orient its normal too";
}
