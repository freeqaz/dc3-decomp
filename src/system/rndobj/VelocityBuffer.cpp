#include "rndobj\VelocityBuffer.h"
#include "math\Mtx.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\BaseMaterial.h"
#include "rndobj/Cam.h"
#include "rndobj\Mat.h"
#include "rndobj\Tex.h"
#include "rndobj\Utl.h"
#include "rndobj\Rnd.h"
#include "math\Utl.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Shader.h"
#include "rndobj\Stats_NG.h"
#include "rnddx9\RenderState.h"

RndVelocityBuffer RndVelocityBuffer::sSingleton;

// The bone cap reaches MakeString as a const reference to a POOLED .rdata
// constant (0x826B2158 `lis r10, lbl_8209F810@ha` / 0x826B2222
// `addi r7, r10, lbl_8209F810@l`), not as a stack temporary -- a bare `40`
// literal in the argument list is materialised into its own frame slot instead,
// which also shifts every other slot in the function by four.
static const int kMaxMotionBlurBones = 40;

// 41.666668f == 1000/24: the frame time, in ms, that scores a velocity scale of
// 1.0.  It is a NAMED .rdata constant in the image, not a float literal --
// 0x826B1A94 `lis r10, lbl_8209F7BC@ha` / 0x826B1AB0 `lfs f13, lbl_8209F7BC@l`
// reaches a plain 4-byte .rdata object sitting between the two string literals
// of this TU (build/373307D9/asm/system/rndobj/VelocityBuffer.s:41), where a
// literal would have been emitted as a `__real@4226aaab` pick-any COMDAT.
static const float kVelocityRefFrameMs = 41.666668f;

bool RndXfmCache::GetXfms(
    const RndMesh * __restrict mesh,
    unsigned int startIndex,
    unsigned int numBones,
    const float *&outFloats
) const {
    bool valid;
    const float *floats;
    unsigned int endIndex = startIndex + numBones;
    if ((endIndex > unk1b580)
        || (mMeshPtrs[startIndex] != mesh)
        || (mMeshPtrs[endIndex - 1] != mesh)) {
        floats = nullptr;
        valid = false;
    } else {
        valid = true;
        floats = (const float *)(&unk1f40[startIndex * 12]);
    }
    outFloats = floats;
    return valid;
}

bool RndXfmCache::CacheXfms(
    const RndMesh * __restrict mesh,
    const float * __restrict boneFloats,
    unsigned int numBones,
    unsigned int &outKey
) {
    outKey = 0xffffffff;
    if (unk1b580 + numBones <= 0x7d0) {
        outKey = unk1b580;
        int startIndex = unk1b580;

        // Copy bone transform floats (12 floats/ints per bone = 3 float4 rows)
        unsigned int totalFloats = numBones * 12;
        unsigned int count = 0;
        float *dst = (float *)&unk1f40[startIndex * 12];
        if (totalFloats != 0) {
            int diff = (int)boneFloats - (int)dst;
            do {
                count++;
                *dst = *(float *)((int)dst + diff);
                dst++;
            } while (count < totalFloats);
        }

        // Store mesh pointers (one per bone slot) and per-bone indices
        {
            int *indices = &unk19640[startIndex];
            const RndMesh **meshPtrs = &mMeshPtrs[startIndex];
            if (numBones != 0) {
                --indices;
                unsigned int m = numBones;
                --meshPtrs;
                int idx = 0;
                unsigned int n = numBones;
                do {
                    ++meshPtrs;
                    *meshPtrs = mesh;
                } while (--n != 0);
                do {
                    ++indices;
                    *indices = idx;
                    idx++;
                } while (--m != 0);
            }
        }

        unk1b580 = startIndex + numBones;
    }
    return outKey < 2000U;
}

RndVelocityBuffer::RndVelocityBuffer()
    : unk36be8(0), mActiveXfmCacheIndex(0), mFrame(0), mVelocityTex(nullptr), mMat(nullptr),
      mLastFrameCamera(nullptr) {
    memset(&mViewProjXfm, 0, 0xa4);
}

void RndVelocityBuffer::CacheCameraSettings(RndCam *camera) {
    MILO_ASSERT(camera, 0x88);
    Transform tfa0;
    Hmx::Matrix4 me0;
    camera->GetViewProjectXfms(tfa0, me0);
    mViewProjXfm = tfa0 * me0;
    camera->GetDepthRangeValues(mDepthRangeValues);
    camera->GetCamFrustum(mFrustumNear, (Vector3 (&)[4])mFrustumCorners);
    mCam = camera;
}

bool RndVelocityBuffer::AdvanceFrame(RndCam *cam) {
    mFrameAdvanced = false;
    mActiveXfmCacheIndex ^= 1;
    mFrame++;
    if (cam != mLastFrameCamera) {
        mLastFrameCamera = cam;
        mFrame = 0;
    }
    return (unsigned int)mFrame >= 2;
}

void RndVelocityBuffer::AllocateData(
    unsigned int ui1, unsigned int ui2, unsigned int ui3
) {
    MILO_ASSERT(mVelocityTex == NULL, 0x45);
    mVelocityTex = Hmx::Object::New<RndTex>();
    mVelocityTex->SetBitmap(ui1 / 2, ui2 / 2, ui3, RndTex::kRendered, false, nullptr);
    MILO_ASSERT(mMat == NULL, 0x4A);
    mMat = Hmx::Object::New<RndMat>();
    mMat->SetPerPixelLit(false);
    mMat->SetBlend(BaseMaterial::kBlendSrc);
    mMat->SetZMode(kZModeDisable);
    CreateAndSetMetaMat(mMat);
}

void RndVelocityBuffer::FreeData() {
    RELEASE(mVelocityTex);
    RELEASE(mMat);
}

void RndVelocityBuffer::ResetFrame() { mFrame = 0; }

void RndVelocityBuffer::CacheTransform(
    RndMesh * __restrict mesh,
    const float * __restrict boneFloats,
    unsigned int numBones
) {
    if ((TheRnd.ProcCmds() & kProcessWorld) > 0) {
        int cacheIdx = mActiveXfmCacheIndex;
        unsigned int outKey;
        bool ok = mXfmCaches[cacheIdx].CacheXfms(mesh, boneFloats, numBones, outKey);
        if (ok) {
            mesh->mMotionCache.mCacheKey[cacheIdx] = outKey;
        }
    }
}

void RndVelocityBuffer::DrawMesh(RndMesh *mesh) const {
    MILO_ASSERT(mesh, 0x123);
    MILO_ASSERT(mesh->Showing(), 0x124);
    MILO_ASSERT(TheRnd.DrawMode() == Rnd::kDrawVelocity, 0x125);

    RndMat *mat = mesh->Mat();
    if (mat != nullptr && mat->GetZMode() != kZModeTransparent) {
        mesh->mMotionCache.mShouldCache = true;

        // NumBones() is called ONCE (0x826B1FB4 `subf` over 0x150/0x154,
        // 0x826B1FE0 `divw` by 0x54) and the raw value is spilled to 0x60(r1) at
        // 0x826B1FEC because the notify below takes it by const reference.  It
        // must be declared BEFORE the two cache references and the two keys: the
        // divw is scheduled between the key ADDRESS arithmetic (0x826B1FE4/E8
        // `slwi`) and the key LOADS (0x826B1FF0/F8 `lwzx`), which only happens
        // when the division is already in flight by then.
        int rawBones = mesh->NumBones();

        // The index is xor'd TWICE: 0x826B1FBC `xori r28, r10, 0x1` off the
        // member read, then 0x826B1FC0 `xori r27, r28, 0x1` off THAT.  The
        // current-frame index is derived from the previous-frame one, not read
        // back from mActiveXfmCacheIndex.
        unsigned int prevIdx = mActiveXfmCacheIndex ^ 1;
        unsigned int currIdx = prevIdx ^ 1;

        // Both cache element addresses and both cache keys are materialised
        // BEFORE the clamp and the first GetXfms -- 0x826B1FF4 `addi r3, r11,
        // 0xac` and 0x826B1FFC `addi r8, r10, 0xac` for the two RndXfmCache
        // pointers (element stride 0x1b584, base 0xac), 0x826B1FF0
        // `lwzx r5, r7, r31` and 0x826B1FF8 `lwzx r26, r6, r31` for the keys.
        // Declared after the clamp they are sunk past the first call and
        // recomputed.  The CACHES MUST BE DECLARED FIRST: with the keys first
        // MSVC hoists both `lwzx` above the divw and colours currKey into the
        // callee-saved r26 and currCache into r8, exactly the other way round
        // from the image (95.20 vs 100.0).
        const RndXfmCache &prevCache = mXfmCaches[prevIdx];
        const RndXfmCache &currCache = mXfmCaches[currIdx];
        unsigned int prevKey = mesh->mMotionCache.mCacheKey[prevIdx];
        unsigned int currKey = mesh->mMotionCache.mCacheKey[currIdx];

        // The clamp is `Max(1, n)`: 0x826B1F94 `li r29, 0x1` (shared with the
        // mShouldCache store two instructions later), 0x826B2000
        // `cmpwi cr6, r9, 0x1`, `ble` keeps the 1, `mr r29, r9` otherwise.
        // `if (n <= 0) n = 1;` compares against 0 instead.
        int numBones = Max(1, rawBones);

        // NOT initialised: GetXfms writes the out-parameter on every path, and the
        // image has no zero store -- 0x826B200C hands it `addi r7, r1, 0x50` cold.
        const float *prevFloats;
        if (prevCache.GetXfms(mesh, prevKey, numBones, prevFloats)) {
            const float *currFloats;
            if (currCache.GetXfms(mesh, currKey, numBones, currFloats)) {
                if (rawBones <= kMaxMotionBlurBones) {
                    TheShaderMgr.SetMeshInfo(rawBones, false);
                    RndShader::SelectConfig(mMat, kVelocityObjectShader, false);
                    TheShaderMgr.SetVConstant((VShaderConstant)9, prevFloats, numBones * 3);
                    TheShaderMgr.SetVConstant(
                        (VShaderConstant)0x81, currFloats, numBones * 3
                    );
                    TheShaderMgr.SetVConstant((VShaderConstant)0, unk36bec[prevIdx]);
                    TheShaderMgr.SetVConstant((VShaderConstant)4, unk36bec[currIdx]);
                    TheShaderMgr.SetPConstant(
                        (PShaderConstant)8, (const Vector4 &)mDepthRangeValues
                    );
                    mesh->GetGeomOwner()->DrawFacesInRange(0, -1);
                    TheNgStats->mMotionBlurs++;
                } else {
                    // First %s is the mesh's NAME, not its path: 0x826B21C8
                    // reads 0x24 off the virtual-base-adjusted pointer, which is
                    // Hmx::Object::mName at +0x20, and stores it into the FIRST
                    // argument slot 0x50(r1).  The single PathName() call at
                    // 0x826B21AC fills the SECOND slot, 0x58(r1).
                    MILO_NOTIFY_ONCE(
                        "%s (%s): Has too many bones to apply object motion blur (%d bones of max %d)",
                        mesh->Name(), PathName(mesh), rawBones, kMaxMotionBlurBones
                    );
                }
            }
        }
    }
}

bool RndVelocityBuffer::Draw(RndCam *cam, ObjPtrList<RndDrawable> &drawList) {
    mFrameAdvanced = false;
    float splitMs = mTimer.SplitMs();
    mTimer.Restart();
    float scale = kVelocityRefFrameMs / (splitMs + 1.0f);
        unk36be8 = scale = Min(2.0f, scale);

    // Short-circuit, not bitwise: retail emits two independent branches to the
    // same label (`cmplwi cr6, r29, 0` / `beq`, then `lwz r11, 0xa8(r31)` /
    // `cmplw` / `bne`).  The `&` spelling lowers branchless through
    // subic/subfe/subf/cntlzw/extrwi/and., which is ~40 instructions of noise.
    if (cam != nullptr && cam == mCam) {
        mMat->SetBlend(BaseMaterial::kBlendSrc);
        mMat->SetZMode(kZModeDisable);

        // Both cache slots are addressed BEFORE the memcpy: retail shifts the
        // index twice (`slwi r11,r10,6` / `slwi r10,r10,6`) and materialises
        // `add r25, r11, r8` for the previous frame's matrix up front, keeping
        // it in a callee-saved register across AdvanceFrame and five virtual
        // calls.  Referencing it only at the SetPConstant site recomputes the
        // address there instead.
        //
        // The cache index must be read INLINE in both subscripts, not through a
        // named `int cacheIdx` local.  The local is what made MSVC sink
        // prevXfm's address past the nine intervening calls and rematerialise it
        // at the SetPConstant site from a callee-saved copy of the index PLUS a
        // callee-saved copy of the 0x36bec base -- two registers where the image
        // spends one, which pushed the prologue from `__savegprlr_16` to
        // `__savegprlr_15`, the frame from 0xe0 to 0xf0, and rotated every
        // callee-saved register in the function by one.  That single local was
        // worth 48.74 -> 99.0 canonical.
        // RESIDUAL (w7-as, 99.0 canonical): the last 8 rows are the two address
        // chains emitted in the opposite ORDER inside this one basic block.
        // Image: cur.slwi, prev.slwi, cur.add(r31), prev.xori, cur.add(r8)->r3,
        // prev.add(r31), prev.add(r8)->r25.  Ours starts with prev.slwi and
        // finishes cur one slot later.  Swapping these two declarations is
        // exactly byte-identical, so the order is the MSVC block scheduler's,
        // not the source's.
        ViewProjXfm &curXfm = unk36bec[mActiveXfmCacheIndex];
        ViewProjXfm &prevXfm = unk36bec[mActiveXfmCacheIndex ^ 1];
        memcpy(&curXfm, &mViewProjXfm, 0x40);

        // AdvanceFrame must run unconditionally here (before PreDepthTexture),
        // not inside the if(depthTex) block: the frame counter / cache-index swap
        // has to advance every Draw even when there is no depth texture (native
        // path). frameReady is then applied to mFrameAdvanced only inside the block.
        bool frameReady = AdvanceFrame(cam);
        RndTex *depthTex = TheNgRnd.PreDepthTexture();
        if (depthTex != nullptr) {
            cam->SetTargetTex(mVelocityTex);
            cam->Select();
            TheShaderMgr.SetPConstant((PShaderConstant)9, depthTex);
            TheRenderState.SetTextureFilter(9, (RndRenderState::FilterMode)0, false);
            TheRenderState.SetTextureClamp(9, (RndRenderState::ClampMode)2);
            TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, mViewProjXfm);
            TheShaderMgr.SetPConstant((PShaderConstant)0x86, prevXfm);
            TheNgRnd.DrawRectDepth(
                mFrustumNear,
                (Vector3 (&)[4])mFrustumCorners,
                mDepthRangeValues,
                mMat,
                kVelocityCameraShader
            );

            auto _tmp0 = drawList.size();
            if (_tmp0 != 0) {
                Rnd::Mode savedDrawMode = TheRnd.DrawMode();
                TheRnd.SetDrawMode(Rnd::kDrawVelocity);
                mMat->SetBlend((BaseMaterial::Blend)3);
                mMat->SetZMode(kZModeNormal);
                auto _tmp1 = drawList.end();
                for (ObjPtrList<RndDrawable>::iterator it = drawList.begin();
                     it != _tmp1; ++it) {
                    // Draw(), not DrawShowing(): retail dispatches vtable slot
                    // 0x14 (?Draw@RndDrawable@@UAAXXZ); DrawShowing() is slot
                    // 0x18.  The velocity pass draws every entry of the list it
                    // is handed without re-testing Showing().
                    (*it)->Draw();
                }
                TheRnd.SetDrawMode(savedDrawMode);
            }

            cam->SetTargetTex(nullptr);
            mFrameAdvanced = frameReady;
        }
        mXfmCaches[mActiveXfmCacheIndex].unk1b580 = 0;
    }

    if (mFrameAdvanced) {
        TheShaderMgr.SetPConstant((PShaderConstant)10, mVelocityTex);
        TheRenderState.SetTextureFilter(10, (RndRenderState::FilterMode)1, false);
        TheRenderState.SetTextureClamp(10, (RndRenderState::ClampMode)2);
    } else {
        TheShaderMgr.SetPConstant((PShaderConstant)10, (RndTex *)nullptr);
    }
    return mFrameAdvanced;
}
