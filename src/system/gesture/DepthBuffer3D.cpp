#include "gesture/DepthBuffer3D.h"
#include "gesture/BaseSkeleton.h"
#include "gesture/GestureMgr.h"
#include "gesture/JointUtl.h"
#include "gesture/LiveCameraInput.h"
#include "gesture/Skeleton.h"
#include "hamobj/HamGameData.h"
#include "hamobj/HamPlayerData.h"
#include "hamobj/RhythmDetector.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "math/Utl.h"
#include "obj/Task.h"
#include "os/Debug.h"
#include "rnddx9/Rnd.h"
#include "rnddx9/RenderState.h"
#include "rndobj/Draw.h"
#include "rndobj/Rnd.h"
#include "rndobj/Rnd_NG.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/ShaderOptions.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"
#include <math.h>

LargeQuadRenderData DepthBuffer3D::mQuad;

namespace {
    void JointToVertexData(
        Vector3 &out, const Skeleton &skeleton, SkeletonJoint joint, const Vector4 &bounds
    ) {
        Vector3 screenPos;
        JointScreenPos(skeleton.TrackedJoints()[joint], screenPos);
        out.y = screenPos.z;
        out.x = ((screenPos.x - bounds.x) / (bounds.z - bounds.x) - 0.5f) * 318.0f - 1.0f;
        out.z = (0.5f - (screenPos.y - bounds.y) / (bounds.w - bounds.y)) * 238.0f - 1.0f;
    }

    void VertexToWorld(
        Vector3 &pos, const Transform &xfm, float stretchNearCamera, const Vector4 &depthRange
    ) {
        float depth = (pos.y - 256.0f) * (1.0f / 4096.0f);
        pos.y = depth;
        depth = 1.0f - (depth - depthRange.x) / (depthRange.y - depthRange.x);
        pos.y = depth;
        depth = Clamp(0.0f, 1.0f, depth);
        pos.y = depth;
        float y = (float)pow((double)depth, (double)stretchNearCamera) * -200.0f;
        pos.y = y;
        float x = pos.x;
        float z = pos.z;
        pos.x = xfm.m.x.x * x + xfm.m.y.x * y + xfm.m.z.x * z;
        pos.y = xfm.m.x.y * x + xfm.m.y.y * y + xfm.m.z.y * z;
        pos.z = xfm.m.x.z * x + xfm.m.y.z * y + xfm.m.z.z * z;
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
    bool b5 = false;
    if (skelIdx + 1 > 0) {
        const Transform &localXfm = LocalXfm();
        Skeleton &skeleton = TheGestureMgr->GetSkeleton(skelIdx);
        Vector3 localPos = localXfm.v;
        Vector3 pos;
        JointToVertexData(pos, skeleton, (SkeletonJoint)attachment.mJoint, v1);
        VertexToWorld(pos, localXfm, mStretchNearCamera, v2);
        Add(pos, localPos, newPos);
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
    bs << mNobodyColor;
    bs << mPlayerPalette;
    bs << mPlayerPaletteOffset;
    bs << mMinimalMat;
    bs << mDrawSheet;
    bs << mMesh;
    bs << mPlayerPaletteScale;
    bs << mStretchNearCamera;
    bs << mOpacity;
    bs << mDrawPlayer1;
    bs << mDrawPlayer2;
    bs << mDrawNonPlayers;
    bs << mTile;
    bs << mScaleVoxel;
    bs << mScaleVoxelGap;
    bs << mFishEyeX;
    bs << mFishEyeY;
    bs << mMaxZoom;
    bs << mDebugLayout;
    bs << mMaxDepthZoom;
END_SAVES

INIT_REVS(11, 0)

BEGIN_LOADS(DepthBuffer3D)
    LOAD_REVS(bs)
    ASSERT_REVS(11, 0)
    LOAD_SUPERCLASS(Hmx::Object)
    LOAD_SUPERCLASS(RndDrawable)
    LOAD_SUPERCLASS(RndTransformable)
    if (d.rev > 1) {
        d.stream >> mNobodyColor;
        d >> mPlayerPalette;
        d >> mPlayerPaletteOffset;
        d >> mMinimalMat;
        if (d.rev < 3) {
            int dummy;
            d >> dummy;
        }
    }
    if (d.rev > 2) {
        d >> mDrawSheet;
        d >> mMesh;
    }
    if (d.rev > 3) {
        d >> mPlayerPaletteScale;
        d >> mStretchNearCamera;
    }
    if (d.rev > 4) {
        d >> mOpacity;
    }
    if (d.rev > 5) {
        d >> mDrawPlayer1;
        d >> mDrawPlayer2;
        d >> mDrawNonPlayers;
    }
    if (d.rev > 6) {
        d.stream >> mTile;
        d >> mScaleVoxel;
        d >> mScaleVoxelGap;
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
    if (TheRnd.GetDrawMode() != Rnd::kDrawNormal || !Showing()) {
        return;
    }

    mBoxymanPaletteAnim += 0.0333f;
    if (mBoxymanPaletteAnim > 1.0f) {
        mBoxymanPaletteAnim = 1.0f;
    }

    RndMat *mat = mMinimalMat.Ptr();
    if (mat == nullptr) {
        mat = SetUpWorkingMat();
    }

    RndTex *depthTex = nullptr;

    float d38 = 60.0f, d42 = 2.0f, d43 = 1.0f, d44 = 80.0f;
    float d45 = 0.0f, d46 = 8192.0f, d51 = 0.5f;
    float d36 = 0.0f, d37 = 1.0f, d47 = 0.0f, d48 = 1.0f, d50 = 0.0f, d53 = 1.0f;
    float d41, d49, d52, d39, d40;
    bool has1, has2, has3;
    has1 = has2 = has3 = false;

    if (mPlayerPaletteTex.Ptr() == nullptr) {
        LiveCameraInput *cam = TheGestureMgr->GetLiveCameraInput();
        if (!cam->mDepthPolled) {
            cam->PollNewStream(LiveCameraInput::kBufferDepth);
        }
        if (!cam->mColorPolled) {
            cam->PollNewStream(LiveCameraInput::kBufferColor);
        }
        RndTex *tex = cam->GetStreamTex(LiveCameraInput::kBufferDepth);
        depthTex = tex;
        if (tex == nullptr) {
            MILO_ASSERT(tex, 0x141);
        }
        if (tex->Width() != 0) {
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
            tex->TexelsLock(bits);
            const unsigned short *px = (const unsigned short *)bits;
            for (int row = 0; row < 0x3c; ++row) {
                for (int col = 0; col < 0x50; ++col) {
                    unsigned short v = px ? px[(row * 0x180 + col) * 4] : 0;
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
            tex->TexelsUnlock();

            d53 = d45;
            d48 = d44;
            d36 = d38;
            d37 = d45;
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
                d36 = (float)(n * 10) * 0.005f;
                d36 = (d36 <= d45) ? (d36 - d51) : (d36 + d51);
                d37 = (float)(n * 0x14) * 0.005f;
                d37 = (d37 <= d45) ? (d37 - d51) : (d37 + d51);
                d41 = (float)n * 0.995f;
                d41 = (d41 <= d45) ? (d41 - d51) : (d41 + d51);
                int a = rows[(int)d41] + 1;
                int b = rows[(int)d36] - 1;
                int c = (rows[(int)d37] - 1) - (rows[(int)d36] - 1);
                d37 = (float)a;
                d36 = (float)(d37 - (float)(d37 - ((float)b - (float)c)));
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
            has2 = d36 < d37;
            if (!has2) {
                d36 = d45;
                d37 = d38;
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
            d52 = d37 - d36;
            d47 = d53 - d48;
            d49 = d50 - d46;
            d48 = (d48 + d53) * d51;
            d37 = (d36 + d37) * d51;
            d36 = (d46 + d50) * d51;

            float dt = TheTaskMgr.DeltaUISeconds();
            float smoothDt = 0.15000000596046448f;
            if (dt < 0.15f) {
                smoothDt = TheTaskMgr.DeltaUISeconds();
            }

            if (has3 && has2 && has1 && !unk28c) {
                unk270.SetParams(d49, d49, d45);
                unk25c.SetParams(d36, d36, d45);
                unk234.SetParams(d52, d52, d45);
                unk248.SetParams(d37, d37, d45);
                unk20c.SetParams(d47, d47, d45);
                unk220.SetParams(d48, d48, d45);
            } else if (has3 && has2 && has1) {
                if (100.0f < Abs(unk270.Level() - d49) ||
                    50.0f < Abs(unk25c.Level() - d36) ||
                    3.0f < Abs(unk234.Level() - d52) ||
                    1.5f < Abs(unk248.Level() - d37)) {
                    unk270.Smooth(d49, smoothDt);
                    unk25c.Smooth(d36, smoothDt);
                    unk234.Smooth(d52, smoothDt);
                    unk248.Smooth(d37, smoothDt);
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
            d36 = d53 * d44;
            d47 = d48 * d51;
            d53 = d53 * d38;
            d37 = d36 * d51;
            if (d48 < unk270.Level()) {
                d48 = unk270.Level();
            }
            d50 = d53 * d51;
            if (d36 < unk20c.Level()) {
                d36 = unk20c.Level();
            }
            d46 = d47;
            if (d47 < unk25c.Level()) {
                d46 = unk25c.Level();
            }
            if (d53 < unk234.Level()) {
                d53 = unk234.Level();
            }
            d49 = d37;
            if (d37 < unk220.Level()) {
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
            if (d36 < d44) {
                d48 = d36;
            }
            d36 = d38;
            if (d53 < d38) {
                d36 = d53;
            }
            d40 = d41 - d47;
            if (d46 < d40) {
                d40 = d46;
            }
            d53 = d44 - d37;
            if (d49 < d53) {
                d53 = d49;
            }
            d48 = d48 * d51;
            d37 = d38 - d50;
            if (d52 < d37) {
                d37 = d52;
            }
            d47 = d45;
            if (d45 < (d53 - d48)) {
                d47 = d53 - d48;
            }
            d47 = d47 * 0.012500000186264515f;
            if ((d53 + d48) < d44) {
                d44 = d53 + d48;
            }
            d36 = d36 * d51;
            d48 = d44 * 0.012500000186264515f;
            d53 = d45;
            if (d45 < (d37 - d36)) {
                d53 = d37 - d36;
            }
            d50 = d53 * 0.01666666753590107f;
            if ((d37 + d36) < d38) {
                d38 = d37 + d36;
            }
            d37 = d39 * d51;
            d53 = d38 * 0.01666666753590107f;
            d36 = d45;
            if (d45 < (d40 - d37)) {
                d36 = d40 - d37;
            }
            d36 = d36 * 0.000244140625f;
            if ((d40 + d37) < d41) {
                d41 = d40 + d37;
            }
            d37 = d41 * 0.000244140625f;
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

    for (int i = 0; i < 2; ++i) {
        RndTex *pal;
        if (i == 0) {
            pal = mPlayerPalette.Ptr();
        } else {
            pal = (mBoxymanPalette.Ptr() == nullptr) ? mPlayerPalette.Ptr() : mBoxymanPalette.Ptr();
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

    float p1Slot, p2Slot;
    if (mForceDrawSkeletonIdx == -999) {
        HamPlayerData *pd0 = TheGameData->Player(0);
        Skeleton *s0 = TheGestureMgr->GetSkeletonByTrackingID(pd0->GetSkeletonTrackingID());
        HamPlayerData *pd1 = TheGameData->Player(1);
        Skeleton *s1 = TheGestureMgr->GetSkeletonByTrackingID(pd1->GetSkeletonTrackingID());
        p1Slot = (float)((s0 == nullptr) ? -1 : (s0->SkeletonIndex() + 1));
        p2Slot = (float)((s1 == nullptr) ? -1 : (s1->SkeletonIndex() + 1));
    } else {
        p2Slot = -1.0f;
        p1Slot = (mForceDrawSkeletonIdx < 0) ? -1.0f : (float)(mForceDrawSkeletonIdx + 1);
        mDrawPlayer2 = false;
        mDrawPlayer1 = true;
        mDrawNonPlayers = mForceDrawEnabled;
    }

    double ip;
    float anim1 = (float)(modf((double)mBoxymanPaletteAnim, &ip) * d42 - d43);
    float anim2 = (float)(modf((double)mBoxymanPaletteAnim, &ip) * d42 - d43);
    Vector4 slotParams(p1Slot, p2Slot, anim1, anim2);
    TheShaderMgr.SetVConstant((VShaderConstant)0x42, slotParams);
    TheShaderMgr.SetPConstant((PShaderConstant)0x42, slotParams);

    Vector4 drawFlags(
        (float)mDrawPlayer1, (float)mDrawPlayer2, (float)mDrawNonPlayers, (float)mDebugLayout
    );
    TheShaderMgr.SetPConstant((PShaderConstant)0x43, drawFlags);
    TheShaderMgr.SetVConstant((VShaderConstant)0x43, drawFlags);

    Vector4 voxelParams(mScaleVoxel, mScaleVoxelGap, mFishEyeX, mFishEyeY);
    TheShaderMgr.SetVConstant((VShaderConstant)0x45, voxelParams);

    float tx0, tx1, tx2, tx3;
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
    tx2 = (d48 + d47) * d51;
    tx3 = (d53 + d50) * d51;
    if (d43 < widthSpan) {
        depthSpan = depthSpan * (d43 / widthSpan);
        widthSpan = widthSpan * (d43 / widthSpan);
    }
    tx0 = tx2 - widthSpan * d51;
    tx2 = tx2 + widthSpan * d51;
    tx1 = tx3 - depthSpan * d51;
    tx3 = tx3 + depthSpan * d51;
    if (tx2 <= tx0) {
        MILO_ASSERT(tx0 < tx2, 700);
    }
    if (tx3 <= tx1) {
        MILO_ASSERT(tx1 < tx3, 0x2bd);
    }
    Vector4 texZoom(tx0, tx1, tx2, tx3);
    TheShaderMgr.SetVConstant((VShaderConstant)0x46, texZoom);

    float playerSel;
    if ((!mDrawPlayer1) || mDrawPlayer2 || mDrawNonPlayers) {
        playerSel = d45;
        if ((!mDrawPlayer1) && mDrawPlayer2 && (!mDrawNonPlayers)) {
            playerSel = p2Slot;
        }
    } else {
        playerSel = p1Slot;
    }
    if (d37 <= d36) {
        MILO_ASSERT(d36 < d37, 0x2ce);
    }
    Vector4 depthZoom(d36, d37, playerSel, d45);
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
