#pragma once
#include "rndobj\Mesh.h"
#include "math\Vec.h"
#include "utl/BinStream.h"

struct CompressedVertex_Xbox {
    float mPosX;
    float mPosY;
    float mPosZ;
    int mColor; // 0xc - packed color
    unsigned int mNormal;
    unsigned int mTangent;
    unsigned int mBinormal;
    unsigned int mBoneIndices;
    unsigned int mBoneWeights;
};

// PackVector is defined here, `static`, rather than out-of-line in one of the
// two Mesh.cpp files.  ham_xbox_r.map lists ?PackVector@@YAXAAIABVVector4@@EEEE_N@Z
// TWICE -- 826202e0 from rnddx9:Mesh.obj and 8263a168 from rndobj:Mesh.obj, both
// bare `f` -- which only internal linkage can produce, and the __FILE__ both
// copies bake in is e:\lazer_build_gmc1\system\src\rndobj/MeshVertCompress.h.
// The two shipped bodies are identical word-for-word except for 5 branch
// displacements and each TU's own copy of that file string, which is also why
// /OPT:ICF could not fold them: the string COMDATs sit at different addresses,
// so the bytes differ.
static const unsigned int kBitsOutput = 32;

static void PackVector(
    unsigned int &output,
    const Vector4 &vec,
    unsigned char bitsX,
    unsigned char bitsY,
    unsigned char bitsZ,
    unsigned char bitsW,
    bool normalize
) {
    MILO_ASSERT((bitsX + bitsY + bitsZ + bitsW) == kBitsOutput, 0x39);

    int offsetY = bitsX;
    int offsetZ = bitsY + bitsX;
    int normFactor = normalize ? 1 : 0;
    int kOffsetW = bitsZ + offsetZ;

    int shiftY = bitsY - normFactor;
    int shiftZ = bitsZ - normFactor;
    int shiftX = bitsX - normFactor;
    int shiftW = bitsW - normFactor;

    u32 maskY = (1U << bitsY) - 1;
    u32 maskZ = (1U << bitsZ) - 1;
    u32 maskW = (1U << bitsW) - 1;
    u32 maskX = (1U << bitsX) - 1;
    int maxX = (1 << shiftX) - 1;
    int maxY = (1 << shiftY) - 1;
    int maxZ = (1 << shiftZ) - 1;
    int maxW = (1 << shiftW) - 1;

    MILO_ASSERT(kOffsetW + bitsW == kBitsOutput, 0x4E);

    f32 fy = (f32)(f64)maxY;
    f32 fx = (f32)(f64)maxX;
    f32 fw = (f32)(f64)maxW;
    f32 fz = (f32)(f64)maxZ;

    u32 py = ((u32)(s32)(vec.y * fy)) & maskY;
    u32 px = ((u32)(s32)(vec.x * fx)) & maskX;
    u32 pz = ((u32)(s32)(vec.z * fz)) & maskZ;
    u32 pw = ((u32)(s32)(vec.w * fw)) & maskW;

    output = (pw << kOffsetW) | (pz << offsetZ) | (py << offsetY) | px;
}

void FillCompressedVertex(CompressedVertex_Xbox &, const RndMesh::Vert &, bool);
void SaveCompressedVertex(const CompressedVertex_Xbox &, BinStream &);
