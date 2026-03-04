#include "char/CharBonesMeshes.h"
#include "char/CharUtl.h"
#include "math/Rot.h"
#include "obj/Object.h"
#include "rndobj/Trans.h"
#include "utl/Str.h"
#include <string.h>

RndTransformable *CharBonesMeshes::sDummyMesh;

CharBonesMeshes::CharBonesMeshes() : mMeshes(this, (EraseMode)0, kObjListOwnerControl) {}

CharBonesMeshes::~CharBonesMeshes() { mMeshes.clear(); }

bool CharBonesMeshes::Replace(ObjRef *ref, Hmx::Object *obj) {
    ObjPtrVec<RndTransformable>::iterator it = mMeshes.FindRef(ref);
    if (it != mMeshes.end()) {
        RndTransformable *trans = dynamic_cast<RndTransformable *>(obj);
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
#ifdef HX_NATIVE
    int foundCount = 0, dummyCount = 0;
#endif
    for (int i = 0; i < mBones.size(); i++) {
        RndTransformable *trans = CharUtlFindBoneTrans(mBones[i].name.Str(), Dir());
        if (!trans) {
            if (strncmp("bone_facing", mBones[i].name.Str(), 0xB)) {
                str += MakeString("%s, ", mBones[i].name);
            }
            trans = sDummyMesh;
#ifdef HX_NATIVE
            dummyCount++;
            if (dummyCount <= 5)
                printf("  ReallocateInternal: MISSING bone '%s' in dir '%s' -> sDummyMesh\n",
                       mBones[i].name.Str(), Dir() ? Dir()->Name() : "(null)");
#endif
        }
#ifdef HX_NATIVE
        else {
            foundCount++;
            if (foundCount <= 3)
                printf("  ReallocateInternal: found bone '%s' -> '%s' (%p)\n",
                       mBones[i].name.Str(), trans->Name(), (void*)trans);
        }
#endif
        mMeshes.push_back(trans);
    }
#ifdef HX_NATIVE
    printf("  ReallocateInternal: %d found, %d dummy, %zu total bones, dir='%s'\n",
           foundCount, dummyCount, mBones.size(), Dir() ? Dir()->Name() : "(null)");
#endif
    if (mMeshes.empty())
        return;
    else
        AcquirePose();
}

void CharBonesMeshes::AcquirePose() {
    ObjPtrVec<RndTransformable>::iterator curMesh = mMeshes.begin();

    // Copy positions
    char *scaleOff = mOffsets[TYPE_SCALE] + mStart;
    char *pos = mStart;
    for (; pos < scaleOff; pos += sizeof(Vector3), ++curMesh) {
        *(Vector3 *)pos = (*curMesh)->LocalXfm().v;
    }

    // Copy scales using MakeScale
    pos = mOffsets[TYPE_SCALE] + mStart;
    char *quatOff = mOffsets[TYPE_QUAT] + mStart;
    for (; pos < quatOff; pos += sizeof(Vector3), ++curMesh) {
        MakeScale((*curMesh)->LocalXfm().m, *(Vector3 *)pos);
    }

    // Copy quaternions using Quat::Set
    pos = mOffsets[TYPE_QUAT] + mStart;
    char *rotxOff = mOffsets[TYPE_ROTX] + mStart;
    for (; pos < rotxOff; pos += sizeof(Hmx::Quat), ++curMesh) {
        ((Hmx::Quat *)pos)->Set((*curMesh)->LocalXfm().m);
    }

    // Copy X rotations
    float *rotIt = (float *)(mOffsets[TYPE_ROTX] + mStart);
    float *rotyOff = (float *)(mOffsets[TYPE_ROTY] + mStart);
    for (; rotIt < rotyOff; rotIt++, ++curMesh) {
        *rotIt = GetXAngle((*curMesh)->LocalXfm().m);
    }

    // Copy Y rotations
    float *rotzOff = (float *)(mOffsets[TYPE_ROTZ] + mStart);
    for (; rotIt < rotzOff; rotIt++, ++curMesh) {
        *rotIt = GetYAngle((*curMesh)->LocalXfm().m);
    }

    // Copy Z rotations
    float *endOff = (float *)(mOffsets[TYPE_END] + mStart);
    for (; rotIt < endOff; rotIt++, ++curMesh) {
        *rotIt = GetZAngle((*curMesh)->LocalXfm().m);
    }
}

void CharBonesMeshes::PoseMeshes() {
    ObjPtrVec<RndTransformable>::iterator curMesh = mMeshes.begin();

#ifdef HX_NATIVE
    printf("  CharBonesMeshes::PoseMeshes: mMeshes.size()=%zu mStart=%p mTotalSize=%d\n",
           mMeshes.size(), (void*)mStart, mTotalSize);
    printf("    offsets: POS=0 SCALE=%d QUAT=%d ROTX=%d ROTY=%d ROTZ=%d END=%d\n",
           mOffsets[TYPE_SCALE], mOffsets[TYPE_QUAT], mOffsets[TYPE_ROTX],
           mOffsets[TYPE_ROTY], mOffsets[TYPE_ROTZ], mOffsets[TYPE_END]);
    printf("    counts: POS=%d SCALE=%d QUAT=%d ROTX=%d ROTY=%d ROTZ=%d END=%d\n",
           mCounts[0], mCounts[1], mCounts[2], mCounts[3], mCounts[4], mCounts[5], mCounts[6]);
#endif

    // Set positions
    Vector3 *pos = (Vector3 *)mStart;
    Vector3 *scaleOff = (Vector3 *)(mStart + mOffsets[TYPE_SCALE]);
#ifdef HX_NATIVE
    int posCount = 0;
    for (Vector3 *p = pos; p < scaleOff; p++, posCount++) {
        if (posCount < 2) {
            printf("    POS[%d]: buf=(%.3f,%.3f,%.3f) mesh='%s' pre_local=(%.3f,%.3f,%.3f)\n",
                   posCount, p->x, p->y, p->z,
                   mMeshes[posCount] ? mMeshes[posCount]->Name() : "(null)",
                   mMeshes[posCount] ? mMeshes[posCount]->LocalXfm().v.x : 0,
                   mMeshes[posCount] ? mMeshes[posCount]->LocalXfm().v.y : 0,
                   mMeshes[posCount] ? mMeshes[posCount]->LocalXfm().v.z : 0);
        }
    }
    printf("    POS loop: %d entries (mOffsets[SCALE]=%d / sizeof(Vector3)=%zu)\n",
           posCount, mOffsets[TYPE_SCALE], sizeof(Vector3));
#endif
    for (; pos < scaleOff; pos++, ++curMesh) {
        (*curMesh)->SetLocalPos(*pos);
    }

    // Handle quaternions and rotations if we have enough meshes
    if (mCounts[TYPE_QUAT] < mMeshes.size()) {
        curMesh = mMeshes.begin() + mCounts[TYPE_QUAT];

        // Apply quaternion rotations
        Hmx::Quat *quat = (Hmx::Quat *)(mStart + mOffsets[TYPE_QUAT]);
        Hmx::Quat *quatEnd = (Hmx::Quat *)(mStart + mOffsets[TYPE_ROTX]);
#ifdef HX_NATIVE
        int quatIdx = 0;
        printf("    QUAT section: offset=%d endOffset=%d, count=%d\n",
               mOffsets[TYPE_QUAT], mOffsets[TYPE_ROTX],
               (int)(((char*)quatEnd - (char*)quat) / sizeof(Hmx::Quat)));
#endif
        for (; quat < quatEnd; quat++, ++curMesh) {
#ifdef HX_NATIVE
            if (quatIdx < 3)
                printf("    QUAT[%d]: (%.4f,%.4f,%.4f,%.4f) -> mesh='%s'\n",
                       quatIdx, quat->x, quat->y, quat->z, quat->w,
                       (*curMesh) ? (*curMesh)->Name() : "(null)");
            quatIdx++;
#endif
            Normalize(*quat, *quat);
            MakeRotMatrix(*quat, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply X rotations
        float *rotIt = (float *)(mStart + mOffsets[TYPE_ROTX]);
        float *rotyOff = (float *)(mStart + mOffsets[TYPE_ROTY]);
        for (; rotIt < rotyOff; rotIt++, ++curMesh) {
            MakeRotMatrixX(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply Y rotations
        float *rotzOff = (float *)(mStart + mOffsets[TYPE_ROTZ]);
        for (; rotIt < rotzOff; rotIt++, ++curMesh) {
            MakeRotMatrixY(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }

        // Apply Z rotations
        float *endOff = (float *)(mStart + mOffsets[TYPE_END]);
        for (; rotIt < endOff; rotIt++, ++curMesh) {
            MakeRotMatrixZ(*rotIt, (*curMesh)->DirtyLocalXfm().m);
        }
    }

    // Handle scales if we have enough meshes
    if (mCounts[TYPE_SCALE] < mMeshes.size()) {
        curMesh = mMeshes.begin() + mCounts[TYPE_SCALE];
        Vector3 *scale = (Vector3 *)(mStart + mOffsets[TYPE_SCALE]);
        Vector3 *scaleEnd = (Vector3 *)(mStart + mOffsets[TYPE_QUAT]);
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
