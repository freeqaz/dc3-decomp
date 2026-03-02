// GltfExporter — Converts ObjectDir scene to glTF 2.0.
// Phases: static meshes, skinned meshes (joints/weights), animations.

// cgltf: implementation definitions.
// cgltf_write.h re-includes cgltf.h, but the CGLTF_IMPLEMENTATION section is
// outside the header guard, so we must undef CGLTF_IMPLEMENTATION before including
// cgltf_write.h to prevent double-defining jsmn functions.
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"
#undef CGLTF_IMPLEMENTATION

#define CGLTF_WRITE_IMPLEMENTATION
#include "cgltf/cgltf_write.h"

#include "export/GltfExporter.h"
#include "export/BitmapExport.h"
#include "gfx/Screenshot.h"

#include "obj/Dir.h"
#include "rndobj/Mesh.h"
#include "rndobj/Trans.h"
#include "rndobj/TransAnim.h"
#include "rndobj/BaseMaterial.h"
#include "rndobj/Tex.h"
#include "math/Mtx.h"
#include "math/Key.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sys/stat.h>

namespace GltfExporter {

// ============================================================================
// Coordinate conversion: Milo Z-up → glTF Y-up
// Swap Y↔Z and negate new Z (= old Y) to preserve handedness.
// ============================================================================
static void MiloToGltf(float x, float y, float z, float& ox, float& oy, float& oz) {
    ox = x;
    oy = z;
    oz = -y;
}

static void MiloQuatToGltf(float qx, float qy, float qz, float qw,
                            float& ox, float& oy, float& oz, float& ow) {
    // Same axis swap for quaternion components
    ox = qx;
    oy = qz;
    oz = -qy;
    ow = qw;
}

// ============================================================================
// Binary buffer builder — appends typed data and tracks byte offsets
// ============================================================================
struct BufferBuilder {
    std::vector<uint8_t> data;

    size_t Align(size_t alignment = 4) {
        size_t pad = (alignment - (data.size() % alignment)) % alignment;
        data.insert(data.end(), pad, 0);
        return data.size();
    }

    size_t AppendFloat(float v) {
        size_t off = data.size();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        data.insert(data.end(), p, p + 4);
        return off;
    }

    size_t AppendFloats(const float* v, int count) {
        size_t off = data.size();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(v);
        data.insert(data.end(), p, p + count * 4);
        return off;
    }

    size_t AppendUShort(uint16_t v) {
        size_t off = data.size();
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
        data.insert(data.end(), p, p + 2);
        return off;
    }

    size_t AppendBytes(const uint8_t* p, size_t len) {
        size_t off = data.size();
        data.insert(data.end(), p, p + len);
        return off;
    }

    size_t Size() const { return data.size(); }
};

// ============================================================================
// Helpers
// ============================================================================

// Extract position (translation) from a Milo Transform
static void TransformToTRS(const Transform& xfm,
                            float pos[3], float rot[4], float scl[3]) {
    // Translation
    MiloToGltf(xfm.v.x, xfm.v.y, xfm.v.z, pos[0], pos[1], pos[2]);

    // Rotation: convert Matrix3 → quaternion, then axis-swap
    Hmx::Quat q(xfm.m);
    MiloQuatToGltf(q.x, q.y, q.z, q.w, rot[0], rot[1], rot[2], rot[3]);

    // Scale: extract from matrix column lengths
    float sx = sqrtf(xfm.m.x.x * xfm.m.x.x + xfm.m.x.y * xfm.m.x.y + xfm.m.x.z * xfm.m.x.z);
    float sy = sqrtf(xfm.m.y.x * xfm.m.y.x + xfm.m.y.y * xfm.m.y.y + xfm.m.y.z * xfm.m.y.z);
    float sz = sqrtf(xfm.m.z.x * xfm.m.z.x + xfm.m.z.y * xfm.m.z.y + xfm.m.z.z * xfm.m.z.z);
    // In glTF, after axis swap: X stays, Y=Z, Z=Y
    scl[0] = sx;
    scl[1] = sz;
    scl[2] = sy;
}

// Get output directory from a file path
static std::string DirName(const char* path) {
    std::string s(path);
    size_t pos = s.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return s.substr(0, pos);
}

static std::string BaseName(const char* path) {
    std::string s(path);
    size_t pos = s.find_last_of("/\\");
    if (pos != std::string::npos) s = s.substr(pos + 1);
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos) s = s.substr(0, dot);
    return s;
}

// ============================================================================
// Export implementation
// ============================================================================

bool Export(ObjectDir* dir, const char* outputPath, const Options& opts) {
    if (!dir || !outputPath) return false;

    std::string outDir = DirName(outputPath);
    std::string outBase = BaseName(outputPath);
    mkdir(outDir.c_str(), 0755);

    // ---- Collect objects ----
    std::vector<RndMesh*> meshes;
    std::vector<RndTransAnim*> transAnims;
    std::map<std::string, int> texIndexMap;   // tex name → glTF texture index
    std::map<std::string, int> matIndexMap;   // mat name → glTF material index
    std::vector<RndTex*> textures;
    std::vector<BaseMaterial*> materials;

    // Collect meshes
    {
        ObjDirItr<RndMesh> it(dir, true);
        while (it) {
            RndMesh* mesh = it;
            if (mesh->NumVerts() > 0 && mesh->NumFaces() > 0)
                meshes.push_back(mesh);
            ++it;
        }
    }

    // Collect animations
    if (opts.animations) {
        ObjDirItr<RndTransAnim> it(dir, true);
        while (it) {
            RndTransAnim* anim = it;
            if (anim->TransKeys().NumKeys() > 0 ||
                anim->RotKeys().NumKeys() > 0 ||
                anim->ScaleKeys().NumKeys() > 0) {
                transAnims.push_back(anim);
            }
            ++it;
        }
    }

    // Collect unique textures from mesh materials
    auto addTexture = [&](RndTex* tex) {
        if (!tex || !tex->Name() || !tex->Name()[0]) return;
        std::string name(tex->Name());
        if (texIndexMap.count(name)) return;
        texIndexMap[name] = (int)textures.size();
        textures.push_back(tex);
    };

    auto addMaterial = [&](BaseMaterial* mat) {
        if (!mat || !mat->Name() || !mat->Name()[0]) return;
        std::string name(mat->Name());
        if (matIndexMap.count(name)) return;
        matIndexMap[name] = (int)materials.size();
        materials.push_back(mat);
        addTexture(mat->GetDiffuseTex());
        addTexture(mat->NormalMap());
        addTexture(mat->GetEmissiveMap());
    };

    for (RndMesh* mesh : meshes) {
        RndMat* mat = mesh->Mat();
        if (mat) {
            // RndMat inherits BaseMaterial
            BaseMaterial* baseMat = dynamic_cast<BaseMaterial*>(mat);
            if (baseMat) addMaterial(baseMat);
        }
    }

    if (opts.verbose) {
        printf("GltfExporter: %d meshes, %d materials, %d textures, %d animations\n",
               (int)meshes.size(), (int)materials.size(), (int)textures.size(),
               (int)transAnims.size());
    }

    // ---- Export texture PNGs alongside glTF ----
    std::vector<std::string> texFilenames;
    for (RndTex* tex : textures) {
        std::string filename = std::string(tex->Name()) + ".png";
        std::string fullPath = outDir + "/" + filename;

        const RndBitmap& bmp = tex->Bitmap();
        std::vector<uint8_t> rgba = BitmapExport::ToRGBA(bmp);
        if (!rgba.empty()) {
            WritePNG(fullPath.c_str(), rgba.data(), bmp.Width(), bmp.Height());
        }
        texFilenames.push_back(filename);
    }

    // ---- Build binary buffer ----
    BufferBuilder buf;

    // Track accessor info for each mesh
    struct MeshAccessors {
        size_t posOffset, posCount;
        float posMin[3], posMax[3];
        size_t normOffset;
        size_t uvOffset;
        size_t colorOffset;
        bool hasColors;
        size_t idxOffset, idxCount;
        // Skin data
        size_t jointsOffset, weightsOffset;
        bool hasSkin;
        int numBones;
    };
    std::vector<MeshAccessors> meshAccs(meshes.size());

    for (size_t mi = 0; mi < meshes.size(); mi++) {
        RndMesh* mesh = meshes[mi];
        RndMesh::VertVector& verts = mesh->Verts();
        std::vector<RndMesh::Face>& faces = mesh->Faces();
        int nv = verts.size();
        int nf = (int)faces.size();
        MeshAccessors& acc = meshAccs[mi];

        // Check if any vertex has non-white color
        bool hasColors = false;
        for (int i = 0; i < nv; i++) {
            const Hmx::Color& c = verts[i].color;
            if (c.red < 0.99f || c.green < 0.99f || c.blue < 0.99f || c.alpha < 0.99f) {
                hasColors = true;
                break;
            }
        }
        acc.hasColors = hasColors;

        // Positions
        buf.Align(4);
        acc.posOffset = buf.Size();
        acc.posCount = nv;
        acc.posMin[0] = acc.posMin[1] = acc.posMin[2] = 1e30f;
        acc.posMax[0] = acc.posMax[1] = acc.posMax[2] = -1e30f;
        for (int i = 0; i < nv; i++) {
            float gx, gy, gz;
            MiloToGltf(verts[i].pos.x, verts[i].pos.y, verts[i].pos.z, gx, gy, gz);
            buf.AppendFloat(gx);
            buf.AppendFloat(gy);
            buf.AppendFloat(gz);
            acc.posMin[0] = std::min(acc.posMin[0], gx);
            acc.posMin[1] = std::min(acc.posMin[1], gy);
            acc.posMin[2] = std::min(acc.posMin[2], gz);
            acc.posMax[0] = std::max(acc.posMax[0], gx);
            acc.posMax[1] = std::max(acc.posMax[1], gy);
            acc.posMax[2] = std::max(acc.posMax[2], gz);
        }

        // Normals
        buf.Align(4);
        acc.normOffset = buf.Size();
        for (int i = 0; i < nv; i++) {
            float gx, gy, gz;
            MiloToGltf(verts[i].norm.x, verts[i].norm.y, verts[i].norm.z, gx, gy, gz);
            buf.AppendFloat(gx);
            buf.AppendFloat(gy);
            buf.AppendFloat(gz);
        }

        // UVs
        buf.Align(4);
        acc.uvOffset = buf.Size();
        for (int i = 0; i < nv; i++) {
            buf.AppendFloat(verts[i].tex.x);
            buf.AppendFloat(verts[i].tex.y);
        }

        // Vertex colors (if needed)
        if (hasColors) {
            buf.Align(4);
            acc.colorOffset = buf.Size();
            for (int i = 0; i < nv; i++) {
                buf.AppendFloat(verts[i].color.red);
                buf.AppendFloat(verts[i].color.green);
                buf.AppendFloat(verts[i].color.blue);
                buf.AppendFloat(verts[i].color.alpha);
            }
        }

        // Skin data (joints + weights)
        acc.hasSkin = opts.skins && mesh->IsSkinned();
        acc.numBones = mesh->NumBones();
        if (acc.hasSkin) {
            // JOINTS_0 — 4 unsigned shorts per vertex
            buf.Align(4);
            acc.jointsOffset = buf.Size();
            for (int i = 0; i < nv; i++) {
                for (int b = 0; b < 4; b++) {
                    uint16_t idx = (uint16_t)verts[i].boneIndices[b];
                    if (idx >= (uint16_t)acc.numBones) idx = 0;
                    buf.AppendUShort(idx);
                }
            }

            // WEIGHTS_0 — 4 floats per vertex
            buf.Align(4);
            acc.weightsOffset = buf.Size();
            for (int i = 0; i < nv; i++) {
                buf.AppendFloat(verts[i].boneWeights.x);
                buf.AppendFloat(verts[i].boneWeights.y);
                buf.AppendFloat(verts[i].boneWeights.z);
                buf.AppendFloat(verts[i].boneWeights.w);
            }
        }

        // Indices
        buf.Align(2);
        acc.idxOffset = buf.Size();
        acc.idxCount = nf * 3;
        for (int i = 0; i < nf; i++) {
            buf.AppendUShort(faces[i].v1);
            buf.AppendUShort(faces[i].v2);
            buf.AppendUShort(faces[i].v3);
        }
    }

    // ---- Animation buffer data ----
    struct AnimChannelInfo {
        int transAnimIdx;
        size_t timeOffset, timeCount;
        size_t dataOffset;
        float timeMin, timeMax;
        enum Type { kTranslation, kRotation, kScale } type;
    };
    std::vector<AnimChannelInfo> animChannels;

    const float FPS = 30.0f;
    for (int ai = 0; ai < (int)transAnims.size(); ai++) {
        RndTransAnim* anim = transAnims[ai];

        // Translation keys
        auto& tkeys = anim->TransKeys();
        if (tkeys.NumKeys() > 0) {
            AnimChannelInfo ch;
            ch.transAnimIdx = ai;
            ch.type = AnimChannelInfo::kTranslation;
            ch.timeCount = tkeys.NumKeys();

            buf.Align(4);
            ch.timeOffset = buf.Size();
            ch.timeMin = tkeys.front().frame / FPS;
            ch.timeMax = tkeys.back().frame / FPS;
            for (int k = 0; k < tkeys.NumKeys(); k++)
                buf.AppendFloat(tkeys[k].frame / FPS);

            buf.Align(4);
            ch.dataOffset = buf.Size();
            for (int k = 0; k < tkeys.NumKeys(); k++) {
                float gx, gy, gz;
                MiloToGltf(tkeys[k].value.x, tkeys[k].value.y, tkeys[k].value.z, gx, gy, gz);
                buf.AppendFloat(gx);
                buf.AppendFloat(gy);
                buf.AppendFloat(gz);
            }
            animChannels.push_back(ch);
        }

        // Rotation keys
        auto& rkeys = anim->RotKeys();
        if (rkeys.NumKeys() > 0) {
            AnimChannelInfo ch;
            ch.transAnimIdx = ai;
            ch.type = AnimChannelInfo::kRotation;
            ch.timeCount = rkeys.NumKeys();

            buf.Align(4);
            ch.timeOffset = buf.Size();
            ch.timeMin = rkeys.front().frame / FPS;
            ch.timeMax = rkeys.back().frame / FPS;
            for (int k = 0; k < rkeys.NumKeys(); k++)
                buf.AppendFloat(rkeys[k].frame / FPS);

            buf.Align(4);
            ch.dataOffset = buf.Size();
            for (int k = 0; k < rkeys.NumKeys(); k++) {
                float ox, oy, oz, ow;
                MiloQuatToGltf(rkeys[k].value.x, rkeys[k].value.y,
                               rkeys[k].value.z, rkeys[k].value.w,
                               ox, oy, oz, ow);
                buf.AppendFloat(ox);
                buf.AppendFloat(oy);
                buf.AppendFloat(oz);
                buf.AppendFloat(ow);
            }
            animChannels.push_back(ch);
        }

        // Scale keys
        auto& skeys = anim->ScaleKeys();
        if (skeys.NumKeys() > 0) {
            AnimChannelInfo ch;
            ch.transAnimIdx = ai;
            ch.type = AnimChannelInfo::kScale;
            ch.timeCount = skeys.NumKeys();

            buf.Align(4);
            ch.timeOffset = buf.Size();
            ch.timeMin = skeys.front().frame / FPS;
            ch.timeMax = skeys.back().frame / FPS;
            for (int k = 0; k < skeys.NumKeys(); k++)
                buf.AppendFloat(skeys[k].frame / FPS);

            buf.Align(4);
            ch.dataOffset = buf.Size();
            for (int k = 0; k < skeys.NumKeys(); k++) {
                // Scale: same axis swap as position
                float gx, gy, gz;
                MiloToGltf(skeys[k].value.x, skeys[k].value.y, skeys[k].value.z, gx, gy, gz);
                // But scale should be absolute (no negation)
                buf.AppendFloat(fabsf(gx));
                buf.AppendFloat(fabsf(gy));
                buf.AppendFloat(fabsf(gz));
            }
            animChannels.push_back(ch);
        }
    }

    // ---- Inverse bind matrices for skinned meshes ----
    struct SkinInfo {
        size_t ibmOffset;
        int meshIdx;
    };
    std::vector<SkinInfo> skinInfos;

    for (size_t mi = 0; mi < meshes.size(); mi++) {
        if (!meshAccs[mi].hasSkin) continue;
        RndMesh* mesh = meshes[mi];
        int nb = mesh->NumBones();

        SkinInfo si;
        si.meshIdx = (int)mi;
        buf.Align(4);
        si.ibmOffset = buf.Size();

        for (int b = 0; b < nb; b++) {
            const Transform& off = mesh->BoneOffsetAt(b);
            // Bone offset is inverse bind matrix in Milo space
            // Convert to glTF: 4x4 column-major matrix with axis swap
            // For simplicity, store as 4x4 identity-like with the offset applied
            float m[16];

            // Apply axis swap to the 3x3 rotation part
            // Milo rows: X=(xx,xy,xz), Y=(yx,yy,yz), Z=(zx,zy,zz)
            // glTF (Y-up): swap row Y↔Z, swap col Y↔Z, negate swapped
            m[0]  = off.m.x.x;   m[1]  = off.m.x.z;   m[2]  = -off.m.x.y;  m[3]  = 0;
            m[4]  = off.m.z.x;   m[5]  = off.m.z.z;   m[6]  = -off.m.z.y;  m[7]  = 0;
            m[8]  = -off.m.y.x;  m[9]  = -off.m.y.z;  m[10] = off.m.y.y;   m[11] = 0;

            float tx, ty, tz;
            MiloToGltf(off.v.x, off.v.y, off.v.z, tx, ty, tz);
            m[12] = tx; m[13] = ty; m[14] = tz; m[15] = 1;

            buf.AppendFloats(m, 16);
        }
        skinInfos.push_back(si);
    }

    buf.Align(4); // Final alignment

    // ---- Write binary buffer to .bin file ----
    std::string binFilename = outBase + ".bin";
    std::string binPath = outDir + "/" + binFilename;
    {
        FILE* f = fopen(binPath.c_str(), "wb");
        if (!f) {
            fprintf(stderr, "GltfExporter: cannot write %s\n", binPath.c_str());
            return false;
        }
        fwrite(buf.data.data(), 1, buf.data.size(), f);
        fclose(f);
    }

    // ---- Build cgltf data structure ----
    // Count totals for allocation
    int numAccessors = 0;
    int numBufferViews = 0;
    for (size_t mi = 0; mi < meshes.size(); mi++) {
        numAccessors += 4; // pos, norm, uv, idx
        numBufferViews += 4;
        if (meshAccs[mi].hasColors) { numAccessors++; numBufferViews++; }
        if (meshAccs[mi].hasSkin) { numAccessors += 2; numBufferViews += 2; } // joints, weights
    }
    // Skin inverse bind matrices
    for (auto& si : skinInfos) {
        numAccessors++; numBufferViews++;
    }
    // Animation channels: time + data per channel
    numAccessors += (int)animChannels.size() * 2;
    numBufferViews += (int)animChannels.size() * 2;

    // Allocate all cgltf arrays with calloc
    cgltf_data gltf = {};
    gltf.asset.version = (char*)"2.0";
    gltf.asset.generator = (char*)"DC3 Decomp — GltfExporter";

    // Buffer
    cgltf_buffer gltfBuf = {};
    gltfBuf.size = buf.data.size();
    gltfBuf.uri = (char*)binFilename.c_str();
    gltf.buffers = &gltfBuf;
    gltf.buffers_count = 1;

    // Allocate buffer views and accessors
    auto* bvs = (cgltf_buffer_view*)calloc(numBufferViews, sizeof(cgltf_buffer_view));
    auto* accs = (cgltf_accessor*)calloc(numAccessors, sizeof(cgltf_accessor));
    int bvIdx = 0, accIdx = 0;

    // Helper to create a buffer view + accessor pair
    auto makeAccessor = [&](size_t offset, size_t count, cgltf_type type,
                            cgltf_component_type comp, bool isIndex = false) -> int {
        int myBv = bvIdx++;
        int myAcc = accIdx++;

        bvs[myBv].buffer = &gltfBuf;
        bvs[myBv].offset = offset;
        int compSize = (comp == cgltf_component_type_r_16u) ? 2 : 4;
        int numComps = 1;
        switch (type) {
        case cgltf_type_vec2: numComps = 2; break;
        case cgltf_type_vec3: numComps = 3; break;
        case cgltf_type_vec4: numComps = 4; break;
        case cgltf_type_mat4: numComps = 16; break;
        default: break;
        }
        bvs[myBv].size = count * numComps * compSize;
        bvs[myBv].type = isIndex ? cgltf_buffer_view_type_indices : cgltf_buffer_view_type_vertices;

        accs[myAcc].buffer_view = &bvs[myBv];
        accs[myAcc].offset = 0;
        accs[myAcc].count = count;
        accs[myAcc].type = type;
        accs[myAcc].component_type = comp;

        return myAcc;
    };

    // ---- Textures + Images ----
    auto* gltfImages = (cgltf_image*)calloc(textures.size(), sizeof(cgltf_image));
    auto* gltfTextures = (cgltf_texture*)calloc(textures.size(), sizeof(cgltf_texture));
    for (size_t i = 0; i < textures.size(); i++) {
        gltfImages[i].uri = strdup(texFilenames[i].c_str());
        gltfImages[i].mime_type = (char*)"image/png";
        gltfTextures[i].image = &gltfImages[i];
    }
    gltf.images = gltfImages;
    gltf.images_count = textures.size();
    gltf.textures = gltfTextures;
    gltf.textures_count = textures.size();

    // ---- Materials ----
    auto* gltfMats = (cgltf_material*)calloc(materials.size(), sizeof(cgltf_material));
    for (size_t i = 0; i < materials.size(); i++) {
        BaseMaterial* mat = materials[i];
        cgltf_material& gm = gltfMats[i];
        gm.name = strdup(mat->Name());
        gm.has_pbr_metallic_roughness = true;

        // Base color
        const Hmx::Color& c = mat->GetColor();
        gm.pbr_metallic_roughness.base_color_factor[0] = c.red;
        gm.pbr_metallic_roughness.base_color_factor[1] = c.green;
        gm.pbr_metallic_roughness.base_color_factor[2] = c.blue;
        gm.pbr_metallic_roughness.base_color_factor[3] = c.alpha;

        // Metallic/roughness defaults
        gm.pbr_metallic_roughness.metallic_factor = 0.0f;
        // Derive roughness from specular (higher specular → lower roughness)
        const Hmx::Color& spec = mat->GetSpecularRGB();
        float specIntensity = (spec.red + spec.green + spec.blue) / 3.0f;
        gm.pbr_metallic_roughness.roughness_factor = 1.0f - std::min(specIntensity, 1.0f);

        // Diffuse texture
        RndTex* diffTex = mat->GetDiffuseTex();
        if (diffTex && diffTex->Name() && diffTex->Name()[0]) {
            auto tit = texIndexMap.find(diffTex->Name());
            if (tit != texIndexMap.end()) {
                gm.pbr_metallic_roughness.base_color_texture.texture = &gltfTextures[tit->second];
                gm.pbr_metallic_roughness.base_color_texture.scale = 1.0f;
            }
        }

        // Normal map
        RndTex* normTex = mat->NormalMap();
        if (normTex && normTex->Name() && normTex->Name()[0]) {
            auto tit = texIndexMap.find(normTex->Name());
            if (tit != texIndexMap.end()) {
                gm.normal_texture.texture = &gltfTextures[tit->second];
                gm.normal_texture.scale = 1.0f - mat->GetDeNormal(); // denormal=0 → full strength
            }
        }

        // Emissive
        RndTex* emTex = mat->GetEmissiveMap();
        if (emTex && emTex->Name() && emTex->Name()[0]) {
            auto tit = texIndexMap.find(emTex->Name());
            if (tit != texIndexMap.end()) {
                gm.emissive_texture.texture = &gltfTextures[tit->second];
            }
        }
        float em = mat->GetEmissiveMultiplier();
        gm.emissive_factor[0] = em;
        gm.emissive_factor[1] = em;
        gm.emissive_factor[2] = em;

        // Alpha
        if (mat->GetBlend() == BaseMaterial::kBlendSrcAlpha ||
            mat->GetBlend() == BaseMaterial::kBlendSrcAlphaAdd) {
            gm.alpha_mode = cgltf_alpha_mode_blend;
        } else if (mat->GetAlphaCut()) {
            gm.alpha_mode = cgltf_alpha_mode_mask;
            gm.alpha_cutoff = mat->GetAlphaThreshold() / 255.0f;
        } else {
            gm.alpha_mode = cgltf_alpha_mode_opaque;
        }

        // Double-sided
        gm.double_sided = (mat->GetCull() == kCullNone);
    }
    gltf.materials = gltfMats;
    gltf.materials_count = materials.size();

    // ---- Mesh accessors ----
    struct MeshAccIndices {
        int posAcc, normAcc, uvAcc, colorAcc, idxAcc;
        int jointsAcc, weightsAcc;
    };
    std::vector<MeshAccIndices> meshAccIdx(meshes.size());

    for (size_t mi = 0; mi < meshes.size(); mi++) {
        auto& acc = meshAccs[mi];
        auto& idx = meshAccIdx[mi];

        idx.posAcc = makeAccessor(acc.posOffset, acc.posCount, cgltf_type_vec3,
                                   cgltf_component_type_r_32f);
        // Set min/max on position accessor
        accs[idx.posAcc].has_min = true;
        accs[idx.posAcc].has_max = true;
        memcpy(accs[idx.posAcc].min, acc.posMin, sizeof(float) * 3);
        memcpy(accs[idx.posAcc].max, acc.posMax, sizeof(float) * 3);

        idx.normAcc = makeAccessor(acc.normOffset, acc.posCount, cgltf_type_vec3,
                                    cgltf_component_type_r_32f);
        idx.uvAcc = makeAccessor(acc.uvOffset, acc.posCount, cgltf_type_vec2,
                                  cgltf_component_type_r_32f);

        idx.colorAcc = -1;
        if (acc.hasColors) {
            idx.colorAcc = makeAccessor(acc.colorOffset, acc.posCount, cgltf_type_vec4,
                                         cgltf_component_type_r_32f);
        }

        idx.jointsAcc = -1;
        idx.weightsAcc = -1;
        if (acc.hasSkin) {
            idx.jointsAcc = makeAccessor(acc.jointsOffset, acc.posCount, cgltf_type_vec4,
                                          cgltf_component_type_r_16u);
            idx.weightsAcc = makeAccessor(acc.weightsOffset, acc.posCount, cgltf_type_vec4,
                                           cgltf_component_type_r_32f);
        }

        idx.idxAcc = makeAccessor(acc.idxOffset, acc.idxCount, cgltf_type_scalar,
                                   cgltf_component_type_r_16u, true);
    }

    // ---- Skin IBM accessors ----
    std::vector<int> skinIbmAccs;
    for (auto& si : skinInfos) {
        int nb = meshAccs[si.meshIdx].numBones;
        int a = makeAccessor(si.ibmOffset, nb, cgltf_type_mat4, cgltf_component_type_r_32f);
        skinIbmAccs.push_back(a);
    }

    // ---- Animation accessors ----
    struct AnimAccPair { int timeAcc; int dataAcc; };
    std::vector<AnimAccPair> animAccPairs;
    for (auto& ch : animChannels) {
        AnimAccPair p;
        p.timeAcc = makeAccessor(ch.timeOffset, ch.timeCount, cgltf_type_scalar,
                                  cgltf_component_type_r_32f);
        accs[p.timeAcc].has_min = true;
        accs[p.timeAcc].has_max = true;
        accs[p.timeAcc].min[0] = ch.timeMin;
        accs[p.timeAcc].max[0] = ch.timeMax;

        cgltf_type dataType = cgltf_type_vec3;
        if (ch.type == AnimChannelInfo::kRotation) dataType = cgltf_type_vec4;
        p.dataAcc = makeAccessor(ch.dataOffset, ch.timeCount, dataType,
                                  cgltf_component_type_r_32f);
        animAccPairs.push_back(p);
    }

    gltf.buffer_views = bvs;
    gltf.buffer_views_count = bvIdx;
    gltf.accessors = accs;
    gltf.accessors_count = accIdx;

    // ---- Nodes ----
    // One node per mesh, plus bone nodes for skinned meshes
    // For simplicity, create flat node list (no hierarchy reconstruction)
    int numNodes = (int)meshes.size();

    // Count bone nodes needed
    std::map<RndTransformable*, int> boneNodeMap; // bone → node index
    std::vector<RndTransformable*> boneNodes;
    for (size_t mi = 0; mi < meshes.size(); mi++) {
        if (!meshAccs[mi].hasSkin) continue;
        RndMesh* mesh = meshes[mi];
        for (int b = 0; b < mesh->NumBones(); b++) {
            RndTransformable* bone = mesh->BoneTransAt(b);
            if (bone && boneNodeMap.find(bone) == boneNodeMap.end()) {
                boneNodeMap[bone] = numNodes + (int)boneNodes.size();
                boneNodes.push_back(bone);
            }
        }
    }
    numNodes += (int)boneNodes.size();

    auto* gltfNodes = (cgltf_node*)calloc(numNodes, sizeof(cgltf_node));

    // Mesh nodes
    for (size_t mi = 0; mi < meshes.size(); mi++) {
        RndMesh* mesh = meshes[mi];
        cgltf_node& node = gltfNodes[mi];
        node.name = strdup(mesh->Name());

        // Set transform from mesh's world xfm
        Transform wxfm = mesh->WorldXfm();
        float pos[3], rot[4], scl[3];
        TransformToTRS(wxfm, pos, rot, scl);

        node.has_translation = true;
        memcpy(node.translation, pos, sizeof(pos));
        node.has_rotation = true;
        memcpy(node.rotation, rot, sizeof(rot));
        node.has_scale = true;
        memcpy(node.scale, scl, sizeof(scl));
    }

    // Bone nodes
    for (size_t bi = 0; bi < boneNodes.size(); bi++) {
        RndTransformable* bone = boneNodes[bi];
        cgltf_node& node = gltfNodes[meshes.size() + bi];
        node.name = strdup(bone->Name());

        Transform wxfm = bone->WorldXfm();
        float pos[3], rot[4], scl[3];
        TransformToTRS(wxfm, pos, rot, scl);
        node.has_translation = true;
        memcpy(node.translation, pos, sizeof(pos));
        node.has_rotation = true;
        memcpy(node.rotation, rot, sizeof(rot));
        node.has_scale = true;
        memcpy(node.scale, scl, sizeof(scl));
    }

    gltf.nodes = gltfNodes;
    gltf.nodes_count = numNodes;

    // ---- Meshes (glTF mesh = primitives) ----
    auto* gltfMeshes = (cgltf_mesh*)calloc(meshes.size(), sizeof(cgltf_mesh));
    auto* gltfPrims = (cgltf_primitive*)calloc(meshes.size(), sizeof(cgltf_primitive));

    // Attributes: up to 6 per mesh (pos, norm, uv, color, joints, weights)
    int maxAttrsPerMesh = 6;
    auto* gltfAttrs = (cgltf_attribute*)calloc(meshes.size() * maxAttrsPerMesh, sizeof(cgltf_attribute));

    for (size_t mi = 0; mi < meshes.size(); mi++) {
        RndMesh* mesh = meshes[mi];
        auto& idx = meshAccIdx[mi];

        cgltf_primitive& prim = gltfPrims[mi];
        prim.type = cgltf_primitive_type_triangles;
        prim.indices = &accs[idx.idxAcc];

        // Material
        RndMat* mat = mesh->Mat();
        if (mat) {
            auto mit = matIndexMap.find(mat->Name());
            if (mit != matIndexMap.end()) {
                prim.material = &gltfMats[mit->second];
            }
        }

        // Attributes
        int attrBase = (int)mi * maxAttrsPerMesh;
        int attrCount = 0;

        gltfAttrs[attrBase + attrCount].name = (char*)"POSITION";
        gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_position;
        gltfAttrs[attrBase + attrCount].data = &accs[idx.posAcc];
        attrCount++;

        gltfAttrs[attrBase + attrCount].name = (char*)"NORMAL";
        gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_normal;
        gltfAttrs[attrBase + attrCount].data = &accs[idx.normAcc];
        attrCount++;

        gltfAttrs[attrBase + attrCount].name = (char*)"TEXCOORD_0";
        gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_texcoord;
        gltfAttrs[attrBase + attrCount].data = &accs[idx.uvAcc];
        attrCount++;

        if (idx.colorAcc >= 0) {
            gltfAttrs[attrBase + attrCount].name = (char*)"COLOR_0";
            gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_color;
            gltfAttrs[attrBase + attrCount].data = &accs[idx.colorAcc];
            attrCount++;
        }

        if (idx.jointsAcc >= 0) {
            gltfAttrs[attrBase + attrCount].name = (char*)"JOINTS_0";
            gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_joints;
            gltfAttrs[attrBase + attrCount].data = &accs[idx.jointsAcc];
            attrCount++;

            gltfAttrs[attrBase + attrCount].name = (char*)"WEIGHTS_0";
            gltfAttrs[attrBase + attrCount].type = cgltf_attribute_type_weights;
            gltfAttrs[attrBase + attrCount].data = &accs[idx.weightsAcc];
            attrCount++;
        }

        prim.attributes = &gltfAttrs[attrBase];
        prim.attributes_count = attrCount;

        gltfMeshes[mi].name = strdup(mesh->Name());
        gltfMeshes[mi].primitives = &gltfPrims[mi];
        gltfMeshes[mi].primitives_count = 1;

        gltfNodes[mi].mesh = &gltfMeshes[mi];
    }

    gltf.meshes = gltfMeshes;
    gltf.meshes_count = meshes.size();

    // ---- Skins ----
    auto* gltfSkins = (cgltf_skin*)calloc(skinInfos.size(), sizeof(cgltf_skin));
    // Joint pointer arrays
    std::vector<std::vector<cgltf_node*>> skinJointPtrs(skinInfos.size());

    for (size_t si = 0; si < skinInfos.size(); si++) {
        int mi = skinInfos[si].meshIdx;
        RndMesh* mesh = meshes[mi];
        int nb = mesh->NumBones();

        skinJointPtrs[si].resize(nb);
        for (int b = 0; b < nb; b++) {
            RndTransformable* bone = mesh->BoneTransAt(b);
            auto bit = boneNodeMap.find(bone);
            if (bit != boneNodeMap.end()) {
                skinJointPtrs[si][b] = &gltfNodes[bit->second];
            }
        }

        gltfSkins[si].name = strdup(mesh->Name());
        gltfSkins[si].joints = skinJointPtrs[si].data();
        gltfSkins[si].joints_count = nb;
        gltfSkins[si].inverse_bind_matrices = &accs[skinIbmAccs[si]];

        gltfNodes[mi].skin = &gltfSkins[si];
    }
    gltf.skins = gltfSkins;
    gltf.skins_count = skinInfos.size();

    // ---- Animations ----
    // Group channels by target trans → one glTF animation per target
    // For simplicity, create one big glTF animation containing all channels
    cgltf_animation gltfAnim = {};
    std::vector<cgltf_animation_channel> gltfAnimChannels;
    std::vector<cgltf_animation_sampler> gltfAnimSamplers;

    if (!animChannels.empty()) {
        gltfAnim.name = (char*)"animation";

        for (size_t ci = 0; ci < animChannels.size(); ci++) {
            auto& ch = animChannels[ci];
            RndTransAnim* anim = transAnims[ch.transAnimIdx];
            RndTransformable* target = anim->Trans();
            if (!target) continue;

            // Find the node for this target
            int targetNode = -1;
            // Check mesh nodes first
            for (size_t mi = 0; mi < meshes.size(); mi++) {
                if ((Hmx::Object*)meshes[mi] == (Hmx::Object*)target ||
                    strcmp(meshes[mi]->Name(), target->Name()) == 0) {
                    targetNode = (int)mi;
                    break;
                }
            }
            // Check bone nodes
            if (targetNode < 0) {
                auto bit = boneNodeMap.find(target);
                if (bit != boneNodeMap.end()) {
                    targetNode = bit->second;
                }
            }
            if (targetNode < 0) continue;

            cgltf_animation_sampler sampler = {};
            sampler.input = &accs[animAccPairs[ci].timeAcc];
            sampler.output = &accs[animAccPairs[ci].dataAcc];
            sampler.interpolation = cgltf_interpolation_type_linear;
            gltfAnimSamplers.push_back(sampler);

            cgltf_animation_channel channel = {};
            channel.sampler = &gltfAnimSamplers.back(); // will fix up after
            channel.target_node = &gltfNodes[targetNode];
            switch (ch.type) {
            case AnimChannelInfo::kTranslation: channel.target_path = cgltf_animation_path_type_translation; break;
            case AnimChannelInfo::kRotation:    channel.target_path = cgltf_animation_path_type_rotation; break;
            case AnimChannelInfo::kScale:       channel.target_path = cgltf_animation_path_type_scale; break;
            }
            gltfAnimChannels.push_back(channel);
        }

        // Fix up sampler pointers (they were invalidated by vector reallocation)
        for (size_t i = 0; i < gltfAnimChannels.size(); i++) {
            gltfAnimChannels[i].sampler = &gltfAnimSamplers[i];
        }

        gltfAnim.samplers = gltfAnimSamplers.data();
        gltfAnim.samplers_count = gltfAnimSamplers.size();
        gltfAnim.channels = gltfAnimChannels.data();
        gltfAnim.channels_count = gltfAnimChannels.size();

        if (gltfAnim.channels_count > 0) {
            gltf.animations = &gltfAnim;
            gltf.animations_count = 1;
        }
    }

    // ---- Scene ----
    // All mesh nodes go into the root scene
    std::vector<cgltf_node*> sceneNodes;
    for (size_t mi = 0; mi < meshes.size(); mi++) {
        sceneNodes.push_back(&gltfNodes[mi]);
    }
    for (size_t bi = 0; bi < boneNodes.size(); bi++) {
        sceneNodes.push_back(&gltfNodes[meshes.size() + bi]);
    }

    cgltf_scene gltfScene = {};
    gltfScene.name = (char*)"scene";
    gltfScene.nodes = sceneNodes.data();
    gltfScene.nodes_count = sceneNodes.size();

    gltf.scenes = &gltfScene;
    gltf.scenes_count = 1;
    gltf.scene = &gltfScene;

    // ---- Write glTF JSON ----
    cgltf_options writeOpts = {};
    cgltf_result result = cgltf_write_file(&writeOpts, outputPath, &gltf);

    // ---- Cleanup ----
    for (size_t i = 0; i < textures.size(); i++)
        free(gltfImages[i].uri);
    for (size_t i = 0; i < materials.size(); i++)
        free(gltfMats[i].name);
    for (int i = 0; i < numNodes; i++)
        free(gltfNodes[i].name);
    for (size_t i = 0; i < meshes.size(); i++)
        free(gltfMeshes[i].name);
    for (size_t i = 0; i < skinInfos.size(); i++)
        free(gltfSkins[i].name);

    free(bvs);
    free(accs);
    free(gltfImages);
    free(gltfTextures);
    free(gltfMats);
    free(gltfPrims);
    free(gltfAttrs);
    free(gltfMeshes);
    free(gltfNodes);
    free(gltfSkins);

    if (result != cgltf_result_success) {
        fprintf(stderr, "GltfExporter: cgltf_write_file failed (code %d)\n", result);
        return false;
    }

    if (opts.verbose) {
        printf("GltfExporter: wrote %s (%d meshes, %d materials, %d textures",
               outputPath, (int)meshes.size(), (int)materials.size(), (int)textures.size());
        if (!transAnims.empty())
            printf(", %d animation channels", (int)animChannels.size());
        if (!skinInfos.empty())
            printf(", %d skins", (int)skinInfos.size());
        printf(")\n");
    }

    return true;
}

} // namespace GltfExporter
