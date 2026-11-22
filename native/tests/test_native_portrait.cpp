// A 9:16 frame during a song held upright (np-portrait2, dc3 PLAN W-37 step 2).
//
// Two pure rules: the engine's frame sizes for a portrait logical shape
// (ResolveFrameSizesWithCap: the row cap and the floor measure the SHORT
// side), and the HUD's remap into a portrait frame
// (NativePortraitHudRemapFor, native/src/platform/NativePortrait.h).
#include <gtest/gtest.h>

#include "gfx/GpuDevice.h"
#include "../src/platform/NativePortrait.h"

namespace {
GpuDeviceDesc Following(int lw, int lh) {
    GpuDeviceDesc d;
    d.outputFollowsTarget = true;
    d.logicalWidth = lw;
    d.logicalHeight = lh;
    return d;
}
}

// A Pixel 7's 9:16 box (1081x1922 device px) under the web's 1080-row cap is
// a 1080x1920 frame -- the same pixel count as the landscape 1920x1080 --
// not a 608x1080 one (the cap measured rows, whatever the shape).
TEST(PortraitFrame, TheCapMeasuresTheShortSide) {
    const FrameSizes p = ResolveFrameSizesWithCap(Following(720, 1280), false, true, 1081, 1922, 1080);
    EXPECT_EQ(p.outputW, 1080);
    EXPECT_EQ(p.outputH, 1920);
    // Landscape is exactly as before.
    const FrameSizes l = ResolveFrameSizesWithCap(Following(1280, 720), false, true, 2202, 1081, 1080);
    EXPECT_EQ(l.outputW, 1920);
    EXPECT_EQ(l.outputH, 1080);
    // A box under the cap is fitted, not capped: 9:16 inside 900x2000.
    const FrameSizes f = ResolveFrameSizesWithCap(Following(720, 1280), false, true, 900, 2000, 1080);
    EXPECT_EQ(f.outputW, 900);
    EXPECT_EQ(f.outputH, 1600);
}

// The floor (kOutputMinRows) also measures the short side: 360x640 portrait.
TEST(PortraitFrame, TheFloorMeasuresTheShortSide) {
    const FrameSizes p = ResolveFrameSizesWithCap(Following(720, 1280), false, true, 100, 200, 1080);
    EXPECT_EQ(p.outputW, kOutputMinRows);
    EXPECT_EQ(p.outputH, 640);
    const FrameSizes l = ResolveFrameSizesWithCap(Following(1280, 720), false, true, 200, 100, 1080);
    EXPECT_EQ(l.outputW, 640);
    EXPECT_EQ(l.outputH, kOutputMinRows);
    // A cap below the floor: the floor wins, as before (cap, then floor).
    const FrameSizes c = ResolveFrameSizesWithCap(Following(1280, 720), false, true, 1920, 1080, 200);
    EXPECT_EQ(c.outputH, kOutputMinRows);
}

// The remap is the identity for a 16:9 frame at scale 1: the rule cannot
// move a landscape HUD.
TEST(PortraitHud, ALandscapeFrameAtScaleOneIsTheIdentity) {
    float kx, tx, ky, ty;
    NativePortraitHudRemapFor(16.0f / 9.0f, 1.0f, 0.5f, kx, tx, ky, ty);
    EXPECT_FLOAT_EQ(kx, 1.0f);
    EXPECT_FLOAT_EQ(tx, 0.0f);
    EXPECT_FLOAT_EQ(ky, 1.0f);
    EXPECT_FLOAT_EQ(ty, 0.0f);
}

// In a 9:16 frame at 1.8 frame-widths: the HUD's right edge is the frame's
// right edge, its left edge 2.6 NDC off the left, it keeps the 16:9 shape
// (no stretch), and it is centred vertically.
TEST(PortraitHud, RightAnchoredUnstretchedCentred) {
    const float aspect = 9.0f / 16.0f, scale = 1.8f;
    float kx, tx, ky, ty;
    NativePortraitHudRemapFor(aspect, scale, 0.5f, kx, tx, ky, ty);
    EXPECT_FLOAT_EQ(kx * 1.0f + tx, 1.0f);    // right edge
    EXPECT_NEAR(kx * -1.0f + tx, -2.6f, 1e-5); // left edge, off screen
    // Shape: NDC spans scale back to pixels as width * kx / 2 and height *
    // ky / 2; the HUD must be 16:9 in pixels.
    const float frameW = 1080.0f, frameH = frameW / aspect;
    const float hudW = frameW * kx, hudH = frameH * ky;
    EXPECT_NEAR(hudW / hudH, 16.0f / 9.0f, 1e-4);
    EXPECT_FLOAT_EQ(ty, 0.0f);
    // The flashcard column (x 0.83..0.98 of the 16:9 screen, MEASURED in a
    // landscape frame) lands whole inside the frame.
    for (float u : { 0.83f, 0.98f }) {
        const float x = kx * (2.0f * u - 1.0f) + tx;
        EXPECT_GT(x, -1.0f);
        EXPECT_LT(x, 1.0f);
    }
}
