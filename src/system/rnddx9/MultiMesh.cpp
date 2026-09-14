#include "rnddx9\MultiMesh.h"
#include "obj/Object.h"
#include "rnddx9\Rnd.h"
#include "rnddx9\Mesh.h"
#include "rnddx9\Utl.h"
#include "xdk\D3D9.h"
#include "xdk\d3d9i\d3d9.h"
#include "xdk\d3d9i\d3d9types.h"
#include "utl\Symbol.h"
#include "os\Debug.h"
#include "rndobj\Rnd.h"
#include "rndobj\Shader.h"
#include "rndobj\ShaderMgr.h"
#include "rndobj\Spline.h"
#include "rndobj\Stats_NG.h"
#include "math\Mtx.h"
#include "Memory.h"

// Target: MultiMesh.obj .bss:0x0/0x4 (0x830A182C/30), both zero, in this order.
D3DVertexDeclaration *DxMultiMesh::sVertexDecl;
D3DVertexDeclaration *DxMultiMesh::sMutableVertexDecl;

DxMultiMesh::DxMultiMesh() : mGeomDirtyFlags(0), mBufferCycleIndex(0) {
    for (int i = 0; i < 3; i++) {
        mVertexBuffers[i] = mIndexBuffers[i] = nullptr;
    }
}

DxMultiMesh::~DxMultiMesh() {
    for (int i = 0; i < 3; i++) {
        DX_RELEASE(mIndexBuffers[i]);
        DX_RELEASE(mVertexBuffers[i]);
    }
}

// FILE-SCOPE, not function-local statics.  The image addresses BOTH arrays off
// one base register -- `addi r30, r11, lbl_82F136C8@l` at 0x82623F68 for the
// first, then `addi r3, r30, 0x70` at 0x82623FE4 for the second -- which means
// the compiler knew their separation at compile time.  A function-local static
// gets its OWN COMDAT .data section (measured: two `.data` sections, 0x6c each,
// align 8, in our object), and MSVC cannot compute an offset between two
// sections, so it emitted a second lis/addi pair instead.  At file scope both
// land in the TU's single plain .data and the offset becomes a constant.  The
// 0x70 is 108 bytes of array rounded up to the 8-byte section alignment, which
// is exactly the 4 zero bytes the target data carries at lbl_82F136C8+0x6c.
// (Declaring them adjacently while still function-local does NOT work: 76.6%,
// still two lis/addi.)
static D3DVERTEXELEMENT9 sVertexElement[] = {
    { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
    { 0, 16, D3DDECLTYPE_FLOAT16_2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    { 0, 20, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
    { 0, 24, D3DDECLTYPE_DEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
    { 0, 28, D3DDECLTYPE_UDEC4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
    { 0, 32, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
    { 1, 0, D3DDECLTYPE_UINT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1 },
    D3DDECL_END()
};
static D3DVERTEXELEMENT9 sMutableVertexElement[] = {
    { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
    { 0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
    { 0, 32, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 },
    { 0, 48, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
    { 0, 64, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    { 0, 72, D3DDECLTYPE_SHORT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
    { 0, 80, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT, 0 },
    { 1, 0, D3DDECLTYPE_UINT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 1 },
    D3DDECL_END()
};

void DxMultiMesh::Init() {
    REGISTER_OBJ_FACTORY(DxMultiMesh);
    // DX_ASSERT, not a hand-rolled MILO_FAIL: DxCheck()'s `v ? ERROR_SUCCESS :
    // E_OUTOFMEMORY` is what gets MSVC to materialise 0x8007000E ONCE into a
    // callee-saved register (`lis r8, 0x8007` / `ori r31, r8, 0xe` at
    // 0x82623F7C, then `and. r3, r9, r31` at both 0x82623F90 and 0x82623FFC)
    // and to save r27-r31 for a 0x90 frame.  Spelling the mask inline in this
    // function re-materialised it per site into a volatile register, which cost
    // one callee-saved register, the whole 0x10 of frame, and 21 register
    // swaps.  Same idiom as DxMesh::DxMesh().
    sVertexDecl = D3DDevice_CreateVertexDeclaration(sVertexElement);
    DX_ASSERT(sVertexDecl, 0x97);
    sMutableVertexDecl = D3DDevice_CreateVertexDeclaration(sMutableVertexElement);
    DX_ASSERT(sMutableVertexDecl, 0x9A);
}

void DxMultiMesh::Shutdown() {
    if (sVertexDecl) {
        D3DResource_Release(sVertexDecl);
        sVertexDecl = nullptr;
    }
    if (sMutableVertexDecl) {
        D3DResource_Release(sMutableVertexDecl);
        sMutableVertexDecl = nullptr;
    }
}

void DxMultiMesh::UpdateGeometryBuffers() {
    // Register variables ordered to match calling conventions
    u32 var_r9;
    s32 var_r10;
    void *temp_r3_3;
    void *temp_r11;
    void *temp_r11_2;
    void *temp_r11_3;
    s32 temp_r24;
    s32 temp_r28_2;
    s32 temp_r3;
    DxMesh *owner;
    s32 temp_r28;
    s32 temp_r10;
    void *temp_r11_4;
    s32 temp_r3_2;
    void *temp_r27;
    PhysMemTypeTracker tracker(Symbol("D3D(phys):Mesh"));
    s32 temp_r8;
    void *var_r3;
    s32 temp_r23;
    void *temp_r30;

    u16 temp_r8_2;
    void *temp_r27_ptr;

    // The mesh this uploads is mMesh's GEOMETRY OWNER, not mMesh itself.  The
    // image reads `lwz r9, 0x4c(r29)` (RndMultiMesh::mMesh, 0x40 + 0xc) and
    // then `lwz r30, 0x148(r9)` (RndMesh::mGeomOwner, 0x13c + 0xc) at
    // 0x82622D74-8C, and it is THAT pointer it asserts on, hands to
    // DxMesh::VertFVF (0x82622E44) and takes the vert/face counts from.  We
    // were using mMesh directly, which is a different object for every mesh
    // that shares its geometry with another -- and it also put the two
    // MILO_ASSERT accessors one dereference short, which is why spelling them
    // as accessors used to regress the function.
    owner = (DxMesh *)mMesh->GetGeomOwner();
    temp_r30 = (void *)owner;
    temp_r11 = (void *)((char *)temp_r30 + 0x150);
    temp_r27_ptr = *(void **)((char *)temp_r30 + 0x148);
    temp_r27 = temp_r27_ptr;

    // Shipped literals, ham_xbox_r.map: "!owner->IsSkinned()" (char[20],
    // ??_C@_0BE@CAKEIJLA@) and "owner->Mutable()" (char[17],
    // ??_C@_0BB@JDEEIHMK@).
    MILO_ASSERT(!owner->IsSkinned(), 0x21A);

    MILO_ASSERT(owner->Mutable(), 0x21B);

    temp_r24 = *(u32 *)((char *)this + 0x60) % 3;
    temp_r28 = (temp_r24 + 0x19) * 4;

    if (*(void **)((char *)this + temp_r28) == nullptr) {
        temp_r23 = *(s32 *)((char *)temp_r27 + 0x104);
        owner->VertFVF();
        temp_r3 = (s32)D3DDevice_CreateVertexBuffer(temp_r23 * 0x60, 0, (D3DPOOL)0);
        *(s32 *)((char *)this + temp_r28) = temp_r3;
        // Same -1/0 mask as DxMultiMesh::Init; the image's form is
        // `subic r10, r3, 0x1` / `subfe r10, r10, r10` / `and. r3, r10, r11`
        // at 0x82622EA0-B4.
        temp_r3_2 = ((temp_r3 == 0) ? -1 : 0) & 0x8007000E;
        if (temp_r3_2 != 0) {
            const char *errMsg = DxRnd::Error(temp_r3_2);
            MILO_FAIL("File: %s Line: %d Error: %s\n", __FILE__, 0x225, errMsg);
        }
    }

    // The vertex BufLock is a SCOPED temporary: the image runs its dtor
    // (`lwz r3, 0x64(r31)` / `bl D3DVertexBuffer_Unlock`) immediately after the
    // memcpy and before it reloads mGeomOwner for the index buffer, so the
    // lock cannot live to the end of the function.
    {
        void *bufPtr = *(void **)((char *)this + temp_r28);
        D3DVertexBuffer *vertBuf = (D3DVertexBuffer *)bufPtr;
        BufLock<D3DVertexBuffer> bufLock(vertBuf, 0);

        temp_r3 = *(s32 *)((char *)temp_r27 + 0x104);
        void *srcData = *(void **)((char *)temp_r27 + 0x100);
        void *dstData = bufLock.mDataAddr;

        memcpy(dstData, srcData, temp_r3 * 0x60);
    }

    temp_r11_2 = *(void **)((char *)temp_r30 + 0x148);
    temp_r28_2 = (temp_r24 + 0x1C) * 4;
    temp_r11 = (void *)((char *)temp_r11_2 + 0x110);

    // Computed BEFORE the null test: the image emits the whole
    // (end - begin) / 6 * 3 chain at 0x82622F10-24 and only then takes the
    // `bne` that skips the allocation.
    s32 indexCount = ((*(s32 *)((char *)temp_r11 + 4) -
                      *(s32 *)((char *)temp_r11 + 0)) / 6) * 3;
    if (*(void **)((char *)this + temp_r28_2) == nullptr) {
        void *vb2Ptr = D3DDevice_CreateVertexBuffer(indexCount * 4, 0, (D3DPOOL)0);
        *(void **)((char *)this + temp_r28_2) = vb2Ptr;
    }

    auto _tmp0 = D3DVertexBuffer_Lock((D3DVertexBuffer *)*(void **)((char *)this + temp_r28_2), 0, 0, 0);
    var_r3 = _tmp0;

    temp_r11_3 = *(void **)((char *)temp_r30 + 0x148);
    var_r9 = 0;

    if ((*(s32 *)((char *)temp_r11_3 + 0x114) -
                     *(s32 *)((char *)temp_r11_3 + 0x110)) / 6 != 0) {
        var_r10 = 0;
        // NEGATIVE RESULT (w7-aj): re-evaluating this bound in the loop tail,
        // which is literally what the image does (`lwz r11, 0x148(r30)` /
        // 0x114 - 0x110 / divw at 0x82622FC4-D8), REGRESSES the function --
        // measured twice, 88.4 -> 86.0 before the index-count hoist and
        // 91.5 -> 90.3 after it.  MSVC then keeps the reloaded pointer in a
        // different register than the body wants and the 0x110/0x114 loads
        // swap, which costs more than the six tail rows it buys.  Do not retry.
        u32 indexCount = (u32)((*(s32 *)((char *)temp_r11_3 + 0x114) -
                                  *(s32 *)((char *)temp_r11_3 + 0x110)) / 6);
        do {
            temp_r8 = *(s32 *)((char *)temp_r11_3 + 0x110);
            var_r9++;
            temp_r11_4 = (void *)(var_r10 + temp_r8);
            temp_r8_2 = *(u16 *)((char *)temp_r11_4 + 0);
            var_r10 += 6;
            // Pre-increment stores: the image walks the destination with
            // `stw r8, 0x0(r3)` / `stwu r8, 0x4(r3)` / `stwu r11, 0x4(r3)` /
            // `addi r3, r3, 0x4` (0x82622FA4-C0).  `stwu` is store-with-update,
            // i.e. `*++dst = x`.  MSVC still lowers this to plain `stw` at
            // 0x4/0x8, but the pre-increment spelling is nonetheless worth
            // +0.7 over plain `dst[0]/dst[1]/dst[2]` indexing (94.0 vs 92.9),
            // which also loses the loop's register assignment.  Do not
            // "simplify" it back.
            s32 *dst = (s32 *)var_r3;
            *dst = (s32)temp_r8_2;
            *++dst = (s32)*(u16 *)((char *)temp_r11_4 + 2);
            *++dst = (s32)*(u16 *)((char *)temp_r11_4 + 4);
            temp_r11_3 = *(void **)((char *)temp_r30 + 0x148);
            var_r3 = (void *)(dst + 1);
        } while (var_r9 != indexCount);
    }

    D3DVertexBuffer_Unlock((D3DVertexBuffer *)*(void **)((char *)this + temp_r28_2));
}

void DxMultiMesh::DrawBatchedNewGfx() {
    unsigned int numInstances = mInstances.size();
    if (numInstances == 0)
        return;
    RndMesh *mesh = mMesh;
    DxMesh *owner = static_cast<DxMesh *>(mesh->GetGeomOwner());
    bool fastBillboard = mesh->TransConstraint() == RndTransformable::kConstraintFastBillboardXYZ;
    RndMat *mat = mesh->Mat();
    MILO_ASSERT(!owner->IsSkinned(), 0x251);
    if (owner->Mutable()) {
        UpdateGeometryBuffers();
    }
    int numFaces;
    if (owner->Mutable()) {
        // The cycle index is reduced twice, once per stream, and reduced as
        // UNSIGNED (divwu): a shared local or a signed % costs 4 rows.
        D3DDevice_SetStreamSource(
            TheDxRnd.Device(),
            0,
            mVertexBuffers[(unsigned int)mBufferCycleIndex % 3],
            0,
            0x60,
            1
        );
        D3DDevice_SetStreamSource(
            TheDxRnd.Device(), 1, mIndexBuffers[(unsigned int)mBufferCycleIndex % 3], 0, 4, 1
        );
        D3DDevice_SetVertexDeclaration(TheDxRnd.Device(), sMutableVertexDecl);
        numFaces = owner->Faces().size();
    } else {
        D3DVertexBuffer *verts = owner->unk1a4.buffer;
        D3DDevice_SetStreamSource(
            TheDxRnd.Device(), 0, verts, 0, owner->VertSize(), 1
        );
        D3DDevice_SetStreamSource(
            TheDxRnd.Device(), 1, owner->GetMultimeshFaces(), 0, 4, 1
        );
        D3DDevice_SetVertexDeclaration(TheDxRnd.Device(), sVertexDecl);
        numFaces = owner->mNumFaces;
    }
    int vertsPerInstance = numFaces * 3;
    Vector4 instanceVerts;
    instanceVerts.x = vertsPerInstance;
    instanceVerts.y = vertsPerInstance;
    instanceVerts.z = vertsPerInstance;
    instanceVerts.w = vertsPerInstance;
    TheShaderMgr.SetVConstant((VShaderConstant)0x56, instanceVerts);

    ShaderType shader = fastBillboard ? kMultimeshBBShader : kMultimeshShader;
    // Last vertex-shader constant register available for instance transforms.
    // The global default spline, when one exists, owns the top 48 of them.
    int lastRegister = RndSpline::GlobalDefaultSpline() ? 0xAD : 0xDD;
    do {
        int totalDrawn = 0;
        int batches = 0;
        TheShaderMgr.SetTransform(Transform::IDXfm());
        RndShader::SelectConfig(mat, shader, false);
        InstanceList::iterator it = mInstances.begin();
        while (it != mInstances.end()) {
            int inBatch = 0;
            for (int reg = 0x5C; reg < lastRegister;) {
                if (it == mInstances.end())
                    break;
                Instance &inst = *it;
                ++it;
                if (inst.mIsVisible) {
                    // Local reference, as in DxRnd::DrawRect: it keeps the
                    // manager's pointer in a callee-saved register across the
                    // Matrix4 temporary's constructor instead of reloading the
                    // global afterwards.
                    RndShaderMgr &shaderMgr = TheShaderMgr;
                    shaderMgr.SetVConstant4x3(
                        (VShaderConstant)reg, Hmx::Matrix4(inst.mXfm)
                    );
                    reg += 3;
                    inBatch++;
                }
            }
            if (inBatch > 0) {
                D3DDevice_DrawVertices(
                    TheDxRnd.Device(), D3DPT_TRIANGLELIST, 0, inBatch * vertsPerInstance
                );
                totalDrawn += inBatch;
                batches++;
            }
        }
        if (mat) {
            mat = mat->NextPass();
        }
        TheNgStats->mMultiMeshInsts += totalDrawn;
        TheNgStats->mMultiMeshBatches += batches;
        TheNgStats->mFaces = mesh->NumFaces() * totalDrawn + TheNgStats->mFaces;
    } while (mat);
    mBufferCycleIndex++;
}

void DxMultiMesh::DrawShowing() {
    if (mInstances.empty())
        return;
    RndMesh *mesh = mMesh;
    if (!mMesh)
        return;
    // NumBones() != 0, not IsSkinned(): the target divides by sizeof(RndBone)
    // (a size() computation), where ObjVector::empty() compares begin to end.
    if (mesh->NumBones() != 0) {
        MILO_LOG("MultiMesh: mesh can't be skinned\n");
        return;
    }
    if (!static_cast<DxMesh *>(mesh->GetGeomOwner())->CanDraw())
        return;
    Rnd::Mode mode = TheRnd.DrawMode();
    if (mode == Rnd::kDrawOcclusionDepth)
        return;
    if (mode != Rnd::kDrawNormal)
        return;
    DrawBatchedNewGfx();
}
