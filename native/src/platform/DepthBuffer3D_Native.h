#pragma once
#ifdef HX_NATIVE

// The 360's `depthbuffer_3d` shader on the CPU (PLAN P-4, "boxyman").
//
// DepthBuffer3D (src/system/gesture/DepthBuffer3D.cpp) draws the player as a
// grid of small cubes: it binds the Kinect depth|player texture as a VERTEX
// texture, sets eight constant registers, and draws grid_80by60_cube.mesh
// (80x60 cells, three quads each) with ShaderType 0x1d.  All of the picture is
// made in that shader -- which cells exist, how big each cube is, where it
// sits, what colour it is -- and the native renderer has no counterpart, so
// the native draw was an empty stub and freestyle showed the visualizer
// tunnel with no player in it.
//
// This is a transcription of that shader's Xenos microcode, not a look-alike.
// Source: the `depthbuffer_3d` entry of the game's xbox_shaders archive (one
// permutation), dumped with milo-native-engine/tools/rb3-dc3-parity/xobx.py
// and disassembled with xenia-gpu-shader-compiler --shader_output_type=ucode;
// the instruction numbers in the .cpp are that listing's.  What each register
// holds comes from DepthBuffer3D::DrawShowing, which sets them.
//
// Nothing here touches the renderer: VertexShader()/PixelShader() are pure
// functions of the constants, the depth image and one mesh vertex, so the
// unit tests (native/tests/test_depthbuffer3d.cpp) run with no engine.
// ShadeTile() runs the vertex shader over a whole template mesh and returns
// the triangles that survive; DepthBuffer3DDraw_Native.cpp hands those to the
// renderer.

#include <cstdint>
#include <vector>

namespace NativeDepthBuffer3D {

// The constant registers, named by the VShaderConstant / PShaderConstant
// index DepthBuffer3D::DrawShowing passes (the microcode's c65..c71).
struct Constants {
    // 0x41 (c65): mPlayerPaletteOffset, mPlayerPaletteScale, mStretchNearCamera, mOpacity
    float palette[4] = { 0.f, 1.f, 1.f, 1.f };
    // 0x42 (c66): NUI player index of game player 1, of game player 2 (-1 =
    // no skeleton), palette animation x2 (the shipped shader reads only .xy)
    float slots[4] = { -1.f, -1.f, 0.f, 0.f };
    // 0x43 (c67): mDrawPlayer1, mDrawPlayer2, mDrawNonPlayers, mDebugLayout
    // (the shipped shader reads only .w)
    float draw[4] = { 1.f, 1.f, 1.f, 0.f };
    // 0x44 (c68): mTile.x, mTile.y, tile column, tile row (DrawMesh's loop)
    float tile[4] = { 1.f, 1.f, 0.f, 0.f };
    // 0x45 (c69): mScaleVoxel, mScaleVoxelGap, mFishEyeX, mFishEyeY
    float voxel[4] = { 1.f, 1.f, 0.f, 0.f };
    // 0x46 (c70): the part of the depth image to show, u0 v0 u1 v1
    float texZoom[4] = { 0.f, 0.f, 1.f, 1.f };
    // 0x47 (c71): the depth range to show, near far in (mm - 256) / 4096; the
    // one player index to keep (0 = keep everybody who is somebody); unused
    float depthZoom[4] = { 0.f, 1.f, 0.f, 0.f };
};

// The vertex texture: `width` x `height` texels of depth_mm << 3 | player,
// rows `pitchTexels` apart, top row first (CameraStream_Native.h).
struct DepthImage {
    const uint16_t *texels = nullptr;
    int pitchTexels = 0;
    int width = 0;
    int height = 0;
};

// What the vertex shader writes.
struct VertexOut {
    float pos[3];       // object space: the vector the shader multiplies by the world matrix
    float coverage;     // o1.x  share of this cube's 8 depth samples that are the player
    float cellColumn;   // o1.y  the vertex's column inside its 4-unit cell, 0..3
    float paletteDepth; // o1.z  depth + 16 * distance from the grid corner, in cells
    float front;        // o1.w  1 on the cube's front face (mesh y < -1)
    float nobody;       // o0.w  1 when the cell's player is neither game player
};

// The vertex shader for one mesh vertex (object-space position, the mesh's own
// x y z).  False = the shader collapsed the vertex to the origin (its cell is
// outside the depth image, or holds nobody who is to be drawn); `out` is then
// all zero, as the shader's outputs are.
bool VertexShader(const Constants &c, const DepthImage &depth, const float pos[3], VertexOut &out);

// The pixel shader's palette coordinate for interpolated o1.yzw (the palette
// is sampled with wrap addressing, so the value is not reduced).
float PaletteCoord(const Constants &c, float cellColumn, float paletteDepth, float front);

// The pixel shader.  `paletteTexel` is the 1D palette at PaletteCoord().
void PixelShader(
    const Constants &c, const VertexOut &interpolated, const float paletteTexel[4], float rgba[4]
);

// ---- a whole draw call -----------------------------------------------------

// One vertex ready for a fixed-function style draw: the shader's position and,
// in place of its interpolators, the two numbers the pixel shader needs.
struct ShadedVertex {
    float pos[3];
    float paletteCoord; // PaletteCoord() with this vertex's face terms
    float coverage;
};

// Triangles of one kind: palette-coloured (nobody == false) or the pixel
// shader's flat 0.3 grey for a cell that belongs to neither game player.
// No batch holds more than 65535 vertices (the renderer's 16-bit indices).
struct Batch {
    bool nobody = false;
    std::vector<ShadedVertex> verts;
    std::vector<uint16_t> indices; // 3 per triangle
};

struct TileStats {
    int cells = 0;      // cells with at least one triangle drawn
    int triangles = 0;  // triangles drawn
    int vertices = 0;   // vertices emitted over all batches
    float min[3] = { 0.f, 0.f, 0.f }; // bounds of the emitted positions
    float max[3] = { 0.f, 0.f, 0.f };
};

// Run the vertex shader over a template mesh for the tile in c.tile[2..3] and
// append what survives to `batches`.  positions: numVerts * 3 floats; faces:
// numFaces * 3 indices.  A triangle is dropped when any of its vertices was
// collapsed (the shader collapses whole cells, so that is the whole cube).
//
// The two pixel-shader terms that are tests on interpolated values --
// `front != 1` and `cellColumn == 1` -- are constant over a triangle whose
// vertices agree and false in the interior of one whose vertices do not, so
// each triangle carries them as a constant added to its vertices'
// paletteCoord (a vertex shared by triangles with different terms is emitted
// once per term).
void ShadeTile(
    const Constants &c,
    const DepthImage &depth,
    const float *positions,
    int numVerts,
    const uint16_t *faces,
    int numFaces,
    std::vector<Batch> &batches,
    TileStats *stats
);

// ---- test seam -------------------------------------------------------------

// What the last DepthBuffer3D draws produced (DepthBuffer3DDraw_Native.cpp
// fills it; GET /api/camera reports it).
struct DrawStats {
    unsigned draws = 0;         // DepthBuffer3D tile draws that reached the shader
    unsigned drawsWithCubes = 0; // ... that put at least one triangle on screen
    unsigned frame = 0;         // counter value of the last draw with cubes
    TileStats last;             // the last draw that had cubes
    float lastConstants[7][4] = {}; // 0x41..0x47 of that draw
};
DrawStats &LastDrawStats();

} // namespace NativeDepthBuffer3D

// ---- the renderer side (DepthBuffer3DDraw_Native.cpp) -----------------------

class RndMesh;
class RndTex;
class Transform;

// One iteration of DepthBuffer3D::DrawMesh's tile loop: where the image calls
// mMesh->DrawShowing() with the depthbuffer_3d shader bound.  Runs the shader
// over `gridMesh`'s vertices and draws the surviving cubes at `worldXfm`
// through the standard pipeline, as a prelit mesh textured with `palette`.
// `owner` identifies the DepthBuffer3D (it keeps its generated meshes between
// frames).  Draws nothing when `depthTex` has no CPU texels.
void NativeDepthBuffer3DDrawTile(
    const void *owner,
    RndMesh *gridMesh,
    const Transform &worldXfm,
    const NativeDepthBuffer3D::Constants &c,
    RndTex *depthTex,
    RndTex *palette
);
// The DepthBuffer3D is being destroyed: free what was generated for it.
void NativeDepthBuffer3DRelease(const void *owner);

#endif
