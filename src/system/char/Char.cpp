#include "char\Char.h"
#include "CharBonesMeshes.h"
#include "CharClipSet.h"
#include "CharIKHead.h"
#include "CharMeshHide.h"
#include "CharPollGroup.h"
#include "CharTaskMgr.h"
#include "CharUtl.h"
#include "FileMergerOrganizer.h"
#include "char\CharBlendBone.h"
#include "char\CharBone.h"
#include "char\CharBoneDir.h"
#include "char\CharBoneOffset.h"
#include "char\CharBoneTwist.h"
#include "char\CharBones.h"
#include "char\CharBonesBlender.h"
#include "char\CharClip.h"
#include "char\CharClipGroup.h"
#include "char\CharCollide.h"
#include "char\CharCuff.h"
#include "char\CharDriver.h"
#include "char\CharDriverMidi.h"
#include "char\CharEyeDartRuleset.h"
#include "char\CharEyes.h"
#include "char\CharFaceServo.h"
#include "char\CharForeTwist.h"
#include "char\CharGuitarString.h"
#include "char\CharHair.h"
#include "char\CharIKFingers.h"
#include "char\CharIKFoot.h"
#include "char\CharIKHand.h"
#include "char\CharIKMidi.h"
#include "char\CharIKRod.h"
#include "char\CharIKScale.h"
#include "char\CharIKSliderMidi.h"
#include "char\CharInterest.h"
#include "char\CharLipSync.h"
#include "char\CharLipSyncDriver.h"
#include "char\CharLookAt.h"
#include "char\CharMirror.h"
#include "char\CharNeckTwist.h"
#include "char\CharPosConstraint.h"
#include "char\CharServoBone.h"
#include "char\CharSignalApplier.h"
#include "char\CharSleeve.h"
#include "char\CharTransDraw.h"
#include "char\CharUpperTwist.h"
#include "char\CharWeightSetter.h"
#include "char\CharWeightable.h"
#include "char\Character.h"
#include "char\ClipCollide.h"
#include "char\FileMerger.h"
#include "char\Waypoint.h"
#include "obj\Data.h"
#include "obj\DataFunc.h"
#include "obj\Msg.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "rndobj/Cam.h"
#include "rndobj\Highlight.h"
#include "rndobj\Mat.h"
#include "rndobj\Mesh.h"
#include "rndobj\Overlay.h"
#include "rndobj\Tex.h"
#include "rndobj\Utl.h"
#include "world\Dir.h"
#include <cstring>

// Target: Char.obj .data:0x0 (0x82F08480) = .float -1.
float gCharHighlightY = -1.0f;

CharDebug TheCharDebug;

void CharInit() {
    TheCharDebug.Init();
    Character::Init();
    CharBonesObject::Init();
    REGISTER_OBJ_FACTORY(CharBoneOffset);
    REGISTER_OBJ_FACTORY(CharBlendBone);
    REGISTER_OBJ_FACTORY(CharBone);
    REGISTER_OBJ_FACTORY(CharBonesBlender);
    CharBonesMeshes::Init();
    REGISTER_OBJ_FACTORY(CharBoneTwist);
    CharClip::Init();
    REGISTER_OBJ_FACTORY(CharClipSet);
    REGISTER_OBJ_FACTORY(CharClipGroup);
    REGISTER_OBJ_FACTORY(CharCollide);
    REGISTER_OBJ_FACTORY(CharCuff);
    REGISTER_OBJ_FACTORY(CharDriver);
    REGISTER_OBJ_FACTORY(CharDriverMidi);
    REGISTER_OBJ_FACTORY(CharEyes);
    REGISTER_OBJ_FACTORY(CharInterest);
    REGISTER_OBJ_FACTORY(CharEyeDartRuleset);
    REGISTER_OBJ_FACTORY(CharFaceServo);
    REGISTER_OBJ_FACTORY(CharForeTwist);
    REGISTER_OBJ_FACTORY(CharHair);
    REGISTER_OBJ_FACTORY(CharIKFingers);
    REGISTER_OBJ_FACTORY(CharIKFoot);
    REGISTER_OBJ_FACTORY(CharIKHand);
    REGISTER_OBJ_FACTORY(CharIKHead)
    REGISTER_OBJ_FACTORY(CharIKMidi);
    REGISTER_OBJ_FACTORY(CharIKSliderMidi);
    REGISTER_OBJ_FACTORY(CharIKRod);
    REGISTER_OBJ_FACTORY(CharIKScale);
    REGISTER_OBJ_FACTORY(CharLipSync);
    CharLipSync::Init();
    REGISTER_OBJ_FACTORY(CharLipSyncDriver);
    REGISTER_OBJ_FACTORY(CharLookAt);
    CharMeshHide::Init();
    REGISTER_OBJ_FACTORY(CharMirror);
    REGISTER_OBJ_FACTORY(CharNeckTwist);
    REGISTER_OBJ_FACTORY(CharPollGroup);
    REGISTER_OBJ_FACTORY(CharPosConstraint);
    REGISTER_OBJ_FACTORY(CharServoBone);
    REGISTER_OBJ_FACTORY(CharSignalApplier);
    REGISTER_OBJ_FACTORY(CharSleeve);
    CharTaskMgr::Init();
    REGISTER_OBJ_FACTORY(CharTransDraw);
    REGISTER_OBJ_FACTORY(CharUpperTwist);
    REGISTER_OBJ_FACTORY(CharWeightable);
    REGISTER_OBJ_FACTORY(CharWeightSetter);
    Waypoint::Init();
    REGISTER_OBJ_FACTORY(CharGuitarString);
    REGISTER_OBJ_FACTORY(FileMerger);
    REGISTER_OBJ_FACTORY(CharBoneDir);
    REGISTER_OBJ_FACTORY(ClipCollide);
    FileMergerOrganizer::Init();
    PreloadSharedSubdirs("char");
    CharBoneDir::Init();
    CharUtlInit();
    TheDebug.AddExitCallback(CharTerminate);
}

void CharTerminate() {
    TheDebug.RemoveExitCallback(CharTerminate);
    Character::Terminate();
    CharBoneDir::Terminate();
    CharBonesMeshes::Terminate();
    CharLipSync::Terminate();
}

#pragma region CharDebug

CharDebug::CharDebug() : mObjects(nullptr), mOnce(nullptr) {}

CharDebug::~CharDebug() {}

float CharDebug::UpdateOverlay(RndOverlay *ovl, float hilite_y) {
    gCharHighlightY = hilite_y;
    RndCam *cur = RndCam::Current();
    RndCam *worldCam = nullptr;
    if (TheWorld) {
        worldCam = TheWorld->Cam();
        if (worldCam) {
            worldCam->Select();
        }
    }
    FOREACH (it, mObjects) {
        DisplayObject(*it);
    }
    FOREACH (it, mOnce) {
        DisplayObject(*it);
    }
    mOnce.clear();
    if (mObjects.empty()) {
        ovl->SetShowing(false);
    }
    if (worldCam) {
        cur->Select();
    }
    float ret = gCharHighlightY;
    gCharHighlightY = -1;
    return ret;
}

void CharDebug::Init() {
    DataRegisterFunc("char_debug", OnSetObjects);
    mOverlay = RndOverlay::Find("char_debug", true);
    mOverlay->SetCallback(this);
}

void CharDebug::Once(Hmx::Object *obj) {
    AddObject(obj, true);
    mOverlay->SetShowing(!mObjects.empty() || !mOnce.empty());
}

void CharDebug::AddObject(Hmx::Object *o, bool b) {
    if (o) {
        ObjPtrList<Hmx::Object> &which = b ? mOnce : mObjects;
        auto it = which.find(o);
        if (it == which.end()) {
            which.push_back(o);
        } else {
            which.erase(it);
        }
    }
}

void CharDebug::SetObjects(DataArray *msg) {
    int i = 1;
    bool clear = false;
    bool once = false;
    if (msg->Size() > 1 && msg->Type(1) == kDataSymbol) {
        if (msg->Sym(1) == "clear" || msg->Sym(1) == "once") {
            i = 2;
            clear = msg->Sym(1) == "clear";
            once = msg->Sym(1) == "once";
        }
    }
    if (clear) {
        mObjects.clear();
    }
    for (; i < msg->Size(); i++) {
        AddObject(msg->Obj<Hmx::Object>(i), once);
    }
    mOverlay->SetShowing(!mObjects.empty() || !mOnce.empty());
}

// RESIDUAL (w7-bl, 88.89 canonical, 54 rows): the entire gap is ONE MSVC
// decision -- we hoist the function-local static `mesh` POINTER into a
// callee-saved register and the image re-loads it at every use.  The image
// emits `lwz r11, ?mesh@?8??DisplayObject@...@l(r31)` before each
// `lwz r11, 0x148(r11)` (0x82340C24, 0x82340CC0 inside the vertex loop,
// 0x82340D18, 0x82340D30, 0x82340D40, 0x82340D64), i.e. it treats the
// vertex stores as possibly aliasing the static; we prove they do not and
// cache it in r30.  That single extra live value is the whole cascade: a
// 5th callee-saved GPR (`__savegprlr_27` vs the image's `_28`), frame 0x90
// vs 0x80, and the flat r27..r31 renumbering that accounts for 21 of the
// 54 rows.  Failed spellings (both measured in this worktree): writing the
// loop body as `mesh->Verts()[i].pos.Set(...)` etc. with no `vert`
// reference is WORSE (86.31, 170 rows -- MSVC then rematerialises the
// vector base five times); hoisting `Vector2 uv` out of the loop and using
// `uv.Set(v, u)` is byte-inert (88.89, same 35/3/6/10 rows).
// The `SetObjConcrete<AnimTask>` vs `SetObjConcrete<RndTex>` name in the
// Function Call Diff is an ICF fold -- ObjRefConcrete<T,ObjectDir>::
// SetObjConcrete is the same machine code for every T -- not a wrong callee.
void CharDebug::DisplayObject(Hmx::Object *obj) {
    RndHighlightable *rh = dynamic_cast<RndHighlightable *>(obj);
    if (rh)
        rh->Highlight();
    else {
        RndTex *tex = dynamic_cast<RndTex *>(obj);
        if (tex) {
            static RndMesh *mesh = nullptr;
            static RndMat *mat = nullptr;
            if (!mesh) {
                mesh = Hmx::Object::New<RndMesh>();
                mat = Hmx::Object::New<RndMat>();
                mat->SetUseEnv(false);
                mesh->Verts().resize(4);
                mesh->Faces().resize(2);
                for (int i = 0; i < 4; i++) {
                    float u = i == 1 || i == 2 ? 1.0f : 0.0f;
                    float v = i < 2 ? 1.0f : 0.0f;
                    // Retail's 8-byte copy in this loop goes to +0x40 of the
                    // vertex (`std r8, 0x40(r11)` at 0x82340CF0), which is
                    // Vert::tex -- NOT boneIndices at +0x48.  The quad's UVs
                    // were being written over the bone indices.
                    Vector2 uv(v, u);
                    RndMesh::Vert &vert = mesh->Verts()[i];
                    vert.pos.Set((uv.x + 1) * 20, 0, -(uv.y * 20 - 60));
                    vert.tex = uv;
                    vert.norm.Set(0, -1, 0);
                    vert.boneWeights.Set(0, 0, 0, 0);
                    vert.color.Set(1, 1, 1, 1);
                }
                mesh->Faces()[0].Set(0, 1, 2);
                mesh->Faces()[1].Set(0, 2, 3);
                mesh->Sync(0x13F);
                mesh->SetMat(mat);
            }
            // Retail has no null test on `mat` here: 0x82340D80 loads the
            // static and goes straight into the inlined SetDiffuseTex.
            mat->SetDiffuseTex(tex);
            CreateAndSetMetaMat(mat);
            mesh->DrawShowing();
        }
    }
}

DataNode CharDebug::OnSetObjects(DataArray *a) {
    TheCharDebug.SetObjects(a);
    return 0;
}

#pragma endregion CharDebug

void CharDeferHighlight(Hmx::Object *obj) { TheCharDebug.Once(obj); }
