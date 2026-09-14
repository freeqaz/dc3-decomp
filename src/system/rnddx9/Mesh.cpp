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
    // Known residual, one row: the image's third CreateVertexDeclaration reads
    // `addi r3, r29, 0xb8`, we emit +0xb4.  r29 is the array base (the first
    // call is a bare `mr r3, r29`), and the target data at 0x82F13518 -- a
    // 0x10C-byte unnamed block the map assigns to rnddx9:Mesh.obj -- really
    // does hold FOUR zero bytes at +0xb4, so the image's array is 268 bytes
    // where ours is 264.  0xb8 is not a multiple of sizeof(D3DVERTEXELEMENT9)
    // (12), so no single-array spelling can reach it.  Splitting into separate
    // statics does not help either, and that is measured, not assumed:
    //   three arrays -> 95.5% (MSVC emits a fresh lis/addi per array; the image
    //                   has exactly one lis for the whole block)
    //   two arrays (group3 split off) -> 99.4%, 5 rows, incl. an inserted
    //                   `lis ?sMutableSkinnedVertexElements@...`
    // So this MSVC does not anchor one static array off another, and whatever
    // produced the internal 4-byte hole is not reachable by moving the brace.
    // Leave it as one array; the one-row form below is the best known.
    // Behaviour is unaffected -- [15] is group 3 in OUR layout.
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

// FLOOR (63.3%), and the spelling above is not the problem: the fmadds operand
// order proves the image's second statement really is ScaleAdd(v,v,f,v) and not
// ScaleAddEq(v,v,f) (`lfs f0,0x30(r30)` = tf2 loaded FIRST, then
// `fmadds f0,f0,f31,f13`; contrast the ScaleAddEq(m1.y,...) calls in the Matrix3
// overload below, which load m1 first and emit `fmadds f0,f13,f1,f0`).
//
// The whole 28-byte gap is the prologue.  The image spills all three parameters
// across the `bl` (std r30/r31, stfd f31, then mr r31,r3 / mr r30,r4 / fmr f31,f1
// at 0x82620268-0x8262028C) and reads the vector part back through r30/r31.  We
// emit no callee-saved registers at all and read 0x30(r3) / 0x30(r4) AFTER the
// call, i.e. MSVC propagated the same-TU callee's real register usage --
// ScaleAddEq(Matrix3&) is a leaf that never writes r3/r4/f1 -- and kept the
// arguments live in the volatile registers.  MEASURED NEGATIVES: swapping the two
// definitions so the callee comes second is byte-inert (the propagation is not
// source-order dependent), and deleting the callee's body to force the
// conservative prologue is blocked by check_undefined_decomp_symbols.  Reaching
// the image needs the callee to be invisible to this TU, which it is not.
void ScaleAddEq(Transform &tf1, const Transform &tf2, float f) {
    ScaleAddEq(tf1.m, tf2.m, f);
    ScaleAdd(tf1.v, tf2.v, f, tf1.v);
}


// FloatToHalf and FillCompressedVertex now live in rndobj/MeshVertCompress.h as
// statics, matching the two map rows that prove retail emitted one header
// definition into both this TU and rndobj:Mesh.obj.

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
    float dx = cached.v.x - xfm.v.x;
    float dy = cached.v.y - xfm.v.y;
    float dz = cached.v.z - xfm.v.z;
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
        Vector3 &cachedPos = cached.v;
        cachedPos.x += windForce.x * 0.05f;
        cachedPos.y += windForce.y * 0.05f;
        cachedPos.z += windForce.z * 0.05f;
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
        // The shader manager is named INSIDE the loop: MSVC then hoists the
        // global load into the loop preheader (after the zero-trip guard, where
        // the image has it) and keeps it in a callee-saved register, instead of
        // re-loading it after every Matrix4 construction.  Naming it before the
        // loop instead sinks the load ABOVE the guard and scores worse (96.9).
        RndShaderMgr &shaderMgr = TheShaderMgr;
        shaderMgr.SetVConstant4x3(
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
    if (this != mGeomOwner) {
        if (Mutable() & 0x1f) {
            mGeomOwner->Sync(flags);
        }
        return;
    }
    RndMesh::OnSync(flags);
    if (mMutable) {
        return;
    }
    // RESIDUAL (w7-ai, 95.1%): apart from the face loop below (w7-z's note),
    // everything left follows from ONE missing dead home store. The image emits
    // `stw r24, 0x54(r31)` TWICE around this pair of declarations -- rows 35 and
    // 38, with `addi r25, r24, 0x100` (the verts reference) between them -- off a
    // SINGLE `lwz r24, 0x148(r30)`. We emit only the second. Two dead stores of
    // one value around one load is the "call written twice, CSE'd" signature:
    // the image spells this as `GetGeomOwner()->Verts()` with an RndMesh::Verts()
    // that returns `mVerts`, so GetGeomOwner and Verts are two inline levels and
    // each materialises the receiver. Our RndMesh::Verts() is
    // `return mGeomOwner->mVerts;`, which folds both into one level, and the
    // missing level costs a callee-saved register, which is the whole r25<->r26
    // renaming through rows 41-97.
    //
    // NOT ATTEMPTED, deliberately: the faithful spelling needs
    // RndMesh::Verts() changed to `return mVerts;` in rndobj/Mesh.h and every
    // caller switched to GetGeomOwner()->Verts(). That is a shared-header
    // SEMANTIC change (Verts() on a non-owner mesh currently returns the
    // owner's vector), owned by the rndobj/Mesh lane, with a binary-wide blast
    // radius -- not something this call site can express, since `mVerts` is
    // protected and unreachable through a RndMesh*.
    RndMesh *geom = GetGeomOwner();
    VertVector &verts = Verts();
    if (flags & 0x1f) {
        unsigned int numVerts = 0;
        unsigned int vertSize = 0;
        bool fromCompressed = false;
        int n = verts.size();
        mNumVerts = n;
        if (n != 0) {
            numVerts = n;
            vertSize = VertSize();
        } else if (mNumCompressedVerts != 0) {
            mNumVerts = numVerts = mNumCompressedVerts;
            vertSize = VertSize();
            fromCompressed = true;
        } else {
            unk1a4.Release();
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
                Fill(verts.begin(), verts.end());
            }
        }
    }
    if (flags & 0x20) {
        TheDxRnd.AutoRelease(unk1ac);
        unk1ac = NULL;
        mNumFaces = geom->mFaces.size();
        if (mNumFaces != 0) {
            MILO_ASSERT(mNumFaces <= 0xFFFF, 0x17e);
            unk1ac = (D3DResource *)MakeIndexBuffer(mNumFaces, 6, D3DFMT_INDEX16);
            IBLock<> lock((D3DIndexBuffer *)unk1ac, 0);
            unsigned short *dst = (unsigned short *)lock.mDataAddr;
            // w7-z: the image runs this loop off a single induction variable --
            // the dst pointer, biased by +4 -- and rederives &mFaces[i] from it
            // (`addi r11,r10,4` / `subfic r8,r10,-4` in the preheader, then
            // `add r10,r11,r10; add r10,r10,r8` per iteration).  Our build keeps a
            // separate byte-offset IV and uses `lhzx`/`sthu`.  Refuted spellings:
            // `*dst++ = face.vN;` x3 (moves the `lwz r10,0x68(r31)` reload of
            // lock.mDataAddr INTO the loop, 95.08 -> 94.2).
            for (int i = 0; i < mNumFaces; i++) {
                RndMesh::Face &face = geom->mFaces[i];
                dst[0] = face.v1;
                dst[1] = face.v2;
                dst[2] = face.v3;
                dst += 3;
            }
        }
    }
    if ((flags & 0x200) == 0) {
        if ((mMutable & 0x1f) == 0) {
            mVerts.resize(0);
            ClearCompressedVerts();
        }
        if ((mMutable & 0x20) == 0) {
            std::vector<RndMesh::Face>().swap(mFaces);
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
