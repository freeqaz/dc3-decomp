#include "rnddx9\Part.h"
#include "obj\Object.h"
#include "os\Debug.h"
#include "rnddx9\Rnd.h"
#include "xdk\d3d9i\d3d9.h"
#include "xdk\d3d9i\d3d9types.h"

DxParticleSys::DxParticleSys() {}

D3DVertexDeclaration *DxParticleSys::sVertexDecl;

namespace {
    const D3DVERTEXELEMENT9 sParticleDecl[] = {
        {0, 0x00, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 0x0C, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        {0, 0x10, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 0x20, D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END()
    };
}

void DxParticleSys::Init() {
    REGISTER_OBJ_FACTORY(DxParticleSys)
    MILO_ASSERT(!sVertexDecl, 0x46);
    sVertexDecl = D3DDevice_CreateVertexDeclaration(sParticleDecl);
    DX_ASSERT(sVertexDecl, 0x47);
}
