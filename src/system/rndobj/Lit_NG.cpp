#include "rndobj\Lit_NG.h"
#include "Lit.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\System.h"
#include "rndobj\Draw.h"
#include "Memory.h"
#include "rndobj\Lit.h"
#include "rndobj/Cam.h"
#include "rndobj\Mat.h"
#include "rndobj\Rnd.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "rnddx9\RenderState.h"
#include "math\Mtx.h"
#include <cstring>

bool NgLight::WantShadows() const {
    return GetGfxMode() == kNewGfx && mShadowOverride && !mShadowOverride->empty();
}

bool NgLight::HaveShadows(std::vector<RndDrawable *> &draws) {
    MILO_ASSERT(mShadowOverride && !mShadowOverride->empty(), 0x3D);
    for (ObjPtrList<RndDrawable>::iterator it = mShadowOverride->begin();
         it != mShadowOverride->end();
         ++it) {
        RndDrawable *cur = *it;
        Sphere s;
        if (!cur->MakeWorldSphere(s, false) || SphereConeTest(s.center, s.radius)) {
            draws.push_back(cur);
        }
    }
    return !draws.empty();
}

BEGIN_COPYS(NgLight)
    COPY_SUPERCLASS(RndLight)
    CheckShadowMap();
END_COPYS

BEGIN_LOADS(NgLight)
    RndLight::Load(bs);
    CheckShadowMap();
END_LOADS

NgLight::~NgLight() {
    RELEASE(mShadowRT);
    RELEASE(unk188);
}

NgLight::NgLight() : mShadowRT(0), mShadowMap(0), unk188(0), unk18c(-1) {}

RndTex *NgLight::CreateShadowTex() {
    PhysMemTypeTracker tracker("D3D(phys): Shadow Map");
    RndTex *tex = Hmx::Object::New<RndTex>();
    tex->SetBitmap(0x100, 0x100, 16, RndTex::kRenderedNoZ, false, nullptr);
    return tex;
}

// RESIDUAL (w7-as, 63.1 canonical): instructions 5..62 match EXACTLY -- both
// WorldXfm() expansions, `sc -= xfm1.v`, the three fmuls and both early
// returns.  Everything after that is one problem: we keep too much alive.
// Target prologue saves r27-r31 + f30/f31 with `stwu r1, -0x100`; ours saves
// r24-r31 + f26-f31 with `stwu r1, -0x140`.  The STACK SLOT SET is identical
// (0x50..0xb8, seven 16-byte Vector3 slots, all of them reused two or three
// times) -- `run_diff_inspect mode=stack-layout` reports 10 DIFFER / 11
// PERMUTED and 0 target-only / 0 base-only slots -- so this is not a
// declaration-count problem.  The image materialises every `addi rN, r1,
// <slot>` into a VOLATILE register next to the copy that uses it; we hoist
// four of them into r24/r25/r27/r31 and five float loads into f26-f30, which
// is the whole +0x38 of callee-saved area plus the register permutation that
// dominates the row count (90 instructions across 20 pairs).  Fixing it needs
// the copies re-ordered so each address dies immediately, not a slot removed.
bool NgLight::SphereConeTest(const Vector3 &sphereCenter, float sphereRadius) {
    const Transform &xfm1 = WorldXfm();
    const Transform &xfm2 = WorldXfm();

    Vector3 sc = sphereCenter;
    sc -= xfm1.v;

    // MSVC materialises each of the three products once and re-derives the
    // sum at every use site; naming the products is what stops it contracting
    // them into fmadds.
    float py = xfm2.m.y.y * sc.y;
    float pz = xfm2.m.y.z * sc.z;
    float px = xfm2.m.y.x * sc.x;

    if (px + pz + py < -sphereRadius) {
        return false;
    }

    float range = mRange;
    if (px + pz + py > range + sphereRadius) {
        return false;
    }

    Vector3 axisProj = xfm2.m.y;
    axisProj *= pz + (px + py);

    Vector3 perp = sc;
    perp -= axisProj;

    Vector3 dir = perp;
    Normalize(dir, dir);

    float topR = mTopRadius;
    float botR = mBotRadius;

    Vector3 topPoint = xfm1.v;
    // dirTop is declared first because the image claims its slot first: the
    // two 16-byte copies out of `dir` go 0x70 -> 0xa0 (dirTop, the one later
    // scaled by mTopRadius at 0xa4/0xa8) and only then 0x70 -> 0xb0 (dirBot).
    // Worth one callee-saved FPR and 0x10 of frame: with this order the
    // prologue is __savefpr_26 and the frame Δ is +0x40, the other way round
    // it is __savefpr_25 and +0x50.
    Vector3 dirTop = dir;
    Vector3 dirBot = dir;
    Vector3 axisRange = xfm2.m.y;
    Vector3 botPoint = xfm1.v;
    Vector3 toSphere = sphereCenter;

    dirTop *= topR;
    topPoint += dirTop;

    axisRange *= range;
    botPoint += axisRange;

    toSphere -= topPoint;

    dirBot *= botR;
    Vector3 conePoint = botPoint;
    conePoint += dirBot;

    Vector3 closest = toSphere;

    Vector3 edgeDir = conePoint;
    edgeDir -= topPoint;

    // The image divides ONE into the squared length and multiplies; it does
    // NOT divide numerator by denominator.  0x826B9414 `lis r8,
    // __real@3f800000@ha` / 0x826B9424 `lfs f7, __real@3f800000@l(r8)`, then
    // `fdivs f12, f7, f12` and `fmuls f12, f12, f13`.  A plain `a / b` emits a
    // single `fdivs f12, f12, f13` here and the 1.0f literal never appears at
    // all -- /fp:fast does NOT introduce the reciprocal on its own (we are
    // built with it, and it did not), so the reciprocal is in the source.
    // Different arithmetic, not just different instructions.
    //
    // NEGATIVE RESULT (w7-as, 2026-09-14): this is a deliberate LOSS.  Faithful
    // reciprocal + faithful dirTop/dirBot order = 63.1 canonical; the unfaithful
    // `Dot(a,b) / Dot(b,b)` + reversed order scored 65.6.  Measured 4 ways:
    //   plain divide, dirBot first  65.6   (single fdivs, no 1.0f -- unfaithful)
    //   plain divide, dirTop first  62.3
    //   reciprocal,   dirBot first  61.9
    //   reciprocal,   dirTop first  63.1   <- kept
    // The reciprocal row itself MATCHES in the kept spelling; the 2.5pp is
    // paid in where MSVC schedules the `lis`/`lfs` pair and the regalloc that
    // follows it.
    float invEdgeLenSq = 1.0f / Dot(edgeDir, edgeDir);
    float t = Dot(toSphere, edgeDir) * invEdgeLenSq;

    Vector3 scaled = edgeDir;
    scaled *= t;
    closest -= scaled;

    if (Dot(dir, closest) < 0.0f) {
        return true;
    }
    return Length(closest) < sphereRadius;
}

namespace Hmx {
    /** Dot product of a row of the left-hand transform with a column of the
     * right-hand Matrix4 -- the inner loop of Transform * Matrix4.
     *
     * Same shape (and same reason) as Dot4 in math/Mtx.h: the row comes first
     * so that the caller's `Dot3(t.m.x, b.Col3(0))` evaluates the Col3 call
     * before taking the address of the row. MSVC evaluates arguments right to
     * left, and with the column first the `&t.m.x` computation is hoisted above
     * the call and has to live in a callee-saved register.
     *
     * MSVC swaps the leading pair, so the y term seeds the accumulator to get
     * the z term emitted first. Row z of the transform is the exception -- it
     * comes out z-first from this same spelling, so it uses Dot3ZSeed below. */
    inline float Dot3(const Vector3 &row, const Vector3 &col) {
        float d = col.y * row.y;
        d += col.z * row.z;
        d += col.x * row.x;
        return d;
    }

    /** Dot3 with the seed the other way round; see the note above. */
    inline float Dot3ZSeed(const Vector3 &row, const Vector3 &col) {
        float d = col.z * row.z;
        d += col.y * row.y;
        d += col.x * row.x;
        return d;
    }

    Matrix4 operator*(const Transform &t, const Matrix4 &b) {
        Matrix4 out;

        out.x.x = Dot3(t.m.x, b.Col3(0));
        out.x.y = Dot3(t.m.x, b.Col3(1));
        out.x.z = Dot3(t.m.x, b.Col3(2));
        out.x.w = Dot3(t.m.x, b.Col3(3));

        out.y.x = Dot3(t.m.y, b.Col3(0));
        out.y.y = Dot3(t.m.y, b.Col3(1));
        out.y.z = Dot3(t.m.y, b.Col3(2));
        out.y.w = Dot3(t.m.y, b.Col3(3));

        out.z.x = Dot3ZSeed(t.m.z, b.Col3(0));
        out.z.y = Dot3ZSeed(t.m.z, b.Col3(1));
        out.z.z = Dot3ZSeed(t.m.z, b.Col3(2));
        out.z.w = Dot3ZSeed(t.m.z, b.Col3(3));

        out.w.x = Dot3(t.v, b.Col3(0)) + b.w.x;
        out.w.y = Dot3(t.v, b.Col3(1)) + b.w.y;
        out.w.z = Dot3(t.v, b.Col3(2)) + b.w.z;
        out.w.w = Dot3(t.v, b.Col3(3)) + b.w.w;

        return out;
    }
}

static Transform sIdentityXfm;
static int sIdentityXfmInited;

void NgLight::SetShadowTransforms() {
    if (!(sIdentityXfmInited & 1)) {
        sIdentityXfmInited |= 1;
        Vector3 identityV;
        identityV.Set(0.0f, 0.0f, 0.0f);
        Hmx::Matrix3 identityM;
        identityM.x.Set(1.0f, 0.0f, 0.0f);
        identityM.y.Set(0.0f, 0.0f, 1.0f);
        identityM.z.Set(0.0f, 1.0f, 0.0f);
        sIdentityXfm.m = identityM;
        sIdentityXfm.v = identityV;
    }

    Transform invXfm;
    Invert(WorldXfm(), invXfm);

    Transform lightToWorld;
    Multiply(invXfm, sIdentityXfm, lightToWorld);

    float invRange = 1.0f / mRange;

    Hmx::Matrix4 projMat;
    projMat.x.x = 1.0f; projMat.y.x = 0.0f; projMat.z.x = 0.0f; projMat.w.x = 0.0f;
    projMat.x.y = 0.0f; projMat.y.y = 1.0f; projMat.z.y = 0.0f; projMat.w.y = 0.0f;
    projMat.x.z = 0.0f; projMat.y.z = 0.0f; projMat.z.z = invRange; projMat.w.z = 0.0f;
    projMat.x.w = 0.0f; projMat.y.w = 0.0f; projMat.z.w = (mBotRadius - mTopRadius) * invRange; projMat.w.w = mTopRadius;

    Hmx::Matrix4 shadowMat = lightToWorld * projMat;

    Transform invLight;
    Invert(lightToWorld, invLight);

    TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, shadowMat);
    TheShaderMgr.SetVConstant((VShaderConstant)0x10, Hmx::Matrix4(invLight));
}

void NgLight::RenderShadows(std::vector<RndDrawable *> &shadowCasters) {
    MILO_ASSERT(mShadowRT && !shadowCasters.empty(), 0x112);
    MILO_ASSERT(WantShadows(), 0x113);
#ifdef HX_NATIVE
    // The assert above IS the guard, and it does not stop on native. mShadowRT
    // is null whenever CreateShadowTex() failed (Hmx::Object::New<RndTex> hits a
    // non-fatal MILO_FAIL for an unregistered class and returns null) or when
    // CheckShadowMap's `!mShadowRT && !unk188` arm was skipped because only one
    // of the pair exists. Falling through calls MakeDrawTarget(), a VIRTUAL on
    // RndTex, so it loads a vtable pointer from address 0 -- and every
    // subsequent step (SetAndClearShadowViewport reads mShadowRT->Width(),
    // BlurShadowRT reads dstTex->Width()) does the same. Nothing here can be
    // done without the render target, so do what the assert meant to do.
    if (!mShadowRT || !unk188) {
        return;
    }
#endif
    RndCam *savedCam = RndCam::Current();
    mShadowRT->MakeDrawTarget();
    SetAndClearShadowViewport();
    SetShadowTransforms();
    Rnd::Mode savedDrawMode = TheRnd.DrawMode();
    TheRnd.SetDrawMode(Rnd::kDrawOcclusion);
    for (std::vector<RndDrawable *>::iterator it = shadowCasters.begin(), end = shadowCasters.end();
         it != end;
         ++it) {
        RndDrawable *draw = *it;
        if (draw->Showing()) {
            draw->DrawShowing();
        }
    }
    TheRnd.SetDrawMode(savedDrawMode);
    mShadowRT->FinishDrawTarget();
    BlurShadowRT();
    if (savedCam) {
        savedCam->Select();
    } else {
        TheRnd.GetDefaultCam()->Select();
    }
}

void NgLight::SetAndClearShadowViewport() {
    int width = mShadowRT->Width();
    int height = mShadowRT->Height();
    NgRnd::Viewport vp;
    vp.X = 0;
    vp.Y = 0;
    vp.Width = width;
    vp.Height = height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    TheNgRnd.SetViewport(vp);
    Hmx::Color clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    TheNgRnd.Clear(1, clearColor);
}

void NgLight::CheckShadowMap() {
    if (TheRnd.Drawing()) {
        if (TheShaderMgr.AllowPerPixel()) {
            if (mType == kFakeSpot) {
                if (TheRnd.DrawCount() != unk18c) {
                    bool tempOverride = !mShadowOverride && mShadowObjects.size() != 0;
                    if (tempOverride) {
                        mShadowOverride = &mShadowObjects;
                    }
                    if (WantShadows()) {
                        if (!mShadowRT && !unk188) {
                            mShadowRT = CreateShadowTex();
                            unk188 = CreateShadowTex();
                        }
                        std::vector<RndDrawable *> draws;
                        if (HaveShadows(draws)) {
                            MILO_ASSERT(mShadowRT, 0x81);
                            RenderShadows(draws);
                            mShadowMap = mShadowRT;
                        } else {
                            mShadowMap = TheRnd.GetDefaultTex(Rnd::kDefaultTex_FlatNormal);
                            MILO_ASSERT(mShadowMap, 0x8a);
                        }
                    } else {
                        mShadowMap = TheRnd.GetDefaultTex(Rnd::kDefaultTex_FlatNormal);
                        MILO_ASSERT(mShadowMap, 0x91);
                    }
                    unk18c = TheRnd.DrawCount();
                    if (tempOverride) {
                        mShadowOverride = nullptr;
                    }
                }
            } else {
                RELEASE(mShadowMap);
            }
        }
    }
}

// w7-bt (92.68 -> 100.0 canonical, 143 rows all equal; the 99.9 raw is the
// SetObjConcrete ICF fold adjudicated below).  The w7-ai residual -- one
// extra callee-saved register holding `0x9c + i` across the first
// SetPConstant call (`subi r4, r26, 0x10` for the first index, `mr r4, r26`
// for the second) -- was two source shapes, not register allocation:
//
//   1. The loop is a zero-based counted `for (k = 0; k < 5; k++)` with the
//      tap offset derived as `i = k - 2`, NOT a do/while over `i = -2` with
//      an explicit `taps` down-counter.  MSVC keeps `i` as the register IV
//      (r31: `li r31, -0x2` at 826B9ED4, `extsw r11, r31` at 826B9EF4 for
//      the float conversion) and rewrites both indices `0x8a + k` /
//      `0x9a + k` against it -- `addi r4, r31, 0x8c` at 826B9F18 and
//      `addi r4, r31, 0x9c` at 826B9F5C, each computed from r31 in volatile
//      r4 -- and manufactures the down-counter itself (`li r28, 0x5` at
//      826B9EDC, `subic. r28, r28, 0x1` at 826B9F74).  Written directly as
//      `0x8c + i` / `0x9c + i` on a source-level `i`, the optimizer
//      reassociates the pair into one shared `i + 0x9c` that has to live
//      across the call (the r26 of the w7-ai note); the same reassociation
//      is what the image itself does in DOFProc_NG's SetVHBlurWeights
//      (826ABFB0 `subi r4, r31, 0x10`), where `i` has no other use.
//      Probe ladder: `i + 0x8c` operand order, `0x8cu + i`, a separate
//      `idx` IV (folded back to `i + 0x8e` in r26, 98.41), and
//      `for (i = -2; i < 3)` (drops the down-counter: `cmpwi r31, 3` /
//      `blt`, 95.5) all keep the shared register; `k`-based: 99.15.
//   2. The weight is `kWeights[k]`, not a `pWeight` walked by hand.  With
//      `pWeight = kWeights - 1` and a pre-increment, MSVC hoists the whole
//      `kWeights - 4` into the prologue (r18, the __savegprlr_18 half of
//      the residual); with `*pWeight++` from the bare array it keeps the
//      array address in r20 (826B9E70) and re-derives `subi r27, r20, 0x4`
//      per outer pass (826B9ED8) but orders it before `li r31, -0x2`
//      (99.15); the subscript form gives the image's order and the same
//      `lfsu f0, 0x4(r27)` (826B9F44) -- 99.99.
//
// The last two rows were the order of the two zero stores into the work
// material: the image writes mTexWrap (0xb8, 826B9FF0) before mZMode
// (0x6c, 826B9FF4), and the scheduler only reproduces that with mZMode
// assigned BEFORE mTexWrap in source (the two stores share r21 = 0 and are
// otherwise unordered).
//
// Adjudicated and NOT a wrong callee: row 111 pairs
// SetObjConcrete<ObjRefConcrete<AnimTask,ObjectDir>> against our
// SetObjConcrete<ObjRefConcrete<RndTex,ObjectDir>>. Both are at 82401CD0 in
// icf_aliases.map -- a proven ICF fold, so the target-side name is only the
// fold representative. `workMat->SetDiffuseTex(srcTex)` is correct as written.
void NgLight::BlurShadowRT() {
    static const float kWeights[] = { 0.1f, 0.25f, 0.3f, 0.25f, 0.1f };
    float blurX = 1.0f;
    float blurDir = 0.0f;
    int pass = 0;
    do {
        RndTex *srcTex, *dstTex;
        if (pass == 0) {
            srcTex = mShadowRT;
            dstTex = unk188;
        } else {
            srcTex = unk188;
            dstTex = mShadowRT;
            float tmp = blurX;
            blurX = blurDir;
            blurDir = tmp;
        }

        int w = dstTex->Width();
        int h = dstTex->Height();

        Hmx::Rect rect(0.0f, 0.0f, (float)(long long)w, (float)(long long)h);
        TheShaderMgr.SetNumTaps(5);

        float invW = 1.0f / (float)(long long)w;
        float invH = 1.0f / (float)(long long)h;

        for (int k = 0; k < 5; k++) {
            int i = k - 2;
            Vector4 offset(
                (float)((float)((float)(long long)i * invW) * blurX),
                (float)((float)((float)(long long)i * invH) * blurDir),
                1.0f, 1.0f
            );
            TheShaderMgr.SetPConstant((PShaderConstant)(0x8a + k), offset);

            float wt = kWeights[k];
            Vector4 weight(wt, wt, wt, wt);
            TheShaderMgr.SetPConstant((PShaderConstant)(0x9a + k), weight);
        }

        TheRenderState.SetTextureFilter(0, (RndRenderState::FilterMode)1, false);
        TheRenderState.SetTextureFilter(6, (RndRenderState::FilterMode)1, false);

        dstTex->MakeDrawTarget();

        RndMat *workMat = TheShaderMgr.GetWork();
        workMat->SetDiffuseTex(srcTex);
        workMat->mBlend = BaseMaterial::kBlendSrc;
        workMat->mZMode = kZModeDisable;
        workMat->mTexWrap = kTexWrapClamp;
        workMat->MarkDirty(2);

        Hmx::Color color;
        TheNgRnd.DrawRect(rect, workMat, (ShaderType)1, color, nullptr, nullptr);

        dstTex->FinishDrawTarget();
        pass++;
        TheShaderMgr.SetNumTaps(1);
    } while (pass < 2);
}

void NgLight::Init() {
    REGISTER_OBJ_FACTORY(NgLight);
    PhysMemTypeTracker tracker("D3D(phys):NgLight");
}
