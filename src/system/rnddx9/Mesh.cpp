#include "Mesh.h"
#include "Mat.h"
#include "Rnd.h"
#include "obj/Task.h"
#include "os\Debug.h"
#include "rndobj\BaseMaterial.h"
#include "rndobj\Fur.h"
// Forward slash on purpose: this TU's copy of PackVector carries
// e:\lazer_build_gmc1\system\src\rndobj/MeshVertCompress.h in the image, while
// rndobj/Mesh.cpp's copy carries the backslash form.  The original spelled the
// two includes differently and MSVC writes __FILE__ the way the file was
// reached, so the difference is per-TU and has to be reproduced per-TU.
#include "rndobj/MeshVertCompress.h"
#include "rndobj\Wind.h"
#include "rndobj\Rnd.h"
#include "rndobj\Shader.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Stats_NG.h"
#include "rndobj\VelocityBuffer.h"
#include "rnddx9\Utl.h"
#include "math\Mtx.h"
#include "xdk\D3D9.h"
#include "xdk\d3d9i\d3d9.h"

// Target: Mesh.obj .bss:0x0/0x4/0x8 (0x830A1800/04/08), all zero, in this order.
D3DVertexDeclaration *DxMesh::sVertexDecl;
D3DVertexDeclaration *DxMesh::sMutableVertexDecl;
D3DVertexDeclaration *DxMesh::sMutableSkinnedVertexDecl;

DxMesh::DxMesh() : mNumVerts(0), mNumFaces(0), unk1ac(0), unk1b0(0) {
    // clang-format off
    static D3DVERTEXELEMENT9 sVertexElements[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT16_2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 20, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 24, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
        { 0, 28, D3DDECLTYPE_UDEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
        { 0, 32, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
        D3DDECL_END(),

        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 72, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
        { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
        D3DDECL_END(),

        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
        { 0, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 72, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
        { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
        D3DDECL_END()
    };
    // clang-format on
    if (!sVertexDecl) {
        sVertexDecl = D3DDevice_CreateVertexDeclaration(&sVertexElements[0]);
        DX_ASSERT(sVertexDecl, 0xA8);
    }
    if (!sMutableVertexDecl) {
        sMutableVertexDecl = D3DDevice_CreateVertexDeclaration(&sVertexElements[8]);
        DX_ASSERT(sMutableVertexDecl, 0xAF);
    }
    if (!sMutableSkinnedVertexDecl) {
        sMutableSkinnedVertexDecl =
            D3DDevice_CreateVertexDeclaration(&sVertexElements[15]);
        DX_ASSERT(sMutableSkinnedVertexDecl, 0xB5);
    }
}

DxMesh::~DxMesh() {
    TheDxRnd.AutoRelease(unk1ac);
    unk1ac = nullptr;
    TheDxRnd.AutoRelease(unk1b0);
    unk1b0 = nullptr;
}

unsigned int DxMesh::VertSize() const {
    if (GetGfxMode() == kNewGfx) {
        return 0x24;
    }
    return IsSkinned() ? 0x30 : 0x24;
}

unsigned int DxMesh::VertFVF() const {
    if (GetGfxMode() == kNewGfx) {
        return 0;
    }
    return IsSkinned() ? 0x15A : 0x152;
}


void ScaleAddEq(Hmx::Matrix3 &m1, const Hmx::Matrix3 &m2, float f) {
    ScaleAdd(m1.x, m2.x, f, m1.x);
    ScaleAddEq(m1.y, m2.y, f);
    ScaleAddEq(m1.z, m2.z, f);
}

void ScaleAddEq(Transform &tf1, const Transform &tf2, float f) {
    ScaleAddEq(tf1.m, tf2.m, f);
    ScaleAdd(tf1.v, tf2.v, f, tf1.v);
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

void DxMesh::VertexBufferData::SetData(D3DVertexBuffer *buffer, unsigned int size) {
    MILO_ASSERT(buffer != NULL, 0x1E);
    MILO_ASSERT(size > 0, 0x1F);
    this->buffer = buffer;
    this->size = size;
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

void DxMesh::Fill(RndMesh::Vert *begin, RndMesh::Vert *end) {
    VBLock<CompressedVertex_Xbox> lock(unk1a4.buffer, 0);
    if (begin != end) {
        CompressedVertex_Xbox *dst = (CompressedVertex_Xbox *)lock.mDataAddr;
        do {
            FillCompressedVertex(*dst, *begin, false);
            begin++;
            dst++;
        } while (begin != end);
    }
}

bool DxMesh::CanDraw() const {
    bool hasBuffers = (int)unk1a4.buffer && unk1ac != NULL;
    return hasBuffers || mMutable;
}

void DxMesh::CacheFurTransform(const Transform &xfm, int i, float weight) {
    MILO_ASSERT(mTransformCache.size() > i, 0x1ee);
    Transform &cached = mTransformCache[i];
    float dz = cached.v.z - xfm.v.z;
    float dy = cached.v.y - xfm.v.y;
    float dx = cached.v.x - xfm.v.x;
    if (Dot(xfm.m.y, cached.m.y) >= 0.8660254f
        && dx * dx + dy * dy + dz * dz < 2500.0f) {
        float invWeight = 1.0f - weight;
        cached.m.x *= invWeight;
        cached.m.y *= invWeight;
        cached.m.z *= invWeight;
        cached.v *= invWeight;
        ScaleAddEq(cached, xfm, weight);
    } else {
        cached.m = xfm.m;
        cached.v = xfm.v;
    }
    RndWind *wind = Mat()->GetFur()->GetWind();
    if (wind) {
        Vector3 windForce;
        float windTime = TheTaskMgr.Seconds(TaskMgr::kRealTime);
        wind->GetWind(xfm.v, windTime, windForce);
        cached.v.x += windForce.x * 0.05f;
        cached.v.y += windForce.y * 0.05f;
        cached.v.z += windForce.z * 0.05f;
    }
}

bool DxMesh::CheckFurTransformCache() {
    int numBones = mBones.size();
    if (numBones == 0) {
        numBones = 1;
    }
    if ((unsigned int)numBones != mTransformCache.size()) {
        mTransformCache.resize(numBones);
        for (int i = 0; i < numBones; i++) {
            mTransformCache[i].Reset();
        }
        return true;
    }
    return false;
}

float DxMesh::FurWeight(RndMat *mat) {
    while (mat) {
        if (mat->GetFur()) {
            if (CheckFurTransformCache()) {
                return 1.0f;
            }
            return 1.0f / (mat->GetFur()->GetFluidity() * 6.5f + 1.0f);
        }
        mat = dynamic_cast<RndMat *>(mat->NextPass());
    }
    return -1.0f;
}

// Target: Mesh.obj .data:0xA0 (0x82F136C4), the single 0xBF800000 word.
static float sFurLodBias = -1.0f;

DxMat *DxMesh::DrawFur(DxMat *mat) {
    if (TheRnd.DrawMode() != Rnd::kDrawNormal) {
        return static_cast<DxMat *>(dynamic_cast<RndMat *>(mat->NextPass()));
    }
    DxMesh *owner = static_cast<DxMesh *>(GetGeomOwner());
    MILO_ASSERT(owner && owner->CanDraw(), 0x21B);
    MILO_ASSERT(mat, 0x21D);
    // Each bone costs two 4x3 transform slots (the regular one plus the fur
    // one), and only 43 constant registers are available from
    // kVS_WorldTransform on.
    if (NumBones() * 2 >= 43) {
        MILO_NOTIFY_ONCE(
            "%s: Too many bones for fur (%d > %d)", PathName(this), NumBones(), 21
        );
        return static_cast<DxMat *>(dynamic_cast<RndMat *>(mat->NextPass()));
    }
    RndFur *fur = mat->GetFur();
    MILO_ASSERT(fur, 0x227);
    int numBones = NumBones();
    if (numBones == 0)
        numBones = 1;
    MILO_ASSERT(mTransformCache.size() == numBones, 0x22A);
    for (int i = 0; i < numBones; i++) {
        TheShaderMgr.SetVConstant4x3(
            (VShaderConstant)(kVS_WorldTransform + (numBones + i) * 3),
            Hmx::Matrix4(mTransformCache[i])
        );
    }
    fur->Prep(owner, mat);
    DWORD savedLod12 = D3DDevice_GetSamplerState_MipMapLodBias(TheDxRnd.Device(), 0xC);
    DWORD savedLod0 = D3DDevice_GetSamplerState_MipMapLodBias(TheDxRnd.Device(), 0);
    D3DDevice_SetSamplerState_MipMapLodBias(
        TheDxRnd.Device(), 0xC, *(DWORD *)&sFurLodBias
    );
    D3DDevice_SetSamplerState_MipMapLodBias(
        TheDxRnd.Device(), 0, *(DWORD *)&sFurLodBias
    );
    int numPasses = fur->Layers();
    MILO_ASSERT(numPasses > 0, 0x243);
    DxMat *next = static_cast<DxMat *>(dynamic_cast<RndMat *>(mat->NextPass()));
    for (int i = 0; i < numPasses; i++) {
        fur->Shell(i, owner, mat);
        owner->DrawFacesInRange(0, -1);
    }
    D3DDevice_SetSamplerState_MipMapLodBias(TheDxRnd.Device(), 0xC, savedLod12);
    D3DDevice_SetSamplerState_MipMapLodBias(TheDxRnd.Device(), 0, savedLod0);
    // Sampler 6 is restored to sampler 0's saved bias, not its own -- that is
    // what the shipped code does.
    D3DDevice_SetSamplerState_MipMapLodBias(TheDxRnd.Device(), 6, savedLod0);
    return next;
}

void DxMesh::OnSync(int flags) {
    PhysMemTypeTracker tracker("D3D(phys):Mesh");
    RndMesh *geom = GetGeomOwner();
    if (this != geom) {
        if (Mutable() & 0x1f) {
            geom->Sync(flags);
        }
        return;
    }
    RndMesh::OnSync(flags);
    if (mMutable) {
        return;
    }
    geom = GetGeomOwner();
    if (flags & 0x1f) {
        int numVerts = Verts().size();
        mNumVerts = numVerts;
        unsigned int vertSize;
        bool fromCompressed = false;
        if (numVerts != 0) {
            vertSize = VertSize();
        } else if (mNumCompressedVerts != 0) {
            mNumVerts = numVerts = mNumCompressedVerts;
            vertSize = VertSize();
            fromCompressed = true;
        } else {
            unk1a4.Release();
            vertSize = 0;
        }
        if (unk1a4.buffer == NULL || unk1a4.size != vertSize * numVerts) {
            unk1a4.Release();
            if (numVerts != 0) {
                D3DVertexBuffer *vb =
                    MakeVertexBuffer(numVerts, vertSize, VertFVF(), false);
                unk1a4.SetData(vb, vertSize * numVerts);
            }
        }
        if (unk1a4.buffer != NULL) {
            if (fromCompressed) {
                FillCompressedVerts();
            } else {
                Fill(Verts().begin(), Verts().end());
            }
        }
    }
    if (flags & 0x20) {
        TheDxRnd.AutoRelease(unk1ac);
        unk1ac = NULL;
        mNumFaces = Faces().size();
        if (mNumFaces != 0) {
            MILO_ASSERT(mNumFaces <= 0xFFFF, 0x17e);
            unk1ac = (D3DResource *)MakeIndexBuffer(mNumFaces, 6, D3DFMT_INDEX16);
            IBLock<> lock((D3DIndexBuffer *)unk1ac, 0);
            unsigned short *dst = (unsigned short *)lock.mDataAddr;
            for (int i = 0; i < mNumFaces; i++) {
                RndMesh::Face &face = Faces()[i];
                dst[0] = face.v1;
                dst[1] = face.v2;
                dst[2] = face.v3;
                dst += 3;
            }
        }
    }
    if ((flags & 0x200) == 0) {
        if ((mMutable & 0x1f) == 0) {
            Verts().resize(0);
            ClearCompressedVerts();
        }
        if ((mMutable & 0x20) == 0) {
            std::vector<RndMesh::Face>().swap(Faces());
        }
    }
}

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
    if (TheRnd.DrawMode() == Rnd::kDrawVelocity) {
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
        if (TheRnd.DrawMode() == 9) {
            st = kAllWhiteShader;
        }
        RndShader::SelectConfig(mat, st, false);
        geom->DrawFacesInRange(0, -1);
        mat = next;
    } while (mat);
}

void DxMesh::DrawFacesInRange(int startFace, int numFaces) {
    D3DDevice *device = TheDxRnd.Device();
    if (mMutable) {
        // Mutable meshes have no persistent buffers: the geometry is streamed
        // straight into the command buffer every draw.
        if (Faces().empty())
            return;
        TheNgStats->mMutMeshes++;
        D3DDevice_SetVertexDeclaration(
            device, IsSkinned() ? sMutableSkinnedVertexDecl : sMutableVertexDecl
        );
        void *indexData = nullptr;
        void *vertexData = nullptr;
        HRESULT hr = D3DDevice_BeginIndexedVertices(
            device,
            D3DPT_TRIANGLELIST,
            0,
            Verts().size(),
            Faces().size() * 3,
            D3DFMT_INDEX16,
            0x60,
            &indexData,
            &vertexData
        );
        if (hr) {
            MILO_FAIL(
                "File: %s Line: %d Error: %s\n", __FILE__, 0x35C, DxRnd::Error(hr)
            );
        }
        void *vertexDest = vertexData;
        RndMesh::Face *faceData = Faces().begin();
        RndMesh::Vert *vertData = Verts().begin();
        XMemCpyStreaming_WriteCombined(indexData, faceData, Faces().size() * 6);
        XMemCpyStreaming_WriteCombined(vertexDest, vertData, Verts().size() * 0x60);
        D3DDevice_EndIndexedVertices(device);
        TheNgStats->mFaces += Faces().size();
    } else {
        if (numFaces == -1)
            numFaces = mNumFaces;
        D3DDevice_SetIndices(device, (D3DIndexBuffer *)unk1ac);
        // The buffer has to be read before VertSize() is called, not as part of
        // the (right-to-left) argument evaluation that follows it.
        D3DVertexBuffer *vertexBuffer = unk1a4.buffer;
        unsigned int vertSize = VertSize();
        D3DDevice_SetStreamSource(device, 0, vertexBuffer, 0, vertSize, 1);
        D3DDevice_SetVertexDeclaration(device, sVertexDecl);
        TheNgStats->mRegMeshes++;
        TheNgStats->mFaces += numFaces;
        if (mNumFaces == 0) {
            MILO_NOTIFY_ONCE(
                "%s (%s): Trying to draw mesh with no faces", Name(), PathName(this)
            );
        } else {
            D3DDevice_DrawIndexedVertices(
                device, D3DPT_TRIANGLELIST, 0, startFace * 3, numFaces * 3
            );
        }
        D3DDevice_SetIndices(device, nullptr);
    }
}

void _fake(void) {
    BufLock<struct D3DVertexBuffer> buf(nullptr, 0);
    BufLock<struct D3DIndexBuffer> buf2(nullptr, 0);
}
