// Native-shadow regression tests: HamCharacter::Poll.
//
// HamCharacter::Poll runs every frame for every dancer. The native build
// compiled its own #ifdef HX_NATIVE body, written before the decompiled one
// (793a8a4e5), which stopped after Character::Poll and so dropped two whole
// per-frame effects the Xbox image performs (HamCharacter.s,
// ?Poll@HamCharacter@@UAAXXZ at 82491378):
//
//   1. The prop-attach blend (82491438..82491558): find bone_prop0.mesh and
//      spot_prop0.mesh; blend weight = 0 while a song animation is active
//      (SongAnimation() != -1), else EvaluateFlags(2) of the driver's first
//      clip, else 1; bone_prop0's world xfm := Interp(spot, bone, weight)
//      (Interp(Vector3) 82491504, Interp(Quat) 82491518, MakeRotMatrix
//      8249154C, SetWorldXfm 82491558).
//   2. The robot viseme swap (8249155C..824916B4): find robot_face.mat; the
//      heaviest clip of face.lipdrv's playback names a texture
//      (_strlwr 82491630, ".tex"), falling back to "base.tex"
//      (Find<RndTex> 82491678 / 82491694); the found texture becomes the
//      material's diffuse tex (SetObjConcrete on mat+0x40, mDirty |= 2).
//
// Every test here was watched FAILING against the pre-fix native body.

#include "test_helpers.h"

#include "char/CharClip.h"
#include "char/CharLipSync.h"
#include "char/CharLipSyncDriver.h"
#include "hamobj/HamCharacter.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Tex.h"

#include <cmath>

namespace {

class NativeShadowHamCharTest : public EngineTestFixture {
protected:
    void SetUp() override {
        mChar = Hmx::Object::New<HamCharacter>();
        ASSERT_NE(mChar, nullptr);
        mChar->SetName("shadow_hamchar", ObjectDir::Main());
    }
    void TearDown() override {
        delete mChar;
        mChar = nullptr;
    }

    template <class T>
    T *Add(const char *name) {
        T *obj = Hmx::Object::New<T>();
        obj->SetName(name, mChar);
        return obj;
    }

    HamCharacter *mChar = nullptr;
};

static Transform MakeXfm(float x, float y, float z, float yawRadians) {
    Transform t;
    t.Reset();
    float c = std::cos(yawRadians), s = std::sin(yawRadians);
    // rotation about z
    t.m.x.Set(c, s, 0);
    t.m.y.Set(-s, c, 0);
    t.m.z.Set(0, 0, 1);
    t.v.Set(x, y, z);
    return t;
}

static void ExpectXfmNear(const Transform &a, const Transform &b, const char *what) {
    const float eps = 1e-4f;
    EXPECT_NEAR(a.v.x, b.v.x, eps) << what;
    EXPECT_NEAR(a.v.y, b.v.y, eps) << what;
    EXPECT_NEAR(a.v.z, b.v.z, eps) << what;
    EXPECT_NEAR(a.m.x.x, b.m.x.x, eps) << what;
    EXPECT_NEAR(a.m.x.y, b.m.x.y, eps) << what;
    EXPECT_NEAR(a.m.y.x, b.m.y.x, eps) << what;
    EXPECT_NEAR(a.m.y.y, b.m.y.y, eps) << what;
    EXPECT_NEAR(a.m.z.z, b.m.z.z, eps) << what;
}

// ---------------------------------------------------------------------------
// With no song driver and no camera skeleton, SongAnimation() is 0 (not -1),
// so the image blends with weight 0 (82491478 cmpwi r3,-1; bne -> fmr f31,f30
// = 0.0) and Interp(spot, bone, 0) snaps bone_prop0 onto spot_prop0.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowHamCharTest, PollSnapsBonePropOntoSpotPropDuringSongAnimation) {
    RndMesh *bone = Add<RndMesh>("bone_prop0.mesh");
    RndMesh *spot = Add<RndMesh>("spot_prop0.mesh");
    Transform boneXfm = MakeXfm(1, 2, 3, 0);
    Transform spotXfm = MakeXfm(10, 20, 30, 0.5f);
    bone->SetLocalXfm(boneXfm);
    spot->SetLocalXfm(spotXfm);
    ASSERT_NE(mChar->SongAnimation(), -1) << "fixture precondition: weight-0 arm";

    mChar->Poll();

    ExpectXfmNear(bone->WorldXfm(), spotXfm,
                  "bone_prop0 must take spot_prop0's world xfm (weight 0)");
    ExpectXfmNear(spot->WorldXfm(), spotXfm, "spot_prop0 itself is untouched");
}

// ---------------------------------------------------------------------------
// Control for the test above, and a pin for the null-driver guard: with the
// camera skeleton on and no driver, SongAnimation() is -1; the image then
// reads mDriver->First() with no null test (82491488 lwz r3,0xa0(r29);
// lwz r11,0x58(r3)) -- a zero-page read on the 360 that yields 0, so the
// weight stays 1 and bone_prop0 keeps its own xfm.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowHamCharTest, PollKeepsBonePropAtWeightOneWithoutDriver) {
    RndMesh *bone = Add<RndMesh>("bone_prop0.mesh");
    RndMesh *spot = Add<RndMesh>("spot_prop0.mesh");
    Transform boneXfm = MakeXfm(1, 2, 3, 0.25f);
    bone->SetLocalXfm(boneXfm);
    spot->SetLocalXfm(MakeXfm(10, 20, 30, 0.5f));
    mChar->SetUseCameraSkeleton(true);
    ASSERT_EQ(mChar->Driver(), nullptr);
    ASSERT_EQ(mChar->SongAnimation(), -1) << "fixture precondition: weight-1 arm";

    mChar->Poll();

    ExpectXfmNear(bone->WorldXfm(), boneXfm, "bone_prop0 keeps its xfm (weight 1)");
}

// ---------------------------------------------------------------------------
// face.lipdrv with no playback: clipName stays "base" (82491590 addi r5,
// "base"; beq past the loop when lwz 0x88 is 0), "base.tex" is found, and it
// becomes robot_face.mat's diffuse texture.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowHamCharTest, PollSetsRobotFaceToBaseVisemeWithoutPlayback) {
    RndMat *mat = Add<RndMat>("robot_face.mat");
    Add<CharLipSyncDriver>("face.lipdrv");
    RndTex *base = Add<RndTex>("base.tex");
    RndTex *other = Add<RndTex>("aa.tex");
    mat->SetDiffuseTex(other);
    ASSERT_EQ(mat->GetDiffuseTex(), other);

    mChar->Poll();

    EXPECT_EQ(mat->GetDiffuseTex(), base)
        << "robot_face.mat must show base.tex when no viseme is playing";
}

// ---------------------------------------------------------------------------
// With a playback, the image walks its 32-byte weights (824915C0..82491608):
// a null clip is skipped (lwz r9,0xc; beq), fsel keeps the running max, and
// the clip name is taken only when the max CHANGED -- so the first clip to
// reach the max wins a tie. The name is _strlwr'd (82491630) and ".tex" is
// appended before the Find<RndTex> at 82491678.
// ---------------------------------------------------------------------------
class ProbeLipDriver : public CharLipSyncDriver {
public:
    ProbeLipDriver() {}
    void SetMainPlayback(CharLipSync::PlayBack *pb) { mMainPlayback = pb; }
};

TEST_F(NativeShadowHamCharTest, PollPicksHeaviestVisemeTextureLowercased) {
    RndMat *mat = Add<RndMat>("robot_face.mat");
    ProbeLipDriver *drv = new ProbeLipDriver();
    drv->SetName("face.lipdrv", mChar);
    CharClip *aa = Add<CharClip>("Viseme_AA");
    CharClip *oh = Add<CharClip>("Viseme_OH");
    CharClip *ee = Add<CharClip>("Viseme_EE");
    Add<RndTex>("base.tex");
    Add<RndTex>("viseme_aa.tex");
    RndTex *ohTex = Add<RndTex>("viseme_oh.tex");
    Add<RndTex>("viseme_ee.tex");

    CharLipSync::PlayBack *pb = new CharLipSync::PlayBack();
    pb->mWeights.resize(4);
    pb->mWeights[0].mClip = aa;
    pb->mWeights[0].mCurWeight = 0.2f;
    pb->mWeights[1].mClip = nullptr; // skipped: heaviest, but no clip
    pb->mWeights[1].mCurWeight = 0.9f;
    pb->mWeights[2].mClip = oh;
    pb->mWeights[2].mCurWeight = 0.7f;
    pb->mWeights[3].mClip = ee; // ties OH: max does not change, OH stays
    pb->mWeights[3].mCurWeight = 0.7f;
    drv->SetMainPlayback(pb);

    mChar->Poll();

    EXPECT_EQ(mat->GetDiffuseTex(), ohTex)
        << "expected viseme_oh.tex; got "
        << (mat->GetDiffuseTex() ? mat->GetDiffuseTex()->Name() : "(null)");
}

// ---------------------------------------------------------------------------
// No face.lipdrv at all: the image does Find<CharLipSyncDriver> and then
// `lwz r11, 0x88(r3)` with no null test (8249158C). On the 360 that reads the
// zero page (0), so the playback is null and it still falls back to "base".
// Natively that load must be guarded to reproduce the same result.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowHamCharTest, PollFallsBackToBaseVisemeWithoutLipDriver) {
    RndMat *mat = Add<RndMat>("robot_face.mat");
    RndTex *base = Add<RndTex>("base.tex");
    ASSERT_EQ(mat->GetDiffuseTex(), nullptr);

    mChar->Poll();

    EXPECT_EQ(mat->GetDiffuseTex(), base);
}

// ---------------------------------------------------------------------------
// No viseme texture and no base.tex: the image leaves the material alone and
// notifies once (824916B8..8249175C). Control that the swap is not
// unconditional.
// ---------------------------------------------------------------------------
TEST_F(NativeShadowHamCharTest, PollLeavesRobotFaceAloneWhenNoVisemeTexture) {
    RndMat *mat = Add<RndMat>("robot_face.mat");
    RndTex *other = Add<RndTex>("aa.tex");
    mat->SetDiffuseTex(other);

    mChar->Poll();

    EXPECT_EQ(mat->GetDiffuseTex(), other);
}

} // namespace
