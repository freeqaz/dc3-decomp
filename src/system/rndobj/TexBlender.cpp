#include "rndobj\TexBlender.h"
#include "Utl.h"
#include "obj\Data.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj\Draw.h"
#include "rndobj\Mat.h"
#include "rndobj\Mesh.h"
#include "rndobj\Tex.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj/Cam.h"
#include "rndobj\Shader.h"
#include "rndobj\Rnd_NG.h"
#include "rndobj\PostProc.h"
#include <algorithm>

struct BlendSorter {
    bool operator()(
        const std::pair<RndTexBlendController *, float> &a,
        const std::pair<RndTexBlendController *, float> &b
    ) const {
        return a.second < b.second;
    }
};

#pragma region Hmx::Object

RndTexBlender::RndTexBlender()
    : mBaseMap(this), mNearMap(this), mFarMap(this), mOutputTextures(this),
      mControllerList(this), mOwner(this), mControllerInfluence(1), mRenderedStates(0),
      unkc0(true) {}

BEGIN_HANDLERS(RndTexBlender)
    HANDLE(get_render_textures, OnGetRenderTextures)
    HANDLE_SUPERCLASS(RndDrawable)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndTexBlender)
    SYNC_PROP(base_map, mBaseMap)
    SYNC_PROP(near_map, mNearMap)
    SYNC_PROP(far_map, mFarMap)
    SYNC_PROP(output_texture, mOutputTextures)
    SYNC_PROP(controller_list, mControllerList)
    SYNC_PROP(owner, mOwner)
    SYNC_PROP(controller_influence, mControllerInfluence)
    SYNC_SUPERCLASS(RndDrawable)
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

BEGIN_SAVES(RndTexBlender)
    SAVE_REVS(2, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    SAVE_SUPERCLASS(RndDrawable)
    bs << mOutputTextures;
    bs << mBaseMap;
    bs << mNearMap;
    bs << mFarMap;
    bs << mControllerList;
    bs << mOwner;
    bs << mControllerInfluence;
END_SAVES

BEGIN_COPYS(RndTexBlender)
    COPY_SUPERCLASS(Hmx::Object)
    COPY_SUPERCLASS(RndDrawable)
    CREATE_COPY(RndTexBlender)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mOutputTextures)
        COPY_MEMBER(mBaseMap)
        COPY_MEMBER(mNearMap)
        COPY_MEMBER(mFarMap)
        COPY_MEMBER(mControllerList)
        COPY_MEMBER(mOwner)
        COPY_MEMBER(mControllerInfluence)
    END_COPYING_MEMBERS
    mRenderedStates = 0;
END_COPYS

INIT_REVS(2, 0)

BEGIN_LOADS(RndTexBlender)
    LOAD_REVS(bs);
    ASSERT_REVS(2, 0);
    Hmx::Object::Load(bs);
    RndDrawable::Load(bs);
    bs >> mOutputTextures;
    bs >> mBaseMap;
    bs >> mNearMap;
    bs >> mFarMap;
    bs >> mControllerList;
    bs >> mOwner;
    if (d.rev > 1)
        bs >> mControllerInfluence;
    else
        mControllerInfluence = 0.7071068f;
    mRenderedStates = 0;
END_LOADS

#pragma endregion
#pragma region RndDrawable

float RndTexBlender::GetDistanceToPlane(const Plane &plane, Vector3 &vec) {
    if (mOwner) {
        return mOwner->GetDistanceToPlane(plane, vec);
    } else
        return 0;
}

bool RndTexBlender::MakeWorldSphere(Sphere &sphere, bool b) {
    if (mOwner) {
        return mOwner->MakeWorldSphere(sphere, b);
    } else
        return false;
}

void RndTexBlender::DrawShowing() {
    if (TheRnd.GetDrawMode() == Rnd::kDrawNormal
        && ((TheRnd.ProcCmds() & kProcessWorld) != 0 || TheRnd.ProcCmds() == 0)
        && mOutputTextures) {
        if ((mOutputTextures->GetType() & RndTex::kRenderedNoZ) != RndTex::kRenderedNoZ) {
            MILO_NOTIFY_ONCE(
                "%s: \"%s\" must be renderable with no z-buffer",
                PathName(this),
                mOutputTextures->Name()
            );
        } else {
            if (0x40000 < mOutputTextures->Height() * mOutputTextures->Width()) {
                MILO_NOTIFY_ONCE(
                    "%s: \"%s\" is %d x %d, must be no larger than 512 x 512",
                    PathName(this),
                    mOutputTextures->Name(),
                    mOutputTextures->Height(),
                    mOutputTextures->Width()
                );
            }
            std::vector<std::pair<RndTexBlendController *, float> > nearList, farList,
                customList;
            float influence = mControllerInfluence;
            FOREACH (it, mControllerList) {
                RndTexBlendController *curCtrlr = *it;
                float second;
                RndTexBlendController::BlendState state =
                    curCtrlr->GetBlendState(second, influence);
                switch (state) {
                case 1:
                    nearList.push_back(std::make_pair(curCtrlr, second));
                    break;
                case 2:
                    farList.push_back(std::make_pair(curCtrlr, second));
                    break;
                case 3:
                    customList.push_back(std::make_pair(curCtrlr, second));
                    break;
                }
            }
            if (unkc0 || !nearList.empty() || !farList.empty() || !customList.empty()
                || mRenderedStates != 1) {
                unkc0 = false;
                RndCam *cam = TheRnd.GetDefaultCam();
                RndCam *prevCam = RndCam::Current();
                RndTex *targetTex = prevCam->TargetTex();
                if (targetTex) {
                    MILO_NOTIFY_ONCE(
                        "%s: Cannot render to texture (%s) while already rendering to texture (%s).",
                        PathName(targetTex),
                        PathName(this),
                        PathName(targetTex)
                    );
                }
                cam->SetTargetTex(mOutputTextures);
                cam->Select();
                if (mBaseMap) {
                    RndMat *work = TheShaderMgr.GetWork();
                    SetupMaterial(work, mBaseMap);
                    work->SetAlpha(1);
                    TheNgRnd.DrawRect(
                        Hmx::Rect(
                            0, 0, mOutputTextures->Width(), mOutputTextures->Height()
                        ),
                        work,
                        (ShaderType)6,
                        Hmx::Color(1, 1, 1),
                        nullptr,
                        nullptr
                    );
                    mRenderedStates = 1;
                }
                std::sort(nearList.begin(), nearList.end(), BlendSorter());
                std::sort(farList.begin(), farList.end(), BlendSorter());
                RndTex *nearMap = mNearMap;
                if (nearMap && !nearList.empty()) {
                    mRenderedStates |= 2;
                    RndMat *work = TheShaderMgr.GetWork();
                    Transform xfm;
                    xfm.Reset();
                    TheShaderMgr.SetVConstant((VShaderConstant)4, Hmx::Matrix4(xfm));
                    TheShaderMgr.SetTransform(xfm);
                    SetupMaterial(work, nearMap);
                    work->SetBlend(BaseMaterial::kBlendSrcAlpha);
                    float alpha = -1;
                    FOREACH (it, nearList) {
                        RndTexBlendController *blendCtrlr = it->first;
                        float f17 = it->second;
                        if (f17 != alpha) {
                            work->SetAlpha(f17);
                            RndShader::SelectConfig(work, (ShaderType)0x16, false);
                            alpha = f17;
                        }
                        RndMesh *mesh = blendCtrlr->Mesh();
                        if (mesh->IsSkinned()) {
                            MILO_NOTIFY_ONCE(
                                "%s: \"%s\" should not be a skinned mesh",
                                PathName(this),
                                mesh->Name()
                            );
                        }
                        mesh->DrawFacesInRange(0, -1);
                    }
                    work->SetAlpha(1);
                    if (RndCam::Current()) {
                        TheShaderMgr.SetVConstant(
                            (VShaderConstant)4, RndCam::Current()->GetViewProjMatrix()
                        );
                    }
                }
                if (mFarMap && !farList.empty()) {
                    mRenderedStates |= 4;
                    RndMat *work = TheShaderMgr.GetWork();
                    Transform xfm;
                    xfm.Reset();
                    TheShaderMgr.SetVConstant((VShaderConstant)4, Hmx::Matrix4(xfm));
                    TheShaderMgr.SetTransform(xfm);
                    SetupMaterial(work, mFarMap);
                    work->SetBlend(BaseMaterial::kBlendSrcAlpha);
                    float alpha = -1;
                    FOREACH (it, farList) {
                        RndTexBlendController *blendCtrlr = it->first;
                        float f17 = it->second;
                        if (f17 != alpha) {
                            work->SetAlpha(f17);
                            RndShader::SelectConfig(work, (ShaderType)0x16, false);
                            alpha = f17;
                        }
                        RndMesh *mesh = blendCtrlr->Mesh();
                        if (mesh->IsSkinned()) {
                            MILO_NOTIFY_ONCE(
                                "%s: \"%s\" should not be a skinned mesh",
                                PathName(this),
                                mesh->Name()
                            );
                        }
                        mesh->DrawFacesInRange(0, -1);
                    }
                    work->SetAlpha(1);
                    if (RndCam::Current()) {
                        TheShaderMgr.SetVConstant(
                            (VShaderConstant)4, RndCam::Current()->GetViewProjMatrix()
                        );
                    }
                }
                DrawBlendList(customList, (TexState)8);
                cam->SetTargetTex(nullptr);
                prevCam->Select();
            }
        }
    }
}

#pragma endregion
#pragma region RndTexBlender

RndMat *RndTexBlender::SetupMaterial(RndMat *mat, RndTex *tex) {
    mat->SetZMode(kZModeDisable);
    mat->SetBlend(RndMat::kBlendSrc);
    mat->SetCull(kCullNone);
    mat->SetTexWrap(kTexWrapClamp);
    mat->SetDiffuseTex(tex);
    return mat;
}

void RndTexBlender::DrawBlendList(
    const std::vector<std::pair<RndTexBlendController *, float> > &list,
    TexState state
) {
    RndTex *texmap = (state != 2) ? mNearMap : mFarMap;

    bool texValid = (texmap != nullptr);
    if ((texValid || (state == 8)) && (!list.empty())) {
        mRenderedStates |= state;

        RndMat *mat = TheShaderMgr.GetWork();
        float f31 = 1.0f;
        float f29 = -1.0f;

        Transform xfm;
        xfm.Reset();
        Hmx::Matrix4 viewProjMtx = Hmx::Matrix4(xfm);
        TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, viewProjMtx);
        TheShaderMgr.SetTransform(xfm);
        SetupMaterial(mat, texmap);

        mat->SetBlend(BaseMaterial::kBlendSrcAlpha);

        for (std::vector<std::pair<RndTexBlendController *, float> >::const_iterator it =
                 list.begin();
             it != list.end();
             ++it) {
            RndTexBlendController *controller = it->first;
            float alpha = it->second;

            if (state == 8) {
                mat->SetDiffuseTex(controller->Tex());
            }

            if (alpha != f29 || state == 8) {
                mat->SetAlpha(alpha);
                RndShader::SelectConfig(mat, (ShaderType)0x16, false);
                f29 = alpha;
            }

            RndMesh *mesh = controller->Mesh();
            if (mesh) {
                if (mesh->IsSkinned()) {
                    MILO_NOTIFY_ONCE(
                        "%s: \"%s\" should not be a skinned mesh",
                        PathName(this),
                        mesh->Name()
                    );
                }
                mesh->DrawFacesInRange(0, -1);
            }
        }

        mat->SetAlpha(f31);

        RndCam *cam = RndCam::Current();
        if (cam) {
            TheShaderMgr.SetVConstant(kVS_ViewProjMatrix, cam->GetViewProjMatrix());
        }
    }
}

DataNode RndTexBlender::OnGetRenderTextures(DataArray *) {
    return GetRenderTexturesNoZ(Dir());
}
