#ifdef HX_NATIVE

// The 360's `depthbuffer_3d` vertex and pixel shader on the CPU.  See
// DepthBuffer3D_Native.h for where the microcode comes from; the /* nn */
// numbers below are its instruction numbers, so a line here can be checked
// against the listing.

#include "platform/DepthBuffer3D_Native.h"

#include <cmath>
#include <cstring>

namespace NativeDepthBuffer3D {

namespace {

    // The shader's own literal constants (its c247..c255), by value.
    const float kTwoPi = 6.2831855f;
    const float kPi = 3.1415927f;
    const float kInvFourPi = 0.07957747f;
    const float kInvTwoPi = 0.15915494f;
    const float kGridCols = 80.f; // c249.w
    const float kGridRows = 60.f; // c249.z
    const float kMasked = 4096.f; // c255.x: pushes a not-this-player sample behind every real one

    inline float Sat(float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
    inline float Frac(float x) { return x - std::floor(x); }
    inline float Min(float a, float b) { return a < b ? a : b; }
    inline float Max(float a, float b) { return a > b ? a : b; }

    // tfetch2D tf16, point / point, no mips; DepthBuffer3D sets the stage to
    // clamp (TheRenderState.SetTextureClamp(0, 2)).  The coordinates the
    // shader passes are already saturated.  The texel comes back as its
    // integer value: the shader takes player = 8 * frac(t / 8).
    inline float Fetch(const DepthImage &d, float u, float v) {
        int x = (int)std::floor(u * (float)d.width);
        int y = (int)std::floor(v * (float)d.height);
        if (x < 0)
            x = 0;
        else if (x > d.width - 1)
            x = d.width - 1;
        if (y < 0)
            y = 0;
        else if (y > d.height - 1)
            y = d.height - 1;
        return (float)d.texels[(size_t)y * d.pitchTexels + x];
    }

    // The four samples a cell or a vertex takes around (u, v), in the
    // shader's order: (-,-) (+,-) (-,+) (+,+).  Returns the masked depths and
    // the number of samples that are NOT the wanted player.
    struct Samples {
        float depth[4];  // t / 8: millimetres, plus player / 8
        float masked[4]; // depth + 4096 where the sample is not the wanted player
        float player0;   // player index of sample 0
        float notWanted; // 0..4
    };

    inline void Sample4(
        const Constants &c, const DepthImage &d, float u, float v, float su, float sv, Samples &s
    ) {
        const float u0 = Sat(u - su), u1 = Sat(u + su);
        const float v0 = Sat(v - sv), v1 = Sat(v + sv);
        const float t[4] = { Fetch(d, u0, v0), Fetch(d, u1, v0), Fetch(d, u0, v1),
                             Fetch(d, u1, v1) };
        const float wanted = c.depthZoom[2];
        s.notWanted = 0.f;
        for (int i = 0; i < 4; i++) {
            s.depth[i] = t[i] * 0.125f; /* 46, 47 */
            const float player = std::trunc(Frac(s.depth[i]) * 8.f); /* 48..53 */
            if (i == 0)
                s.player0 = player;
            // /* 54..59 */ c71.z == 0: everybody who is somebody; otherwise
            // exactly that player.
            const float mask = wanted == 0.f ? (player == 0.f ? 1.f : 0.f)
                                             : (player != wanted ? 1.f : 0.f);
            s.notWanted += mask;
            s.masked[i] = s.depth[i] + kMasked * mask; /* 71, 73 */
        }
    }

    // Everything the shader computes that is the same for every vertex of a
    // cell.  The shader recomputes it per vertex; ShadeTile computes it once.
    struct Cell {
        bool live;       // inside the image and not all four samples masked
        float ratio[2];  // G / N: the cell's place in the tile-wide grid
        float du, dv;    // one cell, in texture coordinates
        float count;     // masked cell samples, 0..4
        float nearest;   // min of the masked cell depths
        float nobody;    // o0.w
        float base[2];   // 4 * gap * G
        bool fisheye;    // sin(L) != 0
        float k;         // fisheye scale
    };

    struct Grid {
        float n[2];    // N = tile * (80, 60) - 1
        float invN[2];
        float span[2]; // texZoom extent
    };

    inline void MakeGrid(const Constants &c, Grid &g) {
        g.n[0] = c.tile[0] * kGridCols - 1.f; /* 13 */
        g.n[1] = c.tile[1] * kGridRows - 1.f;
        g.invN[0] = 1.f / g.n[0]; /* 14, 17 */
        g.invN[1] = 1.f / g.n[1];
        g.span[0] = c.texZoom[2] - c.texZoom[0]; /* 14 */
        g.span[1] = c.texZoom[3] - c.texZoom[1];
    }

    // (cx, cy): the cell inside the mesh, 0..79 x 0..59 for the shipped grid.
    void ShadeCell(
        const Constants &c, const DepthImage &d, const Grid &g, float cx, float cy, Cell &cell
    ) {
        // /* 18 */ the cell's place in the whole tiled grid
        const float gx = c.tile[2] * kGridCols + cx;
        const float gy = c.tile[3] * kGridRows + cy;
        cell.ratio[0] = gx * g.invN[0]; /* 20 */
        cell.ratio[1] = gy * g.invN[1];
        // /* 21..23 */ column 0 is the left of the zoom rectangle, row 0 its
        // BOTTOM (the mesh's +z is up, the image's +v is down)
        const float u = cell.ratio[0] * g.span[0] + c.texZoom[0];
        const float v = (1.f - cell.ratio[1]) * g.span[1] + c.texZoom[1];
        cell.live = false;
        // /* 24..31 */ a cell whose centre is off the image is not drawn
        if (u < 0.f || u > 1.f || v < 0.f || v > 1.f)
            return;
        cell.du = g.span[0] * g.invN[0]; /* 32 */
        cell.dv = g.span[1] * g.invN[1];
        Samples s;
        Sample4(c, d, u, v, 0.25f * cell.du, 0.25f * cell.dv, s); /* 33, 35, 38..41 */
        cell.count = s.notWanted; /* 61..63 */
        // /* 64..66 */ nobody wanted in any of the four samples: no cube
        // (unless debug_layout, which draws every cell)
        if (cell.count == 4.f && c.draw[3] == 0.f)
            return;
        cell.live = true;
        cell.nearest = Min(Min(s.masked[0], s.masked[1]), Min(s.masked[3], s.masked[2])); /* 76, 87, 89 */
        // /* 52, 59 */ with one wanted player the cell IS that player;
        // otherwise it is whoever its first sample belongs to
        const float player =
            c.depthZoom[2] == 0.f ? s.player0 : std::trunc(c.depthZoom[2]);
        // /* 70, 72, 74, 75, 105 */ grey, not palette, for a body that is
        // neither game player
        cell.nobody = (player != std::trunc(c.slots[0]) && player != std::trunc(c.slots[1])) ? 1.f : 0.f;
        cell.base[0] = c.voxel[1] * 4.f * gx; /* 82, 97 */
        cell.base[1] = c.voxel[1] * 4.f * gy;
        // /* 67, 68, 74, 75, 80, 83..96 */ the fisheye: a scale on the cell's
        // x and z that grows towards the top corners
        const float fx = c.voxel[2] * ((cell.ratio[0] - 0.5f) * -cell.ratio[1]);
        const float fz = -cell.ratio[1] * c.voxel[3];
        const float len = std::sqrt(std::fabs(fx * fx + fz * fz));
        const float half = kTwoPi * Frac(len * kInvFourPi + 0.5f) - kPi; // len / 2, wrapped
        const float full = kTwoPi * Frac(len * kInvTwoPi + 0.5f) - kPi;  // len, wrapped
        const float sinFull = std::sin(full);
        cell.fisheye = sinFull != 0.f; /* 103 */
        cell.k = cell.fisheye ? (1.f / sinFull) * std::sin(half) + 0.5f : 1.f;
    }

    // The per-vertex remainder.  `cell` must be live.
    void ShadeVertex(
        const Constants &c,
        const DepthImage &d,
        const Grid &g,
        const Cell &cell,
        const float pos[3],
        float lcx,
        float lcy,
        VertexOut &out
    ) {
        const float px = pos[0], py = pos[1], pz = pos[2];
        // /* 16, 19, 22, 23 */ the vertex's own texture coordinate
        const float fx = (c.tile[2] * kGridCols + lcx) * g.invN[0];
        const float fy = (c.tile[3] * kGridRows + lcy) * g.invN[1];
        const float uf = fx * g.span[0] + c.texZoom[0];
        const float vf = (1.f - fy) * g.span[1] + c.texZoom[1];
        Samples s;
        Sample4(c, d, uf, vf, cell.du * (1.f / 6.f), cell.dv * (1.f / 6.f), s); /* 34, 35, 42..45 */
        // /* 69, 72, 79..81 */ of the cube's eight samples, how many are the player
        const float notWanted = cell.count + s.notWanted;
        out.coverage = 1.f - notWanted * 0.125f;
        // /* 76..79, 86..91 */ the vertex's offset from its cell centre, -1..1
        // for the shipped cube, scaled by the coverage: a cube on the
        // silhouette's edge is smaller
        const float tileX = c.tile[2] * 320.f, tileZ = c.tile[3] * 240.f;
        const float offX = 159.f + (tileX - 4.f * std::floor((px + 160.f + tileX) * 0.25f + 0.25f)) + px;
        const float offZ = 119.f + (tileZ - 4.f * std::floor((pz + 120.f + tileZ) * 0.25f + 0.25f)) + pz;
        const float size = c.voxel[0] * 0.125f * (8.f - notWanted); /* 82, 84 */
        float x = (cell.base[0] + (size * offX + 1.f)) * (1.f / c.tile[0]) - 160.f; /* 91, 97, 101 */
        float z = (cell.base[1] + (size * offZ + 1.f)) * (1.f / c.tile[1]) - 120.f;
        if (cell.fisheye) { /* 102, 103 */
            x *= cell.k;
            z *= cell.k;
        }
        // /* 92..95 */ the vertex's depth: the cell's nearest wanted sample,
        // or up to 50 mm nearer where the vertex's own samples allow
        const float farthest =
            Max(Max(s.masked[0], s.masked[1]), Max(s.masked[2], s.masked[3]));
        const float depth = Min(cell.nearest, Max(farthest, cell.nearest - 50.f));
        // /* 67..69, 98..100, 102, 104..107 */ nearer = further out of the
        // grid plane (-y), on mStretchNearCamera's curve
        const float t =
            Sat(1.f - (depth * (1.f / 4096.f) + (-0.0625f - c.depthZoom[0]))
                          * (1.f / (c.depthZoom[1] - c.depthZoom[0])));
        const float y = py - 200.f * std::exp2(std::log2(t) * c.palette[2]);
        out.pos[0] = x; /* 108..111: (x, y, z, 1) . c92..c94 */
        out.pos[1] = y;
        out.pos[2] = z;
        out.cellColumn = std::trunc(Frac(lcx) * 4.f); /* 29..31 */
        out.paletteDepth = depth + 16.f * std::sqrt(std::fabs(lcx * lcx + lcy * lcy)); /* 70, 99, 104, 106, 114, 115 */
        out.front = py < -1.f ? 1.f : 0.f; /* 24 */
        out.nobody = cell.nobody;
    }

    // /* 15 */ the vertex in cell units: a cell is 4 mesh units, the grid's
    // corner cell is centred on (-159, -119).
    inline void CellCoord(const float pos[3], float &lcx, float &lcy) {
        lcx = pos[0] * 0.25f + 40.25f;
        lcy = pos[2] * 0.25f + 30.25f;
    }

    DrawStats sDrawStats;

} // namespace

bool VertexShader(const Constants &c, const DepthImage &depth, const float pos[3], VertexOut &out) {
    std::memset(&out, 0, sizeof(out));
    if (!depth.texels || depth.width <= 0 || depth.height <= 0)
        return false;
    Grid g;
    MakeGrid(c, g);
    float lcx, lcy;
    CellCoord(pos, lcx, lcy);
    Cell cell;
    ShadeCell(c, depth, g, std::trunc(lcx), std::trunc(lcy), cell); /* 17 */
    if (!cell.live)
        return false; /* 117..124: every output zero */
    ShadeVertex(c, depth, g, cell, pos, lcx, lcy, out);
    return true;
}

float PaletteCoord(const Constants &c, float cellColumn, float paletteDepth, float front) {
    // /* 2..4 */ the two side faces sit 128 and 256 further along the palette
    float t = paletteDepth + (front != 1.f ? 128.f : 0.f) + (cellColumn == 1.f ? 128.f : 0.f);
    t -= 256.f; /* 5 */
    return (t * (1.f / 4096.f) + c.palette[0]) * c.palette[1]; /* 6, 7 */
}

void PixelShader(
    const Constants &c, const VertexOut &in, const float paletteTexel[4], float rgba[4]
) {
    const bool nobody = std::fabs(in.nobody) > 0.f;
    for (int i = 0; i < 3; i++)
        rgba[i] = nobody ? 0.3f : paletteTexel[i]; /* 9 */
    const float a = nobody ? c.palette[3] : paletteTexel[3] * c.palette[3]; /* 10, 11 */
    rgba[3] = a * in.coverage; /* 12 */
}

void ShadeTile(
    const Constants &c,
    const DepthImage &depth,
    const float *positions,
    int numVerts,
    const uint16_t *faces,
    int numFaces,
    std::vector<Batch> &batches,
    TileStats *stats
) {
    if (stats)
        *stats = TileStats();
    if (!depth.texels || depth.width <= 0 || depth.height <= 0 || numVerts <= 0 || numFaces <= 0)
        return;
    Grid g;
    MakeGrid(c, g);

    // Cells are memoised on a dense grid; a vertex outside it (no shipped mesh
    // has one) is shaded on its own.
    const int kCacheW = 128, kCacheH = 128;
    enum { kUnknown = 0, kDead = 1, kLive = 2, kCounted = 3 };
    static thread_local std::vector<Cell> cells;
    static thread_local std::vector<uint8_t> cellState;
    static thread_local std::vector<VertexOut> shaded;
    static thread_local std::vector<int> vertCell; // cache slot, -1 uncached, -2 collapsed
    static thread_local std::vector<int> emitBatch, emitIndex;
    cells.resize((size_t)kCacheW * kCacheH);
    cellState.assign((size_t)kCacheW * kCacheH, kUnknown);
    shaded.resize(numVerts);
    vertCell.resize(numVerts);

    for (int i = 0; i < numVerts; i++) {
        const float *p = positions + (size_t)i * 3;
        float lcx, lcy;
        CellCoord(p, lcx, lcy);
        const float cx = std::trunc(lcx), cy = std::trunc(lcy);
        Cell uncached;
        const Cell *cell = &uncached;
        int slot = -1;
        if (cx >= 0.f && cx < (float)kCacheW && cy >= 0.f && cy < (float)kCacheH) {
            slot = (int)cy * kCacheW + (int)cx;
            if (cellState[slot] == kUnknown) {
                ShadeCell(c, depth, g, cx, cy, cells[slot]);
                cellState[slot] = cells[slot].live ? kLive : kDead;
            }
            cell = &cells[slot];
        } else {
            ShadeCell(c, depth, g, cx, cy, uncached);
        }
        if (!cell->live) {
            vertCell[i] = -2;
            continue;
        }
        vertCell[i] = slot;
        ShadeVertex(c, depth, g, *cell, p, lcx, lcy, shaded[i]);
    }

    // Four face-term variants per vertex: bit 0 = +128 for "not the front
    // face", bit 1 = +128 for "the column-1 side face".
    emitBatch.assign((size_t)numVerts * 4, -1);
    emitIndex.resize((size_t)numVerts * 4);
    int current[2] = { -1, -1 }; // open batch per kind (palette, nobody)
    bool first = true;
    for (int f = 0; f < numFaces; f++) {
        const uint16_t *tri = faces + (size_t)f * 3;
        if (tri[0] >= numVerts || tri[1] >= numVerts || tri[2] >= numVerts)
            continue;
        if (vertCell[tri[0]] == -2 || vertCell[tri[1]] == -2 || vertCell[tri[2]] == -2)
            continue;
        const VertexOut &a = shaded[tri[0]], &b = shaded[tri[1]], &d = shaded[tri[2]];
        const bool front = a.front == 1.f && b.front == 1.f && d.front == 1.f;
        const bool column1 = a.cellColumn == 1.f && b.cellColumn == 1.f && d.cellColumn == 1.f;
        const int variant = (front ? 0 : 1) | (column1 ? 2 : 0);
        const int kind = (a.nobody != 0.f || b.nobody != 0.f || d.nobody != 0.f) ? 1 : 0;
        if (current[kind] < 0 || batches[current[kind]].verts.size() + 3 > 65535) {
            batches.push_back(Batch());
            batches.back().nobody = kind == 1;
            current[kind] = (int)batches.size() - 1;
        }
        Batch &batch = batches[current[kind]];
        for (int k = 0; k < 3; k++) {
            const int v = tri[k];
            const size_t key = (size_t)v * 4 + variant;
            if (emitBatch[key] != current[kind]) {
                const VertexOut &o = shaded[v];
                ShadedVertex sv;
                sv.pos[0] = o.pos[0];
                sv.pos[1] = o.pos[1];
                sv.pos[2] = o.pos[2];
                sv.paletteCoord = PaletteCoord(
                    c, column1 ? 1.f : 0.f, o.paletteDepth, front ? 1.f : 0.f
                );
                sv.coverage = o.coverage;
                emitBatch[key] = current[kind];
                emitIndex[key] = (int)batch.verts.size();
                batch.verts.push_back(sv);
                if (stats) {
                    stats->vertices++;
                    for (int ax = 0; ax < 3; ax++) {
                        if (first || sv.pos[ax] < stats->min[ax])
                            stats->min[ax] = sv.pos[ax];
                        if (first || sv.pos[ax] > stats->max[ax])
                            stats->max[ax] = sv.pos[ax];
                    }
                    first = false;
                }
            }
            batch.indices.push_back((uint16_t)emitIndex[key]);
        }
        if (stats) {
            stats->triangles++;
            const int slot = vertCell[tri[0]];
            if (slot >= 0 && cellState[slot] == kLive) {
                cellState[slot] = kCounted;
                stats->cells++;
            }
        }
    }
}

DrawStats &LastDrawStats() { return sDrawStats; }

} // namespace NativeDepthBuffer3D

#endif
