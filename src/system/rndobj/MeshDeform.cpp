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
    //
    // RESIDUAL (w7-bp): 97.20089 canonical (95.5 raw), 896 B, 226/226
    // instructions.  Unmoved.  Two charged clusters remain and I could reach
    // neither from source:
    //   [175]-[180] the image tests the accumulator and branches BEFORE it
    //     materialises the 1.0 literal (`clrlwi.` / `beq` at idx 175/176, then
    //     `lis`/`lfs __real@3f800000`); our build hoists the constant-pool load
    //     above the short-circuit guard.  2 insert / 2 delete.
    //   [218]-[221] the u8 -> bool normalisation at the final store.  The image
    //     BRANCHES -- `clrlwi.` (record bit) / `li r11, 0x1` / `bne` /
    //     `li r11, 0x0` -- where we emit the branchless mask idiom
    //     `clrlwi` (no record bit) / `subic` / `subfe`.
    //   MEASURED NEGATIVES for that second cluster, both against 97.20089:
    //     `if (isIdentity) mSkipInverse = true; else mSkipInverse = false;`
    //         -> 94.1 canonical, and it grew the frame by 0x10 and flipped the
    //            prologue to r23-r31.  Much worse; do not retry.
    //     `mSkipInverse = isIdentity ? true : false;`  -> byte-INERT, 97.2 and
    //            the identical 71 rows.
    // No behavioural divergence: the && chain yields 0 or 1 either way.
    // NEGATIVE, and an instructive one (w7-bp).  Binding
    // `const Hmx::Matrix3 &m = mMeshInverse.m;` and reading all nine elements
    // through it collapses the diff from 71 rows to 19 and lifts raw 95.50 ->
    // 96.82 / fuzzy 95.549 -> 96.871 -- but CANONICAL goes DOWN, 97.20089 ->
    // 97.18304, because canonical forgives the 60 register rows it removes and
    // charges the 6 offset rows it adds.  The image is genuinely inconsistent
    // here: it reads the x row off a cached &mMeshInverse base (`lfs f13,
    // 0x0(r30)`, target idx 179 -- the address `bs >> mMeshInverse` left in
    // r30) and the y and z rows off `this` (`0x50/0x54/0x58(r27)` and
    // `0x60/0x64/0x68(r27)`, idx 194-213).  Spelling every element through
    // `mMeshInverse.m` reproduces the y/z half, which is six of the nine.
    // Keep this spelling; do not "clean it up" with a reference.
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
