#include "rndobj\MeshDeform.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "utl/BinStream.h"
#include "utl\MemMgr.h"
#include "math\Rot.h"

#pragma region Hmx::Object

RndMeshDeform::RndMeshDeform()
    : mMesh(this), mBones(this), mVerts(this), mSkipInverse(0), mDeformed(0) {}

RndMeshDeform::~RndMeshDeform() {}

BEGIN_HANDLERS(RndMeshDeform)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

BEGIN_PROPSYNCS(RndMeshDeform)
    SYNC_PROP(mesh, mMesh)
    SYNC_PROP_SET(num_verts, mVerts.NumVerts(), )
    SYNC_PROP_SET(num_bones, (int)mBones.size(), )
    SYNC_SUPERCLASS(Hmx::Object)
END_PROPSYNCS

void operator<<(BinStream &bs, const RndMeshDeform::BoneDesc &desc) {
    bs << desc.mBone;
    bs << desc.unk14 << desc.unk54;
}

BEGIN_SAVES(RndMeshDeform)
    SAVE_REVS(1, 0)
    SAVE_SUPERCLASS(Hmx::Object)
    bs << mMesh;
    int numBones = mBones.size();
    bs << numBones;
    for (int i = 0; i < numBones; i++) {
        bs << mBones[i];
    }
    mVerts.Save(bs);
    bs << mMeshInverse;
END_SAVES

BEGIN_COPYS(RndMeshDeform)
    COPY_SUPERCLASS(Hmx::Object)
    CREATE_COPY(RndMeshDeform)
    BEGIN_COPYING_MEMBERS
        COPY_MEMBER(mMesh)
        const Transform &src = c->mMeshInverse;
        mMeshInverse = src;
        COPY_MEMBER(mBones)
        COPY_MEMBER(mSkipInverse)
        mVerts.Copy(c->mVerts);
    END_COPYING_MEMBERS
END_COPYS

void operator>>(BinStream &bs, RndMeshDeform::BoneDesc &desc) {
    bs >> desc.mBone;
    bs >> desc.unk14 >> desc.unk54;
}

INIT_REVS(1, 0)

BEGIN_LOADS(RndMeshDeform)
    LOAD_REVS(bs)
    ASSERT_REVS(1, 0)
    Hmx::Object::Load(bs);
    bs >> mMesh;
    int num = 0;
    if (d.rev < 1) {
        bs >> num;
    }
    int bones;
    bs >> bones;
    if (d.rev < 1) {
        mVerts.Clear();
        int i150[64];
        float f250[64];
        for (int i = 0; i < num; i++) {
            int weightIdx = 0;
            for (int j = 0; j < bones; j++) {
                float f74;
                bs >> f74;
                if (f74 != 0) {
                    i150[weightIdx] = j;
                    f250[weightIdx] = f74;
                    weightIdx++;
                }
            }
            mVerts.AppendWeights(weightIdx, i150, f250);
        }
    }
    mBones.resize(bones);
    for (int i = 0; i < bones; i++) {
        bs >> mBones[i];
    }
    if (d.rev > 0) {
        mVerts.Load(bs);
    }
    bs >> mMeshInverse;
    // how NOT to check against the identity matrix.
    //
    // Lane history (29 lines moved): docs/decomp/notes/system/rndobj/MeshDeform.md
    // > MATCHED (w7-bs, 97.20089 -> 100.0 canonical, 99.96 raw, 896 B, 224/224 rows
    // > equal).
    mSkipInverse = mMeshInverse.v == Vector3(0, 0, 0)
        && mMeshInverse.m.x == Vector3(1, 0, 0) && mMeshInverse.m.y == Vector3(0, 1, 0)
        && mMeshInverse.m.z == Vector3(0, 0, 1);
END_LOADS

void RndMeshDeform::PreSave(BinStream &bs) {
    if (mMesh) {
        mMesh->SetKeepMeshData(true);
    }
}

void RndMeshDeform::Print() {
    TheDebug << "num_verts " << mVerts.NumVerts() << "\n";
    TheDebug << "mesh_inverse " << mMeshInverse << "\n";
    TheDebug << "skip_inverse " << mSkipInverse << "\n";
    TheDebug << "mesh " << mMesh.Ptr() << "\n";
    for (int i = 0; i < mBones.size(); i++) {
        BoneDesc &cur = mBones[i];
        TheDebug << "bone" << i << ":\n";
        TheDebug << "   " << cur.mBone.Ptr() << "\n";
        TheDebug << "   " << cur.unk14 << "\n";
        TheDebug << "   " << cur.unk54 << "\n";
    }
    int i = 0;
    auto it = mVerts.begin();
    for (; it < mVerts.end(); ++it, ++i) {
        TheDebug << "weights" << i << ": ";
        unsigned char *cData = (unsigned char *)it.Data();
        // Lane history (6 lines moved): docs/decomp/notes/system/rndobj/MeshDeform.md
        for (int j = 0; j < *cData; j++) {
            float weight = cData[j * 2 + 2] * 0.003921568859368563f;
            unsigned char bone = cData[j * 2 + 1];
            TheDebug << "(" << bone << " " << weight << ") ";
        }
        TheDebug << "\n";
    }
}

#pragma endregion
#pragma region RndMeshDeform

void RndMeshDeform::VertArray::Save(BinStream &bs) {
    bs << mSize;
    bs.Write(mData, mSize);
}

void RndMeshDeform::VertArray::Load(BinStream &bs) {
    int size;
    bs >> size;
    SetSize(size);
    bs.Read(mData, mSize);
}

void RndMeshDeform::VertArray::SetSize(int size) {
    if (mSize != size) {
        mSize = size;
        MemFree(mData);
        mData = MemAlloc(mSize, __FILE__, 0x99, "RndMeshDeform");
    }
}

int RndMeshDeform::VertArray::AppendWeights(int num, int *const boneIndices, float *const weights) {
    MILO_ASSERT(num < VertArray::kMaxWeights, 0x5F);
    int vertCount = NumVerts();
    float sum = 0.0f;
    // Lane history (25 lines moved): docs/decomp/notes/system/rndobj/MeshDeform.md
    // > Swapping the source operands is inert: c2 orders the operands itself (in the IL,
    // > before regasg; the CAND list and registers do not change). (w25-fb)
    for (int i = 0; i < num; i++) {
        for (int j = i + 1; j < num; j++) {
            if (boneIndices[j] == boneIndices[i]) {
                float merged = weights[j];
                weights[i] += merged;
                num--;
                boneIndices[j] = boneIndices[num];
                weights[j] = weights[num];
                j--;
            }
        }
        if (!(weights[i] > 0.0f)) {
            MILO_NOTIFY(
                "%s vert %d has negative weight %g on bone, won't export",
                PathName(mParent),
                vertCount,
                weights[i]
            );
            weights[i] = 0.0f;
        }
        float weight = weights[i];
        sum += weight;
    }
    if (Abs(sum - 1.0f) > 0.05f) {
        MILO_NOTIFY(
            "%s vert %d weights sum to %g, not close enough to 1, check the skinning",
            PathName(mParent),
            vertCount,
            sum
        );
    }
    float scale = 1.0f / sum;
    // append (num*2+1) bytes at end of buffer
    u8 *newEntry = (u8 *)MemResizeElem(
        mData, mSize, end(), 0, (num * 2) + 1, __FILE__, 0x85, "RndMeshDeform"
    );
    *newEntry = (u8)num;
    for (int i = 0; i < num; i++) {
        newEntry[i * 2 + 1] = (u8)boneIndices[i];
        float weight = weights[i];
        float w = weight * scale;
        float clamped = Clamp(0.0f, 1.0f, w);
        newEntry[i * 2 + 2] = (u8)(clamped * 255.0f + 0.5f);
    }
    return vertCount;
}

void RndMeshDeform::VertArray::Copy(const RndMeshDeform::VertArray &a) {
    SetSize(a.mSize);
    memcpy(mData, a.mData, mSize);
}
