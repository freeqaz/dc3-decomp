// Kinect colour-decode pixel evidence.
//
// Why this file exists
// --------------------
// `(anonymous namespace)::YUVtoRGB` in gesture/LiveCameraInput.cpp was declared
// and never defined, so on the native port the linker bound its four call sites
// to a weak `_stub_yuvtorgb` that returns 0: every Kinect colour texel decoded
// to black. The body has since been restored from the DrawUtl.cpp copy. This
// test is the pixel-level proof that the restored decode actually produces
// colour, and it is written so that reintroducing the return-0 stub fails it.
//
// It exercises the REAL, unmodified shared body
// LiveCameraInput::TextureStore::UpdateFromColorBuffer -- the same code the PPC
// build compiles -- with a synthetic frame injected at the layer the Kinect
// hardware would have filled (NUI_IMAGE_FRAME::pFrameTexture, reached through
// LiveCameraInput::StreamBufferData and LockStream).
//
// Avoiding the tautology
// ----------------------
// The expected RGB is NOT a constant captured from a run of this code, and it
// is not a second copy of the same integer expression. It is computed from the
// PUBLISHED BT.601 full-range YCbCr->RGB matrix in floating point:
//
//     R = Y + 1.402   * (Cr - 128)
//     G = Y - 0.344136* (Cb - 128) - 0.714136 * (Cr - 128)
//     B = Y + 1.772   * (Cb - 128)
//
// The shipped body uses the Q16 fixed-point form of exactly that matrix
// (1.402*65536 = 91881.0, 0.714136*65536 = 46802.4, 0.344136*65536 = 22553.4,
// 1.772*65536 = 116129.8 -> the 91881 / -46802 / -22553 / 116130 constants
// visible in the disassembly). Agreement between the two is therefore evidence,
// not restatement. The comparison allows +/-1 in each quantised 5/6/5 channel,
// which is the most the Q16 truncation can differ from the real matrix.
//
// Channel order note (this is what the source actually says)
// ----------------------------------------------------------
// UpdateFromColorBuffer reads a 32-bit word and calls
//     YUVtoRGB(word >> 16 & 0xff, word >> 24, word >> 8)
// The body multiplies its 2nd argument by the 1.772 (blue) coefficient and its
// 3rd by 1.402 (red), so the byte at >>24 is Cb and the byte at >>8 is Cr --
// i.e. the stream is UYVY: [U, Y0, V, Y1] in big-endian memory order. The local
// names `cr`/`cb` in LiveCameraInput.cpp are simply mislabelled; the arithmetic
// is right. The tests below build SOURCE WORDS with that layout, so they pin the
// decode arithmetic rather than any host byte order.

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <iostream>
#include <new>
#include <vector>

#include "gesture/LiveCameraInput.h"
#include "rndobj/Tex.h"
#include "xdk/nui/nuidetroit.h"

#include "../src/platform/NuiImageSurface_Native.h"

namespace {

// --------------------------------------------------------------------------
// Independent reference: the published BT.601 matrix, in floating point.
// --------------------------------------------------------------------------
struct Rgb565 {
    int r5, g6, b5;
};

Rgb565 ReferenceRgb565(int y, int cb, int cr) {
    double dcb = cb - 128.0;
    double dcr = cr - 128.0;
    double r = y + 1.402 * dcr;
    double g = y - 0.344136 * dcb - 0.714136 * dcr;
    double b = y + 1.772 * dcb;
    auto clamp8 = [](double v) {
        if (v < 0.0)
            return 0;
        if (v > 255.0)
            return 255;
        return (int)v;
    };
    Rgb565 out;
    out.r5 = clamp8(r) >> 3;
    out.g6 = clamp8(g) >> 2;
    out.b5 = clamp8(b) >> 3;
    return out;
}

Rgb565 Unpack565(unsigned short px) {
    Rgb565 out;
    out.r5 = (px >> 11) & 0x1f;
    out.g6 = (px >> 5) & 0x3f;
    out.b5 = px & 0x1f;
    return out;
}

// --------------------------------------------------------------------------
// A CPU-backed RndTex. The shipped RndTex::TexelsLock is a virtual that returns
// false on native (only the Xbox DxTex overrides it), so the decode would write
// through a null pointer. Overriding the three texel virtuals is enough for
// UpdateFromColorBuffer, which only uses TexelsLock / TexelsPitch / Width /
// Height.
// --------------------------------------------------------------------------
class CpuScratchTex : public RndTex {
public:
    CpuScratchTex(int w, int h) {
        mWidth = w;
        mHeight = h;
        mBpp = 16;
        mType = RndTex::kScratch;
        mPitch = (unsigned int)(w * 2);
        mTexels.assign((size_t)mPitch * (size_t)h, 0xCD); // poison, not zero
    }
    bool TexelsLock(void *&out) override {
        out = mTexels.data();
        return true;
    }
    void TexelsUnlock() override {}
    unsigned int TexelsPitch() const override { return mPitch; }

    unsigned short Texel(int x, int y) const {
        const unsigned char *row = mTexels.data() + (size_t)mPitch * (size_t)y;
        unsigned short v;
        memcpy(&v, row + (size_t)x * 2, 2);
        return v;
    }

    std::vector<unsigned char> mTexels;
    unsigned int mPitch;
};

// --------------------------------------------------------------------------
// A LiveCameraInput just real enough for StreamBufferData() + LockStream().
//
// LiveCameraInput's constructor calls NuiInitialize / NuiImageStreamOpen /
// NuiAudioCreate and needs a booted renderer, none of which exist here (and the
// desktop port never constructs one: LiveCameraInput::PreInit is only called
// from LiveCameraInput::Init, which has zero callers in dc3-native). So the
// object is zeroed storage; the two members we call are non-virtual and touch
// nothing but mStreams.
// --------------------------------------------------------------------------
class CamPeek : public LiveCameraInput {
public:
    static void AttachColorFrame(LiveCameraInput *cam, const NUI_IMAGE_FRAME *frame) {
        CamPeek *self = static_cast<CamPeek *>(cam);
        self->mStreams[LiveCameraInput::kBufferColor].mFrames[0] = frame;
    }
};

struct FakeCamera {
    std::vector<unsigned char> storage;
    NUI_IMAGE_FRAME frame;
    NuiImageSurface surface;

    FakeCamera(void *bits, unsigned int pitch) : storage(sizeof(LiveCameraInput), 0) {
        memset(&frame, 0, sizeof(frame));
        surface = NuiImageSurface(bits, pitch);
        frame.pFrameTexture = reinterpret_cast<D3DTexture *>(&surface);
        CamPeek::AttachColorFrame(Cam(), &frame);
    }
    LiveCameraInput *Cam() {
        return reinterpret_cast<LiveCameraInput *>(storage.data());
    }
};

// Both objects are placement-new'd into plain heap storage and deliberately
// never destroyed. Two reasons, both verified under gdb:
//   * RndTex carries OBJ_MEM_OVERLOAD, so `new CpuScratchTex` routes through
//     RndTex::operator new -> RndTex::StaticClassName() -> Symbol("Tex") ->
//     StringTable::Add on a null table. The object system is not booted in a
//     unit test, so plain `::operator new` + placement new is the way in.
//   * LiveCameraInput::TextureStore::~TextureStore() does RELEASE(mTex), i.e.
//     `delete mTex`, which would run ~RndTex against that same dead object
//     system.
// Leaking a few hundred KB per test case is the correct trade here.
class DecodeHarness {
public:
    DecodeHarness(int w, int h) {
        mTex = new (::operator new(sizeof(CpuScratchTex))) CpuScratchTex(w, h);
        mStore = new (::operator new(sizeof(LiveCameraInput::TextureStore)))
            LiveCameraInput::TextureStore();
        mStore->mTex = mTex;
    }

    void Decode(LiveCameraInput *cam) { mStore->UpdateFromColorBuffer(cam); }
    CpuScratchTex &Tex() { return *mTex; }

private:
    CpuScratchTex *mTex;
    LiveCameraInput::TextureStore *mStore;
};

// Pack one UYVY macro-pixel the way UpdateFromColorBuffer reads it:
// bits 31..24 = Cb(U), 23..16 = Y0, 15..8 = Cr(V), 7..0 = Y1.
uint32_t PackUYVY(int cb, int y0, int cr, int y1) {
    return ((uint32_t)(cb & 0xff) << 24) | ((uint32_t)(y0 & 0xff) << 16)
        | ((uint32_t)(cr & 0xff) << 8) | (uint32_t)(y1 & 0xff);
}

const int kSrcW = 640; // source is 640 wide == 320 UYVY words per row
const int kSrcH = 480;
const int kWords = kSrcW / 2;

} // namespace

// ===========================================================================
// 1. Neutral grey must stay grey, and pure luma ramps must stay non-zero.
//    This is the direct discriminator against the return-0 stub.
// ===========================================================================
TEST(CameraYuvDecode, LumaRampReachesTheTextureAsNonZeroPixels) {
    std::vector<uint32_t> src((size_t)kWords * kSrcH);
    for (int row = 0; row < kSrcH; ++row) {
        for (int w = 0; w < kWords; ++w) {
            // Grey ramp across the row: chroma pinned at neutral (128).
            int y0 = (w * 2) * 255 / (kSrcW - 1);
            int y1 = (w * 2 + 1) * 255 / (kSrcW - 1);
            src[(size_t)row * kWords + w] = PackUYVY(128, y0, 128, y1);
        }
    }

    DecodeHarness h(kSrcW, kSrcH);
    CpuScratchTex &tex = h.Tex();

    FakeCamera cam(src.data(), (unsigned int)(kWords * sizeof(uint32_t)));
    h.Decode(cam.Cam());

    // -- the bug's exact signature: how much of the frame is zero?
    size_t zero = 0;
    std::map<unsigned short, int> histogram;
    for (int y = 0; y < kSrcH; ++y) {
        for (int x = 0; x < kSrcW; ++x) {
            unsigned short px = tex.Texel(x, y);
            if (px == 0)
                ++zero;
            histogram[px]++;
        }
    }
    const size_t total = (size_t)kSrcW * kSrcH;
    std::cout << "[ pixels   ] " << (total - zero) << " / " << total
              << " texels non-zero, " << histogram.size()
              << " distinct RGB565 values" << std::endl;

    // With the stub in place every texel is 0 and all three of these fail.
    //
    // The ramp legitimately contains some zeros: luma below 8 quantises to 0 in
    // all three 5/6/5 channels, which is the leftmost ~11 columns of every row.
    // So the bound is "a few percent", and the sharper statement is the one
    // below it: nothing in the bright half may be zero.
    EXPECT_LT(zero, total / 20)
        << "zero texels: " << zero << " / " << total
        << " -- a solid-black frame is the YUVtoRGB-stub signature";
    EXPECT_GT(histogram.size(), 32u)
        << "distinct 16-bit values: " << histogram.size()
        << " -- a real luma ramp cannot collapse to a handful of values";

    for (int y = 0; y < kSrcH; y += 41) {
        for (int x = kSrcW / 2; x < kSrcW; x += 7) {
            ASSERT_NE(tex.Texel(x, y), 0)
                << "black texel in the bright half at (" << x << "," << y << ")";
        }
    }

    // -- and the ramp must be monotonic in luma, left to right.
    unsigned short first = tex.Texel(0, 0);
    unsigned short last = tex.Texel(kSrcW - 1, 0);
    EXPECT_LT(Unpack565(first).g6, Unpack565(last).g6);
}

// ===========================================================================
// 2. Per-texel agreement with the published BT.601 matrix.
// ===========================================================================
TEST(CameraYuvDecode, MatchesPublishedBt601Matrix) {
    struct Sample {
        int cb, y0, cr, y1;
        const char *what;
    };
    // Chosen to hit each arm: neutral grey, saturated red, saturated blue,
    // saturated green, and a case that clamps on the high side.
    const Sample samples[] = {
        { 128, 0, 128, 16, "black / near-black" },
        { 128, 128, 128, 200, "neutral grey" },
        { 90, 82, 240, 82, "saturated red" },
        { 240, 41, 110, 41, "saturated blue" },
        { 54, 145, 34, 145, "saturated green" },
        { 128, 250, 200, 250, "clamps high on red" },
        { 200, 250, 128, 250, "clamps high on blue" },
    };

    for (const Sample &s : samples) {
        std::vector<uint32_t> src((size_t)kWords * kSrcH, PackUYVY(s.cb, s.y0, s.cr, s.y1));

        DecodeHarness h(kSrcW, kSrcH);
        CpuScratchTex &tex = h.Tex();
        FakeCamera cam(src.data(), (unsigned int)(kWords * sizeof(uint32_t)));
        h.Decode(cam.Cam());

        // Even columns carry Y0, odd columns carry Y1; both share the chroma.
        Rgb565 want0 = ReferenceRgb565(s.y0, s.cb, s.cr);
        Rgb565 want1 = ReferenceRgb565(s.y1, s.cb, s.cr);

        for (int probe = 0; probe < 3; ++probe) {
            int row = probe * (kSrcH / 3);
            Rgb565 got0 = Unpack565(tex.Texel(0, row));
            Rgb565 got1 = Unpack565(tex.Texel(1, row));

            EXPECT_NEAR(got0.r5, want0.r5, 1) << s.what << " (Y0 red, row " << row << ")";
            EXPECT_NEAR(got0.g6, want0.g6, 1) << s.what << " (Y0 green, row " << row << ")";
            EXPECT_NEAR(got0.b5, want0.b5, 1) << s.what << " (Y0 blue, row " << row << ")";
            EXPECT_NEAR(got1.r5, want1.r5, 1) << s.what << " (Y1 red, row " << row << ")";
            EXPECT_NEAR(got1.g6, want1.g6, 1) << s.what << " (Y1 green, row " << row << ")";
            EXPECT_NEAR(got1.b5, want1.b5, 1) << s.what << " (Y1 blue, row " << row << ")";
        }
    }
}

// ===========================================================================
// 3. Negative control -- the "non-zero" assertion above is not vacuous.
//    A genuinely black frame (Y=0, neutral chroma) must decode to all zeros.
//    If this passed while test 1 also passed for the wrong reason (e.g. the
//    poison fill leaking through), one of the two would have to fail.
// ===========================================================================
TEST(CameraYuvDecode, BlackInputDecodesToBlackNotPoison) {
    std::vector<uint32_t> src((size_t)kWords * kSrcH, PackUYVY(128, 0, 128, 0));

    DecodeHarness h(kSrcW, kSrcH);
    CpuScratchTex &tex = h.Tex();
    FakeCamera cam(src.data(), (unsigned int)(kWords * sizeof(uint32_t)));
    h.Decode(cam.Cam());

    for (int y = 0; y < kSrcH; y += 37) {
        for (int x = 0; x < kSrcW; x += 13) {
            ASSERT_EQ(tex.Texel(x, y), 0) << "at (" << x << "," << y << ")";
        }
    }
}

// ===========================================================================
// 4. The path is guarded: a camera with no frame must not touch the texture.
//    This is the state the desktop port is actually in.
// ===========================================================================
TEST(CameraYuvDecode, NoFrameLeavesTheTextureUntouched) {
    DecodeHarness h(kSrcW, kSrcH);
    CpuScratchTex &tex = h.Tex();

    std::vector<unsigned char> storage(sizeof(LiveCameraInput), 0);
    LiveCameraInput *cam = reinterpret_cast<LiveCameraInput *>(storage.data());
    ASSERT_EQ(cam->StreamBufferData(LiveCameraInput::kBufferColor), nullptr);

    h.Decode(cam);

    for (size_t i = 0; i < tex.mTexels.size(); i += 997) {
        ASSERT_EQ(tex.mTexels[i], 0xCD) << "poison overwritten at byte " << i;
    }
}
