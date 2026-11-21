// test_display_ramp.cpp — the console's scan-out table (item 31).
//
// DxRnd::SetupGamma's ramp pow(i / 256, 0.85) is corrected by the XDK's D3D
// before it reaches the hardware (D3DDevice_SetGammaRamp ->
// D3D::GammaRampCorrected(hw, ramp, 0) -> GetCurrentGammaTable(0): sRGB
// decode, then the display type's encode). gfx/DisplayRamp builds that table
// on the CPU and the pass looks it up per byte; display type sRGB keeps the
// ramp formula per pixel, as before. Reference numbers: dc3-decomp
// archive/np-gfx-2026-11-14/lanes/sharp/item31/gamma_ref.py, an independent
// Python model of the same listing (README there has the decode and the
// sabotage table).

#include "test_helpers.h"

#include "gfx/DisplayRamp.h"
#include "gfx/GpuDevice.h"
#include "obj/Data.h"
#include "obj/DataFile.h"
#include "platform/Rnd_Wgpu.h"
#include "platform/rndshape/RndShape.h"
#include "rndobj/Rnd.h"

#include <cmath>
#include <cstdlib>
#include <vector>

namespace {

// gamma_ref.py: hw_table(game_ramp10(0.85), 2), the 10-bit code the console
// scans out for each frame byte on a TV.
const uint16_t kTV085[256] = {
       0,    3,    6,    8,   10,   13,   15,   17,   19,   21,   24,   27,   29,   33,   36,   39,
      43,   46,   49,   53,   57,   61,   65,   70,   73,   78,   83,   88,   93,   97,  102,  107,
     111,  116,  121,  125,  130,  134,  139,  143,  148,  152,  157,  161,  166,  170,  175,  179,
     183,  188,  192,  196,  201,  206,  210,  215,  219,  223,  227,  232,  236,  240,  245,  249,
     254,  258,  262,  266,  270,  275,  280,  284,  288,  292,  296,  301,  305,  309,  313,  317,
     321,  327,  331,  335,  339,  343,  348,  352,  356,  360,  364,  369,  373,  377,  380,  385,
     389,  393,  397,  401,  406,  410,  414,  418,  423,  427,  430,  434,  439,  443,  447,  451,
     456,  459,  463,  467,  472,  476,  480,  484,  488,  492,  496,  501,  504,  508,  513,  517,
     521,  524,  529,  533,  537,  541,  545,  549,  554,  557,  561,  565,  570,  573,  577,  582,
     585,  589,  594,  598,  601,  606,  610,  613,  618,  622,  625,  629,  634,  637,  641,  646,
     649,  654,  658,  661,  666,  669,  673,  678,  681,  685,  690,  693,  697,  701,  705,  709,
     713,  717,  720,  725,  729,  732,  737,  740,  745,  748,  752,  757,  760,  764,  768,  772,
     775,  780,  783,  788,  792,  795,  800,  803,  808,  811,  815,  819,  823,  826,  831,  834,
     839,  842,  846,  850,  854,  857,  862,  865,  870,  873,  877,  881,  885,  889,  893,  896,
     901,  904,  909,  912,  915,  920,  923,  928,  931,  936,  939,  943,  947,  951,  955,  958,
     962,  966,  970,  974,  978,  981,  985,  989,  993,  997, 1001, 1004, 1008, 1012, 1016, 1020
};

// Probes from gamma_ref.py (python3 gamma_ref.py): {byte, ramp code, TV code,
// TV code of ResetGamma's identity ramp}.
struct Probe {
    int i, ramp, tv, tvIdentity;
};
const Probe kProbes[] = {
    {0, 0, 0, 0},         {1, 9, 3, 1},         {2, 16, 6, 3},        {4, 29, 10, 6},
    {8, 53, 19, 11},      {16, 97, 43, 24},     {32, 174, 111, 66},   {54, 272, 210, 153},
    {64, 315, 254, 193},  {100, 460, 406, 343}, {128, 568, 521, 462}, {192, 801, 775, 741},
    {235, 952, 943, 932}, {254, 1017, 1016, 1017}, {255, 1020, 1020, 1023},
};

// What the pass writes for a 10-bit code into an 8-bit unorm frame: the code
// as a 16-bit entry (x 64), / 65535.
double Level(int code) { return code * 64.0 / 65535.0 * 255.0; }
int ByteOut(int code) { return (int)std::floor(Level(code) + 0.5); }
// A level within 0.01 of a half: the GPU's float -> unorm conversion may round
// either way (measured: code 769, level 191.502, reads 191 on the RTX), so
// such a byte is allowed one step; every other byte must be exact.
bool NearHalf(int code) {
    const double f = Level(code) - std::floor(Level(code));
    return std::fabs(f - 0.5) < 0.01;
}
// The byte for `code` as read back: exact, or within one step at a half.
::testing::AssertionResult ByteIs(int got, int code) {
    const int want = ByteOut(code);
    if (got == want || (NearHalf(code) && std::abs(got - want) <= 1))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "read " << got << ", code " << code << " is byte " << want;
}

// The pass before item 31, on the CPU: code = floor(pow(i / 256, g) * 1024).
int OldFormulaCode(int i, float g) {
    if (i == 0) return 0;
    return (int)std::floor(std::pow((double)i / 256.0, (double)g) * 1024.0);
}

class DisplayRampTest : public EngineTestFixture {};

class DisplayRampGpuTest : public EngineTestFixture {
protected:
    void SetUp() override {
        if (IsSkipped()) return;
        if (!gWgpuRnd || !gWgpuRnd->Gpu().IsReady())
            GTEST_SKIP() << "no GPU device (MILO_NORENDER or no adapter)";
    }
    // A 256 x 4 grey ramp (byte x in column x) through the real pass; returns
    // the red byte of row 1 per column.
    std::vector<int> RunRamp(float g, int type) {
        GpuDevice &gpu = gWgpuRnd->Gpu();
        auto &dev = gpu.Device();
        const int W = 256, H = 4;
        wgpu::TextureDescriptor d{};
        d.size = {(uint32_t)W, (uint32_t)H, 1};
        d.format = wgpu::TextureFormat::RGBA8Unorm;
        d.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
            wgpu::TextureUsage::CopyDst;
        wgpu::Texture frame = dev.CreateTexture(&d);
        std::vector<uint8_t> px((size_t)W * H * 4);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t *p = &px[((size_t)y * W + x) * 4];
                p[0] = p[1] = p[2] = (uint8_t)x;
                p[3] = (uint8_t)(x ^ 0x5A);   // the pass keeps the frame's alpha
            }
        wgpu::TexelCopyTextureInfo dst{};
        dst.texture = frame;
        wgpu::TexelCopyBufferLayout layout{};
        layout.bytesPerRow = W * 4;
        wgpu::Extent3D ext = {(uint32_t)W, (uint32_t)H, 1};
        gpu.Queue().WriteTexture(&dst, px.data(), px.size(), &layout, &ext);
        wgpu::TextureView view = frame.CreateView();
        wgpu::CommandEncoder enc = dev.CreateCommandEncoder();
        EXPECT_TRUE(mRamp.Apply(enc, frame, view, g, gpu, type));
        wgpu::CommandBuffer cmd = enc.Finish();
        gpu.Queue().Submit(1, &cmd);
        std::vector<uint8_t> out((size_t)W * H * 4);
        EXPECT_TRUE(gpu.ReadbackTexture(frame, W, H, out.data(), out.size()));
        std::vector<int> r(W);
        int alphaBad = 0;
        for (int x = 0; x < W; x++) {
            r[x] = out[((size_t)1 * W + x) * 4];
            alphaBad += out[((size_t)1 * W + x) * 4 + 3] != (x ^ 0x5A);
        }
        EXPECT_EQ(alphaBad, 0) << "alpha changed in " << alphaBad << " columns (type " << type << ")";
        return r;
    }
    DisplayRamp mRamp;
};

} // namespace

// The C++ table against the Python model of the same listing, all 256 bytes.
// Sabotages: BT.709's 1.099 -> 1.0993 (S1), the encode threshold 0.018 >
// instead of >= (S2, no effect: no code lands on it), the sRGB decode
// dropped (S3), round instead of truncate after + 0.5 (S4).
TEST_F(DisplayRampTest, TVTableMatchesTheConsoleReference) {
    uint16_t hw[256];
    DisplayRamp::HardwareTable(0.85f, DisplayRamp::kDisplayTV, DisplayRamp::kDefaultPower, hw);
    int bad = 0;
    for (int i = 0; i < 256; i++) {
        if (hw[i] != kTV085[i] && bad++ < 8)
            ADD_FAILURE() << "byte " << i << ": " << hw[i] << ", gamma_ref.py " << kTV085[i];
    }
    EXPECT_EQ(bad, 0);
    for (const Probe &p : kProbes) {
        EXPECT_EQ(DisplayRamp::RampCode(0.85f, p.i), p.ramp) << "SetupGamma's code, byte " << p.i;
        EXPECT_EQ(hw[p.i], p.tv) << "TV, byte " << p.i;
    }
    // ResetGamma's identity ramp under TV: ((i * 1023) / 255) & 0x3FF, corrected.
    uint16_t table[1024];
    DisplayRamp::DisplayTable(DisplayRamp::kDisplayTV, DisplayRamp::kDefaultPower, table);
    for (const Probe &p : kProbes)
        EXPECT_EQ(table[(p.i * 1023) / 255], p.tvIdentity) << "TV, identity ramp, byte " << p.i;
    EXPECT_EQ(table[512], 461);
}

// Display type sRGB: GetCurrentGammaTable round-trips to the identity at all
// 1024 codes, so the VGA console scans DxRnd::SetupGamma's ramp out as given.
// Sabotage: the sRGB encode's 1.055 -> 1.05 (S5).
TEST_F(DisplayRampTest, SRGBTableIsTheIdentity) {
    uint16_t table[1024];
    DisplayRamp::DisplayTable(DisplayRamp::kDisplaySRGB, DisplayRamp::kDefaultPower, table);
    for (int k = 0; k < 1024; k++) ASSERT_EQ(table[k], k) << "code " << k;
    uint16_t hw[256];
    DisplayRamp::HardwareTable(0.85f, DisplayRamp::kDisplaySRGB, DisplayRamp::kDefaultPower, hw);
    for (const Probe &p : kProbes) EXPECT_EQ(hw[p.i], p.ramp) << "byte " << p.i;
}

// The real pass, display type sRGB (and every RB3 shape): byte for byte the
// formula it ran before item 31, at all 256 inputs. Sabotage: sRGB routed
// through the table pass with the TV table (S6); the ramp shader's 1024 ->
// 1023 (S7).
TEST_F(DisplayRampGpuTest, SRGBPassIsTheOldFormula) {
    const std::vector<int> r = RunRamp(0.85f, DisplayRamp::kDisplaySRGB);
    int bad = 0, halves = 0;
    for (int i = 0; i < 256; i++) {
        halves += NearHalf(OldFormulaCode(i, 0.85f));
        const ::testing::AssertionResult ok = ByteIs(r[i], OldFormulaCode(i, 0.85f));
        if (!ok && bad++ < 8) ADD_FAILURE() << "byte " << i << ": " << ok.message();
    }
    EXPECT_EQ(bad, 0);
    EXPECT_LT(halves, 8) << "the one-step allowance covers few bytes";
    // and at another gamma (RB3's tests use 1.3)
    const std::vector<int> r13 = RunRamp(1.3f, DisplayRamp::kDisplaySRGB);
    for (int i = 0; i < 256; i++) EXPECT_TRUE(ByteIs(r13[i], OldFormulaCode(i, 1.3f))) << "g 1.3, byte " << i;
}

// The real pass, display type TV: each byte comes back as the table's code.
// Then back to sRGB on the same object (the pipeline switches back), and TV
// again at another gamma (the table is rewritten). Sabotages: the shader's
// table texel scrambled (S8); the table not re-uploaded when g
// changes (S9); the alpha dropped (S13, RunRamp's alpha check).
TEST_F(DisplayRampGpuTest, TVPassIsTheTable) {
    const std::vector<int> r = RunRamp(0.85f, DisplayRamp::kDisplayTV);
    int bad = 0;
    for (int i = 0; i < 256; i++) {
        const ::testing::AssertionResult ok = ByteIs(r[i], kTV085[i]);
        if (!ok && bad++ < 8) ADD_FAILURE() << "byte " << i << ": " << ok.message();
    }
    EXPECT_EQ(bad, 0);
    EXPECT_EQ(r[16], 11);
    EXPECT_EQ(r[54], 52);
    EXPECT_EQ(r[128], 130);
    const std::vector<int> s = RunRamp(0.85f, DisplayRamp::kDisplaySRGB);
    for (int i = 0; i < 256; i++)
        EXPECT_TRUE(ByteIs(s[i], OldFormulaCode(i, 0.85f))) << "back to sRGB, byte " << i;
    uint16_t hw1[256];
    DisplayRamp::HardwareTable(1.0f, DisplayRamp::kDisplayTV, DisplayRamp::kDefaultPower, hw1);
    const std::vector<int> t1 = RunRamp(1.0f, DisplayRamp::kDisplayTV);
    for (int i = 0; i < 256; i++) EXPECT_TRUE(ByteIs(t1[i], hw1[i])) << "TV at g 1.0, byte " << i;
    EXPECT_NE(t1[54], r[54]) << "g 1.0 and 0.85 differ at 54";
}

// {rnd native_look display_tv|display_srgb|display_off|display_rule}, through
// Rnd's handler as the HTTP eval reaches it. Default (DC3_DISPLAY_GAMMA unset):
// display type TV with the config's 0.85 in every look. Sabotages: display_rule
// leaving the forced type in place (S10); display_off not reaching
// DisplayGamma (S11); the default type sRGB (S12).
TEST_F(DisplayRampTest, NativeLookDisplayTokensSwitchAndRestore) {
    if (getenv("DC3_DISPLAY_GAMMA")) GTEST_SKIP() << "DC3_DISPLAY_GAMMA overrides the default";
    auto token = [](const char *t) {
        char text[96];
        snprintf(text, sizeof(text), "(nil native_look %s)", t);
        DataArray *file = DataReadString(text);
        TheRnd.Handle(file->Array(0), true);
        file->Release();
    };
    EXPECT_EQ(rndshape::DisplayGammaType(), DisplayRamp::kDisplayTV) << "the default is a TV";
    EXPECT_NEAR(rndshape::DisplayGamma(false), 0.85f, 1e-6f);
    token("display_srgb");
    EXPECT_EQ(rndshape::DisplayGammaType(), DisplayRamp::kDisplaySRGB);
    EXPECT_NEAR(rndshape::DisplayGamma(true), 0.85f, 1e-6f);
    token("display_off");
    EXPECT_EQ(rndshape::DisplayGamma(false), 0.0f) << "display_off: no ramp, the front buffer";
    token("display_tv");
    EXPECT_EQ(rndshape::DisplayGammaType(), DisplayRamp::kDisplayTV);
    EXPECT_NEAR(rndshape::DisplayGamma(false), 0.85f, 1e-6f);
    token("display_srgb");
    token("display_rule");
    EXPECT_EQ(rndshape::DisplayTypeForInspection(), -1);
    EXPECT_EQ(rndshape::DisplayGammaType(), DisplayRamp::kDisplayTV) << "display_rule: back to the default";
    EXPECT_NEAR(rndshape::DisplayGamma(false), 0.85f, 1e-6f);
}
