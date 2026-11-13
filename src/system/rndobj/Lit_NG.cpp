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

// Lane history (50 lines moved): docs/decomp/notes/system/rndobj/Lit_NG.md
// > RESIDUAL (w7-as, 63.1 canonical): instructions 5..62 match EXACTLY -- both WorldXfm()
// > expansions, `sc -= xfm1.v`, the three fmuls and both early returns.
// > Verdicts: behaviour bug (w31-c)
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

    dirTop *= mTopRadius;
    topPoint += dirTop;

    axisRange *= range;
    botPoint += axisRange;

    toSphere -= topPoint;

    dirBot *= mBotRadius;
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
    // Lane history (10 lines moved): docs/decomp/notes/system/rndobj/Lit_NG.md
    // > NEGATIVE RESULT (w7-as, 2026-09-14): this is a deliberate LOSS.
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
     * comes out z-first from this same spelling, so it uses Dot3ZSeed below.
     *
     * The variants below all compute exactly ((y*y + z*z) + x*x) (or
     * ((z*z + y*y) + x*x) for the ZSeed pair) -- same operations, same
     * association, so the native result is bit-identical whichever one a
     * call site uses. They differ only in how many named locals they declare.
     * c2 numbers the symbols of each inlined body in order, so every extra
     * local pushes up the symbol ids of everything the *later* calls inline,
     * including the column reference. The x-term multiply is commutative and
     * c2 orders its operands by key (§8.7 in C2RS-BRIDGE.md: a named
     * variable's key is 0x10000|sid<<5, higher key first), so the column ids
     * decide whether the image's `fmuls col, row` or `fmuls row, col` comes
     * out. The per-call choice below reproduces the image's order for all 16
     * x terms; XTerm additionally names the x product so its multiply is
     * keyed as a separate expression. */
    inline float Dot3(const Vector3 &row, const Vector3 &col) {
        float d = col.y * row.y;
        d += col.z * row.z;
        d += col.x * row.x;
        return d;
    }

    inline float Dot3XTerm(const Vector3 &row, const Vector3 &col) {
        float d = col.y * row.y;
        d += col.z * row.z;
        float xTerm = col.x * row.x;
        d += xTerm;
        return d;
    }

    /** Dot3XTerm plus two (resp. three) pass-through copies of the result:
     * symbol-count padding only, see above. */
    inline float Dot3XTermPad2(const Vector3 &row, const Vector3 &col) {
        float d = col.y * row.y;
        d += col.z * row.z;
        float xTerm = col.x * row.x;
        d += xTerm;
        float sum = d;
        float result = sum;
        return result;
    }

    inline float Dot3XTermPad3(const Vector3 &row, const Vector3 &col) {
        float d = col.y * row.y;
        d += col.z * row.z;
        float xTerm = col.x * row.x;
        d += xTerm;
        float sum = d;
        float total = sum;
        float result = total;
        return result;
    }

    /** Dot3 with the seed the other way round; see the note above. */
    inline float Dot3ZSeed(const Vector3 &row, const Vector3 &col) {
        float d = col.z * row.z;
        d += col.y * row.y;
        d += col.x * row.x;
        return d;
    }

    inline float Dot3ZSeedXTerm(const Vector3 &row, const Vector3 &col) {
        float d = col.z * row.z;
        d += col.y * row.y;
        float xTerm = col.x * row.x;
        d += xTerm;
        return d;
    }

    Matrix4 operator*(const Transform &t, const Matrix4 &b) {
        Matrix4 out;
        Vector4 &outX = out.x;
        const Vector3 &rowX = t.m.x;

        outX.x = Dot3XTerm(rowX, b.Col3(0));
        outX.y = Dot3XTermPad2(rowX, b.Col3(1));
        outX.z = Dot3XTermPad3(rowX, b.Col3(2));
        outX.w = Dot3(rowX, b.Col3(3));

        out.y.x = Dot3(t.m.y, b.Col3(0));
        out.y.y = Dot3XTermPad3(t.m.y, b.Col3(1));
        out.y.z = Dot3XTerm(t.m.y, b.Col3(2));
        out.y.w = Dot3XTerm(t.m.y, b.Col3(3));

        out.z.x = Dot3ZSeedXTerm(t.m.z, b.Col3(0));
        out.z.y = Dot3ZSeed(t.m.z, b.Col3(1));
        out.z.z = Dot3ZSeed(t.m.z, b.Col3(2));
        out.z.w = Dot3ZSeedXTerm(t.m.z, b.Col3(3));

        out.w.x = Dot3XTerm(t.v, b.Col3(0)) + b.w.x;
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

// Lane history (44 lines moved): docs/decomp/notes/system/rndobj/Lit_NG.md
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
