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
    // The accumulator is a u8 rather than a bool on purpose: `mSkipInverse` is a
    // bool, and a bool->bool assignment lets MSVC fuse the last conjunction's
    // 0/1 materialisation straight into the `stb`. The target does NOT fuse --
    // it re-tests the accumulator and re-materialises 0/1 before the store,
    // which is the u8->bool conversion (96.152 -> 97.2). Behaviour is identical:
    // an `&&` chain yields 0 or 1 either way.
    //
    // REFUTED (measured, do not re-try): rewriting the three continuation lines
    // as `if (isIdentity) isIdentity = ...;` to try to buy the target's
    // jump-threading -- the target sends groups 1-3's false exits straight to
    // the shared `li r11,0` while we walk each subsequent test. Identical
    // canonical 97.2 and two MORE register rows (63 -> 65 diff_arg, a 5th swap
    // pair). The threading is a backend choice here, not a source shape.
    unsigned char isIdentity =
        mMeshInverse.v.x == 0 && mMeshInverse.v.y == 0 && mMeshInverse.v.z == 0;
    isIdentity = isIdentity && mMeshInverse.m.x.x == 1 && mMeshInverse.m.x.y == 0
        && mMeshInverse.m.x.z == 0;
    isIdentity = isIdentity && mMeshInverse.m.y.x == 0 && mMeshInverse.m.y.y == 1
        && mMeshInverse.m.y.z == 0;
    isIdentity = isIdentity && mMeshInverse.m.z.x == 0 && mMeshInverse.m.z.y == 0
        && mMeshInverse.m.z.z == 1;
    mSkipInverse = isIdentity;
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
        unsigned char *w = cData;
        for (int j = 0; j < *cData; j++) {
            unsigned char bone = *++w;
            float weight = *++w * 0.003921568859368563f;
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
    // count existing verts
    auto& _ref0 = mData;
    u8 *ptr = (u8 *)_ref0;
    u8 *end = ptr + mSize;
    int vertCount = 0;
    while (ptr < end) {
        vertCount++;
        ptr += (*ptr * 2) + 1;
    }
    float sum = 0.0f;
    // RESIDUAL (w7-bi, 91.8 canonical, was 71.2): 7 rows are the stack pair
    // (0x50,0x54) -- the image gives 0x50 to `sum` (shared with the first
    // PathName temp) and 0x54 to `vertCount`; we allocate them the other way.
    // REFUTED, do not re-try: declaring `float sum;` above the counting loop
    // (identical 91.8), and moving the whole `float sum = 0.0f;` there (82.5 --
    // it drags the 0.0f anchor and the init store in front of the loop).
    // Another 5 rows are r23<->r24 (`this` vs the outer index) and the counting
    // loop's rotation: the image branches straight to the bottom test
    // (`b .L_826D8D58` at 0x826D8D40) and homes `vertCount` once afterwards,
    // where we peel a top test and home it eagerly.
    // One fused loop: the dedup scan, the negative-weight report and the sum all
    // live in the same `for (i)` -- 0x826D8D98..0x826D8E68 is a single loop with
    // one `cmpwi cr6, r31, 0x0` zero-trip guard at 0x826D8D68.  The inner scan
    // walks j FORWARD from i+1 and merges j into i (`stfsx f0, r8, r30` writes
    // back to weights[i] at 0x826D8DE4), then fills the hole from the tail and
    // steps j back; there is no `break`, the scan continues from the swapped-in
    // element.
    for (int i = 0; i < num; i++) {
        for (int j = i + 1; j < num; j++) {
            if (boneIndices[j] == boneIndices[i]) {
                weights[i] += weights[j];
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
        sum += weights[i];
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
        _ref0, mSize, (void *)((char *)_ref0 + mSize), 0, (num * 2) + 1, __FILE__, 0x85, "RndMeshDeform"
    );
    *newEntry = (u8)num;
    for (int i = 0; i < num; i++) {
        newEntry[i * 2 + 1] = (u8)boneIndices[i];
        float w = weights[i] * scale;
        newEntry[i * 2 + 2] = (u8)(Clamp(0.0f, 1.0f, w) * 255.0f + 0.5f);
    }
    return vertCount;
}

void RndMeshDeform::VertArray::Copy(const RndMeshDeform::VertArray &a) {
    SetSize(a.mSize);
    memcpy(mData, a.mData, mSize);
}
