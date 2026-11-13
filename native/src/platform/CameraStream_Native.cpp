#ifdef HX_NATIVE

#include "CameraStream_Native.h"
#include "NuiImageSurface_Native.h"

#include "gesture/LiveCameraInput.h"
#include "rndobj/Tex.h"
#include "xdk/nui/nuidetroit.h"
#include "xdk/xapilibi/winerror.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

namespace NativeCamera {

namespace {

// NuiImageStreamGetNextFrame's "nothing new yet": the image only tests the
// result with SUCCEEDED(), and counts 0x83010001 on the colour stream as
// g_ColorNoFrameDataCnt (LiveCameraInput::PollNewStream).
const HRESULT kNoFrameData = (HRESULT)0x83010001;
const HRESULT kOk = 0;

std::mutex sPublishMutex;
Image sPending;          // guarded by sPublishMutex
bool sHavePending = false; // guarded by sPublishMutex
volatile bool sActive = false;

// One latched frame of a stream lives in a slot until the game releases it.
// LiveCameraInput holds at most two frames per stream (mFrames[write/read]),
// and between polls only one; four slots leave the newest frame untouched
// while the game reads the previous one.
const int kSlots = 4;

struct Slot {
    std::vector<uint8_t> bits;
    NuiImageSurface surface;
    NUI_IMAGE_FRAME frame;
    uint32_t seq = 0;
    bool held = false; // handed out by GetNextFrame, not yet released
};

struct Stream {
    NUI_IMAGE_TYPE type;
    NUI_IMAGE_RESOLUTION resolution;
    int pitchBytes;
    int height;
    Slot slots[kSlots];
    int newest = -1;            // slot holding the newest latched frame
    uint32_t delivered = 0;     // seq of the last frame GetNextFrame returned
    bool open = false;
};

Stream sStreams[2] = {
    { NUI_IMAGE_TYPE_COLOR_YUV, NUI_IMAGE_RESOLUTION_640x480, kColorPitchBytes, kColorH },
    { NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX_IN_COLOR_SPACE, NUI_IMAGE_RESOLUTION_320x240,
      kDepthPitchBytes, kDepthH },
};
const int kColorStream = 0;
const int kDepthStream = 1;

uint32_t sLatchSeq = 0;

// A slot the game does not hold and that is not the newest frame.
Slot *FreeSlot(Stream &s, int *outIdx) {
    for (int i = 0; i < kSlots; i++) {
        if (i != s.newest && !s.slots[i].held) {
            *outIdx = i;
            return &s.slots[i];
        }
    }
    return nullptr; // cannot happen with LiveCameraInput's two holders
}

void PrepareSlot(Stream &s, Slot &slot, uint32_t seq, double timestamp) {
    size_t bytes = (size_t)s.pitchBytes * (size_t)s.height;
    if (slot.bits.size() != bytes)
        slot.bits.assign(bytes, 0);
    slot.surface = NuiImageSurface(slot.bits.data(), (unsigned int)s.pitchBytes);
    memset(&slot.frame, 0, sizeof(slot.frame));
    slot.frame.liTimeStamp.QuadPart = (long long)(timestamp * 1e7); // 100 ns ticks
    slot.frame.dwFrameNumber = seq;
    slot.frame.eImageType = s.type;
    slot.frame.eResolution = s.resolution;
    slot.frame.pFrameTexture = reinterpret_cast<D3DTexture *>(&slot.surface);
    slot.seq = seq;
}

uint8_t Clamp8(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

} // namespace

void Publish(Image &&image) {
    std::lock_guard<std::mutex> lock(sPublishMutex);
    sPending = std::move(image);
    sHavePending = true;
    sActive = true;
}

void Stop() {
    std::lock_guard<std::mutex> lock(sPublishMutex);
    sHavePending = false;
    sActive = false;
}

bool Active() { return sActive; }

uint32_t LatchedSequence() { return sLatchSeq; }

void ResetForTest() {
    std::lock_guard<std::mutex> lock(sPublishMutex);
    sHavePending = false;
    sActive = false;
    sLatchSeq = 0;
    for (Stream &s : sStreams) {
        s.newest = -1;
        s.delivered = 0;
        for (Slot &slot : s.slots) {
            slot.held = false;
            slot.seq = 0;
        }
    }
}

bool Latch(int (*slotForTrackId)(int)) {
    Image img;
    {
        std::lock_guard<std::mutex> lock(sPublishMutex);
        if (!sHavePending)
            return false;
        img = std::move(sPending);
        sHavePending = false;
    }
    if (img.width <= 0 || img.height <= 0
        || img.rgb.size() < (size_t)img.width * img.height * 3)
        return false;

    int playerIndex[kMaxPersons];
    uint16_t depthMm[kMaxPersons];
    int n = img.numPersons < kMaxPersons ? img.numPersons : kMaxPersons;
    for (int k = 0; k < n; k++) {
        int slot = slotForTrackId ? slotForTrackId(img.trackIds[k]) : -1;
        playerIndex[k] = (slot >= 0 && slot < 6) ? slot + 1 : 0;
        int mm = img.depthM[k] > 0.0f ? (int)(img.depthM[k] * 1000.0f + 0.5f)
                                      : kDefaultPersonDepthMm;
        if (mm < kMinDepthMm)
            mm = kMinDepthMm;
        if (mm > kMaxDepthMm)
            mm = kMaxDepthMm;
        depthMm[k] = (uint16_t)mm;
    }

    uint32_t seq = ++sLatchSeq;
    int idx;
    Stream &color = sStreams[kColorStream];
    if (Slot *slot = FreeSlot(color, &idx)) {
        PrepareSlot(color, *slot, seq, img.timestamp);
        ConvertColor(img.rgb.data(), img.width, img.height,
                     reinterpret_cast<uint32_t *>(slot->bits.data()));
        color.newest = idx;
    }
    Stream &depth = sStreams[kDepthStream];
    if (Slot *slot = FreeSlot(depth, &idx)) {
        PrepareSlot(depth, *slot, seq, img.timestamp);
        if (img.mask.size() >= (size_t)img.width * img.height) {
            ConvertDepth(img.mask.data(), img.width, img.height, playerIndex, depthMm, n,
                         reinterpret_cast<uint16_t *>(slot->bits.data()));
        }
        depth.newest = idx;
    }
    return true;
}

const char *ParseWireBlock(const uint8_t *p, const uint8_t *end, const int *trackIds,
                           const float *depthM, int numPersons, double timestamp,
                           Image &out) {
    out = Image();
    if (end - p < kWireHeaderBytes)
        return "short header";
    uint16_t w, h;
    memcpy(&w, p, 2);
    memcpy(&h, p + 2, 2);
    int rgbFormat = p[4], maskFormat = p[5];
    p += kWireHeaderBytes;
    if (w == 0 && h == 0)
        return nullptr; // no picture this frame
    if (w == 0 || h == 0 || w > kWireMaxDim || h > kWireMaxDim)
        return "bad size";
    if (rgbFormat != kWireRgb888 || maskFormat != kWireMaskOrdinal)
        return "unknown format";
    size_t px = (size_t)w * h;
    if ((size_t)(end - p) < px * 4)
        return "truncated pixels";
    out.width = w;
    out.height = h;
    out.rgb.assign(p, p + px * 3);
    out.mask.assign(p + px * 3, p + px * 4);
    out.numPersons = numPersons < kMaxPersons ? numPersons : kMaxPersons;
    for (int i = 0; i < out.numPersons; i++) {
        out.trackIds[i] = trackIds[i];
        out.depthM[i] = depthM[i];
    }
    out.timestamp = timestamp;
    return nullptr;
}

void ConvertColor(const uint8_t *rgb, int w, int h, uint32_t *out) {
    const int words = kColorW / 2;
    for (int y = 0; y < kColorH; y++) {
        const uint8_t *srcRow = rgb + (size_t)((y * h) / kColorH) * w * 3;
        uint32_t *dst = out + (size_t)y * words;
        for (int x = 0; x < words; x++) {
            int yy[2], cb = 0, cr = 0;
            for (int i = 0; i < 2; i++) {
                // mirrored: Kinect column c shows camera column w-1-c (header)
                const uint8_t *p = srcRow + (size_t)(w - 1 - ((2 * x + i) * w) / kColorW) * 3;
                int r = p[0], g = p[1], b = p[2];
                // BT.601 full range -- the inverse of the matrix the decoders
                // use (91881 / -46802 / -22553 / 116130 in Q16).
                yy[i] = (19595 * r + 38470 * g + 7471 * b + 32768) >> 16;
                cb += ((-11059 * r - 21709 * g + 32768 * b + 32768) >> 16) + 128;
                cr += ((32768 * r - 27439 * g - 5329 * b + 32768) >> 16) + 128;
            }
            uint32_t u = Clamp8((cb + 1) >> 1), v = Clamp8((cr + 1) >> 1);
            dst[x] = (u << 24) | ((uint32_t)Clamp8(yy[0]) << 16) | (v << 8)
                | (uint32_t)Clamp8(yy[1]);
        }
    }
}

void ConvertDepth(const uint8_t *mask, int w, int h, const int *playerIndex,
                  const uint16_t *depthMm, int numPersons, uint16_t *out) {
    for (int y = 0; y < kDepthH; y++) {
        const uint8_t *srcRow = mask + (size_t)((y * h) / kDepthH) * w;
        uint16_t *dst = out + (size_t)y * kDepthPitchTexels;
        for (int x = 0; x < kDepthW; x++) {
            int k = srcRow[w - 1 - (x * w) / kDepthW]; // mirrored, as ConvertColor
            uint16_t texel = 0; // nobody, and no depth: the webcam has none
            if (k >= 1 && k <= numPersons && playerIndex[k - 1] > 0)
                texel = (uint16_t)((depthMm[k - 1] << 3) | playerIndex[k - 1]);
            dst[x] = texel;
        }
        for (int x = kDepthW; x < kDepthPitchTexels; x++)
            dst[x] = 0;
    }
}

} // namespace NativeCamera

// ---- the NUI image-stream entry points LiveCameraInput calls ---------------
//
// Typed definitions; the untyped return-0 stubs these replace were removed from
// native/src/engine_stubs_generated.cpp.  A handle is a Stream*; a frame's
// pFrameTexture is a NuiImageSurface the D3DLineTexture_LockRect shim
// understands.

using namespace NativeCamera;

extern "C" {

HRESULT NuiImageStreamOpen(NUI_IMAGE_TYPE eImageType, NUI_IMAGE_RESOLUTION eResolution,
                           DWORD, DWORD, HANDLE, HANDLE *phStreamHandle) {
    if (!phStreamHandle)
        return E_INVALIDARG;
    *phStreamHandle = nullptr;
    for (auto &s : sStreams) {
        if (s.type == eImageType && s.resolution == eResolution) {
            s.open = true;
            *phStreamHandle = &s;
            return kOk;
        }
    }
    fprintf(stderr, "NuiImageStreamOpen: no native stream for type %d resolution %d\n",
            (int)eImageType, (int)eResolution);
    return E_INVALIDARG;
}

HRESULT NuiImageStreamGetNextFrame(HANDLE hStream, DWORD,
                                   const NUI_IMAGE_FRAME **ppcImageFrame) {
    if (!ppcImageFrame)
        return E_INVALIDARG;
    Stream *s = static_cast<Stream *>(hStream);
    if (s != &sStreams[0] && s != &sStreams[1])
        return E_INVALIDARG;
    if (s->newest < 0 || s->slots[s->newest].seq == s->delivered)
        return kNoFrameData; // ppcImageFrame untouched, as on the 360
    Slot &slot = s->slots[s->newest];
    slot.held = true;
    s->delivered = slot.seq;
    *ppcImageFrame = &slot.frame;
    return kOk;
}

HRESULT NuiImageStreamReleaseFrame(HANDLE hStream, const NUI_IMAGE_FRAME *pImageFrame) {
    Stream *s = static_cast<Stream *>(hStream);
    if (s != &sStreams[0] && s != &sStreams[1])
        return E_INVALIDARG;
    for (Slot &slot : s->slots) {
        if (&slot.frame == pImageFrame) {
            slot.held = false;
            return kOk;
        }
    }
    return E_INVALIDARG;
}

} // extern "C"

// ---- LiveCameraInput::GetStreamTex's texture --------------------------------
//
// On the 360 GetStreamTex points a DxTex at the NUI frame's D3D texture, and
// FreestyleMoveRecorder::Poll then TexelsLocks it and walks the depth texels
// with the 360's pitch.  This RndTex does the same for a NuiImageSurface.
namespace {

class StreamTex : public RndTex {
public:
    StreamTex(int w, int h) : mSurface(nullptr) {
        mWidth = w;
        mHeight = h;
        mBpp = 16;
        mType = RndTex::kScratch;
    }
    bool TexelsLock(void *&out) override {
        out = mSurface ? mSurface->mBits : nullptr;
        return out != nullptr;
    }
    void TexelsUnlock() override {}
    unsigned int TexelsPitch() const override { return mSurface ? mSurface->mPitch : 0; }

    const NuiImageSurface *mSurface;
};

} // namespace

RndTex *LiveCameraInput::NativeStreamTex(BufferType type, void *bufferData) {
    static StreamTex *sTex[2] = { nullptr, nullptr };
    int i = type == kBufferDepth ? 1 : 0;
    if (!sTex[i])
        sTex[i] = i ? new StreamTex(kDepthW, kDepthH) : new StreamTex(kColorW, kColorH);
    const NuiImageSurface *surf = static_cast<const NuiImageSurface *>(bufferData);
    sTex[i]->mSurface = (surf && surf->mMagic == NuiImageSurface::kMagic) ? surf : nullptr;
    return sTex[i];
}

#endif
