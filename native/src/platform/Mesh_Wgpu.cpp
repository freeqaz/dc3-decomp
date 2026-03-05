// DC3 Native Port — WebGPU Mesh Drawing
// Provides RndMesh::DrawShowing() implementation for native build.
// Uses side tables for GPU vertex/index buffers.
// Supports both static and bone-skinned meshes.

#include "platform/Rnd_Wgpu.h"
#include "gfx/VertexFormats.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"
#include "rndobj/Cam.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/CubeTex.h"
#include "math/Mtx.h"
#include <unordered_set>

#include <algorithm>
#include <unordered_map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" {
#include "gfx/mikktspace.h"
}

// External: get GPU texture view for a RndTex (defined in Tex_Wgpu.cpp)
extern wgpu::TextureView GetGpuTexView(RndTex* tex);
extern wgpu::TextureView GetGpuCubeTexView(RndCubeTex* cubeTex);

// ============================================================================
// GPU mesh data side table
// ============================================================================

struct GpuMeshData {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    int numIndices = 0;
    int numVertices = 0;
    bool skinned = false;
    bool uploaded = false;
    int32_t depthBias = 0;  // set by viewer for combined meshes
};

static std::unordered_map<RndMesh*, GpuMeshData> sMeshGpuData;

// Dummy bone bind group for static meshes (pipeline layout requires group 3)
static wgpu::Buffer sDummyBoneBuffer;
static wgpu::BindGroup sDummyBoneBindGroup;

static void EnsureDummyBoneBindGroup() {
    if (sDummyBoneBindGroup) return;
    if (!gWgpuRnd) return;

    // Create a small buffer with identity matrices
    BoneUniforms identity{};
    memset(&identity, 0, sizeof(identity));
    for (int i = 0; i < kMaxBones; i++) {
        identity.bones[i][0]  = 1.0f; // m[0][0]
        identity.bones[i][5]  = 1.0f; // m[1][1]
        identity.bones[i][10] = 1.0f; // m[2][2]
        identity.bones[i][15] = 1.0f; // m[3][3]
    }

    wgpu::BufferDescriptor bd{};
    bd.size = sizeof(BoneUniforms);
    bd.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    sDummyBoneBuffer = gWgpuRnd->Gpu().Device().CreateBuffer(&bd);
    gWgpuRnd->Gpu().Queue().WriteBuffer(sDummyBoneBuffer, 0, &identity, sizeof(identity));

    wgpu::BindGroupEntry entry{};
    entry.binding = 0;
    entry.buffer = sDummyBoneBuffer;
    entry.offset = 0;
    entry.size = sizeof(BoneUniforms);

    wgpu::BindGroupDescriptor desc{};
    desc.layout = gWgpuRnd->Pipelines().BoneLayout();
    desc.entryCount = 1;
    desc.entries = &entry;
    sDummyBoneBindGroup = gWgpuRnd->Gpu().Device().CreateBindGroup(&desc);
}

// Cleanup — called from RndMesh destructor to release GPU resources
void CleanupGpuMesh(RndMesh* mesh) {
    sMeshGpuData.erase(mesh);
}

// Invalidate GPU cache when mesh data changes (called from RndMesh::Sync)
void RndMesh::OnSync(int flags) {
    auto it = sMeshGpuData.find(this);
    if (it != sMeshGpuData.end()) {
        it->second.uploaded = false;
    }
}

// Transparent draw queue — meshes with alpha blend are deferred and sorted
struct DeferredDraw {
    RndMesh* mesh;
    float distSq; // squared distance from camera to centroid
    RndCam* cam;  // camera active when queued (restored during flush)
};
static std::vector<DeferredDraw> sTransparentQueue;

static bool IsTransparentBlend(int blend) {
    return blend == BaseMaterial::kBlendSrcAlpha ||
           blend == BaseMaterial::kBlendSrcAlphaAdd ||
           blend == BaseMaterial::kBlendAdd ||
           blend == BaseMaterial::kBlendSubtract ||
           blend == BaseMaterial::kPreMultAlpha;
}

// Forward declaration — draws a mesh immediately (called for both opaque and deferred)
static void DrawMeshImmediate(RndMesh* mesh);

// Called from EndDrawing to flush transparent draws
void FlushTransparentDraws() {
    if (sTransparentQueue.empty()) return;

    // Sort back-to-front (farthest first)
    std::sort(sTransparentQueue.begin(), sTransparentQueue.end(),
        [](const DeferredDraw& a, const DeferredDraw& b) {
            return a.distSq > b.distSq;
        });

    for (auto& dd : sTransparentQueue) {
        // Restore the camera that was active when this mesh was queued
        if (dd.cam && dd.cam != RndCam::Current())
            dd.cam->Select();
        DrawMeshImmediate(dd.mesh);
    }
    sTransparentQueue.clear();
}

// Set depth bias for a mesh (used by viewer to push combined meshes behind splits)
void SetMeshDepthBias(RndMesh* mesh, int32_t bias) {
    sMeshGpuData[mesh].depthBias = bias;
}

// ============================================================================
// Helper: Convert Transform to ObjectUniforms
// ============================================================================

static void FillObjectUniforms(const Transform& worldXfm, ObjectUniforms& obj) {
    // World matrix — row-major, WGSL reads as column-major (correct transpose)
    obj.world[0]  = worldXfm.m.x.x; obj.world[1]  = worldXfm.m.x.y; obj.world[2]  = worldXfm.m.x.z; obj.world[3]  = 0;
    obj.world[4]  = worldXfm.m.y.x; obj.world[5]  = worldXfm.m.y.y; obj.world[6]  = worldXfm.m.y.z; obj.world[7]  = 0;
    obj.world[8]  = worldXfm.m.z.x; obj.world[9]  = worldXfm.m.z.y; obj.world[10] = worldXfm.m.z.z; obj.world[11] = 0;
    obj.world[12] = worldXfm.v.x;   obj.world[13] = worldXfm.v.y;   obj.world[14] = worldXfm.v.z;   obj.world[15] = 1;

    // Compute inverse-transpose of the upper 3x3 for correct normal transformation
    // under non-uniform scale. For pure rotation this equals the rotation matrix.
    const Hmx::Matrix3& m = worldXfm.m;
    float det = m.x.x * (m.y.y * m.z.z - m.y.z * m.z.y)
              - m.x.y * (m.y.x * m.z.z - m.y.z * m.z.x)
              + m.x.z * (m.y.x * m.z.y - m.y.y * m.z.x);
    if (fabsf(det) > 1e-12f) {
        float invDet = 1.0f / det;
        // Inverse of 3x3, then transposed — stored row-major
        obj.worldInvTranspose[0]  = (m.y.y * m.z.z - m.y.z * m.z.y) * invDet;
        obj.worldInvTranspose[1]  = (m.y.z * m.z.x - m.y.x * m.z.z) * invDet;
        obj.worldInvTranspose[2]  = (m.y.x * m.z.y - m.y.y * m.z.x) * invDet;
        obj.worldInvTranspose[3]  = 0;
        obj.worldInvTranspose[4]  = (m.x.z * m.z.y - m.x.y * m.z.z) * invDet;
        obj.worldInvTranspose[5]  = (m.x.x * m.z.z - m.x.z * m.z.x) * invDet;
        obj.worldInvTranspose[6]  = (m.x.y * m.z.x - m.x.x * m.z.y) * invDet;
        obj.worldInvTranspose[7]  = 0;
        obj.worldInvTranspose[8]  = (m.x.y * m.y.z - m.x.z * m.y.y) * invDet;
        obj.worldInvTranspose[9]  = (m.x.z * m.y.x - m.x.x * m.y.z) * invDet;
        obj.worldInvTranspose[10] = (m.x.x * m.y.y - m.x.y * m.y.x) * invDet;
        obj.worldInvTranspose[11] = 0;
        obj.worldInvTranspose[12] = 0;
        obj.worldInvTranspose[13] = 0;
        obj.worldInvTranspose[14] = 0;
        obj.worldInvTranspose[15] = 1;
    } else {
        // Degenerate — fall back to world matrix
        memcpy(obj.worldInvTranspose, obj.world, 64);
    }
}

// ============================================================================
// Helper: Convert Transform to a row-major 4x4 float array
// ============================================================================

static void TransformToMat4(const Transform& xfm, float* out) {
    out[0]  = xfm.m.x.x; out[1]  = xfm.m.x.y; out[2]  = xfm.m.x.z; out[3]  = 0;
    out[4]  = xfm.m.y.x; out[5]  = xfm.m.y.y; out[6]  = xfm.m.y.z; out[7]  = 0;
    out[8]  = xfm.m.z.x; out[9]  = xfm.m.z.y; out[10] = xfm.m.z.z; out[11] = 0;
    out[12] = xfm.v.x;   out[13] = xfm.v.y;   out[14] = xfm.v.z;   out[15] = 1;
}

// ============================================================================
// Fix zero-alpha vertex colors (common for texture-only meshes)
// ============================================================================

template<typename VertType>
static void FixZeroAlpha(VertType* verts, int count) {
    bool allAlphaZero = true;
    bool allRGBZero = true;
    int checkCount = count < 10 ? count : 10;
    for (int i = 0; i < checkCount; i++) {
        if (verts[i].color[3] > 0.001f) { allAlphaZero = false; }
        if (verts[i].color[0] > 0.001f || verts[i].color[1] > 0.001f || verts[i].color[2] > 0.001f) {
            allRGBZero = false;
        }
    }
    if (allAlphaZero) {
        for (int i = 0; i < count; i++) {
            verts[i].color[3] = 1.0f;
            // Only force RGB to white if it's also all zero (truly unused vertex colors).
            // Preserve meaningful RGB (e.g. baked AO) when only alpha is missing.
            if (allRGBZero) {
                verts[i].color[0] = verts[i].color[1] = verts[i].color[2] = 1.0f;
            }
        }
    }
}

// ============================================================================
// MikkTSpace tangent generation callbacks
// ============================================================================

struct MikkUserData {
    void* verts;         // GpuVertex* or GpuVertexSkinned*
    const uint16_t* indices;
    int numFaces;
    int numVerts;
    bool skinned;
};

template<typename V>
static V& GetMikkVert(MikkUserData* ud, int face, int vert) {
    int idx = ((const uint16_t*)ud->indices)[face * 3 + vert];
    return ((V*)ud->verts)[idx];
}

static int mikkGetNumFaces(const SMikkTSpaceContext* ctx) {
    return ((MikkUserData*)ctx->m_pUserData)->numFaces;
}
static int mikkGetNumVerticesOfFace(const SMikkTSpaceContext*, int) { return 3; }

static void mikkGetPosition(const SMikkTSpaceContext* ctx, float pos[], int face, int vert) {
    auto* ud = (MikkUserData*)ctx->m_pUserData;
    if (ud->skinned) {
        auto& v = GetMikkVert<GpuVertexSkinned>(ud, face, vert);
        pos[0] = v.pos[0]; pos[1] = v.pos[1]; pos[2] = v.pos[2];
    } else {
        auto& v = GetMikkVert<GpuVertex>(ud, face, vert);
        pos[0] = v.pos[0]; pos[1] = v.pos[1]; pos[2] = v.pos[2];
    }
}
static void mikkGetNormal(const SMikkTSpaceContext* ctx, float norm[], int face, int vert) {
    auto* ud = (MikkUserData*)ctx->m_pUserData;
    if (ud->skinned) {
        auto& v = GetMikkVert<GpuVertexSkinned>(ud, face, vert);
        norm[0] = v.norm[0]; norm[1] = v.norm[1]; norm[2] = v.norm[2];
    } else {
        auto& v = GetMikkVert<GpuVertex>(ud, face, vert);
        norm[0] = v.norm[0]; norm[1] = v.norm[1]; norm[2] = v.norm[2];
    }
}
static void mikkGetTexCoord(const SMikkTSpaceContext* ctx, float uv[], int face, int vert) {
    auto* ud = (MikkUserData*)ctx->m_pUserData;
    if (ud->skinned) {
        auto& v = GetMikkVert<GpuVertexSkinned>(ud, face, vert);
        uv[0] = v.uv[0]; uv[1] = v.uv[1];
    } else {
        auto& v = GetMikkVert<GpuVertex>(ud, face, vert);
        uv[0] = v.uv[0]; uv[1] = v.uv[1];
    }
}
static void mikkSetTSpaceBasic(const SMikkTSpaceContext* ctx,
    const float tangent[], float sign, int face, int vert) {
    auto* ud = (MikkUserData*)ctx->m_pUserData;
    if (ud->skinned) {
        auto& v = GetMikkVert<GpuVertexSkinned>(ud, face, vert);
        v.tangent[0] = tangent[0]; v.tangent[1] = tangent[1];
        v.tangent[2] = tangent[2]; v.tangent[3] = sign;
    } else {
        auto& v = GetMikkVert<GpuVertex>(ud, face, vert);
        v.tangent[0] = tangent[0]; v.tangent[1] = tangent[1];
        v.tangent[2] = tangent[2]; v.tangent[3] = sign;
    }
}

static void ComputeMikkTangents(void* verts, const uint16_t* indices,
    int numFaces, int numVerts, bool skinned) {
    MikkUserData ud;
    ud.verts = verts;
    ud.indices = indices;
    ud.numFaces = numFaces;
    ud.numVerts = numVerts;
    ud.skinned = skinned;

    SMikkTSpaceInterface iface{};
    iface.m_getNumFaces = mikkGetNumFaces;
    iface.m_getNumVerticesOfFace = mikkGetNumVerticesOfFace;
    iface.m_getPosition = mikkGetPosition;
    iface.m_getNormal = mikkGetNormal;
    iface.m_getTexCoord = mikkGetTexCoord;
    iface.m_setTSpaceBasic = mikkSetTSpaceBasic;

    SMikkTSpaceContext ctx{};
    ctx.m_pInterface = &iface;
    ctx.m_pUserData = &ud;

    genTangSpaceDefault(&ctx);
}

// ============================================================================
// Helper: Upload mesh vertex/index data to GPU
// ============================================================================

static bool EnsureMeshUploaded(RndMesh* mesh) {
    if (!gWgpuRnd) return false;

    auto it = sMeshGpuData.find(mesh);
    if (it != sMeshGpuData.end() && it->second.uploaded) {
        return true;
    }

    RndMesh* geomOwner = mesh->GetGeomOwner();
    if (!geomOwner) geomOwner = mesh;

    int numVerts = geomOwner->NumVerts();
    int numFaces = geomOwner->NumFaces();
    int numCompressedVerts = geomOwner->NumCompressedVerts();
    bool skinned = mesh->IsSkinned();

    // Check if we have vertices (either uncompressed or compressed)
    if (numVerts <= 0 && numCompressedVerts <= 0) {
        fprintf(stderr, "Mesh_Wgpu: skipping '%s' — no vertices\n", mesh->Name());
        return false;
    }
    if (numFaces <= 0) {
        fprintf(stderr, "Mesh_Wgpu: skipping '%s' — no faces\n", mesh->Name());
        return false;
    }

    int vertCount = (numVerts > 0) ? numVerts : numCompressedVerts;
    bool isCompressed = (numCompressedVerts > 0 && geomOwner->CompressedVerts());

    // Build index buffer first (needed for MikkTSpace tangent generation)
    int numIndices = numFaces * 3;
    int allocIndices = (numIndices + 1) & ~1; // round up to even for 4-byte alignment
    uint16_t* indices = new uint16_t[allocIndices]();
    auto& faces = geomOwner->Faces();
    for (int i = 0; i < numFaces; i++) {
        indices[i * 3 + 0] = faces[i].v1;
        indices[i * 3 + 1] = faces[i].v2;
        indices[i * 3 + 2] = faces[i].v3;
    }

    wgpu::Buffer vertexBuf;
    int unpacked = 0;

    if (skinned) {
        // Skinned vertex path
        GpuVertexSkinned* verts = new GpuVertexSkinned[vertCount];
        if (isCompressed) {
            unpacked = VertexFormats::UnpackCompressedSkinnedVertices(
                geomOwner->CompressedVerts(), numCompressedVerts, verts, vertCount);
        } else {
            unpacked = VertexFormats::UnpackSkinnedVertices(*geomOwner, verts, vertCount);
            // Compute tangents via MikkTSpace for uncompressed meshes
            // (compressed meshes already have tangent data from the original Xbox vertex stream)
            if (unpacked > 0) {
                ComputeMikkTangents(verts, indices, numFaces, unpacked, true);
            }
        }
        if (unpacked <= 0) {
            fprintf(stderr, "Mesh_Wgpu: failed to unpack skinned vertices for '%s'\n", mesh->Name());
            delete[] verts;
            delete[] indices;
            return false;
        }

        // Fix zero vertex colors for opaque meshes — many meshes don't use vertex color
        // and have all-zero RGBA, which would multiply baseColor to black in the shader.
        // Only skip for intentionally transparent blends (SrcAlpha, Add, etc.)
        if (!mesh->Mat() || mesh->Mat()->GetBlend() <= BaseMaterial::kBlendSrc)
            FixZeroAlpha(verts, unpacked);

        wgpu::BufferDescriptor vbDesc{};
        vbDesc.size = unpacked * sizeof(GpuVertexSkinned);
        vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        vertexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&vbDesc);
        gWgpuRnd->Gpu().Queue().WriteBuffer(vertexBuf, 0, verts, unpacked * sizeof(GpuVertexSkinned));
        delete[] verts;
    } else {
        // Static vertex path
        GpuVertex* verts = new GpuVertex[vertCount];
        if (isCompressed) {
            unpacked = VertexFormats::UnpackCompressedVertices(
                geomOwner->CompressedVerts(), numCompressedVerts, verts, vertCount);
        } else {
            unpacked = VertexFormats::UnpackStaticVertices(*geomOwner, verts, vertCount);
            // Compute tangents via MikkTSpace for uncompressed meshes
            if (unpacked > 0) {
                ComputeMikkTangents(verts, indices, numFaces, unpacked, false);
            }
        }
        if (unpacked <= 0) {
            fprintf(stderr, "Mesh_Wgpu: failed to unpack vertices for '%s' (verts=%d, compressed=%d)\n",
                    mesh->Name(), numVerts, numCompressedVerts);
            delete[] verts;
            delete[] indices;
            return false;
        }
        // Fix zero vertex colors for opaque meshes — many meshes don't use vertex color
        // and have all-zero RGBA, which would multiply baseColor to black in the shader.
        // Only skip for intentionally transparent blends (SrcAlpha, Add, etc.)
        if (!mesh->Mat() || mesh->Mat()->GetBlend() <= BaseMaterial::kBlendSrc)
            FixZeroAlpha(verts, unpacked);

        wgpu::BufferDescriptor vbDesc{};
        vbDesc.size = unpacked * sizeof(GpuVertex);
        vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        vertexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&vbDesc);
        gWgpuRnd->Gpu().Queue().WriteBuffer(vertexBuf, 0, verts, unpacked * sizeof(GpuVertex));
        delete[] verts;
    }

    size_t ibAlignedSize = (numIndices * sizeof(uint16_t) + 3) & ~3u;

    wgpu::BufferDescriptor ibDesc{};
    ibDesc.size = ibAlignedSize;
    ibDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer indexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&ibDesc);
    gWgpuRnd->Gpu().Queue().WriteBuffer(indexBuf, 0, indices, ibAlignedSize);
    delete[] indices;

    GpuMeshData& data = sMeshGpuData[mesh];
    data.vertexBuffer = vertexBuf;
    data.indexBuffer = indexBuf;
    data.numIndices = numIndices;
    data.numVertices = unpacked;
    data.skinned = skinned;
    data.uploaded = true;
    return true;
}

// ============================================================================
// RndMesh::DrawShowing — the hot path
// ============================================================================

static int sDrawCallsThisFrame = 0;
static int sFrameCounter = 0;

void RndMesh_ResetFrameStats() {
    if (sFrameCounter > 0 && sFrameCounter % 300 == 0) {
        printf("DC3 Render: Frame %d — %d mesh draw calls\n", sFrameCounter, sDrawCallsThisFrame);
    }
    sDrawCallsThisFrame = 0;
    sFrameCounter++;
}

void RndMesh::DrawShowing() {
    if (!gWgpuRnd || !gWgpuRnd->IsInPass()) return;
    if (!Showing()) return;

    // Skip LOD meshes (drawn by Character::DrawLod in the full engine,
    // but we iterate all meshes directly in the viewer)
    if (strstr(Name(), "_lod")) return;

    sDrawCallsThisFrame++;

    // Get material
    RndMat* mat = Mat();
    if (!mat) return;

    // Defer transparent meshes for back-to-front sorting
    if (IsTransparentBlend(mat->GetBlend())) {
        float distSq = 0.0f;
        RndCam* cam = RndCam::Current();
        if (cam) {
            const Vector3& camPos = cam->WorldXfm().v;
            const Vector3& meshPos = WorldXfm().v;
            float dx = meshPos.x - camPos.x;
            float dy = meshPos.y - camPos.y;
            float dz = meshPos.z - camPos.z;
            distSq = dx*dx + dy*dy + dz*dz;
        }
        sTransparentQueue.push_back({this, distSq, RndCam::Current()});
        return;
    }

    DrawMeshImmediate(this);
}

static void DrawMeshImmediate(RndMesh* mesh) {
    if (!gWgpuRnd || !gWgpuRnd->IsInPass()) return;

    // Re-upload scene uniforms if camera changed (e.g., UI camera vs world camera)
    gWgpuRnd->EnsureSceneUniformsCurrent();

    RndMat* mat = mesh->Mat();
    if (!mat) return;

    // Skip multiply-blend meshes with near-black color — these produce Dst*0=0
    // wiping the framebuffer to black. They're meant to be animated to white/transparent
    // but if the animation hasn't set them up yet, skip rather than destroy the scene.
    if (mat->GetBlend() == BaseMaterial::kBlendMultiply) {
        const Hmx::Color& c = mat->GetColor();
        if (c.red < 0.01f && c.green < 0.01f && c.blue < 0.01f) {
            return;
        }
    }

    // Ensure mesh data is on GPU
    if (!EnsureMeshUploaded(mesh)) return;

    auto& meshData = sMeshGpuData[mesh];
    auto& pass = gWgpuRnd->CurrentPass();
    bool skinned = meshData.skinned;

    // --- Pipeline selection ---
    PipelineKey key{};
    key.shaderType = 18; // kStandardShader
    key.blend = (WgpuBlend)mat->GetBlend();
    key.zMode = !mesh->Name()[0] ? (WgpuZMode)0 : (WgpuZMode)mat->GetZMode(); // No depth for text
    key.cull = !mesh->Name()[0] ? (WgpuCull)0 : (WgpuCull)mat->GetCull(); // No cull for text
    key.stencil = (WgpuStencil)mat->GetStencil();
    key.layout = skinned ? VertexLayoutType::Skinned : VertexLayoutType::Static;
    key.targetFormat = gWgpuRnd->Gpu().SurfaceFormat();
    key.alphaCut = mat->GetAlphaCut();
    key.alphaWrite = mat->GetAlphaWrite();
    key.alphaToCoverage = mat->GetAlphaCut();
    key.depthBias = meshData.depthBias;

    wgpu::RenderPipeline pipeline = gWgpuRnd->Pipelines().GetPipeline(key);
    if (!pipeline) return;

    pass.SetPipeline(pipeline);

    // --- Material uniforms (group 1) ---
    MaterialUniforms matUni{};
    const Hmx::Color& matColor = mat->GetColor();
    matUni.color[0] = matColor.red;
    matUni.color[1] = matColor.green;
    matUni.color[2] = matColor.blue;
    matUni.color[3] = matColor.alpha;
    if (mat->GetAlphaCut()) {
        matUni.alphaThreshold = mat->GetAlphaThreshold() / 255.0f;
    } else {
        matUni.alphaThreshold = 0.0f;
    }

    // Specular
    const Hmx::Color& spec = mat->GetSpecularRGB();
    float specPower = spec.alpha > 0.0f ? spec.alpha : 0.0f;
    float specScale = 1.0f;
    // Per-pixel-lit materials without normal map: the Xbox shader uses normal map
    // alpha as specular mask. Without it, attenuate specular and raise min power
    // to avoid unrealistically broad sheen across entire surfaces.
    if (specPower > 0.0f && specPower < 32.0f) {
        specPower = 32.0f;  // tighten the specular lobe
        specScale = 0.4f;   // reduce intensity
    }
    matUni.specularColor[0] = spec.red * specScale;
    matUni.specularColor[1] = spec.green * specScale;
    matUni.specularColor[2] = spec.blue * specScale;
    matUni.specularColor[3] = 1.0f;
    matUni.specularPower = specPower;

    // Emissive — only applies when an emissive map texture exists
    // Without a map, emissiveMultiplier defaults to 1.0 which would add
    // the full diffuse color as self-illumination (completely wrong)
    matUni.emissiveMultiplier = mat->GetEmissiveMap() ? mat->GetEmissiveMultiplier() : 0.0f;

    // Rim lighting
    const Hmx::Color& rim = mat->GetRimRGB();
    matUni.rimColor[0] = rim.red;
    matUni.rimColor[1] = rim.green;
    matUni.rimColor[2] = rim.blue;
    matUni.rimColor[3] = rim.alpha > 0.0f ? rim.alpha : 0.0f;
    matUni.rimLightUnder = mat->GetRimLightUnder() ? 1.0f : 0.0f;

    // Intensify
    matUni.intensify = mat->GetIntensify() ? 2.0f : 1.0f;

    // Shader variation (skin, hair, etc.)
    // DC3 skin materials often have shader_variation=0 but use "_skin" in the name.
    // Detect skin by either the explicit flag or name convention.
    ShaderVariation variation = mat->GetShaderVariation();
    if (variation == kShaderVariationNone) {
        const char* matName = mat->Name();
        if (strstr(matName, "_skin") || strstr(matName, "_head")) {
            variation = kShaderVariationSkin;
        }
    }
    matUni.shaderVariation = (float)variation;

    // Second specular lobe (used by skin shader)
    const Hmx::Color& spec2 = mat->GetSpecular2RGB();
    matUni.specular2Color[0] = spec2.red;
    matUni.specular2Color[1] = spec2.green;
    matUni.specular2Color[2] = spec2.blue;
    matUni.specular2Color[3] = spec2.alpha > 0.0f ? spec2.alpha : 0.0f;

    // Texture
    RndTex* diffTex = mat->GetDiffuseTex();
    wgpu::TextureView diffuseTexView;
    if (diffTex) {
        diffTex->PresyncBitmap();
        diffuseTexView = GetGpuTexView(diffTex);
    }

    if (diffuseTexView) {
        matUni.useTexture = 1.0f;
    } else {
        matUni.useTexture = 0.0f;
        diffuseTexView = gWgpuRnd->WhiteTexView();
    }

    // Normal map and additional material properties
    matUni.deNormal = mat->GetDeNormal();
    matUni.hasNormalMap = mat->NormalMap() ? 1.0f : 0.0f;
    matUni.anisotropy = mat->GetAnisotropy();

    // Per-material fog: mFog AND blend mode allows fog
    BaseMaterial::Blend blend = mat->GetBlend();
    bool allowFog = mat->GetFog() &&
        blend != BaseMaterial::kBlendDest && blend != BaseMaterial::kBlendAdd &&
        blend != BaseMaterial::kBlendSubtract && blend != BaseMaterial::kBlendSrcAlphaAdd;
    matUni.materialFogEnabled = allowFog ? 1.0f : 0.0f;

    // Prelit: vertex color is pre-lit, skip lighting
    // Text meshes (created by RndText::FontMap) have no name and need prelit
    // since the UI environment may have no lights/ambient
    bool isTextMesh = !mesh->Name()[0];
    matUni.prelit = (mat->Prelit() || isTextMesh) ? 1.0f : 0.0f;
    matUni.useAlphaAsRGB = isTextMesh ? 1.0f : 0.0f;

    // Detail normal map
    matUni.normDetailTiling = mat->GetNormDetailTiling();
    matUni.normDetailStrength = mat->GetNormDetailStrength();
    matUni.hasNormDetailMap = mat->GetNormDetailMap() ? 1.0f : 0.0f;

    // TexGen mode and transform
    matUni.texGenMode = (float)mat->GetTexGen();
    if (mat->GetTexGen() == kTexGenXfm || mat->GetTexGen() == kTexGenXfmOrigin ||
        mat->GetTexGen() == kTexGenProjected) {
        const Transform& xfm = mat->TexXfm();
        matUni.texXfmRow0[0] = xfm.m.x.x; matUni.texXfmRow0[1] = xfm.m.x.y;
        matUni.texXfmRow0[2] = xfm.v.x;   matUni.texXfmRow0[3] = xfm.v.z;
        matUni.texXfmRow1[0] = xfm.m.y.x; matUni.texXfmRow1[1] = xfm.m.y.y;
        matUni.texXfmRow1[2] = xfm.v.y;   matUni.texXfmRow1[3] = 0.0f;
    }

    // Resolve all material texture views
    WgpuRnd::MaterialTexViews texViews;
    texViews.diffuse = diffuseTexView;

    auto resolveMap = [](RndTex* tex, wgpu::TextureView& fallback) -> wgpu::TextureView {
        if (!tex) return fallback;
        tex->PresyncBitmap();
        wgpu::TextureView v = GetGpuTexView(tex);
        return v ? v : fallback;
    };
    texViews.normal   = resolveMap(mat->NormalMap(),      gWgpuRnd->FlatNormalTexView());
    texViews.specular = resolveMap(mat->GetSpecularMap(), gWgpuRnd->WhiteTexView());
    // Eye materials: boost emissive so sclera/iris stay bright (compensates for
    // missing environment map reflections that the real game uses for eye shine)
    bool isEyeMat = strstr(mat->Name(), "eyes") || strstr(mat->Name(), "eye_");
    if (isEyeMat) {
        matUni.emissiveMultiplier = std::max(matUni.emissiveMultiplier, 1.0f);
    }
    texViews.emissive = resolveMap(mat->GetEmissiveMap(),
        isEyeMat ? gWgpuRnd->WhiteTexView() : gWgpuRnd->BlackTexView());
    texViews.rim      = resolveMap(mat->GetRimMap(),      gWgpuRnd->WhiteTexView());

    // Detail normal map
    texViews.normDetail = resolveMap(mat->GetNormDetailMap(), gWgpuRnd->FlatNormalTexView());

    // Environment cube map
    RndCubeTex* environMap = mat->GetEnvironMap();
    if (environMap && mat->GetUseEnviron()) {
        wgpu::TextureView cubeView = GetGpuCubeTexView(environMap);
        texViews.environCube = cubeView ? cubeView : gWgpuRnd->BlackCubeTexView();
        matUni.environMapStrength = 1.0f;
        matUni.environMapFalloff = mat->GetEnvironMapFalloff() ? 1.0f : 0.0f;
        matUni.environMapSpecMask = mat->GetEnvironMapSpecMask() ? 1.0f : 0.0f;
    } else {
        texViews.environCube = gWgpuRnd->BlackCubeTexView();
        matUni.environMapStrength = 0.0f;
        // If material references an env map we can't render, boost emissive
        // so the diffuse texture stays bright (e.g. eye sclera, glossy surfaces)
        if (environMap) {
            matUni.emissiveMultiplier = std::max(matUni.emissiveMultiplier, 0.6f);
        }
    }

    uint32_t matOffset = gWgpuRnd->MaterialRing().Write(
        gWgpuRnd->Gpu().Queue(), &matUni, sizeof(matUni));

    // Sampler
    SamplerDesc sampDesc{};
    switch (mat->GetTexWrap()) {
    case kTexWrapClamp:
        sampDesc.addressU = wgpu::AddressMode::ClampToEdge;
        sampDesc.addressV = wgpu::AddressMode::ClampToEdge;
        break;
    case kTexWrapRepeat:
        sampDesc.addressU = wgpu::AddressMode::Repeat;
        sampDesc.addressV = wgpu::AddressMode::Repeat;
        break;
    case kTexWrapMirror:
        sampDesc.addressU = wgpu::AddressMode::MirrorRepeat;
        sampDesc.addressV = wgpu::AddressMode::MirrorRepeat;
        break;
    default:
        sampDesc.addressU = wgpu::AddressMode::ClampToEdge;
        sampDesc.addressV = wgpu::AddressMode::ClampToEdge;
        break;
    }
    wgpu::Sampler sampler = gWgpuRnd->Gpu().GetSampler(sampDesc);

    // Map sampler — always repeat for tiled texture maps
    SamplerDesc mapSampDesc{};
    mapSampDesc.addressU = wgpu::AddressMode::Repeat;
    mapSampDesc.addressV = wgpu::AddressMode::Repeat;
    wgpu::Sampler mapSampler = gWgpuRnd->Gpu().GetSampler(mapSampDesc);

    wgpu::BindGroup matBG = gWgpuRnd->CreateMaterialBindGroup(
        matOffset, sizeof(MaterialUniforms), texViews, sampler, mapSampler);
    pass.SetBindGroup(1, matBG);

    // --- Object uniforms (group 2) ---
    ObjectUniforms objUni{};
    if (skinned) {
        // Skinned: bone matrices already produce world-space positions,
        // so object transform must be identity to avoid double-transform
        Transform identity;
        identity.Reset();
        FillObjectUniforms(identity, objUni);
    } else {
        FillObjectUniforms(mesh->WorldXfm(), objUni);
    }

    uint32_t objOffset = gWgpuRnd->ObjectRing().Write(
        gWgpuRnd->Gpu().Queue(), &objUni, sizeof(objUni));

    wgpu::BindGroup objBG = gWgpuRnd->CreateObjectBindGroup(
        objOffset, sizeof(ObjectUniforms));
    pass.SetBindGroup(2, objBG);

    // --- Bone uniforms (group 3) ---
    if (skinned) {
        BoneUniforms boneUni{};
        memset(&boneUni, 0, sizeof(boneUni));

        int numBones = mesh->NumBones();
        if (numBones > kMaxBones) numBones = kMaxBones;

        for (int i = 0; i < numBones; i++) {
            RndTransformable* boneTrans = mesh->BoneTransAt(i);
            if (boneTrans) {
                Transform skinMatrix;
                Multiply(mesh->BoneOffsetAt(i), boneTrans->WorldXfm(), skinMatrix);
                TransformToMat4(skinMatrix, boneUni.bones[i]);
            } else {
                boneUni.bones[i][0]  = 1.0f;
                boneUni.bones[i][5]  = 1.0f;
                boneUni.bones[i][10] = 1.0f;
                boneUni.bones[i][15] = 1.0f;
            }
        }

        // Fill remaining slots with identity
        for (int i = numBones; i < kMaxBones; i++) {
            boneUni.bones[i][0]  = 1.0f;
            boneUni.bones[i][5]  = 1.0f;
            boneUni.bones[i][10] = 1.0f;
            boneUni.bones[i][15] = 1.0f;
        }

        uint32_t boneOffset = gWgpuRnd->BoneRing().Write(
            gWgpuRnd->Gpu().Queue(), &boneUni, sizeof(boneUni));

        wgpu::BindGroup boneBG = gWgpuRnd->CreateBoneBindGroup(
            boneOffset, sizeof(BoneUniforms));
        pass.SetBindGroup(3, boneBG);
    } else {
        // Static mesh: bind dummy bone bind group (pipeline layout requires group 3)
        EnsureDummyBoneBindGroup();
        pass.SetBindGroup(3, sDummyBoneBindGroup);
    }

    // --- Draw ---
    size_t vertexSize = skinned ? sizeof(GpuVertexSkinned) : sizeof(GpuVertex);
    pass.SetVertexBuffer(0, meshData.vertexBuffer, 0, meshData.numVertices * vertexSize);
    pass.SetIndexBuffer(meshData.indexBuffer, wgpu::IndexFormat::Uint16, 0,
                        meshData.numIndices * sizeof(uint16_t));
    pass.DrawIndexed(meshData.numIndices);

    // --- Multi-pass materials ---
    // Walk the NextPass chain and draw additional passes with the same geometry
    BaseMaterial* nextPass = mat->NextPass();
    while (nextPass) {
        // Pipeline may differ (blend mode, z mode, etc.)
        PipelineKey npKey = key;
        npKey.blend = (WgpuBlend)nextPass->GetBlend();
        npKey.zMode = (WgpuZMode)nextPass->GetZMode();
        npKey.cull = (WgpuCull)nextPass->GetCull();
        npKey.stencil = (WgpuStencil)nextPass->GetStencil();
        npKey.alphaCut = nextPass->GetAlphaCut();
        npKey.alphaWrite = nextPass->GetAlphaWrite();
        npKey.alphaToCoverage = nextPass->GetAlphaCut();

        wgpu::RenderPipeline npPipeline = gWgpuRnd->Pipelines().GetPipeline(npKey);
        if (npPipeline) {
            pass.SetPipeline(npPipeline);

            // Fill material uniforms from nextPass
            MaterialUniforms npMatUni{};
            const Hmx::Color& npc = nextPass->GetColor();
            npMatUni.color[0] = npc.red; npMatUni.color[1] = npc.green;
            npMatUni.color[2] = npc.blue; npMatUni.color[3] = npc.alpha;
            npMatUni.alphaThreshold = nextPass->GetAlphaCut() ? nextPass->GetAlphaThreshold() / 255.0f : 0.0f;
            const Hmx::Color& nps = nextPass->GetSpecularRGB();
            float npSpecPower = nps.alpha > 0.0f ? nps.alpha : 0.0f;
            npMatUni.specularPower = npSpecPower;
            npMatUni.specularColor[0] = nps.red; npMatUni.specularColor[1] = nps.green;
            npMatUni.specularColor[2] = nps.blue; npMatUni.specularColor[3] = 1.0f;
            npMatUni.emissiveMultiplier = nextPass->GetEmissiveMap() ? nextPass->GetEmissiveMultiplier() : 0.0f;
            npMatUni.intensify = nextPass->GetIntensify() ? 2.0f : 1.0f;
            npMatUni.deNormal = nextPass->GetDeNormal();
            npMatUni.hasNormalMap = nextPass->NormalMap() ? 1.0f : 0.0f;
            npMatUni.prelit = nextPass->Prelit() ? 1.0f : 0.0f;
            npMatUni.texGenMode = (float)nextPass->GetTexGen();

            // Resolve textures
            WgpuRnd::MaterialTexViews npTexViews;
            wgpu::TextureView npDiffuse;
            if (nextPass->GetDiffuseTex()) {
                npDiffuse = GetGpuTexView(nextPass->GetDiffuseTex());
            }
            if (npDiffuse) {
                npMatUni.useTexture = 1.0f;
                npTexViews.diffuse = npDiffuse;
            } else {
                npMatUni.useTexture = 0.0f;
                npTexViews.diffuse = gWgpuRnd->WhiteTexView();
            }
            npTexViews.normal = nextPass->NormalMap() ? GetGpuTexView(nextPass->NormalMap()) : gWgpuRnd->FlatNormalTexView();
            if (!npTexViews.normal) npTexViews.normal = gWgpuRnd->FlatNormalTexView();
            npTexViews.specular = nextPass->GetSpecularMap() ? GetGpuTexView(nextPass->GetSpecularMap()) : gWgpuRnd->WhiteTexView();
            if (!npTexViews.specular) npTexViews.specular = gWgpuRnd->WhiteTexView();
            npTexViews.emissive = nextPass->GetEmissiveMap() ? GetGpuTexView(nextPass->GetEmissiveMap()) : gWgpuRnd->BlackTexView();
            if (!npTexViews.emissive) npTexViews.emissive = gWgpuRnd->BlackTexView();
            npTexViews.rim = nextPass->GetRimMap() ? GetGpuTexView(nextPass->GetRimMap()) : gWgpuRnd->WhiteTexView();
            if (!npTexViews.rim) npTexViews.rim = gWgpuRnd->WhiteTexView();
            npTexViews.environCube = gWgpuRnd->BlackCubeTexView();
            npTexViews.normDetail = nextPass->GetNormDetailMap() ? GetGpuTexView(nextPass->GetNormDetailMap()) : gWgpuRnd->FlatNormalTexView();
            if (!npTexViews.normDetail) npTexViews.normDetail = gWgpuRnd->FlatNormalTexView();

            uint32_t npMatOffset = gWgpuRnd->MaterialRing().Write(
                gWgpuRnd->Gpu().Queue(), &npMatUni, sizeof(npMatUni));

            wgpu::BindGroup npMatBG = gWgpuRnd->CreateMaterialBindGroup(
                npMatOffset, sizeof(MaterialUniforms), npTexViews, sampler, mapSampler);
            pass.SetBindGroup(1, npMatBG);

            // Object + bone bind groups unchanged, just re-draw
            pass.DrawIndexed(meshData.numIndices);
        }
        nextPass = nextPass->NextPass();
    }
}

// ============================================================================
// Shadow depth drawing — simplified path for shadow map generation
// ============================================================================

void DrawMeshShadow(RndMesh* mesh) {
    if (!gWgpuRnd || !gWgpuRnd->InShadowPass()) return;

    // Ensure mesh data is on GPU
    if (!EnsureMeshUploaded(mesh)) return;

    auto& meshData = sMeshGpuData[mesh];
    auto& pass = gWgpuRnd->ShadowPass();
    bool skinned = meshData.skinned;

    // Select shadow pipeline
    if (skinned) {
        pass.SetPipeline(gWgpuRnd->ShadowSkinnedPipeline());
    } else {
        pass.SetPipeline(gWgpuRnd->ShadowStaticPipeline());
    }

    // Object uniforms (group 1) — world matrix
    ObjectUniforms objUni{};
    if (skinned) {
        Transform identity;
        identity.Reset();
        FillObjectUniforms(identity, objUni);
    } else {
        FillObjectUniforms(mesh->WorldXfm(), objUni);
    }

    uint32_t objOffset = gWgpuRnd->ObjectRing().Write(
        gWgpuRnd->Gpu().Queue(), &objUni, sizeof(objUni));

    // Create object bind group using shadow-specific layout
    {
        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = gWgpuRnd->ObjectRing().Buffer();
        entry.offset = objOffset;
        entry.size = sizeof(ObjectUniforms);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = gWgpuRnd->ShadowObjectBGL();
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        wgpu::BindGroup objBG = gWgpuRnd->Gpu().Device().CreateBindGroup(&bgDesc);
        pass.SetBindGroup(1, objBG);
    }

    // Bone uniforms (group 2) — only for skinned
    if (skinned) {
        BoneUniforms boneUni{};
        memset(&boneUni, 0, sizeof(boneUni));

        int numBones = mesh->NumBones();
        if (numBones > kMaxBones) numBones = kMaxBones;

        for (int i = 0; i < numBones; i++) {
            RndTransformable* boneTrans = mesh->BoneTransAt(i);
            if (boneTrans) {
                Transform skinMatrix;
                Multiply(mesh->BoneOffsetAt(i), boneTrans->WorldXfm(), skinMatrix);
                TransformToMat4(skinMatrix, boneUni.bones[i]);
            } else {
                boneUni.bones[i][0]  = 1.0f;
                boneUni.bones[i][5]  = 1.0f;
                boneUni.bones[i][10] = 1.0f;
                boneUni.bones[i][15] = 1.0f;
            }
        }
        for (int i = numBones; i < kMaxBones; i++) {
            boneUni.bones[i][0]  = 1.0f;
            boneUni.bones[i][5]  = 1.0f;
            boneUni.bones[i][10] = 1.0f;
            boneUni.bones[i][15] = 1.0f;
        }

        uint32_t boneOffset = gWgpuRnd->BoneRing().Write(
            gWgpuRnd->Gpu().Queue(), &boneUni, sizeof(boneUni));

        wgpu::BindGroupEntry entry{};
        entry.binding = 0;
        entry.buffer = gWgpuRnd->BoneRing().Buffer();
        entry.offset = boneOffset;
        entry.size = sizeof(BoneUniforms);

        wgpu::BindGroupDescriptor bgDesc{};
        bgDesc.layout = gWgpuRnd->ShadowBoneBGL();
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        wgpu::BindGroup boneBG = gWgpuRnd->Gpu().Device().CreateBindGroup(&bgDesc);
        pass.SetBindGroup(2, boneBG);
    }

    // Draw
    size_t vertexSize = skinned ? sizeof(GpuVertexSkinned) : sizeof(GpuVertex);
    pass.SetVertexBuffer(0, meshData.vertexBuffer, 0, meshData.numVertices * vertexSize);
    pass.SetIndexBuffer(meshData.indexBuffer, wgpu::IndexFormat::Uint16, 0,
                        meshData.numIndices * sizeof(uint16_t));
    pass.DrawIndexed(meshData.numIndices);
}
