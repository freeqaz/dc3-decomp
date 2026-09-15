#include "synth_xbox\IPP_basicmath_xbox.h"

namespace IPP {
// w8-j 2026-09-15 -- FLOOR at 83.333% (match_percent_normalized) for both
// Add_InPlace and Mul_InPlace, 48 B each, 9 of 13 instructions equal.  The whole
// residual is the ISSUE ORDER of the two loads, and nothing else:
//     target   lfs  f0, 0x0(r11)   /  lfsx f13, r10, r11 /  fadds f0, f13, f0
//     ours     lfsx f0, r10, r11   /  lfs  f13, 0x0(r11) /  fadds f0, f0,  f13
// Both compute f1[i] + f2[i] and store to f2[i]; only which register each load
// lands in differs, so objdiff scores it 1 insert + 1 delete + 2 diff_arg.
//
// REFUTED, do not re-derive -- three source spellings, three full ninja builds,
// all 83.333%, and `run_diff_inspect mode=mismatches` returns the SAME four rows
// every time, i.e. MSVC canonicalises them to identical code:
//   (a) f2[i] += f1[i];                       (the form below)
//   (b) f2[i] = f1[i] + f2[i];                source operand order src-first
//   (c) f2[i] = f2[i] + f1[i];                source operand order dst-first
//   (d) float acc = f2[i]; f2[i] = f1[i]*acc; explicit temp to force the dst load
// (d) is the interesting one: an explicit temp naming the dst load does not even
// move it ahead of the indexed load.
//
// The calibration is Mul() below, which is at 100.0%: for `f3[i] = f1[i]*f2[i]`
// MSVC loads the LEFT operand first (`lfsx f0` = f1[i]) and emits
// `fmuls f0, f0, f13`, i.e. issue order and operand order both follow the source.
// The in-place pair breaks that rule in the image -- it loads the dst (the plain
// `lfs 0(r11)`, which is also the store target) first but places it SECOND in the
// fadds/fmuls.  No source operand order can express "first in issue order, second
// in operand order", which is why (b) and (c) are indistinguishable here.  This
// is scheduler state, not source; it is permuter territory if anything.
    void Add_InPlace(unsigned int size, const float *f1, float *f2) {
        if (size == 0)
            return;
        for (unsigned int i = 0; i < size; i++) {
            f2[i] += f1[i];
        }
    }

    void MulConstant_InPlace(unsigned int size, float *f1, float f2) {
        if (size == 0)
            return;
        for (unsigned int i = 0; i < size; i++) {
            f1[i] *= f2;
        }
    }

    void Mul_InPlace(unsigned int size, const float *f1, float *f2) {
        if (size == 0)
            return;
        for (unsigned int i = 0; i < size; i++) {
            f2[i] *= f1[i];
        }
    }

    void Mul(unsigned int size, const float *f1, const float *f2, float *f3) {
        if (size == 0)
            return;
        for (unsigned int i = 0; i < size; i++) {
            f3[i] = f1[i] * f2[i];
        }
    }

}
