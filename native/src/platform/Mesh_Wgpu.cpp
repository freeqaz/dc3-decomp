// DC3 Native Port — WebGPU Mesh Drawing
// Provides RndMesh::DrawShowing() implementation for native build.
// Uses side tables for GPU vertex/index buffers.

#include "platform/Rnd_Wgpu.h"
#include "gfx/VertexFormats.h"
#include "rndobj/Mesh.h"
#include "rndobj/Mat.h"
#include "rndobj/Tex.h"
#include "rndobj/Trans.h"
#include "rndobj/Cam.h"
#include "rndobj/BaseMaterial.h"

#include <unordered_map>
#include <cstdio>
#include <cstring>

// External: get GPU texture view for a RndTex (defined in Tex_Wgpu.cpp)
extern wgpu::TextureView GetGpuTexView(RndTex* tex);

// ============================================================================
// GPU mesh data side table
// ============================================================================

struct GpuMeshData {
    wgpu::Buffer vertexBuffer;
    wgpu::Buffer indexBuffer;
    int numIndices = 0;
    int numVertices = 0;
    bool uploaded = false;
};

static std::unordered_map<RndMesh*, GpuMeshData> sMeshGpuData;

// ============================================================================
// Helper: Convert Transform to ObjectUniforms
// ============================================================================

static void FillObjectUniforms(const Transform& worldXfm, ObjectUniforms& obj) {
    // World matrix — row-major, WGSL reads as column-major (correct transpose)
    obj.world[0]  = worldXfm.m.x.x; obj.world[1]  = worldXfm.m.x.y; obj.world[2]  = worldXfm.m.x.z; obj.world[3]  = 0;
    obj.world[4]  = worldXfm.m.y.x; obj.world[5]  = worldXfm.m.y.y; obj.world[6]  = worldXfm.m.y.z; obj.world[7]  = 0;
    obj.world[8]  = worldXfm.m.z.x; obj.world[9]  = worldXfm.m.z.y; obj.world[10] = worldXfm.m.z.z; obj.world[11] = 0;
    obj.world[12] = worldXfm.v.x;   obj.world[13] = worldXfm.v.y;   obj.world[14] = worldXfm.v.z;   obj.world[15] = 1;

    // For Tier 1: worldInvTranspose = world (correct for orthogonal rotation + translation)
    memcpy(obj.worldInvTranspose, obj.world, 64);
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

    // Check if we have vertices (either uncompressed or compressed)
    if (numVerts <= 0 && numCompressedVerts <= 0) return false;
    if (numFaces <= 0) return false;

    // Unpack vertices to GPU format
    int vertCount = (numVerts > 0) ? numVerts : numCompressedVerts;
    GpuVertex* verts = new GpuVertex[vertCount];
    int unpacked;
    if (numCompressedVerts > 0 && geomOwner->CompressedVerts()) {
        // Xbox 360 compressed vertex path
        unpacked = VertexFormats::UnpackCompressedVertices(
            geomOwner->CompressedVerts(), numCompressedVerts, verts, vertCount);
    } else {
        // Standard uncompressed vertex path
        unpacked = VertexFormats::UnpackStaticVertices(*geomOwner, verts, vertCount);
    }
    if (unpacked <= 0) {
        delete[] verts;
        return false;
    }

    // Fix: if vertex alpha is zero, force white vertex colors (common for texture-only meshes)
    {
        bool allAlphaZero = true;
        for (int i = 0; i < unpacked && i < 10; i++) {
            if (verts[i].color[3] > 0.001f) { allAlphaZero = false; break; }
        }
        if (allAlphaZero) {
            for (int i = 0; i < unpacked; i++) {
                verts[i].color[0] = verts[i].color[1] = verts[i].color[2] = verts[i].color[3] = 1.0f;
            }
        }
    }

    // Create vertex buffer
    wgpu::BufferDescriptor vbDesc{};
    vbDesc.size = unpacked * sizeof(GpuVertex);
    vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer vertexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&vbDesc);
    gWgpuRnd->Gpu().Queue().WriteBuffer(vertexBuf, 0, verts, unpacked * sizeof(GpuVertex));
    delete[] verts;

    // Create index buffer from faces
    int numIndices = numFaces * 3;
    // Allocate with padding for 4-byte alignment (WebGPU requirement)
    int allocIndices = (numIndices + 1) & ~1; // round up to even for 4-byte alignment
    uint16_t* indices = new uint16_t[allocIndices]();  // zero-init for padding
    auto& faces = geomOwner->Faces();
    for (int i = 0; i < numFaces; i++) {
        indices[i * 3 + 0] = faces[i].v1;
        indices[i * 3 + 1] = faces[i].v2;
        indices[i * 3 + 2] = faces[i].v3;
    }

    // WebGPU requires buffer sizes to be a multiple of 4 bytes
    size_t ibAlignedSize = (numIndices * sizeof(uint16_t) + 3) & ~3u;

    wgpu::BufferDescriptor ibDesc{};
    ibDesc.size = ibAlignedSize;
    ibDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer indexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&ibDesc);
    gWgpuRnd->Gpu().Queue().WriteBuffer(indexBuf, 0, indices, ibAlignedSize);
    delete[] indices;

    GpuMeshData data;
    data.vertexBuffer = vertexBuf;
    data.indexBuffer = indexBuf;
    data.numIndices = numIndices;
    data.numVertices = unpacked;
    data.uploaded = true;

    sMeshGpuData[mesh] = data;
    return true;
}

// ============================================================================
// RndMesh::DrawShowing — the hot path
// ============================================================================

void RndMesh::DrawShowing() {
    if (!gWgpuRnd || !gWgpuRnd->IsInPass()) return;

    // Get material
    RndMat* mat = Mat();
    if (!mat) return;

    // Ensure mesh data is on GPU
    if (!EnsureMeshUploaded(this)) return;

    auto& meshData = sMeshGpuData[this];
    auto& pass = gWgpuRnd->CurrentPass();

    // --- Pipeline selection ---
    PipelineKey key{};
    key.shaderType = 18; // kStandardShader
    key.blend = (WgpuBlend)mat->GetBlend();
    key.zMode = (WgpuZMode)mat->GetZMode();
    key.cull = (WgpuCull)mat->GetCull();
    key.stencil = (WgpuStencil)mat->GetStencil();
    key.layout = VertexLayoutType::Static;
    key.targetFormat = gWgpuRnd->Gpu().SurfaceFormat();
    key.alphaCut = mat->GetAlphaCut();
    key.alphaWrite = mat->GetAlphaWrite();

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
    matUni.alphaThreshold = mat->GetAlphaCut() ? (mat->GetAlphaThreshold() / 255.0f) : 0.0f;

    // Texture
    RndTex* diffTex = mat->GetDiffuseTex();
    wgpu::TextureView texView;
    if (diffTex) {
        // Ensure texture is uploaded
        diffTex->PresyncBitmap();
        texView = GetGpuTexView(diffTex);
    }

    if (texView) {
        matUni.useTexture = 1.0f;
    } else {
        matUni.useTexture = 0.0f;
        texView = gWgpuRnd->WhiteTexView();
    }

    // Write material uniforms to ring buffer
    uint32_t matOffset = gWgpuRnd->MaterialRing().Write(
        gWgpuRnd->Gpu().Queue(), &matUni, sizeof(matUni));

    // Sampler from material's wrap mode
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

    // Create and bind material bind group
    wgpu::BindGroup matBG = gWgpuRnd->CreateMaterialBindGroup(
        matOffset, sizeof(MaterialUniforms), texView, sampler);
    pass.SetBindGroup(1, matBG);

    // --- Object uniforms (group 2) ---
    ObjectUniforms objUni{};
    FillObjectUniforms(WorldXfm(), objUni);

    uint32_t objOffset = gWgpuRnd->ObjectRing().Write(
        gWgpuRnd->Gpu().Queue(), &objUni, sizeof(objUni));

    wgpu::BindGroup objBG = gWgpuRnd->CreateObjectBindGroup(
        objOffset, sizeof(ObjectUniforms));
    pass.SetBindGroup(2, objBG);

    // --- Draw ---
    pass.SetVertexBuffer(0, meshData.vertexBuffer, 0, meshData.numVertices * sizeof(GpuVertex));
    pass.SetIndexBuffer(meshData.indexBuffer, wgpu::IndexFormat::Uint16, 0,
                        meshData.numIndices * sizeof(uint16_t));
    pass.DrawIndexed(meshData.numIndices);
}
