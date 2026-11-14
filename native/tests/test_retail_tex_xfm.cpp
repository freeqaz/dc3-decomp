// test_retail_tex_xfm.cpp -- DC3's texture-transform rows (np-gfx-light round 6,
// docs/native/dc3-retail-lighting.md section 11 in the engine).
//
// For tex_gen Xfm and XfmOrigin the image builds the vertex shader's texgen
// matrix with MakeTex3 (src/system/rndobj/Mat_NG.cpp, linked into this binary
// and used here as the oracle), uploads it to c20/c21 transposed
// (SetVConstant(kVS_TexTransform, Matrix4)), and every standard permutation
// with texGen 0 evaluates (standard_0000000000000010.vs, xenia ucode)
//
//   o0.x = dp2add(r0.yx, c20.yx, c20.w)  =  u*M.x.x + v*M.y.x + M.w.x
//   o0.y = dp2add(r0.yx, c21.yx, c21.w)  =  u*M.x.y + v*M.y.y + M.w.y
//
// MakeTex3 negates m.x.y and m.y.x and, for the centred mode, applies the
// translation before the matrix with tx negated. The native port used to put
// the Transform's rows in directly: a tex_xfm x of 0.25 moved the streetside
// sky 307 px LEFT on native where Xenia moves it 319 px RIGHT, a scaled
// scrolling map scrolled at 1/scale of retail's speed, and a rotation with
// non-uniform scale (RndMatAnim::SetFrame: MakeRotMatrix then Scale) came out
// sheared.
//
// The tests build the material, run BuildMaterialParams /
// BuildPassMaterialParams, and compare (a) on the CPU, the builder's rows as
// the engine shader's kTexGenXfm / kTexGenXfmOrigin branches read them, and
// (b) on the GPU, the engine's own WGSL on those uniforms, against MakeTex3
// evaluated the way the microcode does.
//
// Sabotages tried, 7 of 7 caught (np-gfx-light round 6, sabotage.py):
// rndshape::MatRetailTexXfmRows returning false (the old rows); tx not
// negated; ty negated; the translation added after the matrix instead of
// before; m.x.y / m.y.x not negated (transpose only); the origin mode taking
// the centred formula; FillTexGen not calling the helper.
//
// The GPU half needs a device (skips without one). No game data.

#include "test_helpers.h"
#include "texgen_compute_harness.h"

#include "math/Mtx.h"
#include "math/Rot.h"
#include "platform/MaterialSetup.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Mat.h"

#include <cmath>
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

// The image's own function (src/system/rndobj/Mat_NG.cpp).
void MakeTex3(const Transform &xfm, bool center, Hmx::Matrix4 &out);

namespace {

const UiDrawTerms kWorld{false, false};

struct XfmCase {
    const char *name;
    TexGen texGen;
    float rotDeg;      // about z, MakeRotMatrixZ
    float scale[3];    // Scale(scale, m, m), as RndMatAnim::SetFrame applies it
    float trans[2];
};

const XfmCase kCases[] = {
    {"identity", kTexGenXfm, 0.0f, {1, 1, 1}, {0, 0}},
    {"scroll x 0.25 (the sky probe)", kTexGenXfm, 0.0f, {1, 1, 1}, {0.25f, 0}},
    {"scroll y 0.25", kTexGenXfm, 0.0f, {1, 1, 1}, {0, 0.25f}},
    {"tiled x4 scrolling", kTexGenXfm, 0.0f, {4, 4, 1}, {0.1f, -0.3f}},
    {"rotated 30", kTexGenXfm, 30.0f, {1, 1, 1}, {0, 0}},
    {"rotated 30, scale (2, 0.5), translated", kTexGenXfm, 30.0f, {2, 0.5f, 1}, {0.2f, 0.35f}},
    {"origin: rotated -50, scale (1.5, 3), translated", kTexGenXfmOrigin, -50.0f, {1.5f, 3, 1},
     {0.4f, -0.15f}},
    {"origin: scroll x 0.25", kTexGenXfmOrigin, 0.0f, {1, 1, 1}, {0.25f, 0}},
};

Transform CaseXfm(const XfmCase &c) {
    Transform xfm;
    xfm.Reset();
    MakeRotMatrixZ(c.rotDeg * 3.14159265358979f / 180.0f, xfm.m);
    Scale(Vector3(c.scale[0], c.scale[1], c.scale[2]), xfm.m, xfm.m);
    xfm.v.Set(c.trans[0], c.trans[1], 0.0f);
    return xfm;
}

RndMat *MakeMat(const XfmCase &c) {
    RndMat *mat = Hmx::Object::New<RndMat>();
    mat->SetTexGen(c.texGen);
    mat->SetTexXfm(CaseXfm(c));
    return mat;
}

// The microcode on MakeTex3's matrix.
void Retail(const XfmCase &c, const float uv[2], float out[2]) {
    Hmx::Matrix4 m;
    MakeTex3(CaseXfm(c), c.texGen == kTexGenXfm, m);
    out[0] = uv[0] * m.x.x + uv[1] * m.y.x + m.w.x;
    out[1] = uv[0] * m.x.y + uv[1] * m.y.y + m.w.y;
}

// The engine shader's two branches, on the builder's rows (standard_wgsl.inc
// computeTexGenUV).
void EngineCpu(const MaterialUniforms &u, const float uv[2], float out[2]) {
    if ((int)u.texGenMode == kTexGenXfm) {
        const float cu = uv[0] - 0.5f, cv = uv[1] - 0.5f;
        out[0] = cu * u.texXfmRow0[0] + cv * u.texXfmRow0[1] + u.texXfmRow0[2] + 0.5f;
        out[1] = cu * u.texXfmRow1[0] + cv * u.texXfmRow1[1] + u.texXfmRow1[2] + 0.5f;
    } else {
        out[0] = uv[0] * u.texXfmRow0[0] + uv[1] * u.texXfmRow0[1] + u.texXfmRow0[2];
        out[1] = uv[0] * u.texXfmRow1[0] + uv[1] * u.texXfmRow1[1] + u.texXfmRow1[2];
    }
}

const float kUvs[][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {0.5f, 0.5f}, {-0.91f, 0.405f},
                         {0.37f, 2.6f}};
const size_t kNumUvs = sizeof(kUvs) / sizeof(kUvs[0]);

class RetailTexXfmTest : public EngineTestFixture {};

class RetailTexXfmGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
};

} // namespace

// Both builders write MakeTex3's rows, for every case.
TEST_F(RetailTexXfmTest, BuilderRowsAreTheImagesMakeTex3) {
    ASSERT_NE(gWgpuRnd, nullptr);
    for (const XfmCase &c : kCases) {
        RndMat *mat = MakeMat(c);
        const MaterialUniforms prim = BuildMaterialParams(mat, false, kWorld).uniforms;
        const MaterialUniforms pass = BuildPassMaterialParams(mat, kWorld).uniforms;
        delete mat;
        for (size_t i = 0; i < kNumUvs; i++) {
            float want[2], gotPrim[2], gotPass[2];
            Retail(c, kUvs[i], want);
            EngineCpu(prim, kUvs[i], gotPrim);
            EngineCpu(pass, kUvs[i], gotPass);
            for (int a = 0; a < 2; a++) {
                EXPECT_NEAR(gotPrim[a], want[a], 1e-5f)
                    << c.name << ", primary, uv " << i << (a ? " v" : " u");
                EXPECT_NEAR(gotPass[a], want[a], 1e-5f)
                    << c.name << ", NextPass, uv " << i << (a ? " v" : " u");
            }
        }
    }
}

// The direction a tex_xfm translation moves the image: x opposite to the UV
// offset, y along it -- the asymmetry the sky probe measured on Xenia.
TEST_F(RetailTexXfmTest, TranslationSignsAreRetails) {
    ASSERT_NE(gWgpuRnd, nullptr);
    const float uv[2] = {0.5f, 0.5f};
    float out[2];
    RndMat *x = MakeMat(kCases[1]);
    EngineCpu(BuildMaterialParams(x, false, kWorld).uniforms, uv, out);
    EXPECT_NEAR(out[0], 0.25f, 1e-6f) << "tex_xfm x +0.25 samples u - 0.25";
    EXPECT_NEAR(out[1], 0.5f, 1e-6f);
    RndMat *y = MakeMat(kCases[2]);
    EngineCpu(BuildMaterialParams(y, false, kWorld).uniforms, uv, out);
    EXPECT_NEAR(out[0], 0.5f, 1e-6f);
    EXPECT_NEAR(out[1], 0.75f, 1e-6f) << "tex_xfm y +0.25 samples v + 0.25";
    delete x;
    delete y;
}

// The engine's WGSL, on the builder's uniforms, is the microcode.
TEST_F(RetailTexXfmGpuTest, ShaderUvIsTheMicrocodes) {
    std::vector<TexGenVertex> verts;
    for (size_t i = 0; i < kNumUvs; i++) {
        TexGenVertex v{{0.0f, 0.0f, 0.5f, 1.0f}, {kUvs[i][0], kUvs[i][1]}};
        verts.push_back(v);
    }
    for (const XfmCase &c : kCases) {
        RndMat *mat = MakeMat(c);
        const MaterialParams params = BuildMaterialParams(mat, false, kWorld);
        delete mat;
        const std::vector<float> got = RunTexGenCompute(params.uniforms, verts);
        ASSERT_EQ(got.size(), 2 * kNumUvs) << c.name;
        for (size_t i = 0; i < kNumUvs; i++) {
            float want[2];
            Retail(c, kUvs[i], want);
            for (int a = 0; a < 2; a++)
                EXPECT_NEAR(got[2 * i + a], want[a], 1e-5f)
                    << c.name << ", uv " << i << (a ? " v" : " u");
        }
    }
}
