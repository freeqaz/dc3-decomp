#include "gfx/VertexFormats.h"
#include "rndobj/Mesh.h"
#include "rndobj/MeshVertCompress.h"

#include <algorithm>
#include <cmath>
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

// ============================================================================
// Unpack Xbox 360 compressed vertices to GpuVertex
// CompressedVertex_Xbox: 36 bytes each, big-endian on disc
// ============================================================================

static float UnpackFloat_BE(int bits) {
    // Byte-swap int from big-endian to little-endian, then reinterpret as float
    unsigned int val = __builtin_bswap32((unsigned int)bits);
    float f;
    memcpy(&f, &val, 4);
    return f;
}

static void UnpackColor_BE(int packed, float out[4]) {
    // Byte-swap first
    unsigned int val = __builtin_bswap32((unsigned int)packed);
    // ABGR packed: R=low byte, A=high byte
    out[0] = (float)((val >> 0) & 0xFF) / 255.0f;  // R
    out[1] = (float)((val >> 8) & 0xFF) / 255.0f;  // G
    out[2] = (float)((val >> 16) & 0xFF) / 255.0f; // B
    out[3] = (float)((val >> 24) & 0xFF) / 255.0f; // A
}

static void UnpackNormal_BE(int packed, float out[3]) {
    // 10-10-10-2 packed normal, big-endian
    unsigned int val = __builtin_bswap32((unsigned int)packed);
    // DEC3N: x=bits[0:9], y=bits[10:19], z=bits[20:29], w=bits[30:31]
    int ix = (int)(val << 22) >> 22;  // sign-extend 10 bits
    int iy = (int)(val << 12) >> 22;
    int iz = (int)(val << 2) >> 22;
    out[0] = ix / 511.0f;
    out[1] = iy / 511.0f;
    out[2] = iz / 511.0f;
}

int UnpackCompressedVertices(const unsigned char* compressedData, int numVerts,
                             GpuVertex* out, int maxVerts) {
    int count = std::min(numVerts, maxVerts);
    const CompressedVertex_Xbox* cverts = (const CompressedVertex_Xbox*)compressedData;

    for (int i = 0; i < count; i++) {
        const CompressedVertex_Xbox& cv = cverts[i];
        GpuVertex& gv = out[i];

        // Position: float stored as int bits (big-endian)
        gv.pos[0] = UnpackFloat_BE(cv.mPosX);
        gv.pos[1] = UnpackFloat_BE(cv.mPosY);
        gv.pos[2] = UnpackFloat_BE(cv.mPosZ);

        // Normal: 10-10-10-2 packed
        UnpackNormal_BE(cv.mNormal, gv.norm);

        // Color: packed RGBA
        UnpackColor_BE(cv.mColor, gv.color);

        // UV: packed into mTangent as 10-10-10-2 format
        // PackVector(tangent, (tex.x, tex.y, 0, 0), 10,10,10,2, normalize=true)
        // normalize=true → multiply by 511.0, mask to 10 bits
        // To unpack: extract 10-bit unsigned values, divide by 511.0
        {
            unsigned int tval = __builtin_bswap32((unsigned int)cv.mTangent);
            unsigned int ux = tval & 0x3FF;         // bits 0-9: tex.x
            unsigned int uy = (tval >> 10) & 0x3FF; // bits 10-19: tex.y
            gv.uv[0] = (float)ux / 511.0f;
            gv.uv[1] = (float)uy / 511.0f;
        }
    }
    return count;
}

} // namespace VertexFormats
