// DC3 Native Port — Bone setup utilities
// Extracted from Mesh_Wgpu.cpp to share bone matrix computation
// between DrawMeshImmediate and DrawMeshShadow.

#include "platform/BoneSetup.h"
#include "platform/TransformUtils.h"
#include "rndobj/Mesh.h"
#include "rndobj/Trans.h"
#include "math/Mtx.h"
#include <cstring>

// Dummy bone bind group for static meshes (pipeline layout requires group 3)
static wgpu::Buffer sDummyBoneBuffer;
static wgpu::BindGroup sDummyBoneBindGroup;

void FillBoneUniforms(RndMesh* mesh, BoneUniforms& out) {
    memset(&out, 0, sizeof(out));

    int numBones = mesh->NumBones();
    if (numBones > kMaxBones) numBones = kMaxBones;

    for (int i = 0; i < numBones; i++) {
        RndTransformable* boneTrans = mesh->BoneTransAt(i);
        if (boneTrans) {
            const Transform& wt = boneTrans->WorldXfm();
            // Sanity check: if bone WorldXfm has garbage values,
            // fall back to identity to prevent vertices from going to infinity
            bool valid = (fabsf(wt.v.x) < 100000.0f &&
                          fabsf(wt.v.y) < 100000.0f &&
                          fabsf(wt.v.z) < 100000.0f);
            if (valid) {
                Transform skinMatrix;
                Multiply(mesh->BoneOffsetAt(i), wt, skinMatrix);
                TransformToMat4(skinMatrix, out.bones[i]);
            } else {
                // Bad bone — use identity
                out.bones[i][0]  = 1.0f;
                out.bones[i][5]  = 1.0f;
                out.bones[i][10] = 1.0f;
                out.bones[i][15] = 1.0f;
            }
        } else {
            out.bones[i][0]  = 1.0f;
            out.bones[i][5]  = 1.0f;
            out.bones[i][10] = 1.0f;
            out.bones[i][15] = 1.0f;
        }
    }

    // Fill remaining slots with identity
    for (int i = numBones; i < kMaxBones; i++) {
        out.bones[i][0]  = 1.0f;
        out.bones[i][5]  = 1.0f;
        out.bones[i][10] = 1.0f;
        out.bones[i][15] = 1.0f;
    }
}

void EnsureDummyBoneBindGroup() {
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
    bd.label = "DummyBones";
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

wgpu::BindGroup GetDummyBoneBindGroup() {
    return sDummyBoneBindGroup;
}
