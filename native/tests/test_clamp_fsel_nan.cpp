// Float Min / Max / Clamp / ClampEq (math/Utl.h) follow retail's fsel on NaN.
//
// Port of rb3-xenon e58933c62 (lane W16-UJ, "native Clamp keeps NaN like
// retail's fsel"). The float specialisations exist "for the use of fsel
// instructions": MSVC compiles `(x - y < 0) ? x : y` to
// `fsubs d,x,y ; fsel r,d,y,x`, and `fsel fD,fA,fC,fB` is `fA >= 0 ? fC : fB`
// with an unordered (NaN) fA selecting fB. Read in dc3's own image,
// RndHiResScreen's `Clamp(0.0f, 1.0f, x0)` at 0x826270A8..0x826270C8:
//     fneg f5,f12 ; fsel f12,f5,f13,f12     Max(0, v)  = fsel(0 - v, 0, v)
//     fsubs f8,f12,f0 ; fsel f12,f8,f0,f12  Min(v, 1)  = fsel(v - 1, 1, v)
// so retail Clamp(0, 1, NaN) is NaN. The C++ condition `x - y < 0` is false
// for NaN and returned the OTHER operand natively, mapping NaN to the lower
// bound.
//
// The reference below is a model of the fsel instruction written here, not a
// call into Utl.h.
#include "test_helpers.h"

#include "math/Utl.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace {

// fsel fD,fA,fC,fB
float Fsel(float a, float c, float b) { return (a >= 0.0f) ? c : b; }
float RetailMin(float x, float y) { return Fsel(x - y, y, x); }
float RetailMax(float x, float y) { return Fsel(x - y, x, y); }
float RetailClamp(float lo, float hi, float v) { return RetailMin(RetailMax(lo, v), hi); }

// The pre-port native spelling, for the finite-input control.
float OldMin(float x, float y) { return (x - y < 0) ? x : y; }
float OldMax(float x, float y) { return (x - y < 0) ? y : x; }

bool SameBits(float a, float b) { return memcmp(&a, &b, sizeof(float)) == 0; }

std::vector<float> Grid() {
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float den = std::numeric_limits<float>::denorm_min();
    return { nan, -nan, inf, -inf, 0.0f, -0.0f, den, -den, 1.0f, -1.0f,
             2.0f, -2.0f, 0.5f, -0.5f, 1e30f, -1e30f, 3.25f, -7.75f };
}

bool AnyNaN(float a, float b) { return std::isnan(a) || std::isnan(b); }

} // namespace

TEST(ClampFselNaN, NaNOperandsSelectLikeFsel) {
    std::vector<float> g = Grid();
    int cases = 0, bad = 0;
    std::string firstBad;
    auto check = [&](const char *what, float x, float y, float got, float want) {
        cases++;
        if (!SameBits(got, want)) {
            if (bad++ == 0) {
                char buf[160];
                snprintf(buf, sizeof buf, "%s(%g, %g) = %g, retail fsel gives %g", what,
                         x, y, got, want);
                firstBad = buf;
            }
        }
    };
    for (float x : g) {
        for (float y : g) {
            if (!AnyNaN(x, y) && !std::isnan(x - y))
                continue;
            check("Min", x, y, Min(x, y), RetailMin(x, y));
            check("Max", x, y, Max(x, y), RetailMax(x, y));
        }
    }
    ASSERT_GT(cases, 50) << "grid lost its NaN cases";
    EXPECT_EQ(bad, 0) << bad << " of " << cases << " NaN-difference cases differ; first: "
                      << firstBad;
}

TEST(ClampFselNaN, ClampOfNaNIsNaN) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // The retail site above, and rb3-xenon's CompressDelta Clamp(-2, 2, d).
    EXPECT_TRUE(std::isnan(Clamp(0.0f, 1.0f, nan))) << "got " << Clamp(0.0f, 1.0f, nan);
    EXPECT_TRUE(std::isnan(Clamp(-2.0f, 2.0f, nan))) << "got " << Clamp(-2.0f, 2.0f, nan);
    EXPECT_TRUE(std::isnan(RetailClamp(-2.0f, 2.0f, nan)));

    float v = nan;
    float lo = -2.0f, hi = 2.0f;
    ClampEq(v, lo, hi);
    EXPECT_TRUE(std::isnan(v)) << "ClampEq turned NaN into " << v;

    // Every (lo, hi, v) in the grid with a NaN anywhere.
    std::vector<float> g = Grid();
    int cases = 0, bad = 0;
    for (float lo2 : g)
        for (float hi2 : g)
            for (float v2 : g) {
                if (!std::isnan(lo2) && !std::isnan(hi2) && !std::isnan(v2))
                    continue;
                cases++;
                if (!SameBits(Clamp(lo2, hi2, v2), RetailClamp(lo2, hi2, v2)))
                    bad++;
            }
    ASSERT_GT(cases, 1000);
    EXPECT_EQ(bad, 0) << bad << " of " << cases << " NaN Clamp cases differ from fsel";
}

// Control: for inputs whose difference is not NaN the fsel spelling and the
// original `x - y < 0` spelling are the same function, so the port changes no
// finite result.
TEST(ClampFselNaN, NonNaNInputsUnchanged) {
    std::vector<float> g = Grid();
    int cases = 0, bad = 0;
    for (float x : g)
        for (float y : g) {
            if (AnyNaN(x, y) || std::isnan(x - y))
                continue;
            cases++;
            if (!SameBits(Min(x, y), OldMin(x, y)) || !SameBits(Min(x, y), RetailMin(x, y)))
                bad++;
            if (!SameBits(Max(x, y), OldMax(x, y)) || !SameBits(Max(x, y), RetailMax(x, y)))
                bad++;
        }
    ASSERT_GT(cases, 200);
    EXPECT_EQ(bad, 0);
}
