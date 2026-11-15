// DepthBuffer3D's native draw: the depthbuffer_3d shader on the CPU (PLAN P-4,
// "boxyman", branch np-boxyman).
//
// On a 360 the freestyle section of a Perform song (and the loading screen)
// shows the player as a figure made of small cubes: DepthBuffer3D draws
// grid_80by60_cube.mesh with a vertex shader that reads the Kinect depth|player
// image and moves, shrinks, culls and colours each cube.  Natively that draw
// was an empty stub -- freestyle showed the visualizer tunnel with nobody in
// it.  platform/DepthBuffer3D_Native.cpp is a transcription of the shader's
// microcode; these tests pin what it computes with no engine and no GPU.
//
// Every expected number below is worked out from what the shader does (see
// the header), not captured from a run:
//
//   a cell (m, n) of the 80x60 grid looks at the image at
//       u = x0 + m/79 * (x1 - x0),  v = y0 + (1 - n/59) * (y1 - y0)
//   (row 0 is the BOTTOM of the image: the mesh's +z is up), taking four
//   samples a quarter cell either side; a vertex takes four more, a sixth of
//   a cell either side of its own place.  coverage = share of those eight
//   that are the wanted player; the cube is scaled by it; its y is
//       y - 200 * sat(1 - ((mm + player/8 - 256)/4096 - near) / (far - near)) ^ stretch
//
// Sabotage-checked (see the commit message for what each change failed).

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "platform/DepthBuffer3D_Native.h"

using namespace NativeDepthBuffer3D;

namespace {

// A 320x240 depth|player image at the native stream's 384-texel pitch.  The
// padding columns hold player 1 standing close: a reader that takes the width
// for the pitch draws them.
struct Image {
    static const int kW = 320, kH = 240, kPitch = 384;
    std::vector<uint16_t> texels;
    explicit Image(int mm = 3000, int player = 0) : texels((size_t)kPitch * kH) {
        for (int y = 0; y < kH; y++)
            for (int x = 0; x < kPitch; x++)
                texels[(size_t)y * kPitch + x] =
                    x < kW ? Texel(mm, player) : Texel(900, 1);
    }
    static uint16_t Texel(int mm, int player) { return (uint16_t)(mm << 3 | player); }
    // [x0, x1) x [y0, y1), y from the top of the image
    void Rect(int x0, int y0, int x1, int y1, int mm, int player) {
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++)
                texels[(size_t)y * kPitch + x] = Texel(mm, player);
    }
    DepthImage View() const {
        DepthImage d;
        d.texels = texels.data();
        d.pitchTexels = kPitch;
        d.width = kW;
        d.height = kH;
        return d;
    }
};

// grid_80by60_cube.mesh's layout (measured from the shipped mesh): a cell is
// 4 units, cell (m, n) has its origin at (4m - 160, 4n - 120), and holds three
// quads of four vertices -- the cube's front (y = -2), its left side and its
// top (both running back to y = 0).
struct Template {
    std::vector<float> positions;
    std::vector<uint16_t> faces;
    int NumVerts() const { return (int)positions.size() / 3; }
    int NumFaces() const { return (int)faces.size() / 3; }
};

const float kCube[12][3] = {
    { 0.5f, -2, 0 },    { 2, -2, 0 },       { 0.5f, -2, 1.5f }, { 2, -2, 1.5f }, // front
    { 0.5f, -2, 0 },    { 0.5f, -2, 1.5f }, { 0, 0, 0.5f },     { 0, 0, 2 },     // left
    { 0, 0, 2 },        { 0.5f, -2, 1.5f }, { 1.5f, 0, 2 },     { 2, -2, 1.5f }, // top
};

void CellVertex(int m, int n, int i, float pos[3]) {
    pos[0] = 4.f * m - 160.f + kCube[i][0];
    pos[1] = kCube[i][1];
    pos[2] = 4.f * n - 120.f + kCube[i][2];
}

void AddCell(Template &t, int m, int n) {
    const int base = t.NumVerts();
    for (int i = 0; i < 12; i++) {
        float p[3];
        CellVertex(m, n, i, p);
        t.positions.insert(t.positions.end(), p, p + 3);
    }
    for (int q = 0; q < 3; q++) {
        const int b = base + q * 4;
        const int idx[6] = { b, b + 1, b + 2, b + 2, b + 1, b + 3 };
        for (int k = 0; k < 6; k++)
            t.faces.push_back((uint16_t)idx[k]);
    }
}

Template GridTemplate(int cols = 80, int rows = 60) {
    Template t;
    for (int n = rows - 1; n >= 0; n--) // the shipped mesh starts at the top row
        for (int m = 0; m < cols; m++)
            AddCell(t, m, n);
    return t;
}

// What boxyman's first layer sets, less the zoom: game player 1 is NUI player
// 1, nobody is game player 2; show player 1; depth window 0.2 .. 0.8.
Constants Player1() {
    Constants c;
    c.slots[0] = 1.f;
    c.slots[1] = -1.f;
    c.depthZoom[0] = 0.2f;
    c.depthZoom[1] = 0.8f;
    c.depthZoom[2] = 1.f;
    return c;
}

struct Shaded {
    std::vector<Batch> batches;
    TileStats stats;
};

Shaded Shade(const Constants &c, const Image &img, const Template &t) {
    Shaded s;
    DepthImage d = img.View();
    ShadeTile(
        c, d, t.positions.data(), t.NumVerts(), t.faces.data(), t.NumFaces(), s.batches,
        &s.stats
    );
    return s;
}

// The shader's y for a vertex at mesh height `py` over a sample `mm` deep.
float ExpectedY(const Constants &c, float py, float mm, int player) {
    float t = 1.f - ((mm + player / 8.f - 256.f) / 4096.f - c.depthZoom[0])
                        / (c.depthZoom[1] - c.depthZoom[0]);
    t = std::min(1.f, std::max(0.f, t));
    return py - 200.f * std::pow(t, c.palette[2]);
}

} // namespace

TEST(DepthBuffer3DShader, NobodyInTheImageDrawsNothing) {
    Image img(2000, 0);
    Template grid = GridTemplate();
    Shaded s = Shade(Player1(), img, grid);
    EXPECT_EQ(s.stats.triangles, 0);
    EXPECT_EQ(s.stats.cells, 0);
    EXPECT_TRUE(s.batches.empty());

    // ... and somebody who is not the wanted player is nobody too
    Image other(2000, 2);
    EXPECT_EQ(Shade(Player1(), other, grid).stats.triangles, 0);

    float pos[3];
    CellVertex(40, 30, 0, pos);
    VertexOut out;
    out.coverage = 9.f;
    DepthImage d = img.View();
    EXPECT_FALSE(VertexShader(Player1(), d, pos, out));
    EXPECT_EQ(out.coverage, 0.f) << "a collapsed vertex's outputs are all zero";
    EXPECT_EQ(out.pos[0], 0.f);
    EXPECT_EQ(out.pos[1], 0.f);
    EXPECT_EQ(out.pos[2], 0.f);
}

// The image's top-left corner is the grid's low x, high z.  Player 1 fills
// image columns 0..81 and rows 0..61.  A cell's samples sit at
// 320 * (m +- 0.25) / 79 and 240 * (59 - n +- 0.25) / 59, so the cells with a
// sample inside are m <= 20 (left sample at 80.0) and n >= 44 (top sample at
// 60.0): 21 x 16.
TEST(DepthBuffer3DShader, APlayerInTheImagesTopLeftIsCubesAtLowXHighZ) {
    Image img;
    img.Rect(0, 0, 82, 62, 2000, 1);
    Shaded s = Shade(Player1(), img, GridTemplate());
    EXPECT_EQ(s.stats.cells, 21 * 16);
    EXPECT_EQ(s.stats.triangles, 21 * 16 * 6) << "three quads a cube";
    EXPECT_EQ(s.stats.vertices, 21 * 16 * 12);
    ASSERT_EQ(s.batches.size(), 1u);
    EXPECT_FALSE(s.batches[0].nobody);
    EXPECT_EQ((int)s.batches[0].indices.size(), s.stats.triangles * 3);
    EXPECT_NEAR(s.stats.min[0], -160.f, 1e-3f);
    EXPECT_LE(s.stats.max[0], 4.f * 20 - 160.f + 2.f + 1e-3f);
    EXPECT_GE(s.stats.min[2], 4.f * 44 - 120.f - 1e-3f);
    EXPECT_NEAR(s.stats.max[2], 118.f, 1e-3f);
    // every cube is pushed out of the grid plane by the same depth
    EXPECT_NEAR(s.stats.min[1], ExpectedY(Player1(), -2.f, 2000.f, 1), 0.01f);
    EXPECT_NEAR(s.stats.max[1], ExpectedY(Player1(), 0.f, 2000.f, 1), 0.01f);
}

// A cell wholly inside the player: the cube keeps the mesh's own x and z and
// moves along -y by its depth.
TEST(DepthBuffer3DShader, ACellInsideThePlayerIsTheMeshCubeMovedOutByItsDepth) {
    Image img(2000, 1);
    Constants c = Player1();
    DepthImage d = img.View();
    const int m = 5, n = 50;
    const float column[12] = { 1, 3, 1, 3, 1, 1, 1, 1, 1, 1, 2, 3 };
    for (int i = 0; i < 12; i++) {
        float pos[3];
        CellVertex(m, n, i, pos);
        VertexOut out;
        ASSERT_TRUE(VertexShader(c, d, pos, out)) << i;
        EXPECT_NEAR(out.pos[0], pos[0], 1e-3f) << i;
        EXPECT_NEAR(out.pos[2], pos[2], 1e-3f) << i;
        EXPECT_NEAR(out.pos[1], ExpectedY(c, pos[1], 2000.f, 1), 0.01f) << i;
        EXPECT_FLOAT_EQ(out.coverage, 1.f) << i;
        EXPECT_EQ(out.front, pos[1] < -1.f ? 1.f : 0.f) << i;
        EXPECT_EQ(out.cellColumn, column[i]) << i;
        EXPECT_EQ(out.nobody, 0.f) << i;
        const float lcx = pos[0] / 4.f + 40.25f, lcy = pos[2] / 4.f + 30.25f;
        EXPECT_NEAR(out.paletteDepth, 2000.125f + 16.f * std::sqrt(lcx * lcx + lcy * lcy), 0.01f)
            << i;
    }
}

// Cell 20 of the top-left player above straddles the silhouette's right edge
// (image column 82).  Of its four cell samples the two left ones (80.0) are
// the player and the two right ones (82.03) are not.  The vertex at the cube's
// left (cell coordinate 20.375) samples columns 81.86 and 83.21 -- one in, one
// out -- so 4 of its 8 samples are the player; the vertex at its right
// (20.75) samples 83.4 and 84.7, so 2 of 8.
TEST(DepthBuffer3DShader, ACubeOnTheSilhouettesEdgeIsSmallerAndFainter) {
    Image img;
    img.Rect(0, 0, 82, 62, 2000, 1);
    DepthImage d = img.View();
    Constants c = Player1();
    const float x0 = 4.f * 20 - 160.f, z0 = 4.f * 50 - 120.f;

    float left[3] = { x0 + 0.5f, -2.f, z0 };
    VertexOut out;
    ASSERT_TRUE(VertexShader(c, d, left, out));
    EXPECT_FLOAT_EQ(out.coverage, 0.5f);
    // the vertex is 0.5 left of and 1 below the cube's centre (x0+1, z0+1)
    EXPECT_NEAR(out.pos[0], x0 + 1.f - 0.5f * 0.5f, 1e-3f);
    EXPECT_NEAR(out.pos[2], z0 + 1.f - 0.5f * 1.f, 1e-3f);
    // its depth is still the player's, not the background's
    EXPECT_NEAR(out.pos[1], ExpectedY(c, -2.f, 2000.f, 1), 0.01f);

    float right[3] = { x0 + 2.f, -2.f, z0 };
    ASSERT_TRUE(VertexShader(c, d, right, out));
    EXPECT_FLOAT_EQ(out.coverage, 0.25f);
    EXPECT_NEAR(out.pos[0], x0 + 1.f + 0.25f * 1.f, 1e-3f);
    EXPECT_NEAR(out.pos[2], z0 + 1.f - 0.25f * 1.f, 1e-3f);

    // mScaleVoxel scales the cube about its centre, mScaleVoxelGap the pitch
    c.voxel[0] = 2.f;
    ASSERT_TRUE(VertexShader(c, d, right, out));
    EXPECT_NEAR(out.pos[0], x0 + 1.f + 2.f * 0.25f * 1.f, 1e-3f);
    c.voxel[0] = 1.f;
    c.voxel[1] = 0.5f;
    ASSERT_TRUE(VertexShader(c, d, right, out));
    EXPECT_NEAR(out.pos[0], 0.5f * 4.f * 20 + 1.f + 0.25f - 160.f, 1e-3f);
}

TEST(DepthBuffer3DShader, NearerIsFurtherOutOfTheGridPlane) {
    Constants c = Player1();
    float pos[3];
    CellVertex(40, 30, 0, pos);
    VertexOut nearOut, farOut, out;
    Image nearImg(1500, 1), farImg(2500, 1);
    DepthImage dn = nearImg.View(), df = farImg.View();
    ASSERT_TRUE(VertexShader(c, dn, pos, nearOut));
    ASSERT_TRUE(VertexShader(c, df, pos, farOut));
    EXPECT_LT(nearOut.pos[1], farOut.pos[1]);
    // 1000 mm of a (0.8 - 0.2) * 4096 mm window, over 200 units
    EXPECT_NEAR(farOut.pos[1] - nearOut.pos[1], 200.f * (1000.f / 4096.f) / 0.6f, 0.01f);
    EXPECT_NEAR(nearOut.pos[1], ExpectedY(c, -2.f, 1500.f, 1), 0.01f);

    // the window's ends: at or beyond `far` the cube stays in the grid plane,
    // at or before `near` it is 200 out
    Image beyond(3800, 1), before(900, 1);
    DepthImage d1 = beyond.View(), d2 = before.View();
    ASSERT_TRUE(VertexShader(c, d1, pos, out));
    EXPECT_NEAR(out.pos[1], -2.f, 1e-3f);
    ASSERT_TRUE(VertexShader(c, d2, pos, out));
    EXPECT_NEAR(out.pos[1], -202.f, 1e-3f);

    // mStretchNearCamera is the curve's exponent (boxyman: 0.6)
    c.palette[2] = 0.6f;
    ASSERT_TRUE(VertexShader(c, df, pos, out));
    const float t = 1.f - ((2500.125f - 256.f) / 4096.f - 0.2f) / 0.6f;
    EXPECT_NEAR(out.pos[1], -2.f - 200.f * std::pow(t, 0.6f), 0.01f);
}

// The cell's depth is the nearest of its four samples; a vertex whose own
// samples are all nearer than that comes forward with them, but by 50 mm at
// most.  Cell 10 samples image columns 39 and 41; its right-hand vertex
// (cell coordinate 10.75) samples columns 42 and 44.
TEST(DepthBuffer3DShader, AVertexComesAtMostFiftyMillimetresNearerThanItsCell) {
    Constants c = Player1();
    const float x0 = 4.f * 10 - 160.f, z0 = 4.f * 30 - 120.f;
    float pos[3] = { x0 + 2.f, -2.f, z0 };
    const float ring = 16.f * std::sqrt(10.75f * 10.75f + 30.25f * 30.25f);
    VertexOut out;

    Image step(2000, 1);
    step.Rect(42, 0, 320, 240, 1900, 1);
    DepthImage d = step.View();
    ASSERT_TRUE(VertexShader(c, d, pos, out));
    EXPECT_NEAR(out.paletteDepth - ring, 1950.125f, 0.01f) << "clamped to 50 mm";
    EXPECT_NEAR(out.pos[1], ExpectedY(c, -2.f, 1950.f, 1), 0.01f);

    Image small(2000, 1);
    small.Rect(42, 0, 320, 240, 1980, 1);
    d = small.View();
    ASSERT_TRUE(VertexShader(c, d, pos, out));
    EXPECT_NEAR(out.paletteDepth - ring, 1980.125f, 0.01f);

    // further away than the cell: the cell's depth stands
    Image back(2000, 1);
    back.Rect(42, 0, 320, 240, 2400, 1);
    d = back.View();
    ASSERT_TRUE(VertexShader(c, d, pos, out));
    EXPECT_NEAR(out.paletteDepth - ring, 2000.125f, 0.01f);
}

// Two people: player 1 in image columns 0..99, player 2 in 220..319.  Cells
// with a sample in the first are m <= 24 (25 columns), in the second m >= 55
// (25 columns); all 60 rows.
TEST(DepthBuffer3DShader, ThePlayerSelectorKeepsOnePlayer) {
    Image img;
    img.Rect(0, 0, 100, 240, 2000, 1);
    img.Rect(220, 0, 320, 240, 2200, 2);
    Template grid = GridTemplate();

    Constants c = Player1(); // slots: 1 is game player 1, nobody is game player 2
    Shaded one = Shade(c, img, grid);
    EXPECT_EQ(one.stats.cells, 25 * 60);
    EXPECT_LE(one.stats.max[0], 4.f * 24 - 160.f + 2.f + 1e-3f);
    ASSERT_EQ(one.batches.size(), 1u);
    EXPECT_FALSE(one.batches[0].nobody);

    c.depthZoom[2] = 2.f;
    c.slots[1] = 2.f;
    Shaded two = Shade(c, img, grid);
    EXPECT_EQ(two.stats.cells, 25 * 60);
    EXPECT_GE(two.stats.min[0], 4.f * 55 - 160.f - 1e-3f);
    ASSERT_EQ(two.batches.size(), 1u);
    EXPECT_FALSE(two.batches[0].nobody);
}

// The pixel shader paints a body that is neither game player flat grey.
TEST(DepthBuffer3DShader, ABodyThatIsNotAGamePlayerIsGrey) {
    Image img;
    img.Rect(0, 0, 100, 240, 2000, 1);
    img.Rect(220, 0, 320, 240, 2200, 2);
    Template grid = GridTemplate();

    Constants c = Player1();
    c.depthZoom[2] = 0.f; // everybody who is somebody
    Shaded both = Shade(c, img, grid);
    EXPECT_EQ(both.stats.cells, 50 * 60);
    ASSERT_EQ(both.batches.size(), 2u);
    int palette = 0, grey = 0;
    for (const Batch &b : both.batches) {
        (b.nobody ? grey : palette) += (int)b.indices.size() / 3;
        // the grey ones are player 2, on the right
        for (const ShadedVertex &v : b.verts) {
            if (b.nobody)
                ASSERT_GE(v.pos[0], 4.f * 55 - 160.f - 1e-3f);
            else
                ASSERT_LE(v.pos[0], 4.f * 24 - 160.f + 2.f + 1e-3f);
        }
    }
    EXPECT_EQ(palette, 25 * 60 * 6);
    EXPECT_EQ(grey, 25 * 60 * 6);

    c.slots[1] = 2.f; // now player 2 is a game player as well
    Shaded players = Shade(c, img, grid);
    ASSERT_EQ(players.batches.size(), 1u);
    EXPECT_FALSE(players.batches[0].nobody);
    EXPECT_EQ(players.stats.cells, 50 * 60);

    Constants px;
    px.palette[3] = 0.5f; // mOpacity
    VertexOut in = {};
    in.coverage = 0.75f;
    const float pal[4] = { 0.2f, 0.4f, 0.6f, 0.8f };
    float rgba[4];
    in.nobody = 1.f;
    PixelShader(px, in, pal, rgba);
    EXPECT_FLOAT_EQ(rgba[0], 0.3f);
    EXPECT_FLOAT_EQ(rgba[1], 0.3f);
    EXPECT_FLOAT_EQ(rgba[2], 0.3f);
    EXPECT_FLOAT_EQ(rgba[3], 0.5f * 0.75f);
    in.nobody = 0.f;
    PixelShader(px, in, pal, rgba);
    EXPECT_FLOAT_EQ(rgba[0], 0.2f);
    EXPECT_FLOAT_EQ(rgba[1], 0.4f);
    EXPECT_FLOAT_EQ(rgba[2], 0.6f);
    EXPECT_FLOAT_EQ(rgba[3], 0.8f * 0.5f * 0.75f);
}

// palette coordinate = ((depth term + 128 off the front face + 128 on the
// column-1 side face - 256) / 4096 + mPlayerPaletteOffset) * mPlayerPaletteScale
TEST(DepthBuffer3DShader, ThePaletteCoordinate) {
    Constants c;
    c.palette[0] = 0.25f;
    c.palette[1] = 2.f;
    EXPECT_FLOAT_EQ(PaletteCoord(c, 0.f, 2304.f, 1.f), 1.5f);
    EXPECT_FLOAT_EQ(PaletteCoord(c, 3.f, 2304.f, 1.f), 1.5f);
    EXPECT_FLOAT_EQ(PaletteCoord(c, 0.f, 2304.f, 0.f), 1.5625f);
    EXPECT_FLOAT_EQ(PaletteCoord(c, 1.f, 2304.f, 0.f), 1.625f);
    EXPECT_FLOAT_EQ(PaletteCoord(c, 1.f, 2304.f, 1.f), 1.5625f);
}

// One cube through ShadeTile: its front quad, left side and top are three
// steps along the palette (0, 256 and 128), which is what shades the cube.
TEST(DepthBuffer3DShader, TheThreeFacesOfACubeSitAtThreePlacesOnThePalette) {
    Image img(2000, 1);
    Constants c = Player1();
    c.palette[0] = 0.1f;
    c.palette[1] = 2.f;
    Template one;
    AddCell(one, 5, 50);
    Shaded s = Shade(c, img, one);
    ASSERT_EQ(s.stats.triangles, 6);
    ASSERT_EQ(s.batches.size(), 1u);
    const Batch &b = s.batches[0];
    ASSERT_EQ(b.verts.size(), 12u);
    ASSERT_EQ(b.indices.size(), 18u);
    const float face[3] = { 0.f, 256.f, 128.f };
    for (int i = 0; i < 12; i++) {
        float pos[3];
        CellVertex(5, 50, i, pos);
        const float lcx = pos[0] / 4.f + 40.25f, lcy = pos[2] / 4.f + 30.25f;
        const float term =
            2000.125f + 16.f * std::sqrt(lcx * lcx + lcy * lcy) + face[i / 4] - 256.f;
        EXPECT_NEAR(b.verts[i].paletteCoord, (term / 4096.f + 0.1f) * 2.f, 1e-4f) << i;
        EXPECT_NEAR(b.verts[i].pos[0], pos[0], 1e-3f) << i;
        EXPECT_NEAR(b.verts[i].pos[2], pos[2], 1e-3f) << i;
        EXPECT_FLOAT_EQ(b.verts[i].coverage, 1.f) << i;
    }
    const uint16_t expected[18] = { 0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7, 8, 9, 10, 10, 9, 11 };
    for (int i = 0; i < 18; i++)
        EXPECT_EQ(b.indices[i], expected[i]) << i;
}

// DepthBuffer3D zooms in on the player by showing part of the image.  With
// the top-left quarter of the image shown, the top-left player above is twice
// as big: samples at 160 * (m +- 0.25) / 79 and 120 * (59 - n +- 0.25) / 59,
// so m <= 40 and n >= 29.
TEST(DepthBuffer3DShader, TheZoomRectangleSelectsThePartOfTheImage) {
    Image img;
    img.Rect(0, 0, 82, 62, 2000, 1);
    Constants c = Player1();
    c.texZoom[2] = 0.5f;
    c.texZoom[3] = 0.5f;
    Shaded s = Shade(c, img, GridTemplate());
    EXPECT_EQ(s.stats.cells, 41 * 31);
    EXPECT_LE(s.stats.max[0], 4.f * 40 - 160.f + 2.f + 1e-3f);
    EXPECT_GE(s.stats.min[2], 4.f * 29 - 120.f - 1e-3f);
}

// A zoom rectangle that hangs off the image: the cells outside are not drawn
// (the texture clamps, and without the test the edge column would smear).
// u = m/79 - 0.5 is on the image from m = 40.
TEST(DepthBuffer3DShader, CellsOffTheImageAreNotDrawn) {
    Image img(2000, 1);
    Constants c = Player1();
    c.texZoom[0] = -0.5f;
    c.texZoom[2] = 0.5f;
    Shaded s = Shade(c, img, GridTemplate());
    EXPECT_EQ(s.stats.cells, 40 * 60);
    EXPECT_GE(s.stats.min[0], 4.f * 40 - 160.f - 1e-3f);
}

// boxyman draws with tile 0.8 x 0.8: 64 x 48 of the cells cover the image and
// are spread over the whole 320 x 240 of the mesh.
TEST(DepthBuffer3DShader, ATileBelowOneSpreadsFewerCellsOverTheWholeGrid) {
    Image img(2000, 1);
    Constants c = Player1();
    c.tile[0] = 0.8f;
    c.tile[1] = 0.8f;
    Shaded s = Shade(c, img, GridTemplate());
    EXPECT_EQ(s.stats.cells, 64 * 48);
    EXPECT_NEAR(s.stats.min[0], -160.f, 1e-3f);
    EXPECT_NEAR(s.stats.max[0], (4.f * 63 + 2.f) / 0.8f - 160.f, 1e-2f);
    EXPECT_NEAR(s.stats.min[2], -120.f, 1e-3f);
    EXPECT_NEAR(s.stats.max[2], (4.f * 47 + 2.f) / 0.8f - 120.f, 1e-2f);
}

// The renderer's indices are 16 bits.  A 100 x 60 template with the cube's
// corners shared between its faces is 42,000 vertices; each face needs its
// own palette term, so the shader output is 72,000 (half the image is shown,
// so that every cell is on it).
TEST(DepthBuffer3DShader, NoBatchOutgrowsSixteenBitIndices) {
    Image img(2000, 1);
    Constants c = Player1();
    c.texZoom[2] = 0.5f;
    Template grid;
    const float corner[7][3] = { { 0.5f, -2, 0 }, { 2, -2, 0 },   { 0.5f, -2, 1.5f },
                                 { 2, -2, 1.5f }, { 0, 0, 0.5f }, { 0, 0, 2 },
                                 { 1.5f, 0, 2 } };
    const int quads[3][4] = { { 0, 1, 2, 3 }, { 0, 2, 4, 5 }, { 5, 2, 6, 3 } };
    for (int n = 0; n < 60; n++) {
        for (int m = 0; m < 100; m++) {
            const int base = grid.NumVerts();
            for (int i = 0; i < 7; i++) {
                grid.positions.push_back(4.f * m - 160.f + corner[i][0]);
                grid.positions.push_back(corner[i][1]);
                grid.positions.push_back(4.f * n - 120.f + corner[i][2]);
            }
            for (int q = 0; q < 3; q++) {
                const int idx[6] = { quads[q][0], quads[q][1], quads[q][2],
                                     quads[q][2], quads[q][1], quads[q][3] };
                for (int k = 0; k < 6; k++)
                    grid.faces.push_back((uint16_t)(base + idx[k]));
            }
        }
    }
    ASSERT_LE(grid.NumVerts(), 65536) << "the template itself must be indexable";
    Shaded s = Shade(c, img, grid);
    EXPECT_EQ(s.stats.cells, 100 * 60);
    EXPECT_EQ(s.stats.triangles, 100 * 60 * 6);
    // a shared corner is emitted once per face; a split inside a quad emits
    // the quad's shared edge again in the new batch
    EXPECT_GE(s.stats.vertices, 100 * 60 * 12);
    EXPECT_LE(s.stats.vertices, 100 * 60 * 12 + 2 * ((int)s.batches.size() - 1));
    ASSERT_GE(s.batches.size(), 2u) << "72,000 vertices cannot be one batch";
    size_t triangles = 0, vertices = 0;
    for (const Batch &b : s.batches) {
        EXPECT_LE(b.verts.size(), 65535u);
        EXPECT_FALSE(b.verts.empty());
        for (uint16_t i : b.indices)
            ASSERT_LT((size_t)i, b.verts.size());
        triangles += b.indices.size() / 3;
        vertices += b.verts.size();
    }
    EXPECT_EQ((int)triangles, s.stats.triangles);
    EXPECT_EQ((int)vertices, s.stats.vertices);
}
