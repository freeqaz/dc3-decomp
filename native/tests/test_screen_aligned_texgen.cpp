// test_screen_aligned_texgen.cpp -- DC3's screen-aligned texture coordinates
// (np-gfx-light round 5, docs/native/dc3-retail-lighting.md section 10 in the
// engine).
//
// A material with screen_aligned set (BaseMaterial::mScreenAligned) draws with
// ShaderOptions bit 13 (RndShaderStandard::CalcShaderOpts). The shipped
// permutation standard_0000000000002010.vs (xenia-gpu-shader-compiler ucode)
// replaces the vertex UV with the vertex's screen position before the texgen
// rows c20/c21:
//
//   r0.xy = oPos.xy * rcp(oPos.w) * 0.5 + 0.5;  r0.z = 1 - r0.y
//   o0.x  = dp2add(r0.zx, c20.yx, c20.w);  o0.y = dp2add(r0.zx, c21.yx, c21.w)
//
// The plain permutation (0x10) feeds the vertex UV to the same dp2adds.
// Streetside's peak sky (sky_peak_1..3.mat, a cylinder at radius ~7000 with
// tex_gen Xfm, tex_xfm y 0.2) is one: on Xenia its searchlight fan spans the
// screen and the mirror floor reflects the texture's navy bottom, while the
// native port mapped the cylinder's own UVs and reflected stars.
//
// These tests pin the material flag (rndshape::MatScreenAligned through
// BuildMaterialParams / BuildPassMaterialParams, colorMod.w bit 9) and then
// run the engine's own WGSL -- texGenSourceUV and computeTexGenUV, the two
// calls vs_main makes -- in a compute shader on the uniforms the builder
// wrote, against the microcode's arithmetic above.
//
// Sabotages tried (each fails a test here): texGenSourceUV returning the
// vertex UV unconditionally; FillScreenAligned not called for the primary
// material; not called for a NextPass; v not flipped (ndc.y * 0.5 + 0.5);
// rndshape::MatScreenAligned returning false.
//
// The GPU half needs a device (skips without one). No game data.

#include "test_helpers.h"
#include "texgen_compute_harness.h"

#include "math/Mtx.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Declared here as test_native_ui_material_terms.cpp does: dc3's own
// native/src/platform/MaterialSetup.h (a pre-engine copy) is earlier on the
// include path and shadows the engine's.
struct UiDrawTerms {
    bool gammaShading = false;
    bool retailMaterialTerms = false;
};
MaterialParams BuildMaterialParams(RndMat *mat, bool isTextMesh, const UiDrawTerms &uiTerms);
MaterialParams BuildPassMaterialParams(BaseMaterial *pass, const UiDrawTerms &uiTerms);

namespace {

const UiDrawTerms kWorld{false, false};

// The sky's material as streetside authors it, less its textures:
// tex_gen Xfm, tex_xfm translated by (0, 0.2), screen_aligned as given.
RndMat *MakeSkyMat(bool screenAligned) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetTexGen(kTexGenXfm);
    Transform xfm;
    xfm.Reset();
    xfm.v.y = 0.2f;
    mat->SetTexXfm(xfm);
    mat->SetProperty("screen_aligned", DataNode(screenAligned ? 1 : 0));
    return mat;
}

bool ScreenAlignedBit(const MaterialUniforms &u) { return ((u.colorMod[3] >> 9) & 1u) != 0; }

// Clip positions with w far from 1 (the sky sits ~7000 units out) and both
// signs of x and y, plus one off screen; vertex UVs as the cylinder carries
// them (u in -1..1 around, v 0..1 down).
const TexGenVertex kVerts[] = {
    {{-3500.0f, 1800.0f, 6900.0f, 7000.0f}, {-0.91f, 0.405f}},
    {{2100.0f, -900.0f, 6950.0f, 7050.0f}, {0.95f, 0.596f}},
    {{0.0f, 0.0f, 10.0f, 11.0f}, {0.5f, 0.21f}},
    {{-12.5f, 30.0f, 19.0f, 20.0f}, {0.0f, 1.0f}},
};
const size_t kNumVerts = sizeof(kVerts) / sizeof(kVerts[0]);

// The shipped microcode for the material MakeSkyMat builds. MakeTex3 of an
// identity rotation translated by (0, 0.2) uploads c20 = (1, 0, -, 0) and
// c21 = (0, 1, -, 0.2): o0 = (r.x, r.y + 0.2) for whichever r the
// permutation feeds it.
void Expected(const TexGenVertex &v, bool screenAligned, float out[2]) {
    float r[2] = {v.uv[0], v.uv[1]};
    if (screenAligned) {
        const float rw = 1.0f / v.clip[3];
        const float sx = v.clip[0] * rw * 0.5f + 0.5f;
        const float sy = v.clip[1] * rw * 0.5f + 0.5f;
        r[0] = sx;
        r[1] = 1.0f - sy;
    }
    out[0] = r[0];
    out[1] = r[1] + 0.2f;
}

class ScreenAlignedTexGenTest : public EngineTestFixture {};

class ScreenAlignedTexGenGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }

    void Check(bool screenAligned) {
        RndMat *mat = MakeSkyMat(screenAligned);
        const MaterialParams params = BuildMaterialParams(mat, false, kWorld);
        delete mat;
        const std::vector<float> got = RunTexGenCompute(
            params.uniforms, std::vector<TexGenVertex>(kVerts, kVerts + kNumVerts));
        ASSERT_EQ(got.size(), 2 * kNumVerts);
        for (size_t i = 0; i < kNumVerts; i++) {
            float want[2];
            Expected(kVerts[i], screenAligned, want);
            for (int a = 0; a < 2; a++)
                EXPECT_NEAR(got[2 * i + a], want[a], 1e-5f)
                    << (screenAligned ? "screen-aligned" : "vertex-UV") << " vertex " << i
                    << (a ? " v" : " u");
        }
    }
};

} // namespace

// The flag is DC3's material state, carried by both builders.
TEST_F(ScreenAlignedTexGenTest, BuildersCarryTheMaterialFlag) {
    ASSERT_NE(gWgpuRnd, nullptr);
    RndMat *on = MakeSkyMat(true);
    RndMat *off = MakeSkyMat(false);
    ASSERT_TRUE(on->GetScreenAligned());
    ASSERT_FALSE(off->GetScreenAligned());
    EXPECT_TRUE(rndshape::MatScreenAligned(on));
    EXPECT_FALSE(rndshape::MatScreenAligned(off));
    EXPECT_TRUE(ScreenAlignedBit(BuildMaterialParams(on, false, kWorld).uniforms));
    EXPECT_FALSE(ScreenAlignedBit(BuildMaterialParams(off, false, kWorld).uniforms));
    EXPECT_TRUE(ScreenAlignedBit(BuildPassMaterialParams(on, kWorld).uniforms));
    EXPECT_FALSE(ScreenAlignedBit(BuildPassMaterialParams(off, kWorld).uniforms));
    // The flag lives above the colour-mod mode byte: the mode stays none.
    EXPECT_EQ(BuildMaterialParams(on, false, kWorld).uniforms.colorMod[3] & 0xFFu, 0u);
    delete on;
    delete off;
}

// The vertex shader's UV for a screen-aligned material: the microcode's.
TEST_F(ScreenAlignedTexGenGpuTest, ScreenAlignedUvIsTheVertexScreenPosition) {
    Check(true);
}

// And for every other material, the vertex UV through the same rows.
TEST_F(ScreenAlignedTexGenGpuTest, OtherMaterialsKeepTheVertexUv) {
    Check(false);
}
