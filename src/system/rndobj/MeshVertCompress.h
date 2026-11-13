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
//
// Lane history (75 lines moved): docs/decomp/notes/system/rndobj/MeshVertCompress.md
// > Verdicts: Do not re-open (w8-k)
static const unsigned int kBitsOutput = 32;

// Lane history (32 lines moved): docs/decomp/notes/system/rndobj/MeshVertCompress.md
// > Do not re-apply it: the chain-vs-tree difference has to be reached without moving
// > which name binds which register. (w17-a)
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
    int offsetZ = offsetY + bitsY;
    int normFactor = normalize ? 1 : 0;
    int kOffsetW = bitsZ + offsetZ;

    int shiftY = bitsY - normFactor;
    int shiftZ = bitsZ - normFactor;
    int shiftX = bitsX - normFactor;
    int shiftW = bitsW - normFactor;

    u32 maskX = (1U << bitsX) - 1;
    u32 maskY = (1U << bitsY) - 1;
    u32 maskZ = (1U << bitsZ) - 1;
    u32 maskW = (1U << bitsW) - 1;
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

static inline unsigned short FloatToHalf(float value) {
    unsigned int raw = *(unsigned int *)&value;
    unsigned int iValue = raw & 0x7FFFFFFF;
    unsigned int sign = (raw >> 16) & 0x8000;
    if (iValue > 0x47FFEFFF) {
        return (unsigned short)(sign | 0x7FFF);
    }
    if (iValue < 0x38800000) {
        unsigned int shift = 113 - (iValue >> 23);
        iValue = (0x800000 | (iValue & 0x7FFFFF)) >> shift;
    } else {
        iValue -= 0x38000000;
    }
    return (unsigned short)(sign | ((((iValue >> 13) & 1) + iValue + 0xFFF) >> 13));
}

// Lane history (41 lines moved): docs/decomp/notes/system/rndobj/MeshVertCompress.md
// > Do not retry declaration order, argument inlining, or commuting this `|`. (w7-bx)
// > Verdicts: Do not retry (w7-bx)
static void FillCompressedVertex(
    CompressedVertex_Xbox &compressed, const RndMesh::Vert &vert, bool normalize
) {
    // Pack color (ARGB D3DCOLOR format)
    u32 green = (u32)(vert.color.green * 255.0f);
    u32 blue = (u32)(vert.color.blue * 255.0f);
    // Lane history (17 lines moved): docs/decomp/notes/system/rndobj/MeshVertCompress.md
    // > Composing both exchanges, BOTH sides store alpha<<24 | red<<16 | green<<8 | blue
    // > i.e. D3DCOLOR ARGB, which is also what this vertex element must be. (w9-e)
    // > Verdicts: NOT A BUG
    u32 alpha = (u32)(vert.color.alpha * 255.0f);
    u32 red = (u32)(vert.color.red * 255.0f);
    compressed.mColor = ((((alpha << 8) | (red & 0xFF)) << 8) | (green & 0xFF))
            << 8
        | (blue & 0xFF);

    // Pack bone weights as UDEC4N
    PackVector(
        (unsigned int &)compressed.mBoneIndices, vert.boneWeights, 10, 10, 10, 2, false
    );

    // Copy position as float bit patterns
    *(f32 *)(&compressed.mPosX) = vert.pos.x;
    *(f32 *)(&compressed.mPosY) = vert.pos.y;
    *(f32 *)(&compressed.mPosZ) = vert.pos.z;

    // Pack UV as float16_2
    unsigned short halfU = FloatToHalf(vert.tex.x);
    unsigned short halfV = FloatToHalf(vert.tex.y);
    compressed.mNormal = (halfU << 16) | halfV;

    // Pack normal as DEC4N
    float normZ = vert.norm.z;
    float normY = vert.norm.y;
    Vector4 normVec(vert.norm.x, normY, normZ, 0.0f);
    PackVector((unsigned int &)compressed.mTangent, normVec, 10, 10, 10, 2, true);

    // Pack tangent as DEC4N
    PackVector((unsigned int &)compressed.mBinormal, vert.tangent, 10, 10, 10, 2, true);

    // Pack bone indices as UBYTE4
    compressed.mBoneWeights = (((int)vert.boneIndices[3] * 0x100
        + (int)vert.boneIndices[2]) * 0x100
        + (int)vert.boneIndices[1]) * 0x100
        + (int)vert.boneIndices[0];
}

void SaveCompressedVertex(const CompressedVertex_Xbox &, BinStream &);
