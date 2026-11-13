#ifdef HX_NATIVE

#include "StreamCompose_Native.h"
#include "CameraStream_Native.h"
#include "rndobj/Tex.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

// StreamDisplay values (gesture/StreamRenderer.h).
enum {
    kColor = 0,
    kBasicDepth = 1,
    kPlayerDepthVis = 2,
    kPlayerDepthShell = 3,
    kPlayerDepthShell2 = 4,
    kBlackAndWhite = 5,
    kPlayerGreenscreen = 6,
    kPlayerDepthGreenscreen = 7,
    kCrewPhoto = 8,
};

uint8_t To8(float v) {
    if (v <= 0.f)
        return 0;
    if (v >= 1.f)
        return 255;
    return (uint8_t)(v * 255.f + 0.5f);
}

int Clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

// The decoders' Q16 BT.601 matrix (YUVtoRGB in LiveCameraInput.cpp / DrawUtl.cpp),
// at 8 bits per channel instead of RGB565.
void DecodeUYVY(uint32_t word, int which, uint8_t *rgb) {
    int u = (int)(word >> 24) - 128;
    int v = (int)((word >> 8) & 0xff) - 128;
    int y = which ? (int)(word & 0xff) : (int)((word >> 16) & 0xff);
    rgb[0] = (uint8_t)Clamp255(y + ((91881 * v) >> 16));
    rgb[1] = (uint8_t)Clamp255(y + ((-46802 * v - 22553 * u) >> 16));
    rgb[2] = (uint8_t)Clamp255(y + ((116130 * u) >> 16));
}

// Which of StreamRenderer's player colours NUI player index p gets, or -1.
int ColorSlotFor(int p, const NativeStreamComposeParams &prm) {
    if (p <= 0)
        return -1;
    for (int i = 0; i < 6; i++) {
        if (prm.playerIndexes[i] == p) {
            // Only the DC2 shell has six colours; the older modes colour players
            // 1 and 2 and give everyone else colour 3 (the p3 constant is the
            // last one DrawToTexture sets for them).
            if (prm.display != kPlayerDepthShell2 && i > 2)
                return 2;
            return i;
        }
    }
    return -1;
}

void BlurPass(std::vector<uint8_t> &img, std::vector<uint8_t> &tmp, int w, int h) {
    tmp.resize(img.size());
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xl = x > 0 ? x - 1 : x, xr = x < w - 1 ? x + 1 : x;
            for (int c = 0; c < 4; c++) {
                int s = img[(y * w + xl) * 4 + c] + 2 * img[(y * w + x) * 4 + c]
                    + img[(y * w + xr) * 4 + c];
                tmp[(y * w + x) * 4 + c] = (uint8_t)((s + 2) >> 2);
            }
        }
    }
    for (int y = 0; y < h; y++) {
        int yu = y > 0 ? y - 1 : y, yd = y < h - 1 ? y + 1 : y;
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++) {
                int s = tmp[(yu * w + x) * 4 + c] + 2 * tmp[(y * w + x) * 4 + c]
                    + tmp[(yd * w + x) * 4 + c];
                img[(y * w + x) * 4 + c] = (uint8_t)((s + 2) >> 2);
            }
        }
    }
}

struct Uploaded {
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    std::string name; // taken at upload: the texture may be gone by the time it is read
    unsigned uploads = 0;
    double ageMsSum = 0.0; // camera frame's age (publish -> upload), summed
    double composeMsSum = 0.0;
};
double sComposeStart = 0.0; // NativeStreamCompose's start, for the upload that follows
std::map<RndTex *, Uploaded> sUploaded;
unsigned sUploadCount = 0;

} // namespace

void NativeStreamCompose(const uint16_t *depth, int depthPitchTexels, const uint32_t *uyvy,
                         int colorPitchWords, const NativeStreamComposeParams &prm,
                         int outW, int outH, uint8_t *rgba) {
    using namespace NativeCamera;
    sComposeStart = NowSeconds();
    const uint8_t nobody[4] = { To8(prm.nobody[0]), To8(prm.nobody[1]), To8(prm.nobody[2]),
                                To8(prm.nobody[3]) };
    uint8_t colors[6][4];
    for (int i = 0; i < 6; i++)
        for (int c = 0; c < 4; c++)
            colors[i][c] = To8(prm.playerColor[i][c]);

    for (int y = 0; y < outH; y++) {
        int dy = (y * kDepthH) / outH, cy = (y * kColorH) / outH;
        for (int x = 0; x < outW; x++) {
            int dx = (x * kDepthW) / outW, cx = (x * kColorW) / outW;
            uint8_t *o = rgba + ((size_t)y * outW + x) * 4;
            uint16_t texel = depth ? depth[(size_t)dy * depthPitchTexels + dx] : 0;
            int player = texel & 7;
            uint8_t rgb[3] = { 0, 0, 0 };
            if (uyvy)
                DecodeUYVY(uyvy[(size_t)cy * colorPitchWords + cx / 2], cx & 1, rgb);
            memcpy(o, nobody, 4);
            switch (prm.display) {
            case kColor:
                if (uyvy) {
                    o[0] = rgb[0], o[1] = rgb[1], o[2] = rgb[2], o[3] = 255;
                }
                break;
            case kBlackAndWhite:
                if (uyvy) {
                    uint8_t l = (uint8_t)((rgb[0] * 77 + rgb[1] * 150 + rgb[2] * 29) >> 8);
                    o[0] = o[1] = o[2] = l, o[3] = 255;
                }
                break;
            case kBasicDepth:
                if (depth) {
                    int mm = texel >> 3;
                    uint8_t g = mm ? (uint8_t)Clamp255(255 - (mm - kMinDepthMm) * 255
                                                             / (kMaxDepthMm - kMinDepthMm))
                                   : 0;
                    o[0] = o[1] = o[2] = g, o[3] = 255;
                }
                break;
            case kPlayerDepthVis:
            case kPlayerDepthShell:
            case kPlayerDepthShell2: {
                int slot = ColorSlotFor(player, prm);
                if (slot >= 0)
                    memcpy(o, colors[slot], 4);
                break;
            }
            case kPlayerGreenscreen:
            case kPlayerDepthGreenscreen:
                if (uyvy && player > 0) {
                    o[0] = rgb[0], o[1] = rgb[1], o[2] = rgb[2], o[3] = 255;
                }
                break;
            case kCrewPhoto:
                if (uyvy) {
                    float k = player > 0 ? 1.f : prm.backgroundBrightness;
                    o[0] = To8(rgb[0] / 255.f * k), o[1] = To8(rgb[1] / 255.f * k);
                    o[2] = To8(rgb[2] / 255.f * k), o[3] = 255;
                }
                break;
            default:
                break;
            }
        }
    }
    if (prm.numBlurs > 0) {
        std::vector<uint8_t> img(rgba, rgba + (size_t)outW * outH * 4), tmp;
        int passes = prm.numBlurs > 64 ? 64 : prm.numBlurs;
        for (int i = 0; i < passes; i++)
            BlurPass(img, tmp, outW, outH);
        memcpy(rgba, img.data(), img.size());
    }
}

// milo-native-engine, platform/Tex_Wgpu.cpp (declared in platform/TexGpu.h,
// which drags in the WebGPU headers).
void UploadRGBAToRndTex(RndTex *tex, const uint8_t *rgba, int w, int h);

void NativeStreamUpload(RndTex *tex, const uint8_t *rgba, int w, int h) {
    Uploaded &u = sUploaded[tex];
    u.pixels.assign(rgba, rgba + (size_t)w * h * 4);
    u.w = w;
    u.h = h;
    u.name = tex->Name() ? tex->Name() : "";
    u.uploads++;
    double now = NativeCamera::NowSeconds();
    if (NativeCamera::LatchedPublishTime() > 0.0)
        u.ageMsSum += (now - NativeCamera::LatchedPublishTime()) * 1000.0;
    if (sComposeStart > 0.0)
        u.composeMsSum += (now - sComposeStart) * 1000.0;
    sUploadCount++;
    UploadRGBAToRndTex(tex, rgba, w, h);
}

const uint8_t *NativeStreamLastPixels(RndTex *tex, int *w, int *h) {
    auto it = sUploaded.find(tex);
    if (it == sUploaded.end())
        return nullptr;
    if (w)
        *w = it->second.w;
    if (h)
        *h = it->second.h;
    return it->second.pixels.data();
}

unsigned NativeStreamUploadCount() { return sUploadCount; }

// GestureMgr_Native.cpp
bool NativeCameraStandInAttached();

// GET /api/camera (HttpServer.cpp).
std::string Dc3CameraJson(int gridW, int gridH) {
    if (gridW > 640)
        gridW = 640;
    if (gridH > 480)
        gridH = 480;
    std::string j = "{\"active\":";
    j += NativeCamera::Active() ? "true" : "false";
    j += ",\"standIn\":";
    j += NativeCameraStandInAttached() ? "true" : "false";
    j += ",\"latched\":" + std::to_string(NativeCamera::LatchedSequence());
    j += ",\"uploads\":" + std::to_string(sUploadCount);
    char ms[64];
    snprintf(ms, sizeof ms, "%.3f", NativeCamera::LatchMsMean());
    j += std::string(",\"latchMsMean\":") + ms;
    j += ",\"grid\":[" + std::to_string(gridW) + "," + std::to_string(gridH) + "]";
    j += ",\"textures\":[";
    bool first = true;
    for (auto &kv : sUploaded) {
        const Uploaded &u = kv.second;
        if (!first)
            j += ",";
        first = false;
        j += "{\"name\":\"" + u.name + "\",\"w\":" + std::to_string(u.w)
            + ",\"h\":" + std::to_string(u.h) + ",\"uploads\":" + std::to_string(u.uploads);
        // composeMsMean: compose + blur + upload; frameAgeMsMean: from the
        // frame's arrival (Publish) to this upload -- the engine's share of
        // the camera-to-screen latency, up to the frame's draw
        snprintf(ms, sizeof ms, "%.3f", u.uploads ? u.composeMsSum / u.uploads : 0.0);
        j += std::string(",\"composeMsMean\":") + ms;
        snprintf(ms, sizeof ms, "%.3f", u.uploads ? u.ageMsSum / u.uploads : 0.0);
        j += std::string(",\"frameAgeMsMean\":") + ms + ",\"rows\":[";
        for (int gy = 0; gy < gridH; gy++) {
            j += gy ? ",\"" : "\"";
            int y = (int)(((long)gy * 2 + 1) * u.h / (2L * gridH));
            for (int gx = 0; gx < gridW; gx++) {
                int x = (int)(((long)gx * 2 + 1) * u.w / (2L * gridW));
                const uint8_t *px = &u.pixels[((size_t)y * u.w + x) * 4];
                char hex[9];
                snprintf(hex, sizeof hex, "%02x%02x%02x%02x", px[0], px[1], px[2], px[3]);
                j += hex;
            }
            j += "\"";
        }
        j += "]}";
    }
    j += "]}";
    return j;
}

#endif
