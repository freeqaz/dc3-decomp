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

static int sBoneGarbageLogCount = 0;

void FillBoneUniforms(RndMesh* mesh, BoneUniforms& out) {
    memset(&out, 0, sizeof(out));

    int numBones = mesh->NumBones();
    if (numBones > kMaxBones) numBones = kMaxBones;

    // Periodic diagnostic: dump arm bone transforms every 200 frames after startup
    static int sBoneDiagFrames = 0;
    static int sBoneDiagCount = 0;
    sBoneDiagFrames++;
    // Fire at frames 1000, 1200, 1400 for the main body mesh (40 bones)
    bool doDiag = (sBoneDiagFrames >= 1000 && sBoneDiagCount < 3
                   && numBones >= 20 && mesh->Name()[0]
                   && (sBoneDiagFrames % 200 == 0));
    if (doDiag) {
        sBoneDiagCount++;
        fprintf(stderr, "\n=== BONE DIAG mesh='%s' numBones=%d frame=%d ===\n",
                mesh->Name(), numBones, sBoneDiagFrames);
        // Dump all bone names first
        fprintf(stderr, "  ALL BONES:");
        for (int b = 0; b < numBones; b++) {
            RndTransformable* bt = mesh->BoneTransAt(b);
            fprintf(stderr, " [%d]'%s'", b, bt ? bt->Name() : "NULL");
        }
        fprintf(stderr, "\n");
    }

    for (int i = 0; i < numBones; i++) {
        RndTransformable* boneTrans = mesh->BoneTransAt(i);
        if (boneTrans) {
            const Transform& wt = boneTrans->WorldXfm();

            // Log arm-related bones + first 3 for context
            bool isArm = (strstr(boneTrans->Name(), "Arm") || strstr(boneTrans->Name(), "arm")
                       || strstr(boneTrans->Name(), "shoulder") || strstr(boneTrans->Name(), "Shoulder")
                       || strstr(boneTrans->Name(), "clavicle") || strstr(boneTrans->Name(), "hand")
                       || strstr(boneTrans->Name(), "elbow") || strstr(boneTrans->Name(), "foreTwist")
                       || strstr(boneTrans->Name(), "Twist") || strstr(boneTrans->Name(), "upperArm")
                       || strstr(boneTrans->Name(), "foreArm"));
            if (doDiag && (i < 3 || isArm)) {
                const Transform& local = boneTrans->LocalXfm();
                const Transform& offset = mesh->BoneOffsetAt(i);
                RndTransformable* parent = boneTrans->TransParent();
                fprintf(stderr, "  bone[%d] '%s' ptr=%p parent='%s' dirty=%d\n",
                        i, boneTrans->Name(), (void*)boneTrans,
                        parent ? parent->Name() : "(none)",
                        boneTrans->Dirty());
                fprintf(stderr, "    localRot: [%.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f]\n",
                        local.m.x.x, local.m.x.y, local.m.x.z,
                        local.m.y.x, local.m.y.y, local.m.y.z,
                        local.m.z.x, local.m.z.y, local.m.z.z);
                fprintf(stderr, "    localPos: (%.3f, %.3f, %.3f)\n", local.v.x, local.v.y, local.v.z);
                fprintf(stderr, "    worldRot: [%.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f]\n",
                        wt.m.x.x, wt.m.x.y, wt.m.x.z,
                        wt.m.y.x, wt.m.y.y, wt.m.y.z,
                        wt.m.z.x, wt.m.z.y, wt.m.z.z);
                fprintf(stderr, "    worldPos: (%.3f, %.3f, %.3f)\n", wt.v.x, wt.v.y, wt.v.z);
                fprintf(stderr, "    offset:   [%.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f] + (%.3f, %.3f, %.3f)\n",
                        offset.m.x.x, offset.m.x.y, offset.m.x.z,
                        offset.m.y.x, offset.m.y.y, offset.m.y.z,
                        offset.m.z.x, offset.m.z.y, offset.m.z.z,
                        offset.v.x, offset.v.y, offset.v.z);
            }

            bool valid = (fabsf(wt.v.x) < 100000.0f &&
                          fabsf(wt.v.y) < 100000.0f &&
                          fabsf(wt.v.z) < 100000.0f);
            if (valid) {
                Transform skinMatrix;
                Multiply(mesh->BoneOffsetAt(i), wt, skinMatrix);
                TransformToMat4(skinMatrix, out.bones[i]);
                if (doDiag && (i < 3 || isArm)) {
                    fprintf(stderr, "    skin:     [%.3f %.3f %.3f %.1f / %.3f %.3f %.3f %.1f / %.3f %.3f %.3f %.1f / %.3f %.3f %.3f %.1f]\n",
                            out.bones[i][0], out.bones[i][1], out.bones[i][2], out.bones[i][3],
                            out.bones[i][4], out.bones[i][5], out.bones[i][6], out.bones[i][7],
                            out.bones[i][8], out.bones[i][9], out.bones[i][10], out.bones[i][11],
                            out.bones[i][12], out.bones[i][13], out.bones[i][14], out.bones[i][15]);
                }
            } else {
                if (sBoneGarbageLogCount < 20) {
                    fprintf(stderr, "BoneSetup: garbage WorldXfm bone[%d] '%s' on mesh '%s' pos=(%.2e,%.2e,%.2e)\n",
                            i, boneTrans->Name(), mesh->Name(),
                            wt.v.x, wt.v.y, wt.v.z);
                    sBoneGarbageLogCount++;
                }
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

void BoneSetupTerminate() {
    sDummyBoneBuffer = nullptr;
    sDummyBoneBindGroup = nullptr;
}
