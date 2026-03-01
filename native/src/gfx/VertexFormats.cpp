#include "gfx/VertexFormats.h"
#include "rndobj/Mesh.h"

#include <algorithm>
#include <cstring>

namespace VertexFormats {

// ============================================================================
// Helper to build VertexAttribute (Dawn structs have nextInChain as first member)
// ============================================================================

static wgpu::VertexAttribute MakeAttr(wgpu::VertexFormat fmt, uint64_t offset, uint32_t loc) {
    wgpu::VertexAttribute a{};
    a.format = fmt;
    a.offset = offset;
    a.shaderLocation = loc;
    return a;
}

// ============================================================================
// Static vertex layout: pos(3f) + norm(3f) + color(4f) + uv(2f) = 48 bytes
// ============================================================================

static wgpu::VertexAttribute sStaticAttrs[4];
static wgpu::VertexBufferLayout sStaticLayout;
static bool sStaticInited = false;

static void InitStaticLayout() {
    if (sStaticInited) return;
    sStaticAttrs[0] = MakeAttr(wgpu::VertexFormat::Float32x3, 0,  0); // position
    sStaticAttrs[1] = MakeAttr(wgpu::VertexFormat::Float32x3, 12, 1); // normal
    sStaticAttrs[2] = MakeAttr(wgpu::VertexFormat::Float32x4, 24, 2); // color
    sStaticAttrs[3] = MakeAttr(wgpu::VertexFormat::Float32x2, 40, 3); // uv

    sStaticLayout.arrayStride = sizeof(GpuVertex);
    sStaticLayout.stepMode = wgpu::VertexStepMode::Vertex;
    sStaticLayout.attributeCount = 4;
    sStaticLayout.attributes = sStaticAttrs;
    sStaticInited = true;
}

const wgpu::VertexBufferLayout& StaticLayout() {
    InitStaticLayout();
    return sStaticLayout;
}

// ============================================================================
// Skinned vertex layout: static + boneWeights(4f) + boneIndices(4u8) = 72 bytes
// ============================================================================

static wgpu::VertexAttribute sSkinnedAttrs[6];
static wgpu::VertexBufferLayout sSkinnedLayout;
static bool sSkinnedInited = false;

static void InitSkinnedLayout() {
    if (sSkinnedInited) return;
    sSkinnedAttrs[0] = MakeAttr(wgpu::VertexFormat::Float32x3, 0,  0); // position
    sSkinnedAttrs[1] = MakeAttr(wgpu::VertexFormat::Float32x3, 12, 1); // normal
    sSkinnedAttrs[2] = MakeAttr(wgpu::VertexFormat::Float32x4, 24, 2); // color
    sSkinnedAttrs[3] = MakeAttr(wgpu::VertexFormat::Float32x2, 40, 3); // uv
    sSkinnedAttrs[4] = MakeAttr(wgpu::VertexFormat::Float32x4, 48, 4); // boneWeights
    sSkinnedAttrs[5] = MakeAttr(wgpu::VertexFormat::Uint8x4,   64, 5); // boneIndices

    sSkinnedLayout.arrayStride = sizeof(GpuVertexSkinned);
    sSkinnedLayout.stepMode = wgpu::VertexStepMode::Vertex;
    sSkinnedLayout.attributeCount = 6;
    sSkinnedLayout.attributes = sSkinnedAttrs;
    sSkinnedInited = true;
}

const wgpu::VertexBufferLayout& SkinnedLayout() {
    InitSkinnedLayout();
    return sSkinnedLayout;
}

// ============================================================================
// Vertex unpacking from RndMesh::Vert
// ============================================================================

int UnpackStaticVertices(const RndMesh& mesh, GpuVertex* out, int maxVerts) {
    // Access verts through mGeomOwner (same as mesh.Verts())
    RndMesh* owner = const_cast<RndMesh*>(&mesh);
    int numVerts = std::min(owner->NumVerts(), maxVerts);

    for (int i = 0; i < numVerts; i++) {
        const RndMesh::Vert& v = owner->Verts(i);
        GpuVertex& gv = out[i];

        gv.pos[0] = v.pos.x;
        gv.pos[1] = v.pos.y;
        gv.pos[2] = v.pos.z;

        gv.norm[0] = v.norm.x;
        gv.norm[1] = v.norm.y;
        gv.norm[2] = v.norm.z;

        gv.color[0] = v.color.red;
        gv.color[1] = v.color.green;
        gv.color[2] = v.color.blue;
        gv.color[3] = v.color.alpha;

        gv.uv[0] = v.tex.x;
        gv.uv[1] = v.tex.y;
    }
    return numVerts;
}

int UnpackSkinnedVertices(const RndMesh& mesh, GpuVertexSkinned* out, int maxVerts) {
    RndMesh* owner = const_cast<RndMesh*>(&mesh);
    int numVerts = std::min(owner->NumVerts(), maxVerts);

    for (int i = 0; i < numVerts; i++) {
        const RndMesh::Vert& v = owner->Verts(i);
        GpuVertexSkinned& gv = out[i];

        gv.pos[0] = v.pos.x;
        gv.pos[1] = v.pos.y;
        gv.pos[2] = v.pos.z;

        gv.norm[0] = v.norm.x;
        gv.norm[1] = v.norm.y;
        gv.norm[2] = v.norm.z;

        gv.color[0] = v.color.red;
        gv.color[1] = v.color.green;
        gv.color[2] = v.color.blue;
        gv.color[3] = v.color.alpha;

        gv.uv[0] = v.tex.x;
        gv.uv[1] = v.tex.y;

        gv.boneWeights[0] = v.boneWeights.x;
        gv.boneWeights[1] = v.boneWeights.y;
        gv.boneWeights[2] = v.boneWeights.z;
        gv.boneWeights[3] = v.boneWeights.w;

        gv.boneIndices[0] = (uint8_t)v.boneIndices[0];
        gv.boneIndices[1] = (uint8_t)v.boneIndices[1];
        gv.boneIndices[2] = (uint8_t)v.boneIndices[2];
        gv.boneIndices[3] = (uint8_t)v.boneIndices[3];

        gv.pad = 0.0f;
    }
    return numVerts;
}

} // namespace VertexFormats
