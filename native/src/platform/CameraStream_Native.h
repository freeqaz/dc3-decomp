#pragma once
#ifdef HX_NATIVE

// The native stand-in for the Kinect's two image streams (PLAN P-4).
//
// On a 360 the NUI runtime hands LiveCameraInput two streams, opened in its
// constructor (src/system/gesture/LiveCameraInput.cpp):
//
//   colour  NUI_IMAGE_TYPE_COLOR_YUV, 640x480: one 32-bit word per two pixels,
//           bits 31..24 = Cb, 23..16 = Y0, 15..8 = Cr, 7..0 = Y1 (UYVY; the
//           decoders are TextureStore::UpdateFromColorBuffer and DrawUtl's
//           UpdateBufferTex, pinned by native/tests/test_camera_yuv_decode.cpp)
//   depth   NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE, 320x240,
//           16 bits per texel = depth_mm << 3 | player index (0 = nobody,
//           k = skeleton slot k-1), row pitch 384 texels -- the image's
//           FreestyleMoveRecorder::Poll and DepthBuffer3D walk it with a
//           hard-coded 0x180-texel stride (src += 0x600 shorts = 4 rows).
//
// Natively those frames come from the webcam: the pose server (desktop,
// protocol v3) or the page (web, dc3_camera_push) publishes an RGB frame and a
// person-segmentation mask whose values are the 1-based ORDINAL of a person in
// the same frame's pose list.  Latch() turns the newest published frame into
// the two Kinect buffers, mapping each ordinal to the skeleton slot its
// tracking id holds this frame (slot + 1 is the NUI player index, as
// StreamRenderer's GetPlayerIndexes and CopyPlayerMask assume).
//
// The buffers reach the decompiled code at the layer the hardware filled: the
// NuiImageStream* entry points below return NUI_IMAGE_FRAMEs whose
// pFrameTexture is a NuiImageSurface (NuiImageSurface_Native.h), so
// LiveCameraInput::PollNewStream / StreamBufferData / LockStream run as
// shipped.
//
// No source, no stream: until a frame is published Active() is false, the
// LiveCameraInput stand-in is never created (GestureMgr::GetLiveCameraInput
// stays null exactly as before P-4) and NuiImageStreamGetNextFrame reports
// "no frame".

#include <cstdint>
#include <vector>

namespace NativeCamera {

const int kColorW = 640;
const int kColorH = 480;
const int kColorPitchBytes = kColorW * 2; // UYVY: 2 bytes per pixel
const int kDepthW = 320;
const int kDepthH = 240;
const int kDepthPitchTexels = 384; // the 360's tiled pitch (0x180)
const int kDepthPitchBytes = kDepthPitchTexels * 2;
const int kMaxPersons = 6;
// Depth written for a segmented person whose pose carried no usable depth, and
// the clamp for the ones that did (Kinect's useful range).
const int kDefaultPersonDepthMm = 2500;
const int kMinDepthMm = 800;
const int kMaxDepthMm = 4000;

// One published camera frame, in the transports' common form.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgb;  // width * height * 3, top row first, R G B
    std::vector<uint8_t> mask; // width * height; 0 = nobody, k = person ordinal
    int numPersons = 0;        // ordinals 1..numPersons are meaningful
    int trackIds[kMaxPersons] = {};
    float depthM[kMaxPersons] = {}; // the person's root depth (camera z), metres
    double timestamp = 0.0;
    double publishedAt = 0.0; // set by Publish (NowSeconds)
};

// Any thread.  The newest image wins; an unlatched older one is dropped.
void Publish(Image &&image);
// The transport's camera went away (pose server exit, page stopped pushing).
// The last latched frame stays where the game holds it -- a 360 keeps its last
// frame too -- but Active() turns false and no new frame is delivered.
void Stop();
// A frame has been published since the last Stop().
bool Active();

// Main thread, once per engine frame: convert the newest published image into
// the Kinect buffers.  `slotForTrackId` maps a tracking id to its skeleton slot
// (-1 = not in a slot this frame).  Returns true when a new frame was latched.
bool Latch(int (*slotForTrackId)(int));
// Count of latched frames (0 = none yet).
uint32_t LatchedSequence();
// Timing (steady clock, seconds): when the newest latched frame was published
// (its arrival from the transport), and the mean / last cost of Latch's
// conversions, in milliseconds.  For GET /api/camera's latency figures.
double LatchedPublishTime();
double LatchMsMean();
double NowSeconds();

// Test seam: forget every frame and the active flag.
void ResetForTest();

// ---- wire --------------------------------------------------------------------
//
// pose_server.py protocol v3 (magic 0x44503303) is v2 plus this block after
// the persons, little-endian:
//   [u16 width][u16 height][u8 rgb_format][u8 mask_format][u16 pad]
//   [width*height*3 bytes RGB, top row first]   rgb_format 1 = RGB888
//   [width*height bytes mask]                   mask_format 1 = u8, 0 = nobody,
//                                               k = k-th person of this packet
// width == 0 && height == 0: no picture this frame (nothing follows).
const int kWireRgb888 = 1;
const int kWireMaskOrdinal = 1;
const int kWireHeaderBytes = 8;
const int kWireMaxDim = 1920;

// Parse one block into `out` (trackIds / depthM: the packet's persons, in
// order).  Returns nullptr on success (out.width == 0 for "no picture"), else
// what was wrong.
const char *ParseWireBlock(const uint8_t *p, const uint8_t *end, const int *trackIds,
                           const float *depthM, int numPersons, double timestamp,
                           Image &out);

// ---- pure conversions (no state; unit-tested) -----------------------------
//
// Orientation.  The wire picture is the camera's own view, as captured: a
// player facing it has their right hand on the picture's LEFT (the convention
// NativeSkeletonProvider::NormalizedToMeters and pose_mediapipe.py already
// use, x = (0.5 - nx) * width).  A Kinect frame is a MIRROR view: the image's
// projection, recovered from JointUtl.s (u = 160 + 285.63 x/z), puts the
// player's right (+x) on the image's RIGHT, and every consumer -- CursorPanel's
// crown, placed with the skeleton's ScreenPos -- assumes the picture agrees.
// So both conversions mirror horizontally: Kinect column c is camera column
// w-1-c (nearest-sampled).

// RGB (any size) -> 640x480 UYVY words, nearest-neighbour scaled, mirrored.
// `out` holds kColorH rows of kColorPitchBytes / 4 words.
void ConvertColor(const uint8_t *rgb, int w, int h, uint32_t *out);
// Mask (any size) -> 320x240 depth|player texels at kDepthPitchTexels, mirrored.
// playerIndex[k-1] is the NUI player index for ordinal k (0 = drop it),
// depthMm[k-1] its depth.
void ConvertDepth(const uint8_t *mask, int w, int h, const int *playerIndex,
                  const uint16_t *depthMm, int numPersons, uint16_t *out);

} // namespace NativeCamera

#endif
