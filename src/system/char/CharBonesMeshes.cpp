#include "char\CharBonesMeshes.h"
#ifdef HX_NATIVE
#include <cstdlib>
#include <cstring>
#include <cstdio>
#endif
#include "char\CharUtl.h"
#include "math\Rot.h"
#include "obj/Object.h"
#include "rndobj\Trans.h"
#include "utl\Str.h"
#include <string.h>

RndTransformable *CharBonesMeshes::sDummyMesh;

CharBonesMeshes::CharBonesMeshes() : mMeshes(this, (EraseMode)0, kObjListOwnerControl) {}

CharBonesMeshes::~CharBonesMeshes() { mMeshes.clear(); }

bool CharBonesMeshes::Replace(ObjRef *ref, Hmx::Object *obj) {
    ObjPtrVec<RndTransformable>::iterator it = mMeshes.FindRef(ref);
    if (it != mMeshes.end()) {
        RndTransformable *trans = obj ? dynamic_cast<RndTransformable *>(obj) : 0;
        mMeshes.Set(it, trans);
        if (!*it) {
            mMeshes.Set(it, sDummyMesh);
        }
        return true;
    }
    return Hmx::Object::Replace(ref, obj);
}

void CharBonesMeshes::ReallocateInternal() {
    CharBonesAlloc::ReallocateInternal();
    String str;
    mMeshes.clear();
    mMeshes.reserve(mBones.size());
    for (int i = 0; i < mBones.size(); i++) {
        RndTransformable *trans = CharUtlFindBoneTrans(mBones[i].name.Str(), Dir());
        if (!trans) {
            if (strncmp("bone_facing", mBones[i].name.Str(), 0xB)) {
                str += MakeString("%s, ", mBones[i].name);
            }
            trans = sDummyMesh;
        }
        mMeshes.push_back(trans);
    }
    if (mMeshes.empty())
        return;
    else
        AcquirePose();
}

void CharBonesMeshes::AcquirePose() {
    // Typed cursors over the channel blocks (the og-dc3 spelling). With char*
    // cursors the &mOffsets[TYPE_ROTX] CSE temp got sid 392, so its load's
    // commutative sort key (0x10008) fell below the mStart-value temp's and
    // the two adds in the quat/rot-x headers listed mStart first; the image
    // lists the offset first, as this spelling does.
    ObjPtrVec<RndTransformable>::iterator curMesh = mMeshes.begin();
    Vector3 *vecEnd = (Vector3 *)ScaleOffset();
    for (Vector3 *it = (Vector3 *)Start(); it < vecEnd; ++it, ++curMesh) {
        *it = (*curMesh)->LocalXfm().v;
    }
    vecEnd = (Vector3 *)QuatOffset();
    for (Vector3 *it = (Vector3 *)ScaleOffset(); it < vecEnd; ++it, ++curMesh) {
        MakeScale((*curMesh)->LocalXfm().m, *it);
    }
    Hmx::Quat *quatEnd = (Hmx::Quat *)RotXOffset();
    for (Hmx::Quat *it = (Hmx::Quat *)QuatOffset(); it < quatEnd; ++it, ++curMesh) {
        it->Set((*curMesh)->LocalXfm().m);
    }
    float *rotIt = (float *)RotXOffset();
    float *rotEnd = (float *)(mStart + mOffsets[TYPE_ROTY]);
    for (; rotIt < rotEnd; ++rotIt, ++curMesh) {
        *rotIt = GetXAngle((*curMesh)->LocalXfm().m);
    }
    rotEnd = (float *)(mStart + mOffsets[TYPE_ROTZ]);
    for (; rotIt < rotEnd; ++rotIt, ++curMesh) {
        *rotIt = GetYAngle((*curMesh)->LocalXfm().m);
    }
    rotEnd = (float *)EndOffset();
    for (; rotIt < rotEnd; ++rotIt, ++curMesh) {
        *rotIt = GetZAngle((*curMesh)->LocalXfm().m);
    }
}

void CharBonesMeshes::PoseMeshes() {
    ObjPtrVec<RndTransformable>::iterator curMesh = mMeshes.begin();

#ifdef HX_NATIVE
    // Frame-keyed foot-plant guard (see CharIKFoot.cpp): clears guarded leg bones at each new
    // frame's first PoseMeshes so the legit pose runs; a LATER PoseMeshes this frame skips a
    // freshly-planted leg bone (in the QUAT loop below) so the plant survives the poll order.
    { extern void Dc3PlantGuardTick(); Dc3PlantGuardTick(); }
#endif

    // Set positions
    auto& start = mStart;
    Vector3 *pos = (Vector3 *)start;
    Vector3 *scaleOff = (Vector3 *)(start + mOffsets[TYPE_SCALE]);
    for (; pos < scaleOff; pos++, ++curMesh) {
#ifdef HX_NATIVE
        { extern bool Dc3PlantGuarded(RndTransformable *);
          if (Dc3PlantGuarded(*curMesh)) continue; }
#endif
        (*curMesh)->SetLocalPos(*pos);
#ifdef HX_NATIVE
        {
            static int sFootikLog = 0;
            const char *nm = (*curMesh) ? (*curMesh)->Name() : nullptr;
            if (getenv("DC3_IK_DIAG") && sFootikLog < 24 && nm && strstr(nm, "footik")) {
                sFootikLog++;
                fprintf(stderr, "DC3_IK_DIAG FootikPos[%d] dir=%s mesh=%s ptr=%p pos=(%.4f,%.4f,%.4f)\n",
                        sFootikLog, Dir() ? Dir()->Name() : "?", nm, (void *)(*curMesh),
                        pos->x, pos->y, pos->z);
            }
        }
#endif
    }

    // Handle quaternions and rotations if we have enough meshes
    if (mCounts[TYPE_QUAT] < mMeshes.size()) {
        curMesh = mMeshes.begin() + mCounts[TYPE_QUAT];

        // Apply quaternion rotations
        Hmx::Quat *quatEnd = (Hmx::Quat *)(start + mOffsets[TYPE_ROTX]);
        Hmx::Quat *quat = (Hmx::Quat *)(start + mOffsets[TYPE_QUAT]);
        for (; quat < quatEnd; quat++, ++curMesh) {
            Normalize(*quat, *quat);
#ifdef HX_NATIVE
            // Skip a leg bone that the foot-plant guarded THIS frame (a later overwrite pose).
            { extern bool Dc3PlantGuarded(RndTransformable *);
              if (Dc3PlantGuarded(*curMesh)) continue; }
#endif
            MakeRotMatrix(*quat, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply X rotations
        float *rotIt = (float *)(start + mOffsets[TYPE_ROTX]);
        float *rotyOff = (float *)(start + mOffsets[TYPE_ROTY]);
        for (; rotIt < rotyOff; rotIt++, ++curMesh) {
            MakeRotMatrixX(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply Y rotations
        float *rotzOff = (float *)(start + mOffsets[TYPE_ROTZ]);
        for (; rotIt < rotzOff; rotIt++, ++curMesh) {
            MakeRotMatrixY(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply Z rotations
        float *endOff = (float *)(start + mOffsets[TYPE_END]);
        for (; rotIt < endOff; rotIt++, ++curMesh) {
            MakeRotMatrixZ(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }
    }

    // Handle scales if we have enough meshes
    if (mCounts[TYPE_SCALE] < mMeshes.size()) {
        curMesh = mMeshes.begin() + mCounts[TYPE_SCALE];
        Vector3 *scaleEnd = (Vector3 *)(start + mOffsets[TYPE_QUAT]);
        Vector3 *scale = (Vector3 *)(start + mOffsets[TYPE_SCALE]);
        for (; scale < scaleEnd; scale++, ++curMesh) {
            Transform &xfm = (*curMesh)->DirtyLocalXfm();
            Vector3 scaleVec;
            MakeScale(xfm.m, scaleVec);
            xfm.m.x *= scale->x / scaleVec.x;
            xfm.m.y *= scale->y / scaleVec.y;
            xfm.m.z *= scale->z / scaleVec.z;
        }
    }
}

void CharBonesMeshes::StuffMeshes(std::list<Hmx::Object *> &oList) {
    for (int i = 0; i < mMeshes.size(); i++) {
        oList.push_back(mMeshes[i]);
    }
}

BEGIN_PROPSYNCS(CharBonesMeshes)
    SYNC_PROP(meshes, mMeshes)
    SYNC_SUPERCLASS(CharBonesObject)
END_PROPSYNCS

void CharBonesMeshes::Init() { sDummyMesh = Hmx::Object::New<RndTransformable>(); }

void CharBonesMeshes::Terminate() {}

// w8-c: the image's CharBonesMeshes.obj carries an out-of-line
// `PropSync<RndTransformable>(RndTransformable *&, ...)` COMDAT (264 B, at
// 0x8234D0F8 in build/373307D9/asm/system/char/CharBonesMeshes.s) that NOTHING
// in the TU calls -- retail's ObjPtrVec PropSync inlines the two call sites
// (the asserts at line 0x66 appear inline inside
// `??$PropSync@VRndTransformable@@@@YA_NAAV?$ObjPtrVec@...`), and MSVC emits the
// instantiation anyway.  We build at /O1 (=> /Ob1), where MSVC declines to
// expand it even when the template is marked `inline`, so writing the call
// instead of the hand-inlined body costs the enclosing function 100.0 -> 68.0
// while gaining this row: measured, net -4,344 B on the headline.  An explicit
// instantiation buys the COMDAT with no code change at all.
template bool
PropSync<RndTransformable>(RndTransformable *&, DataNode &, DataArray *, int, PropOp);
