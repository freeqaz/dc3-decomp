#include "gesture\DepthBuffer3D.h"
#include "gesture\BaseSkeleton.h"
#include "gesture\GestureMgr.h"
#include "gesture\JointUtl.h"
#include "gesture\LiveCameraInput.h"
#include "gesture\Skeleton.h"
#include "hamobj\HamGameData.h"
#include "hamobj\HamPlayerData.h"
#include "hamobj\RhythmDetector.h"
#include "math\Mtx.h"
#include "obj/Object.h"
#include "math\Utl.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "rnddx9\Rnd.h"
#include "rnddx9\RenderState.h"
#include "rndobj\Draw.h"
#include "rndobj\Rnd.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\ShaderOptions.h"
#include "rndobj\Tex.h"
#include "rndobj\Trans.h"
#include <math.h>

LargeQuadRenderData DepthBuffer3D::mQuad;

namespace {
    void JointToVertexData(
        Vector3 &out, const Skeleton &skeleton, SkeletonJoint joint, const Vector4 &bounds
    ) {
        Vector3 screenPos;
        JointScreenPos(skeleton.TrackedJoints()[joint], screenPos);
        // Set(), not three assignments: `out` is a Vector3& that may alias the
        // `const Vector4&` bounds, so an early store to out.y pins every later
        // bounds load below it. The image issues all four bounds loads and both
        // fdivs before the first store (0x82DED7F0..0x82DED83C all precede
        // `stfs f6, 0x4(r31)` at 0x82DED844).
        // REFUTED (3 variants, all byte-identical at 99.8): hoisting the x
        // expression into a named local ahead of the z one, hoisting both, and
        // hoisting just the two divisions. MSVC evaluates the two component
        // expressions right-to-left regardless, so the z formula's four loads
        // come first where the image's x formula's do; 10 rows, no register or
        // stack difference, arithmetic identical.
        out.Set(
            ((screenPos.x - bounds.x) / (bounds.z - bounds.x) - 0.5f) * 318.0f - 1.0f,
            screenPos.z,
            (0.5f - (screenPos.y - bounds.y) / (bounds.w - bounds.y)) * 238.0f - 1.0f
        );
    }

    void VertexToWorld(
        Vector3 &pos, const Transform &xfm, float stretchNearCamera, const Vector4 &depthRange
    ) {
        float depth = (pos.y - 256.0f) * (1.0f / 4096.0f);
        pos.y = depth;
        pos.y = 1.0f - (depth - depthRange.x) / (depthRange.y - depthRange.x);
        // Clamp reads pos.y back rather than a local: the image keeps the store
        // at 0x82DED8E0, which is dead-store-eliminated if the clamp takes a
        // register temp instead.
        pos.y = Clamp(0.0f, 1.0f, pos.y);
        pos.y = (float)pow((double)pos.y, (double)stretchNearCamera) * -200.0f;
        // Multiply(v, Matrix3, out) -- not three open-coded assignments.  Its
        // Vector3::Set computes all three components before ANY store, which is
        // what lets the image hoist all nine xfm loads above the first
        // `stfs ..., 0x0(r31)` (0x82DED918..0x82DED93C, all before 0x82DED958).
        // Written out, the store to pos.x may alias xfm and pins every later
        // load below it.
        Multiply(pos, xfm.m, pos);
    }

    RndMat *SetUpWorkingMat() {
        RndMat *mat = TheShaderMgr.GetWork();
        mat->SetBlend(BaseMaterial::kBlendSrc);
        mat->SetZMode(kZModeDisable);
        mat->SetTexWrap(kTexWrapClamp);
        return mat;
    }
}

DepthBuffer3D::DepthBuffer3D()
    : mDrawSheet(0), mDrawPlayer1(1), mDrawPlayer2(1), mDrawNonPlayers(1),
      mDebugLayout(0), mNobodyColor(0, 0, 0, 0), mPlayerPalette(this), mBoxymanPalette(this),
      mBoxymanPaletteAnim(1), mPlayerPaletteOffset(0), mPlayerPaletteScale(1), mMinimalMat(this),
      mMesh(this), mStretchNearCamera(1), mOpacity(1), mPlayer1Grooviness(0), mPlayer2Grooviness(0),
      mForceDrawSkeletonIdx(0xfffffc19), mForceDrawEnabled(1), mPlayerPaletteTex(this), mTile(1.5, 1.5), mScaleVoxel(1),
      mScaleVoxelGap(1), mFishEyeX(0), mFishEyeY(0), mGroovinessDetector1(this), mGroovinessDetector2(this),
      unk20c(80, 4, 4), unk220(40, 4, 4), unk234(60, 3, 3), unk248(30, 3, 3),
      unk25c(2048, 204.8f, 204.8f), unk270(4096, 204.8f, 204.8f), mMaxZoom(1),
      mMaxDepthZoom(1), unk28c(0) {}

DepthBuffer3D::~DepthBuffer3D() {}

BEGIN_HANDLERS(DepthBuffer3D)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(RndTransformable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(DepthBuffer3D)
    SYNC_PROP(nobody_color, mNobodyColor)
    SYNC_PROP(nobody_alpha, mNobodyColor.alpha)
    SYNC_PROP_SET(
        player_palette, mPlayerPalette.Ptr(), SetPlayerPalette(_val.Obj<RndTex>())
    )
    SYNC_PROP(player_palette_offset, mPlayerPaletteOffset)
    SYNC_PROP(player_palette_scale, mPlayerPaletteScale)
    SYNC_PROP(minimal_mat, mMinimalMat)
    SYNC_PROP(draw_sheet, mDrawSheet)
    SYNC_PROP(mesh, mMesh)
    SYNC_PROP(stretch_near_camera, mStretchNearCamera)
    SYNC_PROP(opacity, mOpacity)
    SYNC_PROP(draw_player_1, mDrawPlayer1)
    SYNC_PROP(draw_player_2, mDrawPlayer2)
    SYNC_PROP(draw_non_players, mDrawNonPlayers)
    SYNC_PROP(tile_x, mTile.x)
    SYNC_PROP(tile_y, mTile.y)
    SYNC_PROP(scale_voxel, mScaleVoxel)
    SYNC_PROP(scale_voxelgap, mScaleVoxelGap)
    SYNC_PROP(fisheye_x, mFishEyeX)
    SYNC_PROP(fisheye_y, mFishEyeY)
    SYNC_PROP(max_zoom, mMaxZoom)
    SYNC_PROP(max_depth_zoom, mMaxDepthZoom)
    SYNC_PROP(debug_layout, mDebugLayout)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(RndTransformable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void DepthBuffer3D::Init() {
    REGISTER_OBJ_FACTORY(DepthBuffer3D);
    TheNgRnd.CreateLargeQuad(0x140, 0xF0, mQuad);
}

void DepthBuffer3D::UpdateAttachment(
    DepthBuffer3DAttachment &attachment, const Vector4 &v1, const Vector4 &v2
) {
    int skelIdx = TheGestureMgr->GetSkeletonIndexByTrackingID(
        TheGameData->Player(attachment.player)->GetSkeletonTrackingID()
    );
    Vector3 newPos;
    // w14-f: 93.47 -> 100.  The image tests skelIdx + 1 and passes
    // (skelIdx + 1) - 1 to GetSkeleton (`addic. r11, r3, 0x1` / `subi r4, r11,
    // 0x1`), and accumulates into newPos in place: newPos is seeded with
    // localXfm.v (its 0x50 slot is the localXfm.v copy) and `+= pos`.  The old
    // NEGATIVE RESULT note (separate localPos/pos locals, Add into newPos) is
    // superseded.  b5 declared after skelNum keeps it out of a callee-saved reg.
    int skelNum = skelIdx + 1;
    bool b5 = false;
    if (skelNum > 0) {
        const Transform &localXfm = LocalXfm();
        Skeleton &skeleton = TheGestureMgr->GetSkeleton(skelNum - 1);
        newPos = localXfm.v;
        Vector3 pos;
        JointToVertexData(pos, skeleton, (SkeletonJoint)attachment.mJoint, v1);
        VertexToWorld(pos, localXfm, mStretchNearCamera, v2);
        newPos += pos;
        attachment.obj->SetTransConstraint(mConstraint, nullptr, false);
        Normalize(localXfm.m, attachment.obj->DirtyLocalXfm().m);
        b5 = true;
    }
    if (!b5) {
        newPos.Set(100000, 100000, 100000);
    }
    attachment.obj->SetLocalPos(newPos);
}

void DepthBuffer3D::AddAttachment(const DepthBuffer3DAttachment &attachment) {
    MILO_ASSERT(attachment.obj, 0x390);
    // RESIDUAL (w8-r, 97.714, 3 rows -- 1 branch-address, 2 real).  The
    // image's post-loop test is `li r10, 0x0` immediately followed by
    // `cmplwi cr6, r10, 0x0` (0x82DF14F8/0x82DF14FC): a compare of a register
    // just set to zero AGAINST zero, which constant propagation would have
    // deleted inside one function body.  It survives only across an inline
    // boundary -- the `return nullptr` fall-off of an inlined helper plus the
    // caller's own `if (!ret)` -- and the image's r10 doubles as the loop
    // iterator, so the sentinel is a POINTER, never an iterator re-compared
    // against end().  Two reconstructions MEASURED and both worse than this
    // spelling: (a) an inline loop with a local `existing` pointer sentinel
    // scores 95.714 -- MSVC folds the test away completely, leaving all three
    // dispatch instructions target-only; (b) an anonymous-namespace
    // `FindAttachment(vector&, RndTransformable*)` helper DOES reproduce the
    // `li rN, 0` + `cmplwi 0` pair exactly, but scores 94.286: it moves `this`
    // from r30 to r29 and `0x24` from r29 to r30 (21 rename rows) and forces
    // `begin()` to be reloaded after the loop (`lwz r10, 0x0(r31)`) where the
    // image keeps the pre-loop value live in r11.  The sentinel is real; the
    // spelling that buys it has not been found.
    std::vector<DepthBuffer3DAttachment>::iterator it;
    for (it = mAttachments.begin(); it != mAttachments.end(); ++it) {
        if (it->obj == attachment.obj) {
            break;
        }
    }
    if (it == mAttachments.end()) {
        mAttachments.resize(mAttachments.size() + 1);
        DepthBuffer3DAttachment &back = mAttachments[mAttachments.size() - 1];
        back = attachment;
        back.unk20 = (int)back.obj->TransParent();
        back.obj->SetTransParent(mParent, false);
        back.obj->SetTransConstraint(mConstraint, nullptr, false);
    }
}

void DepthBuffer3D::SetPlayerPalette(RndTex *tex) {
    if (tex && mPlayerPalette != tex) {
        if (mBoxymanPaletteAnim != 1) {
            MILO_WARN_ONCE("dropping boxyman palette animation %f\n", mBoxymanPaletteAnim);
        }
        mBoxymanPaletteAnim = 0;
        if (mBoxymanPalette) {
            mBoxymanPalette = mPlayerPalette;
        }
        mPlayerPalette = tex;
    }
}

void DepthBuffer3D::SetGrooviness(float f1) {
    mPlayer1Grooviness = f1;
    mPlayer2Grooviness = f1;
    mGroovinessDetector1 = nullptr;
    mGroovinessDetector2 = nullptr;
}

void DepthBuffer3D::SetGrooviness(RhythmDetector *r1, RhythmDetector *r2) {
    mGroovinessDetector1 = r1;
    mGroovinessDetector2 = r2;
}

void DepthBuffer3D::ForceDrawSkeletonIndex(int i1, bool b2) {
    mForceDrawSkeletonIdx = i1;
    mForceDrawEnabled = b2;
}

void DepthBuffer3D::ListDrawChildren(std::list<RndDrawable *> &out) {
    if (mMesh.Ptr() != nullptr) {
        out.push_back(mMesh.Ptr());
    }
}

void DepthBuffer3D::DrawMesh() {
    mMesh->SetShowing(true);
    Transform savedXfm = mMesh->LocalXfm();
    mMesh->SetLocalXfm(WorldXfm());
    int savedVersion = mMesh->mMeshVersion;
    mMesh->mMeshVersion = 0x1d;
    Vector4 v(mTile.x, mTile.y, 0.0f, 0.0f);
    for (float i = 0.0f; i < v.x; i += 1.0f) {
        v.z = i;
        for (float j = 0.0f; j < v.y; j += 1.0f) {
            v.w = j;
            TheShaderMgr.SetVConstant((VShaderConstant)0x44, v);
            mMesh->DrawShowing();
        }
    }
    mMesh->mMeshVersion = savedVersion;
    mMesh->SetLocalXfm(savedXfm);
    mMesh->SetShowing(false);
}

#ifdef HX_NATIVE
void DepthBuffer3D::DrawShowing() {
    // Kinect depth rendering not available on native
}
void DepthBuffer3D::Save(BinStream &) {}
void DepthBuffer3D::Copy(const Hmx::Object *, Hmx::Object::CopyType) {}
void DepthBuffer3D::Load(BinStream &) {}
#else

BEGIN_SAVES(DepthBuffer3D)
    SAVE_REVS(11, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    SAVE_SUPERCLASS(RndTransformable)
    bs << mNobodyColor << mPlayerPalette << mPlayerPaletteOffset;
    bs << mMinimalMat << mDrawSheet << mMesh;
    bs << mPlayerPaletteScale << mStretchNearCamera << mOpacity;
    bs << mDrawPlayer1 << mDrawPlayer2 << mDrawNonPlayers;
    bs << mTile << mScaleVoxel << mScaleVoxelGap << mFishEyeX << mFishEyeY;
    bs << mMaxZoom << mDebugLayout << mMaxDepthZoom;
END_SAVES

INIT_REVS(11, 0)

// COMDAT-SELECTION LEVER: MEASURED, nothing to fix in this unit (w8-r).
// The mechanism is NOT "add `inline` to make the callee a COMDAT" -- that was
// retracted.  MSVC/Xenon puts every function we compile into its own COMDAT, so
// the map's flag column reports the COMDAT *selection type* in the section
// symbol's aux record at offset 14 (`f i` = IMAGE_COMDAT_SELECT_ANY, bare `f` =
// NODUPLICATES), and the dtk-carved target objects carry no selection byte at
// all, so the map is its only carrier.  Reasoning from where our source puts a
// definition is therefore not evidence; run
// scripts/analysis/comdat_selection_audit.py.  Measured over
// build/373307D9/src/system/gesture/DepthBuffer3D.obj: ZERO selection
// mismatches in either direction.
//
// DrawShowing does carry this lane's one real save-set difference -- the image
// calls __savefpr_18 / __restfpr_18 (f18-f31, 14 FPRs, at 0x82DEF8BC and
// 0x82DF0CEC) where we call __savefpr_17 / __restfpr_17 (f17-f31, 15), i.e. WE
// hold one extra callee-saved FPR, alongside a +0x10 frame delta -- but do NOT
// read that as a lead for the selection lever.  Measured elsewhere: a sibling
// lane's RndSpotlight::BuildBeam is the same textbook symptom (image
// `bl __savegprlr_14`, ours `_16`) and stayed BYTE-IDENTICAL at 85.3415 after
// its same-TU callee's selection class was matched, and all four selection
// closures verified anywhere so far were fidelity-only with zero score
// movement.  The extra FPR is the "double literal masquerades as an FPR floor"
// question instead: we hold one value in a callee-saved FPR that the image
// spills with stfd.
//
// Worth knowing regardless: both funclets fn_82DF0D1C and fn_82DF0D6C are
// DrawShowing's (they immediately follow it and its __unwind$ in
// build/373307D9/asm/system/gesture/DepthBuffer3D.s), and their single charged
// row IS that frame delta -- `subi r31, r12, 0x250` against our 0x260.  So
// closing DrawShowing's frame banks 80 B and 2 functions with it.
//
// w18-c: Load 77.336 -> 100.  The reads are CHAINED `>>` expressions.  The image
// feeds each call's returned reference straight into the next one -- `mr r29, r3`
// after operator>>(BinStream &, Color &) and r29 as the stream for the three
// reads after it; BinStreamRev::operator>>(bool &)'s r3 passed unchanged as the
// next `this`; `lwz r4, 0x8(r3)` (the returned BinStreamRev's .stream) for
// mMesh.  Because the chained calls hand &d to code that returns an opaque
// reference, MSVC can no longer keep d.rev / d.stream in registers, which is
// the "reloaded at every use" storage the w8-r survey saw (0x60/0x68(r1), frame
// 0xa0).  Same reads, same order.  (The lbl_82251220-vs-gRev row is the
// INIT_REVS .rdata pair's target-side name, not a defect.)
BEGIN_LOADS(DepthBuffer3D)
    LOAD_REVS(bs)
    ASSERT_REVS(11, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    LOAD_SUPERCLASS(RndTransformable)
    if (d.rev > 1) {
        d.stream >> mNobodyColor >> mPlayerPalette >> mPlayerPaletteOffset >> mMinimalMat;
        if (d.rev < 3) {
            int dummy;
            d >> dummy;
        }
    }
    if (d.rev > 2) {
        d >> mDrawSheet >> mMesh;
    }
    if (d.rev > 3) {
        d >> mPlayerPaletteScale;
        d >> mStretchNearCamera;
    }
    if (d.rev > 4) {
        d >> mOpacity;
    }
    if (d.rev > 5) {
        d >> mDrawPlayer1 >> mDrawPlayer2 >> mDrawNonPlayers;
    }
    if (d.rev > 6) {
        d.stream >> mTile >> mScaleVoxel >> mScaleVoxelGap;
    }
    if (d.rev > 7) {
        d >> mFishEyeX;
    }
    if (d.rev > 8) {
        d >> mFishEyeY;
    }
    if (d.rev > 9) {
        d >> mMaxZoom;
        d >> mDebugLayout;
    }
    if (d.rev > 10) {
        d >> mMaxDepthZoom;
    }
END_LOADS

BEGIN_COPYS(DepthBuffer3D)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    COPY_SUPERCLASS(RndTransformable)
    CREATE_COPY(DepthBuffer3D)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mNobodyColor)
        COPY_MEMBER(mPlayerPalette)
        COPY_MEMBER(mPlayerPaletteOffset)
        COPY_MEMBER(mPlayerPaletteScale)
        COPY_MEMBER(mMinimalMat)
        COPY_MEMBER(mDrawSheet)
        COPY_MEMBER(mMesh)
        COPY_MEMBER(mStretchNearCamera)
        COPY_MEMBER(mOpacity)
        COPY_MEMBER(mDrawPlayer1)
        COPY_MEMBER(mDrawPlayer2)
        COPY_MEMBER(mDrawNonPlayers)
        COPY_MEMBER(mTile)
        COPY_MEMBER(mScaleVoxel)
        COPY_MEMBER(mScaleVoxelGap)
        COPY_MEMBER(mFishEyeX)
        COPY_MEMBER(mFishEyeY)
    END_COPYING_MEMBERS
END_COPYS

void DepthBuffer3D::DrawShowing() {
    if (TheRnd.DrawMode() != Rnd::kDrawNormal || !Showing()) {
        return;
    }

    mBoxymanPaletteAnim += 0.0333f;
    if (mBoxymanPaletteAnim > 1.0f) {
        mBoxymanPaletteAnim = 1.0f;
    }

    // w18-c (DrawShowing 72.6 canonical): the image keeps `mat` in a frame slot
    // (stw to 0x60(r31) on both arms, `lwz r22, 0x60(r31)` where it is next
    // needed) and we keep it in r22.  `mMinimalMat ? mMinimalMat.Ptr() :
    // SetUpWorkingMat()` reproduces the image's two-store arm shape but scores
    // 72.1 (the value is still register-held).  Other open items: d38/d44 (60.0f,
    // 80.0f) are loaded into FPRs up front on our side only, and most of the
    // remaining rows are the per-pixel loop's register assignment.
    RndMat *mat = mMinimalMat.Ptr();
    if (mat == nullptr) {
        mat = SetUpWorkingMat();
    }

    // BEHAVIOUR FIX (w18-c): the depth texture defaults to mPlayerPaletteTex, not
    // to null.  The image loads mPlayerPaletteTex (`lwz r11, 0x198(r26)`), tests
    // it, and on the non-null path does `mr r21, r11` then branches straight to
    // the SetDiffuseTex(r21) / SetVertShaderTex(r21) block at 0x82DF04xx; r21 is
    // the camera's depth stream texture only on the null path (`mr. r21, r3`
    // after GetStreamTex).  We used to hand a null texture to the material and
    // the vertex shader whenever a palette texture was set.
    RndTex *depthTex = mPlayerPaletteTex.Ptr();

    float d38 = 60.0f, d42 = 2.0f, d43 = 1.0f, d44 = 80.0f;
    float d45 = 0.0f, d46 = 8192.0f, d51 = 0.5f;
    float depthZoomParams[2] = { 0.0f, 1.0f };
    float d47 = 0.0f, d48 = 1.0f, d50 = 0.0f, d53 = 1.0f;
    float d41, d49, d52, d39, d40;
    bool has1, has2, has3;
    has1 = has2 = has3 = false;

    if (depthTex == nullptr) {
        LiveCameraInput *cam = TheGestureMgr->GetLiveCameraInput();
        if (!cam->mDepthPolled) {
            cam->PollNewStream(LiveCameraInput::kBufferDepth);
        }
        if (!cam->mColorPolled) {
            cam->PollNewStream(LiveCameraInput::kBufferColor);
        }
        RndTex *texSource = cam->GetStreamTex(LiveCameraInput::kBufferDepth);
        depthTex = texSource;
        if (texSource == nullptr) {
            MILO_ASSERT(texSource, 0x141);
        }
        if (texSource->Width() != 0) {
            std::vector<int> p1Cols;
            std::vector<int> p2Cols;
            std::vector<int> rows;
            std::vector<int> depths;
            p1Cols.reserve(0x12c0);
            p2Cols.reserve(0x12c0);
            rows.reserve(0x12c0);
            depths.reserve(0x12c0);

            HamPlayerData *pd0 = TheGameData->Player(0);
            Skeleton *s0 = TheGestureMgr->GetSkeletonByTrackingID(pd0->GetSkeletonTrackingID());
            HamPlayerData *pd1 = TheGameData->Player(1);
            Skeleton *s1 = TheGestureMgr->GetSkeletonByTrackingID(pd1->GetSkeletonTrackingID());
            int p1idx = (s0 == nullptr) ? -1 : (s0->SkeletonIndex() + 1);
            int p2idx = (s1 == nullptr) ? -1 : (s1->SkeletonIndex() + 1);

            void *bits = nullptr;
            texSource->TexelsLock(bits);
            const unsigned short *px = (const unsigned short *)bits;
            for (int row = 0; row < 0x3c; ++row) {
                for (int col = 0; col < 0x50; ++col) {
                    // No null test on px: the image's lhzx at 0x82DEFB34 is
                    // unconditional (r24 is the TexelsLock result loaded once at
                    // 0x82DEFB1C and never compared). This whole function body is
                    // inside the #else of the HX_NATIVE guard at line 220 -- the
                    // native build compiles the empty DrawShowing stub instead --
                    // so removing the guard cannot fault the native port.
                    unsigned short v = px[(row * 0x180 + col) * 4];
                    int player = v & 7;
                    int depthVal = v >> 3;
                    if (player == p1idx || player == p2idx) {
                        if (player == p1idx) {
                            p1Cols.push_back(col);
                        } else {
                            p2Cols.push_back(col);
                        }
                        if ((player == p1idx && mDrawPlayer1) ||
                            (player == p2idx && mDrawPlayer2)) {
                            rows.push_back(row);
                            depths.push_back(depthVal);
                        }
                    }
                }
            }
            texSource->TexelsUnlock();

            d53 = d45;
            d48 = d44;
            depthZoomParams[0] = d38;
            depthZoomParams[1] = d45;
            d47 = d45;
            d50 = d46;

            if (!p1Cols.empty()) {
                std::sort(p1Cols.begin(), p1Cols.end());
                int n = (int)p1Cols.size();
                int med = p1Cols[n / 5];
                float mean = ((float)p1Cols[(n << 2) / 5] + (float)med) * d51;
                float spread = (((float)p1Cols[(n << 2) / 5] - (float)med) * d42) * d51;
                d41 = mean - spread;
                if (d41 <= d44) {
                    d48 = d41;
                }
                d41 = spread + mean;
                if (d45 <= d41) {
                    d53 = d41;
                }
            }
            if (!p2Cols.empty()) {
                std::sort(p2Cols.begin(), p2Cols.end());
                int n = (int)p2Cols.size();
                float x = (float)p2Cols[(n << 2) / 5];
                float mean = (x + (float)p2Cols[n / 5]) * d51;
                float spread = ((x - (float)p2Cols[n / 5]) * d42) * d51;
                d41 = mean - spread;
                if (d41 <= d48) {
                    d48 = d41;
                }
                d41 = spread + mean;
                if (d53 <= d41) {
                    d53 = d41;
                }
            }
            if (!rows.empty()) {
                std::sort(rows.begin(), rows.end());
                int n = (int)rows.size() - 1;
                depthZoomParams[0] = (float)(n * 10) * 0.005f;
                depthZoomParams[0] = (depthZoomParams[0] <= d45) ? (depthZoomParams[0] - d51) : (depthZoomParams[0] + d51);
                depthZoomParams[1] = (float)(n * 0x14) * 0.005f;
                depthZoomParams[1] = (depthZoomParams[1] <= d45) ? (depthZoomParams[1] - d51) : (depthZoomParams[1] + d51);
                d41 = (float)n * 0.995f;
                d41 = (d41 <= d45) ? (d41 - d51) : (d41 + d51);
                int a = rows[(int)d41] + 1;
                int b = rows[(int)depthZoomParams[0]] - 1;
                int c = (rows[(int)depthZoomParams[1]] - 1) - (rows[(int)depthZoomParams[0]] - 1);
                depthZoomParams[1] = (float)a;
                depthZoomParams[0] = (float)(depthZoomParams[1] - (float)(depthZoomParams[1] - ((float)b - (float)c)));
            }
            if (!depths.empty()) {
                std::sort(depths.begin(), depths.end());
                float dn = (float)((int)depths.size() - 1);
                d47 = dn * 0.009999999776482582f;
                d47 = (d47 <= d45) ? (d47 - d51) : (d47 + d51);
                d41 = dn * 0.9900000095367432f;
                d41 = (d41 <= d45) ? (d41 - d51) : (d41 + d51);
                d50 = dn * d51;
                d50 = (d50 <= d45) ? (d50 - d51) : (d50 + d51);
                float lo = (float)depths[(int)d50];
                float hi = (float)depths[(int)d41];
                float spread = lo - (float)depths[(int)d47];
                if (spread <= (hi - lo)) {
                    spread = hi - lo;
                }
                float half = (((spread + d43) * 3.6f) * d51);
                d50 = lo - half;
                d47 = half + lo;
            }

            has1 = d48 < d53;
            if (!has1) {
                d53 = d44;
                d48 = d45;
            }
            has2 = depthZoomParams[0] < depthZoomParams[1];
            if (!has2) {
                depthZoomParams[0] = d45;
                depthZoomParams[1] = d38;
            }
            has3 = d50 < d47;
            if (!has3) {
                d47 = d46;
                d50 = 256.0f;
            }
            d46 = d50 - 256.0f;
            if (d46 <= d45) {
                d46 = d45;
            }
            d41 = 4096.0f;
            d50 = d47 - 256.0f;
            if (4096.0f <= d50) {
                d50 = d41;
            }
            d52 = depthZoomParams[1] - depthZoomParams[0];
            d47 = d53 - d48;
            d49 = d50 - d46;
            d48 = (d48 + d53) * d51;
            depthZoomParams[1] = (depthZoomParams[0] + depthZoomParams[1]) * d51;
            depthZoomParams[0] = (d46 + d50) * d51;

            float dt = TheTaskMgr.DeltaUISeconds();
            float smoothDt = 0.15000000596046448f;
            if (dt < 0.15f) {
                smoothDt = TheTaskMgr.DeltaUISeconds();
            }

            if (has3 && has2 && has1 && !unk28c) {
                unk270.SetParams(d49, d49, d45);
                unk25c.SetParams(depthZoomParams[0], depthZoomParams[0], d45);
                unk234.SetParams(d52, d52, d45);
                unk248.SetParams(depthZoomParams[1], depthZoomParams[1], d45);
                unk20c.SetParams(d47, d47, d45);
                unk220.SetParams(d48, d48, d45);
            } else if (has3 && has2 && has1) {
                if (100.0f < Abs(unk270.Level() - d49) ||
                    50.0f < Abs(unk25c.Level() - depthZoomParams[0]) ||
                    3.0f < Abs(unk234.Level() - d52) ||
                    1.5f < Abs(unk248.Level() - depthZoomParams[1])) {
                    unk270.Smooth(d49, smoothDt);
                    unk25c.Smooth(depthZoomParams[0], smoothDt);
                    unk234.Smooth(d52, smoothDt);
                    unk248.Smooth(depthZoomParams[1], smoothDt);
                }
                if (4.0f < Abs(unk20c.Level() - d47) ||
                    d42 < Abs(unk220.Level() - d48)) {
                    unk20c.Smooth(d47, smoothDt);
                    unk220.Smooth(d48, smoothDt);
                }
            }
            unk28c = has3 && has2 && has1;

            d53 = d43 / mMaxZoom;
            d48 = d41 / mMaxDepthZoom;
            depthZoomParams[0] = d53 * d44;
            d47 = d48 * d51;
            d53 = d53 * d38;
            depthZoomParams[1] = depthZoomParams[0] * d51;
            if (d48 < unk270.Level()) {
                d48 = unk270.Level();
            }
            d50 = d53 * d51;
            if (depthZoomParams[0] < unk20c.Level()) {
                depthZoomParams[0] = unk20c.Level();
            }
            d46 = d47;
            if (d47 < unk25c.Level()) {
                d46 = unk25c.Level();
            }
            if (d53 < unk234.Level()) {
                d53 = unk234.Level();
            }
            d49 = depthZoomParams[1];
            if (depthZoomParams[1] < unk220.Level()) {
                d49 = unk220.Level();
            }
            d52 = d50;
            if (d50 < unk248.Level()) {
                d52 = unk248.Level();
            }
            d39 = d41;
            if (d48 < d41) {
                d39 = d48;
            }
            d48 = d44;
            if (depthZoomParams[0] < d44) {
                d48 = depthZoomParams[0];
            }
            depthZoomParams[0] = d38;
            if (d53 < d38) {
                depthZoomParams[0] = d53;
            }
            d40 = d41 - d47;
            if (d46 < d40) {
                d40 = d46;
            }
            d53 = d44 - depthZoomParams[1];
            if (d49 < d53) {
                d53 = d49;
            }
            d48 = d48 * d51;
            depthZoomParams[1] = d38 - d50;
            if (d52 < depthZoomParams[1]) {
                depthZoomParams[1] = d52;
            }
            d47 = d45;
            if (d45 < (d53 - d48)) {
                d47 = d53 - d48;
            }
            d47 = d47 * 0.012500000186264515f;
            if ((d53 + d48) < d44) {
                d44 = d53 + d48;
            }
            depthZoomParams[0] = depthZoomParams[0] * d51;
            d48 = d44 * 0.012500000186264515f;
            d53 = d45;
            if (d45 < (depthZoomParams[1] - depthZoomParams[0])) {
                d53 = depthZoomParams[1] - depthZoomParams[0];
            }
            d50 = d53 * 0.01666666753590107f;
            if ((depthZoomParams[1] + depthZoomParams[0]) < d38) {
                d38 = depthZoomParams[1] + depthZoomParams[0];
            }
            depthZoomParams[1] = d39 * d51;
            d53 = d38 * 0.01666666753590107f;
            depthZoomParams[0] = d45;
            if (d45 < (d40 - depthZoomParams[1])) {
                depthZoomParams[0] = d40 - depthZoomParams[1];
            }
            depthZoomParams[0] = depthZoomParams[0] * 0.000244140625f;
            if ((d40 + depthZoomParams[1]) < d41) {
                d41 = d40 + depthZoomParams[1];
            }
            depthZoomParams[1] = d41 * 0.000244140625f;
        }

        RndTex *colorTex = cam->GetStreamTex(LiveCameraInput::kBufferColor);
        TheNgRnd.SetVertShaderTex(colorTex, 1);
        mat->SetNormalMap(colorTex);
        mat->MarkDirty(2);
    }

    mat->SetDiffuseTex(depthTex);
    mat->MarkDirty(2);
    TheNgRnd.SetVertShaderTex(depthTex, 0);
    TheRenderState.SetTextureFilter(0, (RndRenderState::FilterMode)0, false);
    TheRenderState.SetTextureClamp(0, (RndRenderState::ClampMode)2);

    // `pal` lives outside the loop and is only assigned for i == 0 and i == 1:
    // the image tests `cmplwi i, 1` / blt / bne and its fall-through keeps the
    // previous value, seeded from the shared, uninitialised frame slot
    // (`lwz r27, 0x50(r31)` before the loop).  i never exceeds 1, so the third
    // path is dead.  The i == 1 pick selects the ObjPtr itself, then reads it.
    RndTex *pal;
    for (int i = 0; i < 2; ++i) {
        if (i == 0) {
            pal = mPlayerPalette;
        } else if (i == 1) {
            pal = mBoxymanPalette ? mBoxymanPalette : mPlayerPalette;
        }
        if (pal == nullptr) {
            pal = TheRnd.GetDefaultTex(Rnd::kDefaultTex_WhiteTransparent);
        }
        TheShaderMgr.SetPConstant((PShaderConstant)(i + 10), pal);
        TheRenderState.SetTextureFilter(i + 10, (RndRenderState::FilterMode)1, false);
        TheRenderState.SetTextureClamp(i + 10, (RndRenderState::ClampMode)0);
    }

    Vector4 nobody(mNobodyColor.red, mNobodyColor.green, mNobodyColor.blue, mNobodyColor.alpha);
    TheShaderMgr.SetPConstant((PShaderConstant)0x40, nobody);

    Vector4 paletteParams(mPlayerPaletteOffset, mPlayerPaletteScale, mStretchNearCamera, mOpacity);
    TheShaderMgr.SetVConstant((VShaderConstant)0x41, paletteParams);
    TheShaderMgr.SetPConstant((PShaderConstant)0x41, paletteParams);

    // w18-c: written straight into slotParams, as the image does (stfs to its
    // 0xa0/0xa4 slots inside each arm, with the else arm's y = -1.0f sharing the
    // -999 arm's store), and z/w computed in FLOAT: the image converts modf's
    // double result first (`frsp f0, f1`) and then does `fmsubs f0, f0, f18,
    // f19` -- (float)modf(...) * 2.0f - 1.0f.  We used to multiply the double
    // (`fmsub` against 2.0 / 1.0 double literals) and round once at the end.
    Vector4 slotParams;
    if (mForceDrawSkeletonIdx != -999) {
        slotParams.x = (mForceDrawSkeletonIdx < 0) ? -1.0f : (float)(mForceDrawSkeletonIdx + 1);
        mDrawPlayer2 = false;
        mDrawPlayer1 = true;
        mDrawNonPlayers = mForceDrawEnabled;
        slotParams.y = -1.0f;
    } else {
        HamPlayerData *pd0 = TheGameData->Player(0);
        Skeleton *s0 = TheGestureMgr->GetSkeletonByTrackingID(pd0->GetSkeletonTrackingID());
        HamPlayerData *pd1 = TheGameData->Player(1);
        Skeleton *s1 = TheGestureMgr->GetSkeletonByTrackingID(pd1->GetSkeletonTrackingID());
        slotParams.x = (s0 == nullptr) ? -1 : (s0->SkeletonIndex() + 1);
        slotParams.y = (s1 == nullptr) ? -1 : (s1->SkeletonIndex() + 1);
    }

    double ip;
    slotParams.z = (float)modf((double)mBoxymanPaletteAnim, &ip) * d42 - d43;
    slotParams.w = (float)modf((double)mBoxymanPaletteAnim, &ip) * d42 - d43;
    TheShaderMgr.SetVConstant((VShaderConstant)0x42, slotParams);
    TheShaderMgr.SetPConstant((PShaderConstant)0x42, slotParams);

    Vector4 drawFlags(
        (float)mDrawPlayer1, (float)mDrawPlayer2, (float)mDrawNonPlayers, (float)mDebugLayout
    );
    TheShaderMgr.SetPConstant((PShaderConstant)0x43, drawFlags);
    TheShaderMgr.SetVConstant((VShaderConstant)0x43, drawFlags);

    Vector4 voxelParams(mScaleVoxel, mScaleVoxelGap, mFishEyeX, mFishEyeY);
    TheShaderMgr.SetVConstant((VShaderConstant)0x45, voxelParams);

    float texZoomParams[4];
    float span0 = d48 - d47;
    float span1 = d53 - d50;
    if (span0 <= span1) {
        if (span0 < span1) {
            float pad = (span1 - span0) * d51;
            d47 = d47 - pad;
            d48 = pad + d48;
            if (d45 <= d47) {
                if (d43 < d48) {
                    d47 = d47 - (d48 - d43);
                    d48 = d43;
                }
            } else {
                d48 = d48 - d47;
                d47 = d45;
            }
        }
    } else {
        float pad = (span0 - span1) * d51;
        d50 = d50 - pad;
        d53 = pad + d53;
        if (d45 <= d50) {
            if (d43 < d53) {
                d50 = d50 - (d53 - d43);
                d53 = d43;
            }
        } else {
            d53 = d53 - d50;
            d50 = d45;
        }
    }
    float depthSpan = d53 - d50;
    float widthSpan = (d48 - d47) * 1.2f;
    texZoomParams[2] = (d48 + d47) * d51;
    texZoomParams[3] = (d53 + d50) * d51;
    if (d43 < widthSpan) {
        depthSpan = depthSpan * (d43 / widthSpan);
        widthSpan = widthSpan * (d43 / widthSpan);
    }
    texZoomParams[0] = texZoomParams[2] - widthSpan * d51;
    texZoomParams[2] = texZoomParams[2] + widthSpan * d51;
    texZoomParams[1] = texZoomParams[3] - depthSpan * d51;
    texZoomParams[3] = texZoomParams[3] + depthSpan * d51;
    if (texZoomParams[2] <= texZoomParams[0]) {
        MILO_ASSERT(texZoomParams[0] < texZoomParams[2], 700);
    }
    if (texZoomParams[3] <= texZoomParams[1]) {
        MILO_ASSERT(texZoomParams[1] < texZoomParams[3], 0x2bd);
    }
    Vector4 texZoom(texZoomParams[0], texZoomParams[1], texZoomParams[2], texZoomParams[3]);
    TheShaderMgr.SetVConstant((VShaderConstant)0x46, texZoom);

    float playerSel;
    if ((!mDrawPlayer1) || mDrawPlayer2 || mDrawNonPlayers) {
        playerSel = d45;
        if ((!mDrawPlayer1) && mDrawPlayer2 && (!mDrawNonPlayers)) {
            playerSel = slotParams.y;
        }
    } else {
        playerSel = slotParams.x;
    }
    if (depthZoomParams[1] <= depthZoomParams[0]) {
        MILO_ASSERT(depthZoomParams[0] < depthZoomParams[1], 0x2ce);
    }
    Vector4 depthZoom(depthZoomParams[0], depthZoomParams[1], playerSel, d45);
    TheShaderMgr.SetVConstant((VShaderConstant)0x47, depthZoom);

    Vector3 jointPos(0.0f, 0.0f, 0.0f);
    float jointW = d45;
    HamPlayerData *jpd = TheGameData->Player(0);
    int jidx = TheGestureMgr->GetSkeletonIndexByTrackingID(jpd->GetSkeletonTrackingID());
    if (0 < (long long)jidx + 1) {
        Skeleton &js = TheGestureMgr->GetSkeleton(jidx);
        JointToVertexData(jointPos, js, (SkeletonJoint)0, texZoom);
        jointW = d43;
    }
    Vector4 jointParams(jointPos.x, jointPos.y, jointPos.z, jointW);
    TheShaderMgr.SetVConstant((VShaderConstant)0x48, jointParams);

    for (int player = 0; player < 2; ++player) {
        RhythmDetector *det =
            (player == 0) ? mGroovinessDetector1.Ptr() : mGroovinessDetector2.Ptr();
        float g = (player == 0) ? mPlayer1Grooviness : mPlayer2Grooviness;
        Vector4 gv(g, g, g, g);
        int base1 = 100 + player * 0x14;
        int base2 = 140 + player * 0x14;
        for (int k = 0; k < 0x14; ++k) {
            Vector4 d1 = det ? det->Data1(k) : gv;
            TheShaderMgr.SetVConstant((VShaderConstant)(base1 + k), d1);
            Vector4 d2 = det ? det->Data2(k) : gv;
            TheShaderMgr.SetVConstant((VShaderConstant)(base2 + k), d2);
        }
    }

    for (std::vector<DepthBuffer3DAttachment>::iterator it = mAttachments.begin();
         it != mAttachments.end(); ++it) {
        UpdateAttachment(*it, texZoom, depthZoom);
    }

    if (!mDrawSheet) {
        if (mMesh.Ptr() != nullptr) {
            DrawMesh();
        }
    } else {
        TheNgRnd.DrawLargeQuad(mQuad, WorldXfm(), mat, kDepthBuffer3DShader);
    }
}

#endif
