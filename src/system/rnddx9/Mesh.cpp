#include "Mesh.h"
#include "Rnd.h"
#include "rnddx9/Utl.h"
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

D3DVertexBuffer* DxMesh::GetMultimeshFaces() {
    MILO_ASSERT(!Mutable(), 0x1a7);

    if (unk1b0 == nullptr) {
        s32 nVerts = mNumVerts;
        u32 nFaces3 = nVerts * 3;
        u32 bufSize = nFaces3 * 4;

        unk1b0 = D3DDevice_CreateVertexBuffer(bufSize, 0, D3DPOOL_DEFAULT);
        void* vbuf = D3DVertexBuffer_Lock((D3DVertexBuffer*)unk1b0, 0, 0, 0);
        void* ibuf = D3DIndexBuffer_Lock((D3DIndexBuffer*)unk1ac, 0, 0, 0x10);

        if (nFaces3 != 0) {
            u16* src = (u16*)ibuf - 1;
            u32* dst = (u32*)vbuf - 1;
            u32 i = nFaces3;
            while (i--) {
                *++dst = *++src;
            }
        }

        D3DIndexBuffer_Unlock((D3DIndexBuffer*)unk1ac);
        D3DVertexBuffer_Unlock((D3DVertexBuffer*)unk1b0);
    }

    return (D3DVertexBuffer*)unk1b0;
}

void _fake(void) {
    BufLock<struct D3DVertexBuffer> buf(nullptr, 0);
    BufLock<struct D3DIndexBuffer> buf2(nullptr, 0);
}
