#ifdef HX_NATIVE

// DepthBuffer3D's mesh draw on the native renderer (PLAN P-4, "boxyman").
//
// The image draws grid_80by60_cube.mesh with the depthbuffer_3d shader bound;
// the shader moves, scales, culls and colours every cube.  The native
// renderer draws meshes with one standard pipeline and has no per-draw vertex
// program, so the vertex shader is run here on the CPU
// (DepthBuffer3D_Native.cpp) and what it produces is drawn as ordinary
// meshes:
//
//   position      the shader's object-space position; the mesh is placed at
//                 the DepthBuffer3D's world transform, as the image's is
//   colour        the pixel shader samples a 1D palette at a coordinate that
//                 is linear in the vertex outputs, so the palette is the
//                 material's diffuse texture and that coordinate is the
//                 vertex's u (wrap addressing, as the image sets for stage 10)
//   alpha         palette alpha * mOpacity * coverage: material alpha is
//                 mOpacity, vertex alpha the coverage, on a prelit material
//                 (the standard shader multiplies exactly those three)
//   render state  blend / z / cull / alpha-test of the grid mesh's own
//                 material, which is what the image's draw uses
//
// A cell that belongs to neither game player is flat 0.3 grey in the pixel
// shader; those triangles go to a second, untextured mesh.

#include "platform/DepthBuffer3D_Native.h"

#include "gfx/VertexFormats.h"
#include "math/Mtx.h"
#include "obj/Object.h"
#include "rndobj/Mat.h"
#include "rndobj/Mesh.h"
#include "rndobj/Tex.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace NativeDepthBuffer3D;

namespace {

// The grid mesh's geometry, read once.  A milo mesh that is not mutable keeps
// only the 360's packed vertex buffer natively (Verts() is empty).
struct Template {
    const void *source = nullptr; // the buffer it was read from
    int numVerts = 0;
    int numFaces = 0;
    std::vector<float> positions;
    std::vector<uint16_t> faces;
};

const Template *GridTemplate(RndMesh *mesh) {
    static std::map<const RndMesh *, Template> sTemplates;
    RndMesh *geom = mesh->GetGeomOwner() ? mesh->GetGeomOwner() : mesh;
    const int numFaces = geom->NumFaces();
    const int plain = geom->Verts().size();
    const int packed = (int)geom->NumCompressedVerts();
    const unsigned char *packedData = geom->CompressedVerts();
    const int numVerts = plain > 0 ? plain : (packedData ? packed : 0);
    const void *source = plain > 0 ? (const void *)&geom->Verts()[0] : (const void *)packedData;
    if (numVerts <= 0 || numFaces <= 0)
        return nullptr;
    Template &t = sTemplates[geom];
    if (t.source == source && t.numVerts == numVerts && t.numFaces == numFaces)
        return &t;
    t.source = source;
    t.numVerts = numVerts;
    t.numFaces = numFaces;
    t.positions.resize((size_t)numVerts * 3);
    if (plain > 0) {
        for (int i = 0; i < numVerts; i++) {
            const Vector3 &p = geom->Verts()[i].pos;
            t.positions[i * 3 + 0] = p.x;
            t.positions[i * 3 + 1] = p.y;
            t.positions[i * 3 + 2] = p.z;
        }
    } else {
        std::vector<GpuVertex> unpacked(numVerts);
        const int n = VertexFormats::UnpackCompressedVertices(
            packedData, numVerts, unpacked.data(), numVerts
        );
        for (int i = 0; i < numVerts; i++) {
            for (int k = 0; k < 3; k++)
                t.positions[i * 3 + k] = i < n ? unpacked[i].pos[k] : 0.f;
        }
    }
    t.faces.resize((size_t)numFaces * 3);
    std::vector<RndMesh::Face> &faces = geom->Faces();
    for (int i = 0; i < numFaces; i++) {
        t.faces[i * 3 + 0] = faces[i].v1;
        t.faces[i * 3 + 1] = faces[i].v2;
        t.faces[i * 3 + 2] = faces[i].v3;
    }
    return &t;
}

// What one DepthBuffer3D draws with.  Its own meshes: several DepthBuffer3D
// draw in one frame (boxyman has two layers per player), and a mesh's GPU
// copy is per mesh.
struct Owned {
    std::vector<RndMesh *> meshes;
    size_t used = 0;
    RndMat *paletteMat = nullptr;
    RndMat *nobodyMat = nullptr;
};

std::map<const void *, Owned> sOwned;

RndMat *StateMat(RndMat *&slot, RndMat *from, RndTex *diffuse, float grey, float alpha) {
    if (!slot)
        slot = Hmx::Object::New<RndMat>();
    RndMat *mat = slot;
    if (from) {
        mat->SetBlend(from->GetBlend());
        mat->SetZMode(from->GetZMode());
        mat->SetCull(from->GetCull());
        mat->SetAlphaCut(from->GetAlphaCut());
        mat->SetAlphaThreshold(from->GetAlphaThreshold());
        mat->SetAlphaWrite(from->GetAlphaWrite());
    }
    mat->SetPreLit(true);  // the shader has no lighting
    mat->SetUseEnv(false);
    mat->SetTexGen(kTexGenNone);
    mat->SetTexWrap(kTexWrapRepeat); // SetTextureClamp(10, 0): wrap
    mat->SetDiffuseTex(diffuse);
    mat->SetColor(grey, grey, grey);
    mat->SetAlpha(alpha);
    return mat;
}

void FillMesh(RndMesh *mesh, const Batch &batch) {
    // resize(0) first: VertVector::resize copies the old vertices into the
    // new block otherwise.
    mesh->Verts().resize(0);
    mesh->Verts().resize((int)batch.verts.size());
    RndMesh::VertVector &verts = mesh->Verts();
    for (size_t i = 0; i < batch.verts.size(); i++) {
        const ShadedVertex &sv = batch.verts[i];
        RndMesh::Vert &v = verts[(int)i];
        v.pos.Set(sv.pos[0], sv.pos[1], sv.pos[2]);
        v.tex.Set(sv.paletteCoord, 0.5f);
        v.color.Set(1.f, 1.f, 1.f, sv.coverage);
    }
    std::vector<RndMesh::Face> &faces = mesh->Faces();
    faces.resize(batch.indices.size() / 3);
    for (size_t f = 0; f < faces.size(); f++)
        faces[f].Set(batch.indices[f * 3], batch.indices[f * 3 + 1], batch.indices[f * 3 + 2]);
    // 0x1f = the vertex streams.  Not 0x20 (faces): RndMesh::OnSync then
    // splits the faces into bone-palette patches, a greedy reorder that is
    // quadratic in the face count and took ~80 ms per frame for one player's
    // ~3,500 triangles; the native renderer does not draw by patch, and any
    // Sync re-uploads the faces along with the vertices.
    mesh->Sync(0x1f);
}

} // namespace

void NativeDepthBuffer3DDrawTile(
    const void *owner,
    RndMesh *gridMesh,
    const Transform &worldXfm,
    const Constants &c,
    RndTex *depthTex,
    RndTex *palette
) {
    if (!gridMesh || !depthTex)
        return;
    const Template *grid = GridTemplate(gridMesh);
    if (!grid)
        return;

    // The vertex texture.  The native camera stream's texture hands out its
    // 384-pitch depth|player buffer (CameraStream_Native.cpp, StreamTex); a
    // texture with no CPU texels (a render target) cannot be sampled here.
    void *bits = nullptr;
    if (depthTex->Bpp() != 16 || !depthTex->TexelsLock(bits) || !bits)
        return;
    DepthImage image;
    image.texels = (const uint16_t *)bits;
    image.pitchTexels = (int)(depthTex->TexelsPitch() / 2);
    image.width = depthTex->Width();
    image.height = depthTex->Height();
    static thread_local std::vector<Batch> batches;
    batches.clear();
    TileStats stats;
    if (image.pitchTexels >= image.width) {
        ShadeTile(
            c, image, grid->positions.data(), grid->numVerts, grid->faces.data(),
            grid->numFaces, batches, &stats
        );
    }
    depthTex->TexelsUnlock();

    DrawStats &ds = LastDrawStats();
    ds.draws++;
    Owned &own = sOwned[owner];
    if (c.tile[2] == 0.f && c.tile[3] == 0.f)
        own.used = 0; // first tile of this DepthBuffer3D's draw
    if (stats.triangles == 0)
        return;
    ds.drawsWithCubes++;
    ds.frame = ds.draws;
    ds.last = stats;
    const float *regs[7] = { c.palette, c.slots, c.draw, c.tile, c.voxel, c.texZoom, c.depthZoom };
    for (int r = 0; r < 7; r++) {
        for (int k = 0; k < 4; k++)
            ds.lastConstants[r][k] = regs[r][k];
    }

    RndMat *gridMat = gridMesh->Mat();
    for (size_t b = 0; b < batches.size(); b++) {
        const Batch &batch = batches[b];
        if (batch.indices.empty())
            continue;
        if (own.used == own.meshes.size())
            own.meshes.push_back(Hmx::Object::New<RndMesh>());
        RndMesh *mesh = own.meshes[own.used++];
        FillMesh(mesh, batch);
        mesh->SetMat(
            batch.nobody
                ? StateMat(own.nobodyMat, gridMat, nullptr, 0.3f, c.palette[3])
                : StateMat(own.paletteMat, gridMat, palette, 1.f, c.palette[3])
        );
        mesh->SetLocalXfm(worldXfm);
        mesh->SetShowing(true);
        mesh->DrawShowing();
    }
}

void NativeDepthBuffer3DRelease(const void *owner) {
    std::map<const void *, Owned>::iterator it = sOwned.find(owner);
    if (it == sOwned.end())
        return;
    for (size_t i = 0; i < it->second.meshes.size(); i++)
        delete it->second.meshes[i];
    delete it->second.paletteMat;
    delete it->second.nobodyMat;
    sOwned.erase(it);
}

// GET /api/camera's "boxyman" member (StreamCompose_Native.cpp, Dc3CameraJson).
std::string Dc3BoxymanJson() {
    const DrawStats &ds = LastDrawStats();
    char buf[1024];
    snprintf(
        buf, sizeof buf,
        "{\"draws\":%u,\"drawsWithCubes\":%u,\"lastCubeDraw\":%u,\"cells\":%d,"
        "\"triangles\":%d,\"vertices\":%d,\"min\":[%.3f,%.3f,%.3f],\"max\":[%.3f,%.3f,%.3f],"
        "\"texZoom\":[%.4f,%.4f,%.4f,%.4f],\"depthZoom\":[%.4f,%.4f,%.1f],"
        "\"slots\":[%.1f,%.1f],\"opacity\":%.3f}",
        ds.draws, ds.drawsWithCubes, ds.frame, ds.last.cells, ds.last.triangles,
        ds.last.vertices, ds.last.min[0], ds.last.min[1], ds.last.min[2], ds.last.max[0],
        ds.last.max[1], ds.last.max[2], ds.lastConstants[5][0], ds.lastConstants[5][1],
        ds.lastConstants[5][2], ds.lastConstants[5][3], ds.lastConstants[6][0],
        ds.lastConstants[6][1], ds.lastConstants[6][2], ds.lastConstants[1][0],
        ds.lastConstants[1][1], ds.lastConstants[0][3]
    );
    return buf;
}

#endif
