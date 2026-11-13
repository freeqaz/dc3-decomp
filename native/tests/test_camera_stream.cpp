// The native Kinect image streams fed from a webcam (PLAN P-4), unit level.
//
// native/src/platform/CameraStream_Native.{h,cpp} turns a published camera
// picture + person mask into the Kinect's two streams (UYVY colour 640x480,
// depth|player 320x240 at the 360's 384-texel pitch) and serves them through
// the NuiImageStream* entry points LiveCameraInput calls; StreamCompose_Native
// draws StreamRenderer's picture from them.  These pin, with no engine run:
//
//   CameraConvertTest   the colour and depth conversions, against an
//                       independent float BT.601 matrix and the mask itself,
//                       including the mirror (wire = as captured, Kinect = a
//                       mirror view) and the depth pitch.
//   CameraWireTest      the protocol v3 block: a good block, "no picture", and
//                       each malformed shape refused.
//   CameraStreamTest    the NUI stream contract: nothing before a frame, a
//                       latched frame delivered once, a tracking id mapped to
//                       its skeleton slot's player index, Stop().
//   CameraComposeTest   StreamRenderer's player mode colours exactly the mask
//                       that went in; greenscreen keeps the picture there.
//   CameraMeshFilterTest the player-view box is drawn only with a stream.
//
// Sabotage-checked (see the commit): each assertion block failed against a
// deliberately broken conversion / stream / compose / filter.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "platform/CameraStream_Native.h"
#include "platform/MeshFilter.h"
#include "platform/StreamCompose_Native.h"
#include "xdk/nui/nuidetroit.h"

#include "../src/platform/NuiImageSurface_Native.h"

using namespace NativeCamera;

namespace {

// Published BT.601 full-range YCbCr -> RGB, in floating point (the same
// independent reference test_camera_yuv_decode.cpp uses).
void ReferenceRgb(uint32_t word, int which, double rgb[3]) {
    double cb = (double)(word >> 24) - 128.0;
    double y = which ? (double)(word & 0xff) : (double)((word >> 16) & 0xff);
    double cr = (double)((word >> 8) & 0xff) - 128.0;
    rgb[0] = y + 1.402 * cr;
    rgb[1] = y - 0.344136 * cb - 0.714136 * cr;
    rgb[2] = y + 1.772 * cb;
}

// A w x h picture: left half (camera's left) red, right half blue, and a
// person (ordinal 1) as a box in the left third.
struct Picture {
    int w, h;
    std::vector<uint8_t> rgb, mask;
    Picture(int w_, int h_) : w(w_), h(h_), rgb(w_ * h_ * 3), mask(w_ * h_) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t *p = &rgb[(y * w + x) * 3];
                if (x < w / 2)
                    p[0] = 220, p[1] = 30, p[2] = 30;
                else
                    p[0] = 30, p[1] = 40, p[2] = 210;
                mask[y * w + x] = (x >= w / 12 && x < w / 3 && y >= h / 4 && y < h * 3 / 4) ? 1 : 0;
            }
        }
    }
};

int SlotFor5(int trackId) { return trackId == 5 ? 2 : -1; } // tracking id 5 -> slot 2

const NUI_IMAGE_FRAME *NextFrame(HANDLE h) {
    const NUI_IMAGE_FRAME *f = nullptr;
    HRESULT hr = NuiImageStreamGetNextFrame(h, 0, &f);
    return hr == 0 ? f : nullptr;
}

const NuiImageSurface *Surface(const NUI_IMAGE_FRAME *f) {
    return reinterpret_cast<const NuiImageSurface *>(f->pFrameTexture);
}

class CameraStreamTest : public ::testing::Test {
protected:
    void SetUp() override { ResetForTest(); }
    void TearDown() override { ResetForTest(); }
};

} // namespace

// ---------------------------------------------------------------------------

TEST(CameraConvertTest, ColourIsBT601AndMirrored) {
    Picture pic(320, 240); // any size; scaled to 640x480
    std::vector<uint32_t> out(kColorH * kColorW / 2);
    ConvertColor(pic.rgb.data(), pic.w, pic.h, out.data());
    // Kinect column c shows camera column w-1-c: the camera's red LEFT half is
    // the Kinect frame's RIGHT half.
    struct Probe { int x, y; double r, g, b; } probes[] = {
        { 600, 100, 220, 30, 30 }, // Kinect right = camera left = red
        { 40, 400, 30, 40, 210 },  // Kinect left = camera right = blue
    };
    for (const Probe &p : probes) {
        double rgb[3];
        ReferenceRgb(out[p.y * (kColorW / 2) + p.x / 2], p.x & 1, rgb);
        EXPECT_NEAR(rgb[0], p.r, 3.0) << p.x << "," << p.y;
        EXPECT_NEAR(rgb[1], p.g, 3.0) << p.x << "," << p.y;
        EXPECT_NEAR(rgb[2], p.b, 3.0) << p.x << "," << p.y;
    }
}

TEST(CameraConvertTest, DepthCarriesPlayerAndDepthAtThe360Pitch) {
    Picture pic(160, 120); // half resolution: every mask pixel covers 2x2 texels
    std::vector<uint16_t> out(kDepthPitchTexels * kDepthH, 0xffff);
    int playerIndex[1] = { 3 };
    uint16_t depthMm[1] = { 2100 };
    ConvertDepth(pic.mask.data(), pic.w, pic.h, playerIndex, depthMm, 1, out.data());
    int person = 0, wrong = 0;
    for (int y = 0; y < kDepthH; y++) {
        for (int x = 0; x < kDepthPitchTexels; x++) {
            uint16_t t = out[y * kDepthPitchTexels + x];
            if (x >= kDepthW) {
                EXPECT_EQ(t, 0) << "pitch padding at " << x << "," << y;
                continue;
            }
            // mirrored, nearest: texel x reads mask column w-1-(x*w/320)
            int sx = pic.w - 1 - (x * pic.w) / kDepthW, sy = (y * pic.h) / kDepthH;
            bool in = pic.mask[sy * pic.w + sx] != 0;
            uint16_t want = in ? (uint16_t)((2100 << 3) | 3) : 0;
            if (t != want)
                wrong++;
            person += in;
        }
    }
    EXPECT_EQ(wrong, 0);
    EXPECT_GT(person, 4000);
    // the person (camera's left third) is on the Kinect frame's right
    EXPECT_EQ(out[120 * kDepthPitchTexels + 260] & 7, 3);
    EXPECT_EQ(out[120 * kDepthPitchTexels + 60] & 7, 0);
}

TEST(CameraConvertTest, AnOrdinalWithNoSlotIsNobody) {
    Picture pic(320, 240);
    std::vector<uint16_t> out(kDepthPitchTexels * kDepthH);
    int playerIndex[1] = { 0 }; // the person has no skeleton slot this frame
    uint16_t depthMm[1] = { 2100 };
    ConvertDepth(pic.mask.data(), pic.w, pic.h, playerIndex, depthMm, 1, out.data());
    for (uint16_t t : out)
        ASSERT_EQ(t, 0);
}

// ---------------------------------------------------------------------------

TEST(CameraWireTest, BlockShapes) {
    const int w = 4, h = 2;
    std::vector<uint8_t> b = { w, 0, h, 0, kWireRgb888, kWireMaskOrdinal, 0, 0 };
    for (int i = 0; i < w * h * 3; i++)
        b.push_back((uint8_t)i);
    for (int i = 0; i < w * h; i++)
        b.push_back((uint8_t)(i & 1));
    int ids[2] = { 7, 9 };
    float depth[2] = { 2.0f, 3.0f };
    Image img;
    ASSERT_EQ(ParseWireBlock(b.data(), b.data() + b.size(), ids, depth, 2, 1.5, img), nullptr);
    EXPECT_EQ(img.width, w);
    EXPECT_EQ(img.height, h);
    EXPECT_EQ(img.rgb[5], 5);
    EXPECT_EQ(img.mask[1], 1);
    EXPECT_EQ(img.numPersons, 2);
    EXPECT_EQ(img.trackIds[1], 9);
    EXPECT_FLOAT_EQ(img.depthM[1], 3.0f);

    std::vector<uint8_t> none = { 0, 0, 0, 0, 0, 0, 0, 0 };
    ASSERT_EQ(ParseWireBlock(none.data(), none.data() + none.size(), ids, depth, 2, 0, img),
              nullptr);
    EXPECT_EQ(img.width, 0);

    EXPECT_NE(ParseWireBlock(b.data(), b.data() + b.size() - 1, ids, depth, 2, 0, img), nullptr)
        << "truncated pixels accepted";
    EXPECT_NE(ParseWireBlock(b.data(), b.data() + 7, ids, depth, 2, 0, img), nullptr);
    std::vector<uint8_t> fmt = b;
    fmt[4] = 2;
    EXPECT_NE(ParseWireBlock(fmt.data(), fmt.data() + fmt.size(), ids, depth, 2, 0, img), nullptr)
        << "unknown rgb format accepted";
    std::vector<uint8_t> big = b;
    big[0] = 0xff, big[1] = 0xff;
    EXPECT_NE(ParseWireBlock(big.data(), big.data() + big.size(), ids, depth, 2, 0, img), nullptr);
}

// ---------------------------------------------------------------------------

TEST_F(CameraStreamTest, NoFrameUntilOneIsPublished) {
    HANDLE color = nullptr, depth = nullptr;
    ASSERT_EQ(NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR_YUV, NUI_IMAGE_RESOLUTION_640x480, 0, 2,
                                 nullptr, &color), 0);
    ASSERT_EQ(NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE,
                                 NUI_IMAGE_RESOLUTION_320x240, 0, 2, nullptr, &depth), 0);
    EXPECT_FALSE(Active());
    EXPECT_FALSE(Latch(SlotFor5));
    const NUI_IMAGE_FRAME *f = nullptr;
    EXPECT_NE(NuiImageStreamGetNextFrame(color, 0, &f), 0);
    EXPECT_EQ(f, nullptr);
    EXPECT_NE(NuiImageStreamGetNextFrame(depth, 0, &f), 0);
    HANDLE other = nullptr;
    EXPECT_NE(NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH, NUI_IMAGE_RESOLUTION_80x60, 0, 2,
                                 nullptr, &other), 0);
}

TEST_F(CameraStreamTest, ALatchedFrameIsDeliveredOnceWithItsPlayerIndex) {
    HANDLE color = nullptr, depth = nullptr;
    NuiImageStreamOpen(NUI_IMAGE_TYPE_COLOR_YUV, NUI_IMAGE_RESOLUTION_640x480, 0, 2, nullptr,
                       &color);
    NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE,
                       NUI_IMAGE_RESOLUTION_320x240, 0, 2, nullptr, &depth);
    Picture pic(320, 240);
    Image img;
    img.width = pic.w, img.height = pic.h, img.rgb = pic.rgb, img.mask = pic.mask;
    img.numPersons = 1, img.trackIds[0] = 5, img.depthM[0] = 2.25f;
    Publish(std::move(img));
    EXPECT_TRUE(Active());
    ASSERT_TRUE(Latch(SlotFor5));
    EXPECT_EQ(LatchedSequence(), 1u);

    const NUI_IMAGE_FRAME *df = NextFrame(depth);
    ASSERT_NE(df, nullptr);
    const NuiImageSurface *ds = Surface(df);
    ASSERT_EQ(ds->mMagic, +NuiImageSurface::kMagic);
    EXPECT_EQ(ds->mPitch, (unsigned)kDepthPitchBytes);
    const uint16_t *texels = static_cast<const uint16_t *>(ds->mBits);
    // person at camera x in [26,106) -> Kinect x in [214,294); slot 2 -> index 3
    EXPECT_EQ(texels[120 * kDepthPitchTexels + 250], (uint16_t)((2250 << 3) | 3));
    EXPECT_EQ(texels[120 * kDepthPitchTexels + 100], 0);
    EXPECT_EQ(NextFrame(depth), nullptr) << "the same frame was delivered twice";
    EXPECT_EQ(NuiImageStreamReleaseFrame(depth, df), 0);

    const NUI_IMAGE_FRAME *cf = NextFrame(color);
    ASSERT_NE(cf, nullptr);
    EXPECT_EQ(cf->dwFrameNumber, df->dwFrameNumber);
    EXPECT_EQ(Surface(cf)->mPitch, (unsigned)kColorPitchBytes);
    NuiImageStreamReleaseFrame(color, cf);

    Stop();
    EXPECT_FALSE(Active());
    EXPECT_FALSE(Latch(SlotFor5));
    EXPECT_EQ(NextFrame(depth), nullptr);
}

TEST_F(CameraStreamTest, AnUntrackedPersonIsNobody) {
    HANDLE depth = nullptr;
    NuiImageStreamOpen(NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE,
                       NUI_IMAGE_RESOLUTION_320x240, 0, 2, nullptr, &depth);
    Picture pic(320, 240);
    Image img;
    img.width = pic.w, img.height = pic.h, img.rgb = pic.rgb, img.mask = pic.mask;
    img.numPersons = 1, img.trackIds[0] = 6; // not in a skeleton slot
    Publish(std::move(img));
    ASSERT_TRUE(Latch(SlotFor5));
    const NUI_IMAGE_FRAME *df = NextFrame(depth);
    ASSERT_NE(df, nullptr);
    const uint16_t *texels = static_cast<const uint16_t *>(Surface(df)->mBits);
    EXPECT_EQ(texels[120 * kDepthPitchTexels + 250], 0);
    NuiImageStreamReleaseFrame(depth, df);
}

// ---------------------------------------------------------------------------

namespace {

// Kinect-format buffers for a picture, through the real conversions.
struct KinectFrame {
    std::vector<uint32_t> color = std::vector<uint32_t>(kColorH * kColorW / 2);
    std::vector<uint16_t> depth = std::vector<uint16_t>(kDepthPitchTexels * kDepthH);
    explicit KinectFrame(const Picture &pic, int playerIndex) {
        int idx[1] = { playerIndex };
        uint16_t mm[1] = { 2000 };
        ConvertColor(pic.rgb.data(), pic.w, pic.h, color.data());
        ConvertDepth(pic.mask.data(), pic.w, pic.h, idx, mm, 1, depth.data());
    }
};

NativeStreamComposeParams ShellParams() {
    NativeStreamComposeParams p;
    p.display = 4; // kPlayerDepthShell2, the cursor boxes' mode
    const float nobody[4] = { 0.1f, 0.1f, 0.2f, 0.5f };
    memcpy(p.nobody, nobody, sizeof nobody);
    for (int i = 0; i < 6; i++) {
        p.playerColor[i][0] = 1.0f, p.playerColor[i][1] = 0.2f * i, p.playerColor[i][2] = 0.8f;
        p.playerColor[i][3] = 1.0f;
        p.playerIndexes[i] = i + 1; // game player i is skeleton slot i
    }
    return p;
}

} // namespace

TEST(CameraComposeTest, PlayerModeColoursExactlyTheMask) {
    Picture pic(320, 240);
    KinectFrame k(pic, 2); // skeleton slot 1 -> game player 1's colour
    NativeStreamComposeParams prm = ShellParams();
    const int W = 160, H = 120;
    std::vector<uint8_t> out(W * H * 4);
    NativeStreamCompose(k.depth.data(), kDepthPitchTexels, k.color.data(), kColorW / 2, prm, W, H,
                        out.data());
    int mismatches = 0, person = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t *o = &out[(y * W + x) * 4];
            // output (x,y) samples Kinect texel (x*2, y*2), mirrored from the camera
            int sx = pic.w - 1 - (x * 2 * pic.w) / kDepthW, sy = (y * 2 * pic.h) / kDepthH;
            bool in = pic.mask[sy * pic.w + sx] != 0;
            uint8_t want[4];
            if (in) {
                want[0] = 255, want[1] = 51, want[2] = 204, want[3] = 255; // playerColor[1]
                person++;
            } else {
                want[0] = 26, want[1] = 26, want[2] = 51, want[3] = 128; // nobody
            }
            if (memcmp(o, want, 4))
                mismatches++;
        }
    }
    EXPECT_EQ(mismatches, 0);
    EXPECT_GT(person, 1000);
}

TEST(CameraComposeTest, GreenscreenKeepsThePictureOnlyWhereThePlayerIs) {
    Picture pic(320, 240);
    KinectFrame k(pic, 1);
    NativeStreamComposeParams prm = ShellParams();
    prm.display = 6; // kPlayerGreenscreen
    const int W = 320, H = 240;
    std::vector<uint8_t> out(W * H * 4);
    NativeStreamCompose(k.depth.data(), kDepthPitchTexels, k.color.data(), kColorW / 2, prm, W, H,
                        out.data());
    // inside the person (camera left third = red) -> red picture, opaque
    const uint8_t *in = &out[(120 * W + 250) * 4];
    EXPECT_NEAR(in[0], 220, 4);
    EXPECT_NEAR(in[2], 30, 4);
    EXPECT_EQ(in[3], 255);
    // outside: the nobody colour, not the (blue) picture behind
    const uint8_t *bg = &out[(120 * W + 40) * 4];
    EXPECT_EQ(bg[2], 51);
    EXPECT_EQ(bg[3], 128);
}

// ---------------------------------------------------------------------------

TEST(CameraMeshFilterTest, ThePlayerViewBoxDrawsOnlyWithAStream) {
    ResetForTest();
    const char *box[] = { "buffer_left.mesh", "buffer_right.mesh", "buffer_container_left.mesh",
                          "buffer_glass_right.mesh", "buffer_left_crown.mesh",
                          "silhouette_guy_left1" };
    for (const char *m : box)
        EXPECT_TRUE(ShouldSkipMesh(m, nullptr)) << m << " drawn with no camera";
    Image img;
    img.width = 2, img.height = 2, img.rgb.assign(12, 0), img.mask.assign(4, 0);
    Publish(std::move(img));
    for (const char *m : box)
        EXPECT_FALSE(ShouldSkipMesh(m, nullptr)) << m << " hidden with a camera stream";
    // not camera paths: unchanged either way
    EXPECT_TRUE(ShouldSkipMesh("preview.mesh", nullptr));
    EXPECT_TRUE(ShouldSkipMesh("pose_flash_p0.mesh", nullptr));
    EXPECT_TRUE(ShouldSkipMesh("grid_80by60_cube.mesh", nullptr));
    Stop();
    for (const char *m : box)
        EXPECT_TRUE(ShouldSkipMesh(m, nullptr)) << m << " drawn after the stream stopped";
    ResetForTest();
}
