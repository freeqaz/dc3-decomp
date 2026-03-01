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
#include "math/Mtx.h"

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
    bool skinned = false;
    bool uploaded = false;
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
    int checkCount = count < 10 ? count : 10;
    for (int i = 0; i < checkCount; i++) {
        if (verts[i].color[3] > 0.001f) { allAlphaZero = false; break; }
    }
    if (allAlphaZero) {
        for (int i = 0; i < count; i++) {
            verts[i].color[0] = verts[i].color[1] = verts[i].color[2] = verts[i].color[3] = 1.0f;
        }
    }
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
    wgpu::Buffer vertexBuf;
    int unpacked = 0;

    if (skinned) {
        // Skinned vertex path
        GpuVertexSkinned* verts = new GpuVertexSkinned[vertCount];
        if (numCompressedVerts > 0 && geomOwner->CompressedVerts()) {
            unpacked = VertexFormats::UnpackCompressedSkinnedVertices(
                geomOwner->CompressedVerts(), numCompressedVerts, verts, vertCount);
        } else {
            unpacked = VertexFormats::UnpackSkinnedVertices(*geomOwner, verts, vertCount);
        }
        if (unpacked <= 0) {
            fprintf(stderr, "Mesh_Wgpu: failed to unpack skinned vertices for '%s'\n", mesh->Name());
            delete[] verts;
            return false;
        }
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
        if (numCompressedVerts > 0 && geomOwner->CompressedVerts()) {
            unpacked = VertexFormats::UnpackCompressedVertices(
                geomOwner->CompressedVerts(), numCompressedVerts, verts, vertCount);
        } else {
            unpacked = VertexFormats::UnpackStaticVertices(*geomOwner, verts, vertCount);
        }
        if (unpacked <= 0) {
            fprintf(stderr, "Mesh_Wgpu: failed to unpack vertices for '%s' (verts=%d, compressed=%d)\n",
                    mesh->Name(), numVerts, numCompressedVerts);
            delete[] verts;
            return false;
        }
        FixZeroAlpha(verts, unpacked);

        wgpu::BufferDescriptor vbDesc{};
        vbDesc.size = unpacked * sizeof(GpuVertex);
        vbDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
        vertexBuf = gWgpuRnd->Gpu().Device().CreateBuffer(&vbDesc);
        gWgpuRnd->Gpu().Queue().WriteBuffer(vertexBuf, 0, verts, unpacked * sizeof(GpuVertex));
        delete[] verts;
    }

    // Create index buffer from faces
    int numIndices = numFaces * 3;
    int allocIndices = (numIndices + 1) & ~1; // round up to even for 4-byte alignment
    uint16_t* indices = new uint16_t[allocIndices]();
    auto& faces = geomOwner->Faces();
    for (int i = 0; i < numFaces; i++) {
        indices[i * 3 + 0] = faces[i].v1;
        indices[i * 3 + 1] = faces[i].v2;
        indices[i * 3 + 2] = faces[i].v3;
    }

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
    data.skinned = skinned;
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
    bool skinned = meshData.skinned;

    // --- Pipeline selection ---
    PipelineKey key{};
    key.shaderType = 18; // kStandardShader
    key.blend = (WgpuBlend)mat->GetBlend();
    key.zMode = (WgpuZMode)mat->GetZMode();
    key.cull = (WgpuCull)mat->GetCull();
    key.stencil = (WgpuStencil)mat->GetStencil();
    key.layout = skinned ? VertexLayoutType::Skinned : VertexLayoutType::Static;
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

    // Specular
    const Hmx::Color& spec = mat->GetSpecularRGB();
    matUni.specularColor[0] = spec.red;
    matUni.specularColor[1] = spec.green;
    matUni.specularColor[2] = spec.blue;
    matUni.specularColor[3] = 1.0f;
    matUni.specularPower = spec.alpha > 0.0f ? spec.alpha : 0.0f;

    // Emissive
    matUni.emissiveMultiplier = mat->GetEmissiveMultiplier();

    // Rim lighting
    const Hmx::Color& rim = mat->GetRimRGB();
    matUni.rimColor[0] = rim.red;
    matUni.rimColor[1] = rim.green;
    matUni.rimColor[2] = rim.blue;
    matUni.rimColor[3] = rim.alpha > 0.0f ? rim.alpha : 0.0f;

    // Intensify
    matUni.intensify = mat->GetIntensify() ? 2.0f : 1.0f;

    // Texture
    RndTex* diffTex = mat->GetDiffuseTex();
    wgpu::TextureView texView;
    if (diffTex) {
        diffTex->PresyncBitmap();
        texView = GetGpuTexView(diffTex);
    }

    if (texView) {
        matUni.useTexture = 1.0f;
    } else {
        matUni.useTexture = 0.0f;
        texView = gWgpuRnd->WhiteTexView();
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

    // --- Bone uniforms (group 3) ---
    if (skinned) {
        BoneUniforms boneUni{};
        memset(&boneUni, 0, sizeof(boneUni));

        int numBones = NumBones();
        if (numBones > kMaxBones) numBones = kMaxBones;

        for (int i = 0; i < numBones; i++) {
            RndTransformable* boneTrans = BoneTransAt(i);
            if (boneTrans) {
                // Bone matrix = bone's world transform * bind-pose offset
                // The mOffset stores the inverse bind pose (mesh-space to bone-space)
                // Final: skinned_pos = boneWorldXfm * mOffset * vertex_pos
                Transform skinMatrix;
                Multiply(mBones[i].mOffset, boneTrans->WorldXfm(), skinMatrix);
                TransformToMat4(skinMatrix, boneUni.bones[i]);
            } else {
                // Identity fallback
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
}
