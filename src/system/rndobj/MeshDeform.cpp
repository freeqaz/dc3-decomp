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
    // MATCHED (w7-bs, 97.20089 -> 100.0 canonical, 99.96 raw, 896 B, 224/224
    // rows equal).  The image is four INLINED Vector3::operator== calls
    // (Vec.h: `x == v.x && y == v.y && z == v.z`) joined by one `&&` and
    // assigned straight to the bool, and every one of the recorded residuals
    // was that shape:
    //   - each row's compare ends in `li r11,1 / fcmpu / beq / li r11,0 /
    //     clrlwi. r11,r11,24 / beq END` (0x826DAAE4-AAF8 etc.): the inlined
    //     operator materialises its bool result, then the OUTER `&&` tests it;
    //   - all the false exits thread to ONE `li r11,0` at 0x826DABA4 because
    //     the whole thing is a single expression, and the last operand is
    //     re-materialised as the expression's value (`clrlwi.` / `li r11,1` /
    //     `bne` at 0x826DAB98-A0) -- that is the "u8 -> bool normalisation"
    //     w7-bp saw, and the branch-before-`lfs __real@3f800000` at
    //     0x826DAAF4-AB04 follows from the threaded CFG (the 1.0 literal is
    //     only needed on the fall-through path);
    //   - the x row reads `0x0/0x4/0x8(r30)` off the cached &mMeshInverse
    //     while v/y/z read off `this` because the inlined operator's `this`
    //     is `&m.x` = this+0x40, which CSEs with the `bs >> mMeshInverse`
    //     argument; `&m.y`/`&m.z` were derived from r30 (+0x10/+0x20) for the
    //     `>>` calls, not from `this`, so their reads fold to this+0x50/0x60.
    //     w7-bp's `const Hmx::Matrix3 &m` binding put y/z through the pointer
    //     too, which is why it charged 6 offset rows.
    //   - the register permutation (r27<->r30 etc., 63 rows) was the same
    //     thing: with one expression `this` is only live in the compares and
    //     lands in r27, below the three callee-saved temporaries.
    // Superseded, do not re-derive: the u8 accumulator split into four
    // statements (97.2), `if (isIdentity) isIdentity = ...` (97.2, more
    // rows), `if/else` store (94.1), `? true : false` (inert), the Matrix3
    // reference (97.18).  Behaviour is identical: an exact identity test.
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
    int count = 0;
    while (ptr < end) {
        count++;
        ptr += (*ptr * 2) + 1;
    }
    int vertCount = count;
    float sum = 0.0f;
    // The counting loop's result reaches the two MILO_NOTIFYs by REFERENCE
    // (MakeString<char const*,int,float> takes `const int&`), so `vertCount`
    // needs a home slot.  The image homes it exactly ONCE, at 0x826D8D64 --
    // after the loop -- and runs the loop itself on a register (r18).  Writing
    // the loop directly into `vertCount` makes MSVC home it at its definition,
    // i.e. eagerly before the loop, which cost an extra `stw` AND rotated the
    // loop (we peeled a top test where the image branches straight to the
    // bottom one, `b .L_826D8D58` at 0x826D8D40) AND flipped the (0x50,0x54)
    // slot pair.  Splitting the loop counter out into `count` and defining
    // `vertCount` after the loop fixes all three at once: 91.8 -> 95.2.
    //
    // RESIDUAL (w7-bi, 95.2 canonical, was 71.2): 18 of the 29 remaining rows
    // are ONE register-pair inversion and its scheduling fallout.  The image
    // gives the EARLIER-defined value the HIGHER callee-saved register in two
    // pairs -- `this` r24 / outer index r23, and `&mData` r22 / the format
    // string r21 -- and our build assigns both pairs the other way round.  Use
    // counts are identical on both sides (8 and 5), so this is a tie-break
    // inside MSVC's allocator, not a liveness difference.  It cascades into the
    // MemResizeElem tail (rows 130-145), where the same two loads and the
    // `num*2` shift are merely scheduled around the swapped registers.
    // REFUTED, do not re-try (each measured, all byte-identical unless noted):
    //   - `float sum;` declared above the counting loop (91.8, neutral);
    //   - the whole `float sum = 0.0f;` moved above the counting loop (82.5 --
    //     it drags the 0.0f anchor and the init store in front of the loop;
    //     the image's anchor is at 0x826D8D60, AFTER the loop);
    //   - `float sum;` declared BEFORE `vertCount` and assigned after the loop
    //     (byte-identical, so the slot pair is a coloring result, not
    //     declaration order -- the image reuses 0x50 for `sum` AND for the
    //     first PathName temp, 0x826D8D70 vs 0x826D8E24, which only a
    //     liveness-based coloring produces);
    //   - `mSize + ptr` for `ptr + mSize` (row 33) and `weights[i] + sum` for
    //     `sum += weights[i]` (row 74): MSVC normalises both commutative
    //     orders, exactly 95.2 either way.
    //
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
