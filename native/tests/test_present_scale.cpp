// PLAN H-18: the frame is fitted into the window with its aspect kept.
//
// FitPresentRect (engine gfx/PresentScale) picks the rectangle a windowed
// frame (the 1280x720 Rnd virtual resolution) is drawn into inside a window
// of any size: full width with bars above and below for a window taller than
// 16:9, full height with bars left and right for a wider one, centred.
// PresentRectIsIntegerScale picks nearest sampling for an exact 1x/2x/...
// scale (a HiDPI 2560x1440 framebuffer) and linear for anything else.
// The windowed end-to-end checks are NativeWindowTest in test_native_exit.cpp.

#include <gtest/gtest.h>

#include "gfx/PresentScale.h"

namespace {

PresentRect R(int x, int y, int w, int h) {
    PresentRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

std::string Str(const PresentRect &r) {
    return std::to_string(r.w) + "x" + std::to_string(r.h) + "+" + std::to_string(r.x) + "+" +
           std::to_string(r.y);
}

#define EXPECT_RECT(actual, expected) EXPECT_EQ(Str(actual), Str(expected))

TEST(PresentScale, SameSizeIsTheWholeSurface) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1280, 720), R(0, 0, 1280, 720));
}

TEST(PresentScale, SameAspectFillsTheSurface) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1920, 1080), R(0, 0, 1920, 1080));
    EXPECT_RECT(FitPresentRect(1280, 720, 640, 360), R(0, 0, 640, 360));
}

// Taller than 16:9 (a 16:10 monitor): full width, bars above and below.
TEST(PresentScale, ATallerSurfaceIsLetterboxed) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1600, 1000), R(0, 50, 1600, 900));
    EXPECT_RECT(FitPresentRect(1280, 720, 1920, 1200), R(0, 60, 1920, 1080));
    EXPECT_RECT(FitPresentRect(1280, 720, 1000, 900), R(0, 168, 1000, 563));
}

// Wider than 16:9 (an ultrawide, a short window): full height, bars left and right.
TEST(PresentScale, AWiderSurfaceIsPillarboxed) {
    EXPECT_RECT(FitPresentRect(1280, 720, 1800, 700), R(278, 0, 1244, 700));
    EXPECT_RECT(FitPresentRect(1280, 720, 3440, 1440), R(440, 0, 2560, 1440));
}

// The rectangle never leaves the surface and the leftover splits evenly (the
// odd pixel to the right/bottom), for every size up to a 4k window.
TEST(PresentScale, TheRectangleIsInsideTheSurfaceAndCentred) {
    for (int w = 1; w <= 3840; w += 37)
        for (int h = 1; h <= 2160; h += 41) {
            PresentRect r = FitPresentRect(1280, 720, w, h);
            ASSERT_GE(r.x, 0) << w << "x" << h;
            ASSERT_GE(r.y, 0) << w << "x" << h;
            ASSERT_LE(r.x + r.w, w) << w << "x" << h;
            ASSERT_LE(r.y + r.h, h) << w << "x" << h;
            ASSERT_TRUE(r.w == w || r.h == h) << "touches neither pair of edges: " << w << "x" << h;
            ASSERT_LE(w - r.w - 2 * r.x, 1) << w << "x" << h;
            ASSERT_LE(h - r.h - 2 * r.y, 1) << w << "x" << h;
            ASSERT_GE(w - r.w - 2 * r.x, 0) << w << "x" << h;
            ASSERT_GE(h - r.h - 2 * r.y, 0) << w << "x" << h;
        }
}

// No stretching: the picture keeps the frame's aspect within a pixel.
TEST(PresentScale, TheAspectIsKept) {
    for (int w = 64; w <= 3840; w += 53)
        for (int h = 64; h <= 2160; h += 47) {
            PresentRect r = FitPresentRect(1280, 720, w, h);
            // |r.w/r.h - 16/9| within one pixel of rounding on the short side.
            long long err = (long long)r.w * 720 - (long long)r.h * 1280;
            if (err < 0) err = -err;
            ASSERT_LE(err, 1280) << w << "x" << h << " -> " << Str(r);
        }
}

TEST(PresentScale, NoSizeIsAnEmptyRectangle) {
    EXPECT_RECT(FitPresentRect(1280, 720, 0, 720), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(1280, 720, 1280, 0), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(0, 720, 1280, 720), R(0, 0, 0, 0));
    EXPECT_RECT(FitPresentRect(1280, 720, -5, 720), R(0, 0, 0, 0));
}

// Huge sizes do not overflow (64-bit products).
TEST(PresentScale, LargeSizesDoNotOverflow) {
    EXPECT_RECT(FitPresentRect(16384, 9216, 32768, 18432), R(0, 0, 32768, 18432));
    EXPECT_RECT(FitPresentRect(65536, 36864, 32768, 20000), R(0, 784, 32768, 18432));
}

TEST(PresentScale, WholeNumberScalesAreNearest) {
    EXPECT_TRUE(PresentRectIsIntegerScale(R(0, 0, 1280, 720), 1280, 720));
    EXPECT_TRUE(PresentRectIsIntegerScale(R(0, 0, 2560, 1440), 1280, 720));
    EXPECT_TRUE(PresentRectIsIntegerScale(FitPresentRect(1280, 720, 3840, 2400), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 1920, 1080), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 640, 360), 1280, 720));
    // Each axis a whole multiple, but not the same one: a stretch, not a scale.
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 2560, 720), 1280, 720));
    EXPECT_FALSE(PresentRectIsIntegerScale(R(0, 0, 0, 0), 1280, 720));
}

} // namespace
