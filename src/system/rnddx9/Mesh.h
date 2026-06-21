#pragma once
#include "math/Mtx.h"
#include "obj/Object.h"
#include "rnddx9/Object.h"
#include "rndobj/Mesh.h"
#include "utl/PoolAlloc.h"
#include "xdk/D3D9.h"

class DxMat;

class DxMesh : public RndMesh, public DxObject {
    // DxMultiMesh (RndMultiMesh-derived, not a DxMesh subclass) calls the
    // protected VertFVF() on DxMesh* instances — the target binary compiles
    // MultiMesh.cpp against the protected ?VertFVF@DxMesh@@IBAIXZ symbol, so
    // the original source declared this friend.
    friend class DxMultiMesh;
public:
    struct VertexBufferData {
        VertexBufferData() : buffer(0), size(0) {}
        ~VertexBufferData() { Release(); }
        void Release();
        void SetData(D3DVertexBuffer *buf, unsigned int sz);

        D3DVertexBuffer *buffer;
        unsigned int size;
    };
    // Hmx::Object
    virtual ~DxMesh();
    OBJ_CLASSNAME(Mesh)
    OBJ_SET_TYPE(Mesh)
    virtual void Copy(const Hmx::Object *, Hmx::Object::CopyType);
    // RndMesh
    virtual void DrawShowing();
    virtual void DrawFacesInRange(int, int);
    virtual int NumFaces() const { return mNumFaces; }
    virtual int NumVerts() const { return mNumVerts; }

protected:
    virtual void OnSync(int);
    unsigned int VertSize() const;
    void FillCompressedVerts();
    bool CanDraw() const;
    void SetTransforms();
    DxMat *DrawFur(DxMat *);
    float FurWeight(RndMat *);
    void CacheFurTransform(const Transform &, int, float);
    bool CheckFurTransformCache();
    void Fill(RndMesh::Vert *, RndMesh::Vert *);
    unsigned int VertFVF() const;

public:
    D3DVertexBuffer *GetMultimeshFaces();

    NEW_OBJ(DxMesh)

    POOL_OVERLOAD(DxMesh, 0x56);

protected:
    DxMesh();

    static D3DVertexDeclaration *sVertexDecl;
    static D3DVertexDeclaration *sMutableVertexDecl;
    static D3DVertexDeclaration *sMutableSkinnedVertexDecl;

    std::vector<Transform> mTransformCache; // 0x190
    int mNumVerts; // 0x19c
    int mNumFaces; // 0x1a0
    VertexBufferData unk1a4;
    D3DResource *unk1ac;
    D3DResource *unk1b0;
};
