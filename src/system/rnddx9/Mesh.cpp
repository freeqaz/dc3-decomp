#include "Mesh.h"
#include "Mat.h"
#include "Rnd.h"
#include "os/Debug.h"
#include "rndobj/MeshVertCompress.h"
#include "rndobj/Rnd.h"
#include "rndobj/Shader.h"
#include "rndobj/ShaderMgr.h"
#include "rndobj/Stats_NG.h"
#include "rndobj/VelocityBuffer.h"
#include "rnddx9/Utl.h"
#include "math/Mtx.h"
#include "xdk/D3D9.h"
#include "xdk/d3d9i/d3d9.h"

DxMesh::DxMesh() : mNumVerts(0), mNumFaces(0), unk1ac(0), unk1b0(0) {
    if (!sVertexDecl) {
        // clang-format off
        static D3DVERTEXELEMENT9 sVertexElements[] = {
            { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT16_2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            { 0, 20, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
            { 0, 24, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
            { 0, 28, D3DDECLTYPE_UDEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
            { 0, 32, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
            D3DDECL_END()
        };
        // clang-format on
        sVertexDecl = D3DDevice_CreateVertexDeclaration(sVertexElements);
        DX_ASSERT(sVertexDecl, 0xA8);
    }
    if (!sMutableVertexDecl) {
        // clang-format off
        static D3DVERTEXELEMENT9 sMutableVertexElements[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
            { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
            { 0, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            { 0, 72, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
            { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
            D3DDECL_END()
        };
        // clang-format on
        sMutableVertexDecl = D3DDevice_CreateVertexDeclaration(sMutableVertexElements);
        DX_ASSERT(sMutableVertexDecl, 0xAF);
    }
    if (!sMutableSkinnedVertexDecl) {
        // clang-format off
        static D3DVERTEXELEMENT9 sMutableSkinnedVertexElements[] = {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
            { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
            { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
            { 0, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
            { 0, 72, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
            { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
            D3DDECL_END()
        };
        // clang-format on
        sMutableSkinnedVertexDecl =
            D3DDevice_CreateVertexDeclaration(sMutableSkinnedVertexElements);
        DX_ASSERT(sMutableSkinnedVertexDecl, 0xB5);
    }
}

DxMesh::~DxMesh() {
    TheDxRnd.AutoRelease(unk1ac);
    unk1ac = nullptr;
    TheDxRnd.AutoRelease(unk1b0);
    unk1b0 = nullptr;
}

u32 DxMesh::VertFVF() const {
    return 0;
}

static const unsigned int kBitsOutput = 32;

void ScaleAddEq(Hmx::Matrix3 &m1, const Hmx::Matrix3 &m2, float f) {
    ScaleAdd(m1.x, m2.x, f, m1.x);
    ScaleAddEq(m1.y, m2.y, f);
    ScaleAddEq(m1.z, m2.z, f);
}

void ScaleAddEq(Transform &tf1, const Transform &tf2, float f) {
    ScaleAddEq(tf1.m, tf2.m, f);
    ScaleAdd(tf1.v, tf2.v, f, tf1.v);
}

void PackVector(
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

void FillCompressedVertex(
    CompressedVertex_Xbox &compressed, const RndMesh::Vert &vert, bool normalize
) {
    // Pack color (ARGB D3DCOLOR format)
    u32 green = (u32)(vert.color.green * 255.0f);
    u32 blue = (u32)(vert.color.blue * 255.0f);
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

void DxMesh::VertexBufferData::Release() {
    TheDxRnd.AutoRelease((D3DResource *)buffer);
    buffer = nullptr;
    size = 0;
}

void DxMesh::VertexBufferData::SetData(D3DVertexBuffer *buf, unsigned int sz) {
    MILO_ASSERT(buf, 0x1E);
    MILO_ASSERT(sz, 0x1F);
    buffer = buf;
    size = sz;
}

void DxMesh::Copy(const Hmx::Object *src, Hmx::Object::CopyType ty) {
    RndMesh::Copy(src, ty);
    const DxMesh *other = dynamic_cast<const DxMesh *>(src);
    if (other && this == GetGeomOwner() && !mMutable) {
        PhysMemTypeTracker tracker("D3D(phys):Mesh");
        unk1a4.Release();
        mNumVerts = other->mNumVerts;
        if (mNumVerts) {
            D3DVertexBuffer *clone = CloneVertexBuffer(other->unk1a4.buffer);
            unk1a4.SetData(clone, other->unk1a4.size);
        }
        TheDxRnd.AutoRelease(unk1ac);
        unk1ac = nullptr;
        mNumFaces = other->mNumFaces;
        if (mNumFaces) {
            unk1ac = (D3DResource *)CloneIndexBuffer((D3DIndexBuffer *)other->unk1ac);
        }
    }
}

D3DVertexBuffer *DxMesh::GetMultimeshFaces() {
    MILO_ASSERT(!Mutable(), 0x1A7);
    if (!unk1b0) {
        unsigned int numIndices = mNumFaces * 3;
        D3DVertexBuffer *vb =
            D3DDevice_CreateVertexBuffer(numIndices * 4, 0, (D3DPOOL)0);
        unk1b0 = (D3DResource *)vb;
        unsigned int *dst = (unsigned int *)D3DVertexBuffer_Lock(vb, 0, 0, 0);
        unsigned short *src =
            (unsigned short *)D3DIndexBuffer_Lock((D3DIndexBuffer *)unk1ac, 0, 0, 0x10);
        for (unsigned int i = 0; i < numIndices; i++) {
            *dst++ = *src++;
        }
        D3DIndexBuffer_Unlock((D3DIndexBuffer *)unk1ac);
        D3DVertexBuffer_Unlock((D3DVertexBuffer *)unk1b0);
    }
    return (D3DVertexBuffer *)unk1b0;
}

void DxMesh::FillCompressedVerts() {
    MILO_ASSERT(mNumCompressedVerts > 0, 0x115);
    MILO_ASSERT(mCompressedVerts != NULL, 0x116);
    VBLock<CompressedVertex_Xbox> lock(unk1a4.buffer, 0);
    memcpy(lock.mDataAddr, mCompressedVerts, VertSize() * mNumCompressedVerts);
}

void DxMesh::OnSync(int) {}

void DxMesh::SetTransforms() {
    bool shouldCache = mMotionCache.mShouldCache;
    int numProcessed = 0;
    mMotionCache.mShouldCache = false;
    unsigned int boneCount = mBones.size();
    TheShaderMgr.SetMeshInfo(boneCount, HasAOCalc());
    float fw = FurWeight(Mat());
    bool hasFur = fw > 0.0f;
    if (boneCount == 0) {
        TheShaderMgr.UpdateCache(WorldXfm(), 0);
        if (hasFur) {
            CacheFurTransform(WorldXfm(), 0, fw);
        }
    } else {
        RndBone *bone = mBones.begin();
        if (bone != mBones.end()) {
            do {
                Transform local;
                Multiply(bone->mOffset, bone->mBone->WorldXfm(), local);
                TheShaderMgr.UpdateCache(local, numProcessed);
                if (hasFur) {
                    CacheFurTransform(local, numProcessed, fw);
                }
                bone++;
                numProcessed++;
            } while (bone != mBones.end());
        }
        TheNgStats->mBones += numProcessed - 1;
        if (boneCount >= 1) {
            goto upload;
        }
    }
    boneCount = 1;
upload:
    TheShaderMgr.SetVConstant(
        kVS_WorldTransform, TheShaderMgr.ConstantCache(), boneCount * 3
    );
    if (shouldCache) {
        RndVelocityBuffer::Singleton().CacheTransform(
            this, TheShaderMgr.ConstantCache(), boneCount
        );
    }
}

void DxMesh::DrawShowing() {
    DxMesh *geom = static_cast<DxMesh *>(GetGeomOwner());
    if (!geom->CanDraw()) {
        return;
    }
    if (geom->Verts().unkc) {
        geom->Sync(0x1f);
    }
    if (TheRnd.GetDrawMode() == Rnd::kDrawVelocity) {
        RndVelocityBuffer::Singleton().DrawMesh(this);
        return;
    }
    SetTransforms();
    RndMat *mat = Mat();
    RndMat *next;
    do {
        if (mat) {
            if (mat->GetFur()) {
                mat = DrawFur(static_cast<DxMat *>(mat));
                continue;
            }
            next = dynamic_cast<RndMat *>(mat->NextPass());
        } else {
            next = nullptr;
        }
        ShaderType st = kStandardShader;
        if (mMeshVersion != kMaxShaderTypes) {
            st = (ShaderType)mMeshVersion;
        }
        if (TheRnd.GetDrawMode() == 9) {
            st = kAllWhiteShader;
        }
        RndShader::SelectConfig(mat, st, false);
        geom->DrawFacesInRange(0, -1);
        mat = next;
    } while (mat);
}

void _fake(void) {
    BufLock<struct D3DVertexBuffer> buf(nullptr, 0);
    BufLock<struct D3DIndexBuffer> buf2(nullptr, 0);
}
