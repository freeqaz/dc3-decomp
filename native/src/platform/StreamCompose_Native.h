#pragma once
#ifdef HX_NATIVE

// CPU stand-in for StreamRenderer's pixel shaders (PLAN P-4).
//
// On the 360 StreamRenderer::DrawToTexture binds the NUI depth or colour
// texture and draws a full-target rect with one of the stream shaders
// (yuvtorgb, playerdepth_vis/_shell/_shell2, player_greenscreen, ...), then
// ping-pongs it through mNumBlurs blur passes.  Those shaders are Xenos
// microcode in the game's xbox_shaders archive; the native renderer has no
// counterpart.  This computes the same KIND of picture on the CPU from the
// Kinect-format buffers (CameraStream_Native.h) so it can be uploaded into
// the output render target:
//
//   colour modes      the UYVY frame decoded with the decoders' BT.601 matrix
//   player modes      each pixel of player index p gets the colour of the game
//                     player whose skeleton is p (StreamRenderer's player
//                     colours, matched through GetPlayerIndexes), everything
//                     else the "nobody" colour
//   greenscreen       the colour frame where a player is, "nobody" elsewhere
//
// What it does NOT reproduce, because the shader source is not recovered:
// the depth palettes (mPlayer*DepthPalette / offsets), the shell modes'
// exact edge treatment, the crew-photo edge detection and radial blur.  The
// blur is a separable [1 2 1] pass per mNumBlurs.  See docs/native/POSE_INPUT.md.

#include <cstdint>

struct NativeStreamComposeParams {
    int display = 0;             // StreamDisplay (gesture/StreamRenderer.h)
    float playerColor[6][4] = {}; // StreamRenderer mPlayer1..6DepthColor, rgba 0..1
    float nobody[4] = {};        // mPlayerDepthNobody
    int playerIndexes[6] = {};   // GetPlayerIndexes: NUI player index of game player i
    int numBlurs = 0;
    float backgroundBrightness = 0.f; // crew photo: mCrewPhotoBackgroundBrightness
};

// depth: 320x240 texels of (depth_mm << 3 | player), row pitch depthPitchTexels;
// uyvy: 640x480 colour words (2 pixels each), row pitch colorPitchWords.
// Either may be null when the mode does not need it (the output is then the
// "nobody" colour).  rgba: outW*outH*4 bytes, top row first.
void NativeStreamCompose(const uint16_t *depth, int depthPitchTexels, const uint32_t *uyvy,
                         int colorPitchWords, const NativeStreamComposeParams &params,
                         int outW, int outH, uint8_t *rgba);

// Hand the composed picture to the renderer: writes it into `tex`'s GPU render
// target (milo-native-engine UploadRGBAToRndTex) and keeps a CPU copy that
// NativeStreamLastPixels() returns -- the test seam, since a headless run has
// no GPU to read back from.
class RndTex;
void NativeStreamUpload(RndTex *tex, const uint8_t *rgba, int w, int h);
// The last picture uploaded into `tex` (nullptr if none); w/h receive its size.
const uint8_t *NativeStreamLastPixels(RndTex *tex, int *w, int *h);
// Total uploads so far (any texture).
unsigned NativeStreamUploadCount();

#endif
