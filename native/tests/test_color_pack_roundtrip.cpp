// test_color_pack_roundtrip.cpp -- Hmx::Color's 8-bit pack/unpack, and the
// round trip a light's intensity makes through it, as the image computes them.
//
// The image's Hmx::Color::Unpack multiplies each channel by __real@3b808081
// (RndLight::SetPackedColor in build/373307D9/asm/system/rndobj/Lit.s; the
// rb3-xenon image's copy, fn_82497BB0, does the same). That is the nearest float
// to 1/255, which sits slightly above it: the Xenon compiler is /fp:fast and
// folds `/ 255.0f` into the multiply. Native used to divide exactly, which reads
// 126 of the 256 levels one ulp low. Pack truncates, so every
// {light set intensity} (PackedColor -> SetPackedColor) could lose a level, and
// throneroom's crow_underlight_light drained from 255/200/4 to 255/0/0 over a
// song while Xenia held 255/200/3.
//
//   * every level unpacks to the image's float, bit for bit;
//   * Pack(Unpack(x)) is a fixed point for every level and every channel,
//     over many trips (that holds under either spelling: it guards Pack);
//   * the light's intensity trip, through RndLight's own property path, gives
//     what the image's arithmetic gives for every level over many sets. That
//     reference is restated from the target listings: PackedColor is fdivs
//     1/I, then fmuls c*inv, fmuls *255, fctiwz; SetPackedColor is
//     k * 3b808081, then fmuls *I. Note that the image is NOT a fixed point for
//     every level in that trip (some colours settle a few levels lower), so
//     the test requires agreement with the image and not a fixed point;
//   * the crow light keeps its colour over 1,200 sets in [2, 6], the range
//     Midi_Snare.anim drives.
//
// CPU only: no GPU, no game data.
#include "test_helpers.h"

#include "math/Color.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "rndobj/Lit.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

float ImageReciprocal255() {
    const uint32_t bits = 0x3B808081u; // __real@3b808081, read from Lit.s
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

// The image's RndLight::PackedColor: fdivs 1/max(1, max channel), fmuls c*inv,
// fmuls *255, fctiwz (truncate), 8 bits a channel.
int ImagePackedColor(const float c[3]) {
    float m = std::max(1.0f, std::max(c[0], std::max(c[1], c[2])));
    float inv = 1.0f / m;
    int p = 0;
    for (int i = 0; i < 3; i++) {
        float v = c[i] * inv;
        v = v * 255.0f;
        p |= ((int)v & 0xFF) << (8 * i);
    }
    return p;
}

// The image's RndLight::SetPackedColor: k * __real@3b808081, then fmuls *I.
void ImageSetPackedColor(int packed, float intensity, float c[3]) {
    const float r = ImageReciprocal255();
    for (int i = 0; i < 3; i++) {
        float v = (float)((packed >> (8 * i)) & 0xFF) * r;
        c[i] = v * intensity;
    }
}

int LightPacked(RndLight *l) { return l->Property("color", true)->Int(); }

} // namespace

TEST(HmxColorPack, EveryLevelUnpacksToTheImagesFloat) {
    const float r = ImageReciprocal255();
    int wrong = 0;
    for (int k = 0; k < 256; k++) {
        const float want = (float)k * r;
        Hmx::Color c(0.0f, 0.0f, 0.0f, 0.0f);
        c.Unpack(k | (k << 8) | (k << 16));
        Hmx::Color a(0.0f, 0.0f, 0.0f, 0.0f);
        a.UnpackAlpha(k | (k << 8) | (k << 16) | (k << 24));
        for (float got : { c.red, c.green, c.blue, a.red, a.green, a.blue, a.alpha }) {
            if (Bits(got) != Bits(want)) {
                if (wrong++ < 8)
                    ADD_FAILURE() << "level " << k << ": got 0x" << std::hex << Bits(got)
                                  << ", the image reads 0x" << Bits(want) << std::dec;
            }
        }
    }
    EXPECT_EQ(wrong, 0);
}

TEST(HmxColorPack, PackOfUnpackIsAFixedPointForEveryLevel) {
    for (int k = 0; k < 256; k++) {
        // Each channel carries a different level so a swapped shift shows.
        const int packed = k | (((k + 85) & 255) << 8) | (((k + 170) & 255) << 16);
        const int packedA = packed | ((255 - k) << 24);
        int p = packed, pa = packedA;
        for (int trip = 0; trip < 1000; trip++) {
            Hmx::Color c;
            c.alpha = 1.0f;
            c.Unpack(p);
            p = c.Pack();
            Hmx::Color ca;
            ca.UnpackAlpha(pa);
            pa = ca.PackAlpha();
        }
        ASSERT_EQ(p, packed) << "level " << k;
        ASSERT_EQ((unsigned)pa, (unsigned)packedA) << "level " << k;
    }
}

class LightIntensityRoundTrip : public EngineTestFixture {};

// Drives a fresh light (colour 1/1/1, so Intensity() is 1) to `start`, then
// sets its intensity `sets` times, running the image's arithmetic alongside.
// Returns the first set (0-based) whose packed colour or float colour differs
// from the image's, or -1; `nativeEnd`/`imageEnd` get the final packed colours.
static int RunAgainstTheImage(int start, const std::vector<float> &sets, int &nativeEnd, int &imageEnd) {
    RndLight *light = Hmx::Object::New<RndLight>();
    float image[3];
    ImageSetPackedColor(start, 1.0f, image); // {set color}: SetPackedColor(p, Intensity()=1)
    light->SetProperty("color", DataNode(start));
    int firstDiff = -1;
    for (size_t i = 0; i < sets.size() && firstDiff < 0; i++) {
        ImageSetPackedColor(ImagePackedColor(image), sets[i], image);
        light->SetProperty("intensity", DataNode(sets[i]));
        const Hmx::Color &c = light->GetColor();
        if (Bits(c.red) != Bits(image[0]) || Bits(c.green) != Bits(image[1])
            || Bits(c.blue) != Bits(image[2]))
            firstDiff = (int)i;
    }
    nativeEnd = LightPacked(light);
    imageEnd = ImagePackedColor(image);
    delete light;
    return firstDiff;
}

static std::vector<float> SnareSets(unsigned seed, int n) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(2.0f, 6.0f); // Midi_Snare.anim's range
    std::vector<float> sets(n);
    for (float &s : sets) s = dist(rng);
    return sets;
}

TEST_F(LightIntensityRoundTrip, EveryLevelFollowsTheImageOverManySets) {
    const std::vector<float> sets = SnareSets(20261114, 300);
    int diverged = 0;
    for (int k = 0; k < 256; k++) {
        // Red at 255 keeps Intensity() the set value; green and blue sweep.
        const int start = 255 | (k << 8) | ((255 - k) << 16);
        int nativeEnd = 0, imageEnd = 0;
        const int at = RunAgainstTheImage(start, sets, nativeEnd, imageEnd);
        if (at >= 0 && diverged++ < 8)
            ADD_FAILURE() << "level " << k << ": first differs from the image at set " << at
                          << "; after " << sets.size() << " sets native 0x" << std::hex
                          << nativeEnd << ", image 0x" << imageEnd << std::dec;
    }
    EXPECT_EQ(diverged, 0);
}

TEST_F(LightIntensityRoundTrip, CrowUnderlightKeepsItsColour) {
    // throneroom crow_underlight_light: 255/200/4, intensity set on snare hits
    // by Midi_Snare.anim in [2, 6]. Xenia holds 255/200/3 for the whole song;
    // native used to read 255/34/0 by 45 s.
    const int start = 255 | (200 << 8) | (4 << 16);
    int nativeEnd = 0, imageEnd = 0;
    EXPECT_EQ(RunAgainstTheImage(start, SnareSets(1, 1200), nativeEnd, imageEnd), -1);
    EXPECT_EQ(nativeEnd, imageEnd);
    EXPECT_EQ(nativeEnd & 0xFF, 255);
    EXPECT_EQ((nativeEnd >> 8) & 0xFF, 200) << "green drained";
    EXPECT_GE((nativeEnd >> 16) & 0xFF, 3);
}
