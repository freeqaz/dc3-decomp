// A portrait frame during a song (dc3 PLAN W-37 step 2, np-portrait2).
//
// The web page turns the engine's frame on its side (9:16) while a phone held
// upright is in a song (GpuDevice::SetPortraitFrame, portrait.js). Rnd's
// virtual resolution stays 1280x720, so a full-screen viewport covers the
// whole tall frame; what keeps the picture from stretching is here:
//
// - The WORLD camera (TheWorld->Cam(), the one the director cuts between
//   shots) takes its aspect from the frame. RndCam keeps the vertical field
//   of view, so the dancer is as tall as in landscape and fills a 9:16 frame;
//   the sides of the 16:9 shot fall away.
// - Every other camera (the UI's [ui.cam], panel cameras) still draws the
//   authored 16:9 picture, but through a 2D remap of its projection
//   (NativePortraitHudRemap): the 16:9 HUD is drawn hudScale times the frame's
//   width, its RIGHT edge on the frame's right edge, centred vertically. The
//   single-player HUD (the flashcards, the score) lives in the right ~40% of
//   the 16:9 screen, so it lands whole and larger on the tall frame; the left
//   of the HUD (empty for one player) falls off the left edge.
// - A camera that draws into a texture (mTargetTex) is untouched.
//
// The frame's shape is the only switch: a 16:9 frame (landscape, a desktop
// window, every Xbox-shaped test) leaves all of this off.
#pragma once

class RndCam;

// The drawn frame's width over its height; 16/9 when unknown (no renderer).
float NativeFrameAspect();
// True when the frame is taller than it is wide.
bool NativePortraitFrame();
// The world camera in a portrait frame: the ratio (height over width) its
// projection uses in place of the 16:9 one. False otherwise.
bool NativePortraitWorldRatio(const RndCam *cam, float &ratio);
// Any other camera in a portrait frame: x' = kx*x + tx and y' = ky*y + ty in
// normalised device coordinates. False otherwise (identity).
bool NativePortraitHudRemap(const RndCam *cam, float &kx, float &tx, float &ky, float &ty);

// The pure rule, for tests: a 16:9 picture drawn `hudScale` frame-widths
// wide, right edge on the frame's right edge, its centre `centreY` of the
// way down the frame (0 = top, 1 = bottom), in a frame `frameAspect` (w/h).
void NativePortraitHudRemapFor(float frameAspect, float hudScale, float centreY, float &kx,
                               float &tx, float &ky, float &ty);
// How many frame-widths the 16:9 HUD is drawn in a portrait frame (1.8 by
// default: the right 56% of the HUD shows; MILO_PORTRAIT_HUD_SCALE, 1..3).
float NativePortraitHudScale();
