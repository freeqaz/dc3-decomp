#include "FFT.h"
#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdlib>
#include "xdk\LIBCMT\vectorintrinsics.h"

// VMX128 intrinsics used by the AltiVec kernels below.  The shared header
// (xdk/LIBCMT/vectorintrinsics.h) is reached through the PCH, so they are
// declared here rather than there: adding a declaration is all MSVC needs to
// emit the opcode.  (w8-f moved this block from just above
// fft_real_forward_altivec to the top of the file so fft_altivec and
// fft_recursive can use it too; measured inert on every other function in the
// unit -- a declaration reaching a translation unit earlier cannot change the
// code of a function that does not call it.)
extern "C" {
XMVECTOR __vsel(XMVECTOR vSrcA, XMVECTOR vSrcB, XMVECTOR vMask);
XMVECTOR __vaddfp(XMVECTOR vSrcA, XMVECTOR vSrcB);
XMVECTOR __vsubfp(XMVECTOR vSrcA, XMVECTOR vSrcB);
}

// External declarations
int FFTComplex(float* data, long size, long inverse, float* context);
int fft_pingpong(float* data, unsigned long size, long sign, float* context);
int fft_square_matrix(float* data, long size, long inverse, float* context);
int fft_recursive(float* data, unsigned long size, long sign, float* context);
int fft_scalar(float* a, float* b, unsigned long size, long sign, float* twiddle);
int fft_altivec(float* a, float* b, unsigned long size, long sign, float* twiddle);
int fft_real_forward_altivec(float* data, long size, float* context);
void SquareComplexTransposeVector(float* data, long size);

// Lazily-grown ping-pong scratch buffer shared by fft_pingpong / fft_recursive.
struct FftScratch {
    void* buf;
    unsigned long size;
};
static FftScratch g_fftScratch;

// Real-input forward FFT (scalar). Computes a half-length complex FFT then
// recombines the bins. data holds size real samples (treated as size/2 complex
// pairs); context is FFTComplex scratch.
//
// Three spellings in here are load-bearing (w7-an, 80.9 -> 83.7 canonical):
//  * the loop bound is `size >> 2` written INLINE.  Lifted into a
//    `unsigned int count` local it becomes a provable trip count and MSVC
//    converts the loop to CTR (`mtctr` / `bdnz`); the image keeps an explicit
//    `k` against `size >> 2` in r8 (`addi r9, r9, 0x1` / `cmplw cr6, r9, r8` /
//    `blt`, 0x82E50A4C-0x82E50ABC).  Same lever as CalculateSinCosTable below.
//  * `hi` is `data + size - 2`, indexed `hi[1]` / `hi[0]`, NOT `data + size`
//    with `hi[-1]` / `hi[-2]`.  The image computes `subi r10, r30, 0x2` /
//    `slwi` / `add` and then biases `+0x8` inside the loop preheader
//    (0x82E509D4, 0x82E50A18) -- the bias is MSVC's, so the source value it
//    started from is data+size-2.
//  * the low half IS walked with a `float* lo`, and the walk must be spelled
//    as two separate `++lo` BETWEEN the two stores (w7-bl, 83.70 -> 85.70
//    canonical; this REFUTES the earlier w7-an bullet that said to subscript
//    off `data`).  Subscripting `data[k * 2 + 2]` / `data[k * 2 + 3]` makes
//    MSVC fold the advance into one `stfsu fN, 0x8(r11)` with a -4 base bias;
//    storing through `*lo` and stepping it twice reproduces the image's plain
//    `stfs 0x0(r11)` / `stfs 0x4(r11)` pair plus `addi r7, r11, 0x4` /
//    `addi r11, r7, 0x4` (0x82E50A5C, 0x82E50AB8).  Rows 92-110 are now equal.
//
// RESIDUAL (w7-bl, 85.70 canonical, 43 of 111 rows): one FPR colouring
// decision in the DC/Nyquist preheader, and the schedule it drives.
//  * f0 <-> f13 (15 rows): the image loads `c = 1.0` into f13 and `s = 0.0`
//    into f0 -- i.e. c takes the register `im0` died in and s takes `re0`'s.
//    We colour them the other way round, and the whole twiddle recurrence
//    (idx 66-91, `fmadd`/`fnmsub` chain) inherits the swap.
//  * f30 <-> f31 (4 rows): image holds `inv_n` in f31 and `sin_a` in f30; ours
//    are reversed.
//  * The 12 insert/delete rows are the same cause seen as scheduling: the
//    image computes `fmul f10, f30, f30` (cc) immediately after the second
//    `bl sin` (idx 35) so f10 is occupied before the DC/Nyquist loads, which
//    forces the sum into f8 and stores `data[1]` before `data[0]`.  We compute
//    cc after the loads and store `data[0]` first (the 1 OFFSET_SWAP row).
// Measured-inert spellings, all four re-measured in this worktree at 85.70
// with an identical 26/5/6/6 row split:
//    - swapping the `data[0]` / `data[1]` store statements;
//    - hoisting `double cc = sin_a * sin_a;` above the `sin_2a` call (hoping
//      MSVC would sink the multiply to just after the call, as the image has);
//    - routing both bins through named `float diff0` / `float sum0` temps so
//      the subtraction is computed and stored first;
//    - writing `sum_im` as `lo_im + hi_im` (the image's textual operand order
//      for the idx-73 `fadds`) -- COMMUTATIVE_OP_ORDER stays at 1 either way.
//  Swapping the `c` / `s` declarations was already measured inert by w7-an.
//
// w7-br (85.6 -> 88.0 canonical, 39 of 110 rows): the store-order half of the
// residual is source-addressable after all, but the lever is the POSITION of
// the `ss` / `c` / `s` declarations, not the store statements.  With those
// three between the two bin stores, MSVC emits the second store first and
// defers the first bin's fadds/fsubs to just before its store, whichever bin
// is written first (that is why w7-bl's swap read inert: it swapped both).
// Declaring `ss`, `c`, `s` BEFORE the bins and writing the two stores
// adjacent, sum first, gives the image's `stfs 0x0(r31)`-early shape and the
// 2 OFFSET_SWAP rows go.  Also measured: diff-first adjacent 86.7; named
// diff0/sum0 temps with the declarations before the bins 86.7; `cc` folded
// as `sin_a * sin_a * 2.0` inert; `cc = cc * 2.0` moved above the bins
// inert; `hi` / `lo` moved above the bins inert; `data[0]` re-read in place
// of `re0` 85.6 (load order flips); the bins' stores written after `hi`/`lo`
// 85.6 (separation flips them again).
// What is left (39 rows) is ONE scheduling decision and the colouring it
// drives: the image computes `fmul f10, f30, f30` (cc = sin_a^2) as the very
// first instruction after the second `bl sin` (0x82E509BC), before the
// `lfs f0, 0x0(r31)` / `lfs f13, 0x4(r31)` loads (0x82E509C8 / 0x82E509D0).
// We load first, add, store `data[0]`, and only then multiply -- into the f10
// that the sum just vacated -- so the sum never needs f8, c/s take f0/f13
// instead of f13/f0 (0x82E509EC / 0x82E509F4), and the loop inherits the
// swap (14 rows).  Every position of the `cc` statement from the top of the
// block to just above the loop schedules the multiply after the first store.
// f30 <-> f31 (inv_n / sin_a, 4 rows) is the callee-saved pair's allocation
// order and moves with nothing above.
// Bug-family check (worklist): fft_matrix_inverse_columnwise's harvested
// defect was a loop advancing the malloc'd pointer it later freed.  Nothing
// here is freed; `data` (r31) is never advanced, only `lo` and `hi` walk, and
// the image agrees (`addi r11, r31, 0x8` at 0x82E50A00 rebases `lo` off r31).
int fft_real_forward_scalar(float* data, unsigned long size, float* context) {
    if (size < 2) {
        return 0;
    }
    int ret = FFTComplex(data, (long)(size >> 1), -1, context);
    {
        if (ret == 0) {
            float inv_n = 1.0f / (float)(double)(long long)(unsigned int)size;
            float sin_a = (float)sin(inv_n * (float)M_PI);
            double sin_2a = sin(inv_n * (float)(2.0 * M_PI));

            double cc = (double)sin_a * (double)sin_a;
            // ss, c and s are declared BEFORE the DC/Nyquist bins, and the two
            // bin stores are ADJACENT, sum first (w7-br, 85.6 -> 88.0
            // canonical).  With the three declarations between the stores,
            // MSVC emits the SECOND store first and defers the first bin's
            // arithmetic to just before its store; adjacent, the stores keep
            // source order.  Diff-first adjacent is 86.7; pre-computing both
            // bins into named temps changes nothing either way.
            float ss = (float)sin_2a;
            double c = 1.0;
            double s = 0.0;

            // DC / Nyquist bins.
            float re0 = data[0];
            float im0 = data[1];
            data[0] = im0 + re0;
            data[1] = re0 - im0;

            float* hi = data + size - 2;
            float* lo = data + 2;

            cc = cc * 2.0;
            for (unsigned int k = 0; k < (size >> 2); ++k) {
                float hi_im = hi[1];
                float lo_im = lo[1];
                float diff_im = lo_im - hi_im;
                float lo_re = lo[0];
                float hi_re = hi[0];
                float sum_im = hi_im + lo_im;
                float sum_re = hi_re + lo_re;
                float diff_re = lo_re - hi_re;

                // Advance the twiddle via the trig recurrence.
                double upd_c = s * ss + c * cc;
                double upd_s = s * cc - c * ss;
                float neg_diff_im = -diff_im;
                c = c - upd_c;
                s = s - upd_s;

                double a = (double)sum_im * c + (double)sum_re;
                double b = (double)diff_im - (double)diff_re * c;
                double d = (double)neg_diff_im - (double)diff_re * c;
                double e = (double)sum_re - (double)sum_im * c;

                a = a - (double)diff_re * s;
                b = b - (double)sum_im * s;
                d = d - (double)sum_im * s;
                e = e + (double)diff_re * s;

                *lo = (float)a * 0.5f;
                ++lo;
                *lo = (float)b * 0.5f;
                ++lo;
                hi[1] = (float)d * 0.5f;
                hi[0] = (float)e * 0.5f;

                hi -= 2;
            }
        }
    }
    return ret;
}

// Iterative radix-2 Cooley-Tukey complex FFT (scalar, ping-pong buffers).
// a/b are the two scratch buffers of size complex pairs; sign is +/-1 to pick
// the transform direction; twiddle is a precomputed cos/sin table. The final
// pass divides by size when sign > 0 (inverse normalization).
// Builds a quarter-symmetric cos/sin twiddle table. table holds n/2 complex
// (cos,sin) pairs; only the first quarter is computed via trig, the rest filled
// by the (-sin, cos) symmetry. Small-n cases (n < 4) are special-cased.
//
// Two spellings carry this to 100 and neither is cosmetic (w7-an):
//  * `n / 2` must be written INLINE in the subscript, not lifted into a
//    `long half` local.  As a local it is an ordinary loop-invariant and MSVC
//    strength-reduces `table[j + half]` into a second walking pointer
//    (`stfs 0x4(rN)` / `stfsu 0x8(rN)`); written inline it is hoisted by the
//    invariant pass instead and the address is rebuilt every iteration --
//    `add r11, r29, r27` / `slwi` / `add r11, r11, r28`, which is what the
//    image does.  75.0 -> 96.9 canonical on that change alone.
//  * the `j` counter must be spelled `i * 2`, not carried as its own `long j`
//    with `j += 2`.  A source-level `j` gets its `li 0` next to `i`'s, before
//    the zero-trip guard; as a compiler-created induction variable its init
//    lands in the loop PREHEADER after the invariant hoists, which is where
//    the image's `li r29, 0x0` sits.  96.9 -> 100.0.
int CalculateSinCosTable(long n, float* table) {
    if (n < 4) {
        table[0] = 1.0f;
        table[1] = 0.0f;
        if (n == 2) {
            table[3] = 0.0f;
            table[2] = -1.0f;
        }
        return 0;
    }

    long count = n / 4;
    double twoPi = 6.2831854820251465;
    for (long i = 0; i < count; ++i) {
        float angle = (float)((double)i * twoPi / (double)n);
        float cv = (float)cos(angle);
        float sv = (float)sin(angle);
        table[i * 2] = cv;
        table[i * 2 + 1] = sv;
        table[i * 2 + n / 2] = -sv;
        table[i * 2 + n / 2 + 1] = cv;
    }
    return 0;
}

// Runs a complex FFT through the shared ping-pong scratch buffer, growing it on
// demand. Small transforms (< 16 pts) use the scalar kernel; larger ones the
// AltiVec kernel. Returns 0xc on allocation failure.
int fft_pingpong(float* data, unsigned long size, long sign, float* context) {
    int err = 0;
    if (g_fftScratch.size < size) {
        void* old = g_fftScratch.buf;
        g_fftScratch.size = size;
        if (old != 0) {
            free(old);
        }
        void* p = malloc(size << 3);
        g_fftScratch.buf = p;
        if (p == 0) {
            err = 0xc;
            g_fftScratch.buf = 0;
            g_fftScratch.size = 0;
        }
    }
    float* buf = (float*)g_fftScratch.buf;
    int result = err;
    if (err == 0) {
        if (size < 0x10) {
            result = fft_scalar(data, buf, size, sign, context);
        } else {
            result = fft_altivec(data, buf, size, sign, context);
        }
    }
    return result;
}

// Top-level complex FFT dispatcher. Sizes above 0x8000 are decomposed by
// transform length parity: even log2 -> square-matrix path, odd log2 ->
// recursive path. Small sizes go straight through the ping-pong buffer.
int FFTComplex(float* data, long size, long inverse, float* context) {
    if (size <= 0x8000) {
        return fft_pingpong(data, (unsigned long)size, inverse, context);
    }

    long power = 1;
    if (size == 1) {
        power = 0;
    } else {
        long p2 = 2;
        if (size > 2) {
            do {
                p2 *= 2;
                power += 1;
            } while (p2 < size);
        }
    }

    if ((power & 1) == 0) {
        return fft_square_matrix(data, size, inverse, context);
    }
    return fft_recursive(data, (unsigned long)size, inverse, context);
}

// w8-f (2026-09-15).  RECONSTRUCTED FROM THE TARGET LISTING -- there was no
// body here at all before (case 1: declared at the top of this file, defined
// nowhere in src/; objdiff read 532 insert / 0 base = 0.0%).  rb3-xenon's
// src/system/synth_xbox/FFT.cpp declares the same two symbols and also never
// defines them, so there is no port source for this pair in any sibling tree.
//
// One decimation-in-frequency radix-2 step, vectorised, followed by two
// half-length recursive transforms and a de-interleave:
//
//   * grow the shared ping-pong scratch to size/2 complex (the SAME block
//     fft_pingpong runs, but sized `size >> 1`; 0x82E4FCFC-0x82E4FD90);
//   * one butterfly pass over the whole array, four vectors at a time from
//     BOTH ends of BOTH halves at once -- lo ascending from data, lo
//     descending from the midpoint, hi ascending from the midpoint, hi
//     descending from the end.  Sums go back in place; differences are
//     twiddled.  The body is written as two identical steps because the image
//     is (loop count `size >> 4`, pointers advancing 0x20 per iteration:
//     0x82E4FFE4/0x82E50004 for the low read pointer);
//   * FFTComplex on the upper half then the lower half (0x82E503F0,
//     0x82E5040C) -- note the UPPER half first;
//   * copy the lower half's result into the scratch buffer (0x82E50430);
//   * interleave scratch (even bins) with the upper half (odd bins) back into
//     data with the two merge permutes (0x82E5049C-0x82E504F4).
//
// The descending half's twiddle is the negated conjugate of the ascending
// one: for bin N/2-1-k, cos(pi - theta) = -cos(theta) and sin(pi - theta) =
// +sin(theta), which is why cHi is negated WHOLESALE (`vsubfp128 v13, v127,
// v63` at 0x82E4FF88) while the sign pattern inside sLo/sHi is the usual
// {+s, -s} complex-multiply-by-conjugate pair.
//
// `dir` is +1.0 for sign == -1 and -1.0 otherwise (0x82E4FDF4-0x82E4FE10),
// and it also selects which of the two word-interleave permutes is copied to
// the `perm_sel` slot -- the forward one builds {s, -s} and the inverse one
// {-s, s}.
//
// RESIDUAL (w8-f, 48.75376 canonical / 45.417294 fuzzy, 648 emitted against
// 532 in the image).  PARTIAL by construction: the skeleton, the constants,
// the trig recurrence, the recursion and both tail loops are recovered; the
// per-iteration register colouring and the stack-slot assignment are not.
//
// ⚠ READ THIS BEFORE TUNING.  At this structural distance the canonical
// number is dominated by how objdiff ALIGNS two ~600-instruction loop
// bodies, and a one-line source reorder flips the alignment wholesale.  Four
// changes that are individually justified BY THE LISTING all measured WORSE,
// every one of them re-measured from a full `ninja` in this worktree:
//   * moving the eight walking pointers above the sin/cos block, where the
//     image computes them (0x82E4FE40-0x82E4FE94, between the `lfs 1.0` and
//     the `fdivs`): 48.30 -> 27.3 alone.
//   * computing `angle2` AFTER the first sin() call, which is what the image
//     does (`fmuls f24, f31, f0` at 0x82E4FEB4, between `frsp f13, f1` and
//     `fmul f13, f13, f13`) and what fft_real_forward_altivec already spells:
//     48.30 -> 25.56 ALONE.  That is the single most surprising number here.
//   * all four of {v_zero declared above the scratch block so it is loaded
//     into the callee-saved v127 before free/malloc, as at 0x82E4FD20; the
//     body wrapped in `if (ret == 0)` with one return, matching
//     `mr r3, r31` / `cmpwi cr6, r31, 0` / `bne` at 0x82E4FD94; merge_lo /
//     merge_hi declared below the sign branch so MSVC reuses perm_sel_inv's
//     0x90 slot the way the image does at 0x82E4FE50; plus the pointer
//     hoist} TOGETHER: 48.30 -> 31.3 -- even though that variant is strictly
//     CLOSER on every structural measure objdiff reports (frame delta -0x10
//     instead of -0x30, 1 DIFFER / 17 PERMUTED stack slots instead of
//     6 DIFFER / 18 PERMUTED / 3 base-only, and 115 REGISTER_SWAP
//     instructions instead of 192).
// The lesson is that percentage and structural agreement have DECOUPLED
// here.  Do not read a drop as "that reading of the listing was wrong" --
// three of those four changes are demonstrably what the image does.  They
// will pay once the butterfly body itself pairs; until then they only move
// the alignment.  Judge the next round of work on the structural counters,
// and re-check the percentage at the end.
// The one change that paid was the CTR lever, and it paid because it made a
// BRANCH match rather than moving code: 48.30 -> 48.75.
int fft_recursive(float* data, unsigned long size, long sign, float* context) {
    XMVECTORU32 perm_sel_fwd = { 0x00010203, 0x10111213, 0x04050607, 0x14151617 };
    XMVECTORU32 perm_sel_inv = { 0x10111213, 0x00010203, 0x14151617, 0x04050607 };

    int ret = 0;
    unsigned long half = size >> 1;
    if (g_fftScratch.size < half) {
        void* old = g_fftScratch.buf;
        g_fftScratch.size = half;
        if (old != 0) {
            free(old);
        }
        void* p = malloc(half << 3);
        g_fftScratch.buf = p;
        if (p == 0) {
            ret = 0xc;
            g_fftScratch.buf = 0;
            g_fftScratch.size = 0;
        }
    }
    if (ret != 0) {
        return ret;
    }

    // Swaps re/im inside each of the two complex slots of a vector.
    XMVECTORU32 perm_swap = { 0x04050607, 0x00010203, 0x0C0D0E0F, 0x08090A0B };
    // {A.x, A.x, B.w, B.w} -- carries the previous cHi's first lane and the
    // freshly updated c1 into the next iteration's cLo.
    XMVECTORU32 perm_cdup = { 0x00010203, 0x00010203, 0x1C1D1E1F, 0x1C1D1E1F };
    XMVECTORU32 sel_hi = { 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF };
    XMVECTORU32 merge_lo = { 0x00010203, 0x04050607, 0x10111213, 0x14151617 };
    XMVECTORU32 merge_hi = { 0x08090A0B, 0x0C0D0E0F, 0x18191A1B, 0x1C1D1E1F };

    XMVECTOR v_zero = { 0.0f, 0.0f, 0.0f, 0.0f };

    double dir;
    XMVECTOR perm_sel;
    if (sign == -1) {
        dir = 1.0;
        perm_sel = perm_sel_fwd.v;
    } else {
        dir = -1.0;
        perm_sel = perm_sel_inv.v;
    }

    float inv_n = 1.0f / (float)(double)(long long)(unsigned int)size;
    float angle1 = inv_n * (float)(2.0 * M_PI);
    float angle2 = inv_n * (float)(4.0 * M_PI);

    float sin_a = (float)sin(angle1);
    double cc = (double)sin_a * (double)sin_a;
    cc = cc * 2.0;
    double ss = (float)sin(angle2);

    XMVECTORF32 sv;
    sv.f[0] = 0.0f;
    sv.f[1] = 0.0f;
    double s1 = (float)sin(angle1);
    double t1 = s1 * dir;
    sv.f[2] = (float)t1;
    sv.f[3] = (float)(-t1);
    XMVECTOR sLo = sv.v;
    double s2 = (float)sin(angle2);
    double t2 = s2 * dir;
    sv.f[0] = (float)t2;
    sv.f[1] = (float)(-t2);
    XMVECTOR sHi = sv.v;

    sv.f[0] = 1.0f;
    sv.f[1] = 1.0f;
    double c1 = (float)cos(angle1);
    sv.f[2] = (float)c1;
    sv.f[3] = (float)c1;
    XMVECTOR cLo = sv.v;
    double c2 = (float)cos(angle2);
    sv.f[0] = (float)c2;
    sv.f[1] = (float)c2;
    XMVECTOR cHi = sv.v;
    XMVECTOR negCHi = __vsubfp(v_zero, cHi);

    float* loRead = data;
    float* hiRead = data + (size / 4) * 4;
    float* loReadBack = hiRead - 4;
    float* hiReadBack = data + half * 4 - 4;
    float* loWrite = data;
    float* loWriteBack = loReadBack;
    float* hiWrite = hiRead;
    float* hiWriteBack = hiReadBack;

    // w8-f: the bound is written INLINE in each condition -- as a local it is a
    // provable trip count and MSVC converts to CTR (mtctr/bdnz), where the image
    // keeps `addi r11, r11, 0x1` / `cmplw cr6, r11, r22` / `blt` (0x82E5014C,
    // 0x82E5015C, 0x82E50190).  Same lever as CalculateSinCosTable above.

    if (sign == -1) {
        for (unsigned int i = 0; i < (size >> 4); ++i) {
            XMVECTOR hiA = __lvx(hiRead, 0);
            XMVECTOR loA = __lvx(loRead, 0);
            XMVECTOR loB = __lvx(loReadBack, 0);
            XMVECTOR hiB = __lvx(hiReadBack, 0);

            XMVECTOR diffA = __vsubfp(loA, hiA);
            XMVECTOR diffB = __vsubfp(loB, hiB);
            XMVECTOR sumA = __vaddfp(loA, hiA);
            XMVECTOR sumB = __vaddfp(loB, hiB);

            double uc1 = c1 * cc + s1 * ss;
            double uc2 = c2 * cc + s2 * ss;
            double us1 = s1 * cc - c1 * ss;
            double us2 = s2 * cc - c2 * ss;

            XMVECTOR swapA = __vperm(diffA, diffA, perm_swap.v);
            XMVECTOR swapB = __vperm(diffB, diffB, perm_swap.v);

            __stvx(sumA, loWrite, 0);
            __stvx(sumB, loWriteBack, 0);

            c1 = c1 - uc1;
            c2 = c2 - uc2;
            s1 = s1 - us1;
            s2 = s2 - us2;

            XMVECTOR outA = __vmaddfp(sLo, swapA, __vmaddfp(cLo, diffA, v_zero));
            XMVECTOR outB = __vmaddfp(sHi, swapB, __vmaddfp(negCHi, diffB, v_zero));

            __stvx(outA, hiWrite, 0);
            __stvx(outB, hiWriteBack, 0);

            sv.f[3] = (float)c1;
            sv.f[1] = (float)s1;
            sv.f[2] = (float)c2;
            sv.f[0] = (float)s2;
            XMVECTOR trig = sv.v;
            XMVECTOR negTrig = __vsubfp(v_zero, trig);

            XMVECTOR nextSHi = __vperm(trig, negTrig, perm_sel);
            XMVECTOR nextCLo = __vsubfp(v_zero, __vperm(negCHi, negTrig, perm_cdup.v));
            sLo = __vsel(sHi, nextSHi, sel_hi.v);
            negCHi = __vmrglw(negTrig, negTrig);
            sHi = nextSHi;
            cLo = nextCLo;

            loRead += 4;
            hiRead += 4;
            loReadBack -= 4;
            hiReadBack -= 4;
            loWrite += 4;
            loWriteBack -= 4;
            hiWrite += 4;
            hiWriteBack -= 4;

            XMVECTOR hiA2 = __lvx(hiRead, 0);
            XMVECTOR loA2 = __lvx(loRead, 0);
            XMVECTOR loB2 = __lvx(loReadBack, 0);
            XMVECTOR hiB2 = __lvx(hiReadBack, 0);

            XMVECTOR diffA2 = __vsubfp(loA2, hiA2);
            XMVECTOR diffB2 = __vsubfp(loB2, hiB2);
            XMVECTOR sumA2 = __vaddfp(loA2, hiA2);
            XMVECTOR sumB2 = __vaddfp(loB2, hiB2);

            double uc1b = c1 * cc + s1 * ss;
            double uc2b = c2 * cc + s2 * ss;
            double us1b = s1 * cc - c1 * ss;
            double us2b = s2 * cc - c2 * ss;

            XMVECTOR swapA2 = __vperm(diffA2, diffA2, perm_swap.v);
            XMVECTOR swapB2 = __vperm(diffB2, diffB2, perm_swap.v);

            __stvx(sumA2, loWrite, 0);
            __stvx(sumB2, loWriteBack, 0);

            c1 = c1 - uc1b;
            c2 = c2 - uc2b;
            s1 = s1 - us1b;
            s2 = s2 - us2b;

            XMVECTOR outA2 = __vmaddfp(sLo, swapA2, __vmaddfp(cLo, diffA2, v_zero));
            XMVECTOR outB2 = __vmaddfp(sHi, swapB2, __vmaddfp(negCHi, diffB2, v_zero));

            __stvx(outA2, hiWrite, 0);
            __stvx(outB2, hiWriteBack, 0);

            sv.f[3] = (float)c1;
            sv.f[1] = (float)s1;
            sv.f[2] = (float)c2;
            sv.f[0] = (float)s2;
            XMVECTOR trig2 = sv.v;
            XMVECTOR negTrig2 = __vsubfp(v_zero, trig2);

            XMVECTOR nextSHi2 = __vperm(trig2, negTrig2, perm_sel);
            XMVECTOR nextCLo2 = __vsubfp(v_zero, __vperm(negCHi, negTrig2, perm_cdup.v));
            sLo = __vsel(sHi, nextSHi2, sel_hi.v);
            negCHi = __vmrglw(negTrig2, negTrig2);
            sHi = nextSHi2;
            cLo = nextCLo2;

            loRead += 4;
            hiRead += 4;
            loReadBack -= 4;
            hiReadBack -= 4;
            loWrite += 4;
            loWriteBack -= 4;
            hiWrite += 4;
            hiWriteBack -= 4;
        }
    } else {
        // Inverse: every input vector is halved on the way in, so the whole
        // recursion contributes the 1/size normalisation one level at a time
        // (0x82E50198 loads __vmx@3f000000.. and each of the four loads gets a
        // `vmaddcfp128 vN, v0, v127`).
        XMVECTOR v_half = { 0.5f, 0.5f, 0.5f, 0.5f };
        for (unsigned int i = 0; i < (size >> 4); ++i) {
            XMVECTOR loA = __vmaddfp(v_half, __lvx(loRead, 0), v_zero);
            XMVECTOR hiA = __vmaddfp(v_half, __lvx(hiRead, 0), v_zero);
            XMVECTOR loB = __vmaddfp(v_half, __lvx(loReadBack, 0), v_zero);
            XMVECTOR hiB = __vmaddfp(v_half, __lvx(hiReadBack, 0), v_zero);

            double uc1 = c1 * cc + s1 * ss;
            double uc2 = c2 * cc + s2 * ss;
            double us1 = s1 * cc - c1 * ss;
            double us2 = s2 * cc - c2 * ss;

            XMVECTOR diffA = __vsubfp(loA, hiA);
            XMVECTOR sumA = __vaddfp(loA, hiA);
            __stvx(sumA, loWrite, 0);
            XMVECTOR diffB = __vsubfp(loB, hiB);
            XMVECTOR sumB = __vaddfp(loB, hiB);
            __stvx(sumB, loWriteBack, 0);

            c1 = c1 - uc1;
            s1 = s1 - us1;
            c2 = c2 - uc2;
            s2 = s2 - us2;

            XMVECTOR swapA = __vperm(diffA, diffA, perm_swap.v);
            XMVECTOR swapB = __vperm(diffB, diffB, perm_swap.v);

            XMVECTOR outA = __vmaddfp(sLo, swapA, __vmaddfp(cLo, diffA, v_zero));
            XMVECTOR outB = __vmaddfp(sHi, swapB, __vmaddfp(negCHi, diffB, v_zero));

            __stvx(outA, hiWrite, 0);
            __stvx(outB, hiWriteBack, 0);

            sv.f[0] = (float)s2;
            sv.f[1] = (float)s1;
            sv.f[2] = (float)c2;
            sv.f[3] = (float)c1;
            XMVECTOR trig = sv.v;
            XMVECTOR negTrig = __vsubfp(v_zero, trig);

            XMVECTOR nextSHi = __vperm(trig, negTrig, perm_sel);
            XMVECTOR nextCLo = __vsubfp(v_zero, __vperm(negCHi, negTrig, perm_cdup.v));
            sLo = __vsel(sHi, nextSHi, sel_hi.v);
            negCHi = __vmrglw(negTrig, negTrig);
            sHi = nextSHi;
            cLo = nextCLo;

            loRead += 4;
            hiRead += 4;
            loReadBack -= 4;
            hiReadBack -= 4;
            loWrite += 4;
            loWriteBack -= 4;
            hiWrite += 4;
            hiWriteBack -= 4;

            XMVECTOR loA2 = __vmaddfp(v_half, __lvx(loRead, 0), v_zero);
            XMVECTOR hiA2 = __vmaddfp(v_half, __lvx(hiRead, 0), v_zero);
            XMVECTOR loB2 = __vmaddfp(v_half, __lvx(loReadBack, 0), v_zero);
            XMVECTOR hiB2 = __vmaddfp(v_half, __lvx(hiReadBack, 0), v_zero);

            double uc1b = c1 * cc + s1 * ss;
            double uc2b = c2 * cc + s2 * ss;
            double us1b = s1 * cc - c1 * ss;
            double us2b = s2 * cc - c2 * ss;

            XMVECTOR diffA2 = __vsubfp(loA2, hiA2);
            XMVECTOR sumA2 = __vaddfp(loA2, hiA2);
            __stvx(sumA2, loWrite, 0);
            XMVECTOR diffB2 = __vsubfp(loB2, hiB2);
            XMVECTOR sumB2 = __vaddfp(loB2, hiB2);
            __stvx(sumB2, loWriteBack, 0);

            c1 = c1 - uc1b;
            s1 = s1 - us1b;
            c2 = c2 - uc2b;
            s2 = s2 - us2b;

            XMVECTOR swapA2 = __vperm(diffA2, diffA2, perm_swap.v);
            XMVECTOR swapB2 = __vperm(diffB2, diffB2, perm_swap.v);

            XMVECTOR outA2 = __vmaddfp(sLo, swapA2, __vmaddfp(cLo, diffA2, v_zero));
            XMVECTOR outB2 = __vmaddfp(sHi, swapB2, __vmaddfp(negCHi, diffB2, v_zero));

            __stvx(outA2, hiWrite, 0);
            __stvx(outB2, hiWriteBack, 0);

            sv.f[0] = (float)s2;
            sv.f[1] = (float)s1;
            sv.f[2] = (float)c2;
            sv.f[3] = (float)c1;
            XMVECTOR trig2 = sv.v;
            XMVECTOR negTrig2 = __vsubfp(v_zero, trig2);

            XMVECTOR nextSHi2 = __vperm(trig2, negTrig2, perm_sel);
            XMVECTOR nextCLo2 = __vsubfp(v_zero, __vperm(negCHi, negTrig2, perm_cdup.v));
            sLo = __vsel(sHi, nextSHi2, sel_hi.v);
            negCHi = __vmrglw(negTrig2, negTrig2);
            sHi = nextSHi2;
            cLo = nextCLo2;

            loRead += 4;
            hiRead += 4;
            loReadBack -= 4;
            hiReadBack -= 4;
            loWrite += 4;
            loWriteBack -= 4;
            hiWrite += 4;
            hiWriteBack -= 4;
        }
    }

    float* upper = data + size;
    ret = FFTComplex(upper, (long)half, sign, context);
    if (ret != 0) {
        return ret;
    }
    ret = FFTComplex(data, (long)half, sign, context);
    if (ret != 0) {
        return ret;
    }

    float* scratch = (float*)g_fftScratch.buf;
    float* copySrc = data;
    float* copyDst = scratch;
    for (unsigned int i = 0; i < (size >> 4); ++i) {
        XMVECTOR c0 = __lvx(copySrc, 0);
        copySrc += 4;
        XMVECTOR c1v = __lvx(copySrc, 0);
        copySrc += 4;
        XMVECTOR c2v = __lvx(copySrc, 0);
        copySrc += 4;
        XMVECTOR c3v = __lvx(copySrc, 0);
        copySrc += 4;
        __stvx(c0, copyDst, 0);
        copyDst += 4;
        __stvx(c1v, copyDst, 0);
        copyDst += 4;
        __stvx(c2v, copyDst, 0);
        copyDst += 4;
        __stvx(c3v, copyDst, 0);
        copyDst += 4;
    }

    float* evenSrc = scratch;
    float* oddSrc = upper;
    float* out = data;
    for (unsigned int i = 0; i < (size >> 3); ++i) {
        XMVECTOR e0 = __lvx(evenSrc, 0);
        evenSrc += 4;
        XMVECTOR o0 = __lvx(oddSrc, 0);
        oddSrc += 4;
        XMVECTOR m0 = __vperm(e0, o0, merge_lo.v);
        XMVECTOR m1 = __vperm(e0, o0, merge_hi.v);
        XMVECTOR e1 = __lvx(evenSrc, 0);
        evenSrc += 4;
        XMVECTOR o1 = __lvx(oddSrc, 0);
        oddSrc += 4;
        XMVECTOR m2 = __vperm(e1, o1, merge_lo.v);
        XMVECTOR m3 = __vperm(e1, o1, merge_hi.v);
        __stvx(m0, out, 0);
        __stvx(m1, out, 0x10);
        __stvx(m2, out, 0x20);
        __stvx(m3, out, 0x30);
        out += 16;
    }

    return ret;
}

// w8-f (2026-09-15).  RECONSTRUCTED FROM THE TARGET LISTING -- like
// fft_recursive above, there was no body here at all (case 1: declared at the
// top of this file, defined nowhere in src/; objdiff read 762 insert / 0 base
// = 0.0%).  No sibling tree has a definition either; see the fft_recursive
// header comment for the search that established that.
//
// The VECTOR sibling of fft_scalar below, but RADIX-4, not radix-2: each
// pass consumes four inputs a distance size/4 complex apart and emits four
// outputs, and the stage count is therefore (power - 2) / 2 (0x82E4EF08:
// `subi r11, r20, 0x2` / `srawi r11, r11, 1` / `addze. r22`).  Layout:
//
//  * setup + the same power-of-two test fft_scalar runs, plus TWO alignment
//    tests the scalar kernel does not have -- `clrlwi. r11, r24, 28` on `a`
//    (0x82E4ED80) and the same on `b` (0x82E4ED88), each returning 0x16.
//    A VMX kernel needs both buffers 16-byte aligned;
//  * a peeled FIRST pass (0x82E4EDFC) reading `a` and writing `b`, which is
//    where the two `stw r6/r5, -0x128/-0x120` spills of the size*2 and size*6
//    byte strides come from -- the stage loop reloads them (0x82E4F058);
//  * the stage loop, ping-ponging b<->a (0x82E4F408-0x82E4F420 swaps the two
//    pointers and does `slwi r29, r29, 2`, i.e. blk *= 4).  Each stage peels
//    group 0, whose twiddle is (1,0) (0x82E4F084), then walks the remaining
//    groups (0x82E4F250);
//  * four different tails.  When stages == 1 and power is even the last
//    radix-4 stage is specialised (0x82E4F428) into a scaled variant for
//    sign > 0 (0x82E4F4B4, `1/size` splat built at 0x82E4F474-0x82E4F4A8)
//    and an unscaled one (0x82E4F61C).  When power is ODD there is one extra
//    RADIX-2 stage instead, again scaled (0x82E4EF98) or not (0x82E4F754),
//    and which buffer it reads is chosen by bit 1 of power
//    (`rlwinm. r11, r20, 0, 30, 30` at 0x82E4EF30).
//
// The twiddle plumbing: the first pass reads THREE tables per iteration --
// twiddle[j] (r31, +0x10 per step), twiddle[half + j] (r3, 0x82E4EE4C) and a
// double-rate twiddle[2j], twiddle[2j+1] (r8, +0x20 per step).  The first two
// are full vectors holding two complex twiddles and are used with the
// cos-duplicate permute; the last two are consumed one lane at a time with
// `vspltw` because after the merge_lo / merge_hi split each output vector's
// two complex slots share a twiddle index.
//
// The sign of the transform picks three things at once (0x82E4ED90-0x82E4EDB0):
// the {1,-1,1,-1} vs {-1,1,-1,1} multiplier, the {0,~0,0,~0} vs {~0,0,~0,0}
// select mask, and which of the two sin-gather permutes is used.
//
// RESIDUAL (w8-f, 46.934383 canonical / 42.465878 fuzzy, 866 emitted against
// 762 in the image, 282 delete / 104 insert).  PARTIAL: the radix-4
// butterfly, the pass/stage skeleton, the four tails and the constant set are
// recovered; the group-loop pointer algebra is not, and that is where most of
// the 282 deletes live.
//
// ⚠ The same alignment fragility documented on fft_recursive above applies
// here, and it bit the one large structural fix this function obviously
// needs.  The biggest single cluster is 79 instructions, ALL delete
// (idx 213-297): it is the scaled radix-2 tail at 0x82E4EF98, which the image
// lays out immediately after the stage-loop guard, whereas ours sits at the
// end of the function.  The image's own branch says so -- `cmpwi cr6, r22,
// 0x1` / `bne cr6, .L_82E4F084` then `clrlwi. r11, r20, 31` /
// `beq .L_82E4F428` at 0x82E4F074-0x82E4F080 jumps OUT past the stage body to
// a block placed after it, so the source leaves the loop by a forward jump,
// not by falling through.  Reproducing exactly that -- `goto last_radix4`
// from inside the stage loop, the odd-power radix-2 tail immediately after
// the loop, and the last-radix-4 block behind a label after `return 0` --
// measured 46.93 -> 28.72.  Not kept, and NOT refuted as a reading: it moves
// ~136 instructions at once and the diff realigns badly while the butterfly
// bodies themselves still disagree.  Revisit it once a stage body pairs.
int fft_altivec(float* a, float* b, unsigned long size, long sign, float* twiddle) {
    XMVECTORU32 sel_odd = { 0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF };
    XMVECTORU32 perm_sin_fwd = { 0x04050607, 0x14151617, 0x0C0D0E0F, 0x1C1D1E1F };
    XMVECTORU32 sel_even = { 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000 };
    XMVECTORU32 perm_sin_inv = { 0x14151617, 0x04050607, 0x1C1D1E1F, 0x0C0D0E0F };
    XMVECTORU32 perm_swap = { 0x04050607, 0x00010203, 0x0C0D0E0F, 0x08090A0B };
    XMVECTORU32 merge_lo = { 0x00010203, 0x04050607, 0x10111213, 0x14151617 };
    XMVECTORU32 merge_hi = { 0x08090A0B, 0x0C0D0E0F, 0x18191A1B, 0x1C1D1E1F };
    XMVECTORU32 perm_cdup = { 0x00010203, 0x00010203, 0x08090A0B, 0x08090A0B };

    XMVECTOR v_zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMVECTOR v_alt_pos = { 1.0f, -1.0f, 1.0f, -1.0f };
    XMVECTOR v_alt_neg = { -1.0f, 1.0f, -1.0f, 1.0f };

    int p = 1;
    int power;
    if ((long)size == 1) {
        power = 0;
    } else {
        int p2 = 2;
        if ((long)size > 2) {
            do {
                p2 *= 2;
                p += 1;
            } while (p2 < (long)size);
        }
        power = p;
    }

    if ((unsigned int)(1 << power) != (unsigned int)size) {
        return 0x16;
    }
    if (((unsigned int)(size_t)a & 0xf) != 0) {
        return 0x16;
    }
    if (((unsigned int)(size_t)b & 0xf) != 0) {
        return 0x16;
    }

    XMVECTOR alt;
    XMVECTOR sel;
    XMVECTOR perm_sin;
    if (sign < 0) {
        alt = v_alt_pos;
        sel = sel_odd.v;
        perm_sin = perm_sin_fwd.v;
    } else {
        alt = v_alt_neg;
        sel = sel_even.v;
        perm_sin = perm_sin_inv.v;
    }

    unsigned int half = (unsigned int)size >> 1;

    // ---- first pass: a -> b, four inputs size/4 complex apart -------------
    {
        float* rd = a;
        float* wr = b;
        float* tw1 = twiddle;
        float* tw2 = twiddle;
        unsigned int j = 0;
        do {
            XMVECTOR x0 = __lvx(rd, 0);
            XMVECTOR x2 = __lvx(rd + size, 0);
            XMVECTOR d02 = __vsubfp(x0, x2);
            XMVECTOR w1 = __lvx(tw1, 0);
            XMVECTOR wq0 = __lvx(tw2, 0);
            XMVECTOR s02 = __vaddfp(x0, x2);
            XMVECTOR w1c = __vperm(w1, w1, perm_cdup.v);
            XMVECTOR x1 = __lvx(rd + half, 0);
            XMVECTOR nw1 = __vsubfp(v_zero, w1);
            XMVECTOR x3 = __lvx(rd + half + size, 0);
            XMVECTOR q0s = __vspltw(wq0, 1);
            XMVECTOR d13 = __vsubfp(x1, x3);
            XMVECTOR q0c = __vspltw(wq0, 0);
            XMVECTOR s13 = __vaddfp(x1, x3);
            XMVECTOR wq1 = __lvx(tw2 + 4, 0);
            XMVECTOR w2 = __lvx(twiddle + half + j, 0);

            XMVECTOR nw2 = __vsubfp(v_zero, w2);
            XMVECTOR q1s = __vspltw(wq1, 1);
            XMVECTOR q1c = __vspltw(wq1, 0);
            j += 4;

            XMVECTOR m0 = __vmaddfp(d02, w1c, v_zero);
            XMVECTOR sw0 = __vperm(d02, d02, perm_swap.v);
            XMVECTOR nq0s = __vsubfp(v_zero, q0s);
            XMVECTOR nq1s = __vsubfp(v_zero, q1s);
            XMVECTOR w2c = __vperm(w2, w2, perm_cdup.v);
            XMVECTOR s1v = __vperm(w1, nw1, perm_sin);
            rd += 4;
            tw1 += 4;
            tw2 += 8;

            XMVECTOR m1 = __vmaddfp(d13, w2c, v_zero);
            XMVECTOR sw1 = __vperm(d13, d13, perm_swap.v);
            XMVECTOR s2v = __vperm(w2, nw2, perm_sin);
            XMVECTOR t0 = __vmaddfp(sw0, s1v, m0);
            XMVECTOR g0 = __vsel(q0s, nq0s, sel);
            XMVECTOR g1 = __vsel(q1s, nq1s, sel);
            XMVECTOR t1 = __vmaddfp(sw1, s2v, m1);

            XMVECTOR u0 = __vperm(s02, t0, merge_lo.v);
            XMVECTOR u1 = __vperm(s02, t0, merge_hi.v);
            XMVECTOR u2 = __vperm(s13, t1, merge_lo.v);
            XMVECTOR u3 = __vperm(s13, t1, merge_hi.v);

            XMVECTOR q0 = __vsubfp(u0, u2);
            XMVECTOR q1 = __vsubfp(u1, u3);
            XMVECTOR p0 = __vaddfp(u0, u2);
            XMVECTOR p1 = __vaddfp(u1, u3);

            XMVECTOR n0 = __vmaddfp(q0c, q0, v_zero);
            XMVECTOR sq0 = __vperm(q0, q0, perm_swap.v);
            XMVECTOR n1 = __vmaddfp(q1c, q1, v_zero);
            XMVECTOR sq1 = __vperm(q1, q1, perm_swap.v);

            __stvx(p0, wr, 0);
            __stvx(p1, wr, 0x20);
            XMVECTOR r0 = __vmaddfp(g0, sq0, n0);
            XMVECTOR r1 = __vmaddfp(g1, sq1, n1);
            __stvx(r0, wr, 0x10);
            __stvx(r1, wr, 0x30);
            wr += 16;
        } while (j < half);
    }

    // ---- stage loop -------------------------------------------------------
    int stages = (power - 2) / 2;
    float* rd = b;
    float* wr = a;
    int blk = 4;

    while (stages > 0) {
        unsigned int step = (unsigned int)blk >> 2;
        float* q0p = rd;
        float* q1p = rd + half;
        float* q2p = rd + size;
        float* q3p = rd + half + size;

        if (stages == 1 && (power & 1) == 0) {
            break;
        }

        {
            // group 0: twiddle is (1, 0)
            XMVECTOR w = __lvx(twiddle, 0);
            XMVECTOR ws = __vspltw(w, 1);
            XMVECTOR wc = __vspltw(w, 0);
            XMVECTOR wsa = __vmaddfp(ws, alt, v_zero);
            XMVECTOR wca = __vmaddfp(wc, alt, v_zero);
            XMVECTOR nws = __vsubfp(v_zero, ws);

            float* o0 = wr;
            float* o1 = wr + blk * 2;
            float* o2 = wr + blk * 4;
            float* o3 = wr + blk * 6;
            for (unsigned int k = 0; k < step; ++k) {
                XMVECTOR x0 = __lvx(q0p, 0);
                q0p += 4;
                XMVECTOR x1 = __lvx(q1p, 0);
                q1p += 4;
                XMVECTOR x2 = __lvx(q2p, 0);
                q2p += 4;
                XMVECTOR x3 = __lvx(q3p, 0);
                q3p += 4;

                XMVECTOR d02 = __vsubfp(x2, x0);
                XMVECTOR d13 = __vsubfp(x3, x1);
                XMVECTOR s02 = __vaddfp(x2, x0);
                XMVECTOR s13 = __vaddfp(x3, x1);

                XMVECTOR m0 = __vmaddfp(d02, wc, v_zero);
                XMVECTOR m1 = __vmaddfp(d13, nws, v_zero);
                XMVECTOR c0 = __vmaddfp(__vperm(d02, d02, perm_swap.v), wsa, m0);
                XMVECTOR c1 = __vmaddfp(__vperm(d13, d13, perm_swap.v), wca, m1);

                XMVECTOR e0 = __vsubfp(s02, s13);
                XMVECTOR f0 = __vaddfp(s02, s13);
                XMVECTOR e1 = __vsubfp(c0, c1);
                XMVECTOR f1 = __vaddfp(c0, c1);

                XMVECTOR g0 = __vmaddfp(wc, e0, v_zero);
                XMVECTOR g1 = __vmaddfp(wc, e1, v_zero);
                __stvx(f0, o0, 0);
                o0 += 4;
                __stvx(f1, o1, 0);
                o1 += 4;
                __stvx(__vmaddfp(wsa, __vperm(e0, e0, perm_swap.v), g0), o2, 0);
                o2 += 4;
                __stvx(__vmaddfp(wsa, __vperm(e1, e1, perm_swap.v), g1), o3, 0);
                o3 += 4;
            }
        }

        {
            float* t1p = twiddle + blk * 2;
            float* t2p = twiddle + blk;
            float* o0 = wr + blk * 8;
            float* o1 = wr + blk * 10;
            float* o2 = wr + blk * 12;
            float* o3 = wr + blk * 14;
            for (unsigned int g = (unsigned int)blk * 2; g < half; g += (unsigned int)blk * 2) {
                XMVECTOR w1 = __lvx(t1p, 0);
                XMVECTOR w2 = __lvx(t2p, 0);
                XMVECTOR s1 = __vspltw(w1, 1);
                XMVECTOR s2 = __vspltw(w2, 1);
                XMVECTOR c2 = __vspltw(w2, 0);
                XMVECTOR c1 = __vspltw(w1, 0);
                XMVECTOR ns1 = __vsubfp(v_zero, s1);
                XMVECTOR s2a = __vmaddfp(s2, alt, v_zero);
                XMVECTOR ns2 = __vsubfp(v_zero, s2);
                XMVECTOR c2a = __vmaddfp(c2, alt, v_zero);
                XMVECTOR s1sel = __vsel(s1, ns1, sel);

                for (unsigned int k = 0; k < step; ++k) {
                    XMVECTOR x0 = __lvx(q0p, 0);
                    q0p += 4;
                    XMVECTOR x1 = __lvx(q1p, 0);
                    q1p += 4;
                    XMVECTOR x2 = __lvx(q2p, 0);
                    q2p += 4;
                    XMVECTOR x3 = __lvx(q3p, 0);
                    q3p += 4;

                    XMVECTOR d02 = __vsubfp(x2, x0);
                    XMVECTOR d13 = __vsubfp(x3, x1);
                    XMVECTOR s02 = __vaddfp(x2, x0);
                    XMVECTOR s13 = __vaddfp(x3, x1);

                    XMVECTOR m0 = __vmaddfp(d02, c2, v_zero);
                    XMVECTOR m1 = __vmaddfp(d13, ns2, v_zero);
                    XMVECTOR b0 = __vmaddfp(__vperm(d02, d02, perm_swap.v), s2a, m0);
                    XMVECTOR b1 = __vmaddfp(__vperm(d13, d13, perm_swap.v), c2a, m1);

                    XMVECTOR e0 = __vsubfp(s02, s13);
                    XMVECTOR f0 = __vaddfp(s02, s13);
                    XMVECTOR e1 = __vsubfp(b0, b1);
                    XMVECTOR f1 = __vaddfp(b0, b1);

                    XMVECTOR g0 = __vmaddfp(c1, e0, v_zero);
                    XMVECTOR g1 = __vmaddfp(c1, e1, v_zero);
                    __stvx(f0, o0, 0);
                    o0 += 4;
                    __stvx(f1, o1, 0);
                    o1 += 4;
                    __stvx(__vmaddfp(s1sel, __vperm(e0, e0, perm_swap.v), g0), o2, 0);
                    o2 += 4;
                    __stvx(__vmaddfp(s1sel, __vperm(e1, e1, perm_swap.v), g1), o3, 0);
                    o3 += 4;
                }

                t1p += blk * 4;
                t2p += blk * 2;
                o0 += blk * 8;
                o1 += blk * 8;
                o2 += blk * 8;
                o3 += blk * 8;
            }
        }

        {
            float* t = wr;
            wr = rd;
            rd = t;
        }
        stages -= 1;
        blk *= 4;
    }

    // ---- last radix-4 stage (power even) ----------------------------------
    if (stages == 1 && (power & 1) == 0) {
        float* q0p = rd;
        float* q1p = rd + half;
        float* q2p = rd + size;
        float* q3p = rd + half + size;
        if ((power & 3) == 2) {
            wr = rd;
        }
        XMVECTOR w = __lvx(twiddle, 0);
        XMVECTOR ws = __vspltw(w, 1);
        XMVECTOR wc = __vspltw(w, 0);
        XMVECTOR wsa = __vmaddfp(ws, alt, v_zero);
        XMVECTOR wca = __vmaddfp(wc, alt, v_zero);
        XMVECTOR nws = __vsubfp(v_zero, ws);

        float* o0 = wr;
        float* o1 = wr + blk * 4;
        float* o2 = wr + blk * 2;
        float* o3 = wr + blk * 6;
        unsigned int step = (unsigned int)blk >> 2;

        if (sign > 0) {
            float inv_n = 1.0f / (float)(double)(long long)(unsigned int)size;
            XMVECTORF32 sv;
            sv.f[0] = inv_n;
            XMVECTOR scale = __vspltw(sv.v, 0);
            for (unsigned int k = 0; k < step; ++k) {
                XMVECTOR x0 = __lvx(q0p, 0);
                q0p += 4;
                XMVECTOR x1 = __lvx(q1p, 0);
                q1p += 4;
                XMVECTOR x2 = __lvx(q2p, 0);
                q2p += 4;
                XMVECTOR x3 = __lvx(q3p, 0);
                q3p += 4;

                XMVECTOR d02 = __vsubfp(x1, x0);
                XMVECTOR s02 = __vaddfp(x1, x0);
                XMVECTOR d13 = __vsubfp(x2, x3);
                XMVECTOR s13 = __vaddfp(x2, x3);

                XMVECTOR m0 = __vmaddfp(d02, wc, v_zero);
                XMVECTOR m1 = __vmaddfp(d13, nws, v_zero);
                XMVECTOR c0 = __vmaddfp(__vperm(d02, d02, perm_swap.v), wsa, m0);
                XMVECTOR c1 = __vmaddfp(__vperm(d13, d13, perm_swap.v), wca, m1);

                XMVECTOR f0 = __vaddfp(s02, s13);
                XMVECTOR e0 = __vsubfp(s02, s13);
                XMVECTOR f1 = __vaddfp(c0, c1);
                XMVECTOR e1 = __vsubfp(c0, c1);

                __stvx(__vmaddfp(f0, scale, v_zero), o0, 0);
                o0 += 4;
                __stvx(__vmaddfp(f1, scale, v_zero), o1, 0);
                o1 += 4;
                XMVECTOR g0 = __vmaddfp(wc, e0, v_zero);
                XMVECTOR g1 = __vmaddfp(wc, e1, v_zero);
                __stvx(__vmaddfp(__vmaddfp(wsa, __vperm(e0, e0, perm_swap.v), g0), scale, v_zero), o2, 0);
                o2 += 4;
                __stvx(__vmaddfp(__vmaddfp(wsa, __vperm(e1, e1, perm_swap.v), g1), scale, v_zero), o3, 0);
                o3 += 4;
            }
        } else {
            for (unsigned int k = 0; k < step; ++k) {
                XMVECTOR x0 = __lvx(q0p, 0);
                q0p += 4;
                XMVECTOR x1 = __lvx(q1p, 0);
                q1p += 4;
                XMVECTOR x2 = __lvx(q2p, 0);
                q2p += 4;
                XMVECTOR x3 = __lvx(q3p, 0);
                q3p += 4;

                XMVECTOR d02 = __vsubfp(x1, x0);
                XMVECTOR s02 = __vaddfp(x1, x0);
                XMVECTOR d13 = __vsubfp(x2, x3);
                XMVECTOR s13 = __vaddfp(x2, x3);

                XMVECTOR m0 = __vmaddfp(d02, wc, v_zero);
                XMVECTOR m1 = __vmaddfp(d13, nws, v_zero);
                XMVECTOR c0 = __vmaddfp(__vperm(d02, d02, perm_swap.v), wsa, m0);
                XMVECTOR c1 = __vmaddfp(__vperm(d13, d13, perm_swap.v), wca, m1);

                XMVECTOR f0 = __vaddfp(s02, s13);
                XMVECTOR e0 = __vsubfp(s02, s13);
                XMVECTOR f1 = __vaddfp(c0, c1);
                XMVECTOR e1 = __vsubfp(c0, c1);

                __stvx(f0, o0, 0);
                o0 += 4;
                __stvx(f1, o1, 0);
                o1 += 4;
                XMVECTOR g0 = __vmaddfp(wc, e0, v_zero);
                XMVECTOR g1 = __vmaddfp(wc, e1, v_zero);
                __stvx(__vmaddfp(wsa, __vperm(e0, e0, perm_swap.v), g0), o2, 0);
                o2 += 4;
                __stvx(__vmaddfp(wsa, __vperm(e1, e1, perm_swap.v), g1), o3, 0);
                o3 += 4;
            }
        }
        return 0;
    }

    // ---- odd power: one extra RADIX-2 stage -------------------------------
    if ((power & 1) == 0) {
        return 0;
    }

    {
        float* lo = b;
        if ((power & 2) == 0) {
            lo = a;
        }
        float* hi = lo + size;
        float* out = a;
        float* outHi = a + size;

        if (sign > 0) {
            float inv_n = 1.0f / (float)(double)(long long)(unsigned int)size;
            XMVECTORF32 sv;
            sv.f[0] = inv_n;
            XMVECTOR scale = __vspltw(sv.v, 0);
            unsigned int step = (unsigned int)blk >> 3;
            for (unsigned int k = 0; k < step; ++k) {
                XMVECTOR x0 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR s0 = __vmaddfp(x0, scale, v_zero);
                XMVECTOR y0 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x1 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR s1 = __vmaddfp(x1, scale, v_zero);
                XMVECTOR y1 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x2 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR s2 = __vmaddfp(x2, scale, v_zero);
                XMVECTOR y2 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x3 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR s3 = __vmaddfp(x3, scale, v_zero);
                XMVECTOR y3 = __lvx(hi, 0);
                hi += 4;

                __stvx(__vmaddfp(y0, scale, s0), out, 0);
                out += 4;
                __stvx(__vnmsubfp(y0, scale, s0), outHi, 0);
                outHi += 4;
                __stvx(__vmaddfp(y1, scale, s1), out, 0);
                out += 4;
                __stvx(__vnmsubfp(y1, scale, s1), outHi, 0);
                outHi += 4;
                __stvx(__vmaddfp(y2, scale, s2), out, 0);
                out += 4;
                __stvx(__vnmsubfp(y2, scale, s2), outHi, 0);
                outHi += 4;
                __stvx(__vmaddfp(y3, scale, s3), out, 0);
                out += 4;
                __stvx(__vnmsubfp(y3, scale, s3), outHi, 0);
                outHi += 4;
            }
        } else {
            unsigned int step = (unsigned int)blk >> 3;
            for (unsigned int k = 0; k < step; ++k) {
                XMVECTOR y0 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x0 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR a0 = __vaddfp(x0, y0);
                XMVECTOR b0 = __vsubfp(x0, y0);
                XMVECTOR y1 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x1 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR a1 = __vaddfp(x1, y1);
                XMVECTOR b1 = __vsubfp(x1, y1);
                XMVECTOR y2 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x2 = __lvx(lo, 0);
                lo += 4;
                XMVECTOR a2 = __vaddfp(x2, y2);
                XMVECTOR b2 = __vsubfp(x2, y2);
                XMVECTOR y3 = __lvx(hi, 0);
                hi += 4;
                XMVECTOR x3 = __lvx(lo, 0);
                lo += 4;

                __stvx(a0, out, 0);
                out += 4;
                __stvx(b0, outHi, 0);
                outHi += 4;
                XMVECTOR a3 = __vaddfp(x3, y3);
                XMVECTOR b3 = __vsubfp(x3, y3);
                __stvx(a1, out, 0);
                out += 4;
                __stvx(b1, outHi, 0);
                outHi += 4;
                __stvx(a2, out, 0);
                out += 4;
                __stvx(b2, outHi, 0);
                outHi += 4;
                __stvx(a3, out, 0);
                out += 4;
                __stvx(b3, outHi, 0);
                outHi += 4;
            }
        }
    }

    return 0;
}

// RESIDUAL (w7-ay, 82.5 canonical, floor held): every butterfly loop below
// differs from the image in ONE address decision.  Two operands are read
// twice per iteration -- src[1] and the high imaginary at src+stride4+4 --
// and MSVC materialises exactly one of the two addresses for the reload.
// The image materialises the high one (`add r4, r3, r10` at 0x82E4F8C0,
// reload `lfs f9, 0x0(r4)`) and keeps src[1] as `lfs 0x4(r10)` both times;
// we materialise src+4 (`addi r8, r10, 0x4`), reload src[1] through it, and
// then re-express the high REAL load off it with a -4 bias (`subi r30,
// r27, 0x4` / `lfsx f9, r30, r8`), which also flips the load order and the
// FPR numbering behind every fadds/fsubs.  Tried: loading the high imaginary
// into a named local before t_im (inert -- the load order is a consequence of
// the materialisation, not of source order); reading it twice through one
// `const float* hp` (76.1, MSVC then walks hp as a second induction variable).
// The `hi` pointer form was already worse (b54e87b6d).
int fft_scalar(float* a, float* b, unsigned long size, long sign, float* twiddle) {
    float* src = a;
    float* dst = b;

    int p = 1;
    int power;
    if ((long)size == 1) {
        power = 0;
    } else {
        int p2 = 2;
        if ((long)size > 2) {
            do {
                p2 *= 2;
                p += 1;
            } while (p2 < (long)size);
        }
        power = p;
    }

    if ((unsigned int)(1 << power) != (unsigned int)size) {
        return 0x16;
    }

    int stage = power - 1;
    int blk = 1;
    if (stage > 0) {
        unsigned int half = (unsigned int)size >> 1;
        int stride4 = (int)size * 4;
        int stride8 = (int)size * 8;
        do {
            unsigned int group = 0;
            if (half != 0) {
                int blk8 = blk * 8;
                float* tw = twiddle;
                do {
                    float wr = tw[0];
                    float wi = tw[1] * (float)(double)(long long)sign;
                    if (blk > 0) {
                        int ctr = blk;
                        do {
                            float t_im = src[1] - *(float*)((char*)src + stride4 + 4);
                            float h_re = *(float*)((char*)src + stride4);
                            float l_re = src[0];
                            float t_re = l_re - h_re;
                            dst[0] = h_re + l_re;
                            float l_im = src[1];
                            dst[1] = l_im + *(float*)((char*)src + stride4 + 4);
                            src += 2;
                            dst[blk * 2] = t_re * wr - t_im * wi;
                            dst[blk * 2 + 1] = t_re * wi + t_im * wr;
                            dst += 2;
                            ctr -= 1;
                        } while (ctr != 0);
                    }
                    group += blk;
                    dst = (float*)((char*)dst + blk8);
                    tw = (float*)((char*)tw + blk8);
                } while (group < half);
            }
            char* next_dst = (char*)src - stride4;
            src = (float*)((char*)dst - stride8);
            stage -= 1;
            blk *= 2;
            dst = (float*)next_dst;
        } while (stage > 0);
    }

    if (power & 1) {
        dst = src;
    }

    unsigned int group = 0;
    unsigned int half = (unsigned int)size >> 1;
    if (sign > 0) {
        double scale = 1.0 / (double)(long long)(unsigned int)size;
        if (half != 0) {
            int blk8 = blk * 8;
            float* tw = twiddle;
            do {
                float wr = tw[0];
                float wi = tw[1] * (float)(double)(long long)sign;
                if (blk > 0) {
                    int stride4 = (int)size * 4;
                    int ctr = blk;
                    do {
                        float h_re = *(float*)((char*)src + stride4);
                        float l_re = src[0];
                        float t_re = l_re - h_re;
                        float t_im = src[1] - *(float*)((char*)src + stride4 + 4);
                        float p_re = t_im * wi;
                        float p_im = t_im * wr;
                        dst[0] = (float)((double)(h_re + l_re) * scale);
                        float l_im = src[1];
                        dst[1] = (float)((double)(l_im + *(float*)((char*)src + stride4 + 4)) * scale);
                        src += 2;
                        dst[blk * 2] = (float)((double)(t_re * wr - p_re) * scale);
                        dst[blk * 2 + 1] = (float)((double)(t_re * wi + p_im) * scale);
                        dst += 2;
                        ctr -= 1;
                    } while (ctr != 0);
                }
                group += blk;
                dst = (float*)((char*)dst + blk8);
                tw = (float*)((char*)tw + blk8);
            } while (group < half);
        }
    } else if (half != 0) {
        int blk8 = blk * 8;
        float* tw = twiddle;
        do {
            float wr = tw[0];
            float wi = tw[1] * (float)(double)(long long)sign;
            if (blk > 0) {
                int stride4 = (int)size * 4;
                int ctr = blk;
                do {
                    float t_im = src[1] - *(float*)((char*)src + stride4 + 4);
                    float h_re = *(float*)((char*)src + stride4);
                    float l_re = src[0];
                    float t_re = l_re - h_re;
                    dst[0] = h_re + l_re;
                    float l_im = src[1];
                    dst[1] = l_im + *(float*)((char*)src + stride4 + 4);
                    src += 2;
                    dst[blk * 2] = t_re * wr - t_im * wi;
                    dst[blk * 2 + 1] = t_re * wi + t_im * wr;
                    dst += 2;
                    ctr -= 1;
                } while (ctr != 0);
            }
            group += blk;
            dst = (float*)((char*)dst + blk8);
            tw = (float*)((char*)tw + blk8);
        } while (group < half);
    }

    return 0;
}

// VMX constants
extern "C" {
    extern unsigned char __vmx_3f800000bf8000003f800000bf800000[];
    extern unsigned char __vmx_bf8000003f800000bf8000003f800000[];
    extern unsigned char __vmx_00000000000000000000000000000000[];
}

#pragma float_control(precise, on, push)
int fft_matrix_forward_columnwise(float* data, long size, float* context) {
    int ret = 0;
    int power = 1;

    // Declare all VMX types upfront to ensure proper register allocation
    XMVECTOR v_zero;
    XMVECTOR v_sign;
    XMVECTOR v_sin2a;
    XMVECTOR v_sin2;
    XMVECTOR v_im_init;
    XMVECTOR v_cos_vec;
    XMVECTOR v_cos_splat;
    XMVECTOR v_sin_vec;
    XMVECTOR v_sin_merged;
    XMVECTOR v_cos_merged;
    XMVECTOR w_re1, w_im1, w_re2, w_im2;
    XMVECTOR pm_swap_v, pm_lo_v, pm_hi_v;
    XMVECTOR d0, d1, d_swap0, d_swap1;
    XMVECTOR sp_sin2, sp_sin2_2, sp_sin2_3;
    XMVECTOR new_re1, new_re2, new_im1, new_im2;
    XMVECTOR t1, t2, p_im1, p_im2;
    XMVECTOR r1, r2;
    XMVECTOR out_lo, out_hi;
    XMVECTOR a, b, hi;

    XMVECTORF32 sv;
    XMVECTORU32 perm_lo;
    XMVECTORU32 perm_hi;
    XMVECTORU32 perm_swap;

    // Calculate power of 2 for size
    if (size == 1) {
        power = 0;
    } else {
        int p2 = 2;
        if (size > 2) {
            do {
                p2 *= 2;
                power += 1;
            } while (p2 < size);
        }
    }

    // Check if size is power of 2
    if ((1 << power) != size) {
        return 0x16;
    }

    // Check data alignment (must be 16-byte aligned)
    if (((unsigned long)data) & 0xF) {
        return 0x16;
    }

    // Calculate dimensions: rows = 2^(power/2), cols = 2^(ceil(power/2))
    int half_power = power / 2;
    int ceil_half_power = half_power;
    if (power & 1) {
        ceil_half_power = half_power + 1;
    }

    int rows = 1 << half_power;
    int cols = 1 << ceil_half_power;

    // Allocate temporary buffer
    float* temp = (float*)malloc(rows * 0x10);
    if (temp == 0) {
        ret = 0xC;
        goto done_twiddle;
    }

    // Load VMX constants
    XMVECTOR k_zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    XMVECTOR k_sign = { 1.0f, -1.0f, 1.0f, -1.0f };
    v_zero = k_zero;
    v_sign = k_sign;

    // Initialize permutation masks - these will be constructed with lis/ori
    perm_lo.u[0] = 0x00010203;
    perm_lo.u[1] = 0x04050607;
    perm_lo.u[2] = 0x10111213;
    perm_lo.u[3] = 0x14151617;

    perm_hi.u[0] = 0x08090A0B;
    perm_hi.u[1] = 0x0C0D0E0F;
    perm_hi.u[2] = 0x18191A1B;
    perm_hi.u[3] = 0x1C1D1E1F;

    perm_swap.u[0] = 0x04050607;
    perm_swap.u[1] = 0x00010203;
    perm_swap.u[2] = 0x0C0D0E0F;
    perm_swap.u[3] = 0x08090A0B;

    // Step 1: Row gather -> row FFT -> twiddle multiply + scatter
    int half_cols = cols / 2;
    int iter = 0;

    if (half_cols > 0) {
        int half_rows = rows / 2;
        float* temp2 = (float*)((char*)temp + half_rows * 0x10);
        int col_idx = 0;
        double two_d = 2.0;
        float* data_ptr = (float*)data;
        float one_f = 1.0f;
        float pi_f = (float)M_PI;
        float total = (float)(double)((long long)(int)(cols * rows));

        do {
            // Compute twiddle angles
            float angle1 = ((float)(long long)col_idx * pi_f) / total;
            float angle2 = ((float)(long long)(col_idx + 2) * pi_f) / total;

            // sin² recurrence parameters
            double s1d = sin(angle1);
            float sin2_1 = (float)(s1d * s1d * two_d);
            float sin_2a1 = (float)sin(((double)angle1 * two_d));
            sv.f[0] = sin2_1;
            sv.f[2] = sin_2a1;

            double s2d = sin(angle2);
            float sin2_2 = (float)(s2d * s2d * two_d);
            float sin_2a2 = (float)sin(((double)angle2 * two_d));
            sv.f[1] = sin2_2;
            sv.f[3] = sin_2a2;
            v_sin2a = __vmrglw(sv.v, sv.v);
            v_sin2 = __vmrghw(sv.v, sv.v);

            // Start overwriting sv for cos vector
            sv.f[0] = one_f;

            v_im_init = v_sign;
            v_im_init = __vmaddfp(v_sin2a, v_im_init, v_zero);

            sv.f[2] = (float)cos(angle1);
            sv.f[3] = (float)cos(angle2);

            v_cos_vec = __lvx(&sv, 0);

            // Initialize running twiddle factors
            w_im2 = v_sign;
            w_im1 = v_zero;
            v_cos_splat = __vspltw(v_cos_vec, 0);
            w_re1 = v_cos_splat;

            // Phase 3: Overwrite with sin values, load it
            sv.f[2] = (float)s1d;
            v_cos_merged = __vmrglw(v_cos_vec, v_cos_vec);
            w_re2 = v_cos_merged;
            sv.f[3] = (float)s2d;

            v_sin_vec = __lvx(&sv, 0);
            v_sin_merged = __vmrglw(v_sin_vec, v_sin_vec);
            w_im2 = __vmaddfp(v_sin_merged, w_im2, v_zero);

            float* dst1 = temp;
            float* dst2 = temp2;
            char* src_data = (char*)data_ptr;
            int k = 0;

            // Gather a pair of rows out of the column-major matrix into the
            // contiguous scratch halves.
            if (half_rows > 0) {
                int data_stride = half_cols * 0x10;
                do {
                    a = __lvx(src_data, 0);
                    src_data += data_stride;
                    k += 1;
                    b = __lvx(src_data, 0);
                    src_data += data_stride;
                    out_lo = __vperm(a, b, *(XMVECTOR*)&perm_lo);
                    out_hi = __vperm(a, b, *(XMVECTOR*)&perm_hi);
                    __stvx(out_lo, dst1, 0);
                    dst1 += 4;
                    __stvx(out_hi, dst2, 0);
                    dst2 += 4;
                } while (k < half_rows);
            }

            // Row FFT on temp buffer halves
            ret = FFTComplex(temp, rows, -1, context);
            if (ret != 0) goto cleanup;

            ret = FFTComplex((float*)((char*)temp + rows * 8), rows, -1, context);
            if (ret != 0) goto cleanup;

            // Twiddle-multiply the transformed rows and scatter them back.
            {
                // NEGATIVE RESULT on the twiddle loop's two source pointers.
                // The image walks THREE pointers into this loop -- `mr r10, r29`
                // (temp), `mr r9, r26` (temp2), `mr r8, r30` (data_ptr) -- and
                // loads both halves with a zero index: `lvx128 v63, r0, r10` /
                // `lvx128 v62, r0, r9`, advancing each with its own
                // `addi rN, rN, 0x10` (FFT.s, the block at 0x45c-0x558).  We
                // emit only TWO `mr`, plus `subf r7, r29, r26`, and load the
                // second half indexed off the first: `lvx128 v62, r7, r11`.
                // That is MSVC folding src2 into src1 + (temp2 - temp) --
                // induction-variable elimination, and it cascades into the
                // whole loop's vector-register assignment (61 of this
                // function's 102 mismatch rows are in this one loop).
                //
                // Two variants tried, both BYTE-FOR-BYTE INERT (86.8 canonical /
                // 84.9 raw, 319 instructions, 60/6/19/17 row split, identical
                // before and after):
                //   1. `float*` walked with `+= 4` instead of `char*` walked
                //      with `+= 0x10` -- i.e. spelled exactly like the gather
                //      loop 30 lines above, which does NOT get merged even
                //      though its dst1/dst2 stand in the same temp/temp2
                //      relationship.
                //   2. the two increments separated in source order to match
                //      the image's schedule (src1 right after `k += 1`, src2
                //      down inside the recurrence after the last use of `b`).
                // The gather loop's pointers survive because they are STORE
                // destinations; the merge here is a load-side decision the
                // pointer's spelling and its increment's position do not reach.
                //   3. (w7-ay) indexing both loads off temp/temp2 by k
                //      (`__lvx(temp + k * 4, 0)`, no source pointers at all):
                //      WORSE, 86.6 -- strength reduction rebuilds the same
                //      merged pair.
                //   4. (w7-ay) outside the loop: assigning sv.f[] / w_re1 /
                //      w_re2 / w_im2 straight from the sin()/__vspltw/__vmrglw
                //      expressions instead of via the sin2_1/v_cos_splat/
                //      v_cos_merged/v_sin_merged locals: byte-for-byte inert
                //      (the `vor128 v62, v63, v63` copy of v_cos_vec and the
                //      `fmul f0, f1, f1` square-before-copy at the first sin
                //      return are scheduling, not spelling).
                char* src1 = (char*)temp;
                char* src2 = (char*)temp2;
                char* out = (char*)data_ptr;
                k = 0;
                if (half_rows > 0) {
                    int stride = half_cols * 0x10;
                    do {
                        a = __lvx(src1, 0);
                        b = __lvx(src2, 0);

                        // Copy sin² values for this iteration
                        sp_sin2 = v_sin2;
                        sp_sin2_2 = v_sin2;
                        sp_sin2_3 = v_sin2;

                        pm_lo_v = *(XMVECTOR*)&perm_lo;
                        pm_hi_v = *(XMVECTOR*)&perm_hi;
                        pm_swap_v = *(XMVECTOR*)&perm_swap;

                        k += 1;
                        src1 += 0x10;
                        src2 += 0x10;

                        // Begin twiddle recurrence
                        d0 = __vperm(a, b, pm_lo_v);
                        new_re1 = __vnmsubfp(w_re1, sp_sin2, w_re1);
                        d1 = __vperm(a, b, pm_hi_v);
                        new_re2 = __vnmsubfp(w_re2, sp_sin2_2, w_re2);

                        t1 = __vmaddfp(w_re1, d0, v_zero);
                        d_swap0 = __vperm(d0, d0, pm_swap_v);
                        p_im1 = __vnmsubfp(w_im1, sp_sin2, w_im1);

                        t2 = __vmaddfp(w_re2, d1, v_zero);
                        d_swap1 = __vperm(d1, d1, pm_swap_v);
                        p_im2 = __vnmsubfp(w_im2, sp_sin2_3, w_im2);

                        new_re1 = __vnmsubfp(w_im1, v_im_init, new_re1);
                        new_re2 = __vnmsubfp(w_im2, v_im_init, new_re2);
                        new_im1 = __vmaddfp(w_re1, v_im_init, p_im1);
                        new_im2 = __vmaddfp(w_re2, v_im_init, p_im2);

                        r1 = __vmaddfp(w_im1, d_swap0, t1);
                        r2 = __vmaddfp(w_im2, d_swap1, t2);

                        w_re1 = new_re1;
                        w_re2 = new_re2;
                        w_im1 = new_im1;
                        w_im2 = new_im2;

                        __stvx(r1, out, 0);
                        out += stride;
                        __stvx(r2, out, 0);
                        out += stride;
                    } while (k < half_rows);
                }
            }

            iter += 1;
            col_idx += 4;
            data_ptr += 4;
        } while (iter < half_cols);
    }

    // Step 2: Column FFT (forward) on each column, rows-1 down to 0
    int col_i = rows - 1;
    if (col_i >= 0) {
        int neg_stride = -cols;
        int stride8 = neg_stride * 8;
        float* col_ptr = (float*)((char*)data + col_i * cols * 8);
        do {
            ret = FFTComplex(col_ptr, cols, -1, context);
            if (ret != 0) goto cleanup;
            col_i -= 1;
            col_ptr = (float*)(stride8 + (char*)col_ptr);
        } while (col_i >= 0);
    }

done_twiddle:
cleanup:
    free(temp);
    return ret;
}
#pragma float_control(pop)

#pragma float_control(precise, on, push)
int fft_matrix_inverse_columnwise(float *data, long size, float *scratch) {
    int ret = 0;
    int exp = 1;

    if (size == 1) {
        exp = 0;
    } else {
        int pow2 = 2;
        if (size > 2) {
            do {
                pow2 *= 2;
                exp += 1;
            } while (pow2 < size);
        }
    }

    if ((1 << exp) != size) {
        return 0x16;
    }

    if (((unsigned long)data) & 0xF) {
        return 0x16;
    }

    int half_exp = exp / 2;
    int ceil_half_exp = half_exp;
    if (exp & 1) {
        ceil_half_exp = half_exp + 1;
    }

    int cols = 1 << half_exp;
    int rows = 1 << ceil_half_exp;

    float *temp = (float *)malloc(cols * 0x10);
    if (temp == 0) {
        ret = 0xC;
        goto done_twiddle;
    }

    // Load VMX constants into persistent registers (v124=sign, v125=zero in target)
    XMVECTOR v_zero = *(XMVECTOR *)__vmx_00000000000000000000000000000000;
    XMVECTOR v_sign = *(XMVECTOR *)__vmx_bf8000003f800000bf8000003f800000;

    // Permutation masks on stack (3 masks: lo, hi, swap)
    XMVECTORU32 perm_lo = { 0x00010203, 0x04050607, 0x10111213, 0x14151617 };
    XMVECTORU32 perm_hi = { 0x08090A0B, 0x0C0D0E0F, 0x18191A1B, 0x1C1D1E1F };
    XMVECTORU32 perm_swap = { 0x04050607, 0x00010203, 0x0C0D0E0F, 0x08090A0B };

    // Step 1: Column FFT (inverse) on each column, cols-1 down to 0
    int col_i = cols - 1;
    if (col_i >= 0) {
        int neg_stride = -rows;
        int stride8 = neg_stride * 8;
        float *col_ptr = (float *)((char *)data + col_i * rows * 8);
        do {
            ret = FFTComplex(col_ptr, rows, 1, scratch);
            if (ret != 0) goto cleanup;
            col_i -= 1;
            col_ptr = (float *)((char *)col_ptr + stride8);
        } while (col_i >= 0);
    }

    // Step 2: Twiddle factor multiplication + row FFT
    int iter = 0;
    int half_rows = rows / 2;

    if (half_rows > 0) {
        int half_cols = cols / 2;
        float *temp2 = (float *)((char *)temp + half_cols * 0x10);
        int col_idx = 0;
        double two_d = 2.0;
        float *data_ptr = (float *)data;
        float one_f = 1.0f;
        float pi_f = (float)M_PI;
        float total = (float)(double)((long long)(int)(rows * cols));

        XMVECTORF32 sv;

        do {
            // Compute twiddle angles
            float angle1 = ((float)(long long)col_idx * pi_f) / total;
            float angle2 = ((float)(long long)(col_idx + 2) * pi_f) / total;

            // sin² recurrence parameters
            double s1d = sin(angle1);
            float sin2_1 = (float)(s1d * s1d * two_d);
            float sin_2a1 = (float)sin((float)((double)angle1 * two_d));
            sv.f[0] = sin2_1;
            sv.f[2] = sin_2a1;

            double s2d = sin(angle2);
            float sin2_2 = (float)(s2d * s2d * two_d);
            float sin_2a2 = (float)sin((float)((double)angle2 * two_d));
            sv.f[1] = sin2_2;
            sv.f[3] = sin_2a2;
            XMVECTOR v_sin2a = __vmrglw(sv.v, sv.v);
            XMVECTOR v_sin2 = __vmrghw(sv.v, sv.v);

            // Start overwriting sv for cos vector
            sv.f[0] = one_f;

            XMVECTOR v_im_init = v_sign;
            v_im_init = __vmaddfp(v_sin2a, v_im_init, v_zero);

            sv.f[2] = (float)cos(angle1);
            sv.f[3] = (float)cos(angle2);

            XMVECTOR v_cos_vec = __lvx(&sv, 0);

            // Initialize running twiddle factors.  w_re1/w_re2 are formed
            // from v_cos_vec around the two sv stores, in the image's order
            // (0x82E4E97C / 0x82E4E984 straddle the `stfs 0x68(r1)`).  MSVC
            // still sinks the w_re2 merge below the sin-vector load and pays
            // a `vor128` copy of v_cos_vec for it, exactly as in the forward
            // transform; spelling it through v_cos_merged, or after the sin
            // load, is inert or worse.
            XMVECTOR w_im2 = v_sign;
            XMVECTOR w_im1 = v_zero;
            XMVECTOR w_re1 = __vspltw(v_cos_vec, 0);

            // Phase 3: Overwrite with sin values, load it
            sv.f[2] = (float)s1d;
            XMVECTOR w_re2 = __vmrglw(v_cos_vec, v_cos_vec);
            sv.f[3] = (float)s2d;

            XMVECTOR v_sin_vec = __lvx(&sv, 0);
            XMVECTOR v_sin_merged = __vmrglw(v_sin_vec, v_sin_vec);
            w_im2 = __vmaddfp(w_im2, v_sin_merged, v_zero);

            // BEHAVIOURAL FIX (w7-ay): the gather loop below walks COPIES of
            // temp/temp2 -- the image's `mr r9, r28` / `mr r8, r27` at
            // 0x82E4E970-74 -- and r28 (temp) is what the two FFTComplex
            // calls, the deinterleave and free() then use (0x82E4EA78,
            // 0x82E4EAA8, 0x82E4EB1C).  The permuter harvest fb98fec2e had
            // replaced them with `temp += 4` / `temp2 += 4`, which scored
            // 3pp higher (89.0 vs 86.0) and handed FFTComplex, the second
            // loop and free() a pointer half_cols*16 bytes past the malloc.
            // Keep the copies; the score is not the point.  Declared here,
            // before src_data, like the forward transform; scoping them
            // inside the guard is worse (85.6).
            float *dst1 = temp;
            float *dst2 = temp2;
            char *src_data = (char *)data_ptr;
            int k = 0;

            if (half_cols > 0) {
                int data_stride = half_rows * 0x10;
                XMVECTOR pm_swap_v = *(XMVECTOR *)&perm_swap;
                XMVECTOR pm_lo_v = *(XMVECTOR *)&perm_lo;
                XMVECTOR pm_hi_v = *(XMVECTOR *)&perm_hi;

                do {
                    // Load first data element (row 0)
                    XMVECTOR d0 = __lvx(src_data, 0);
                    src_data += data_stride;

                    // Copy sin² values for this iteration
                    XMVECTOR sp_sin2 = v_sin2;
                    XMVECTOR sp_sin2_2 = v_sin2;
                    XMVECTOR sp_sin2_3 = v_sin2;

                    // Begin twiddle recurrence
                    XMVECTOR new_re1 = __vnmsubfp(w_re1, sp_sin2, w_re1);
                    XMVECTOR t1 = __vmaddfp(w_re1, d0, v_zero);
                    XMVECTOR d_swap0 = __vperm(d0, d0, pm_swap_v);

                    // Load second data element (row 1)
                    XMVECTOR d1 = __lvx(src_data, 0);
                    XMVECTOR new_re2 = __vnmsubfp(w_re2, sp_sin2_2, w_re2);
                    XMVECTOR d_swap1 = __vperm(d1, d1, pm_swap_v);

                    XMVECTOR p_im1 = __vnmsubfp(w_im1, sp_sin2, w_im1);
                    XMVECTOR t2 = __vmaddfp(w_re2, d1, v_zero);

                    XMVECTOR p_im2 = __vnmsubfp(w_im2, sp_sin2_3, w_im2);
                    new_re1 = __vnmsubfp(w_im1, v_im_init, new_re1);
                    XMVECTOR r1 = __vmaddfp(w_im1, d_swap0, t1);
                    new_re2 = __vnmsubfp(w_im2, v_im_init, new_re2);
                    XMVECTOR new_im1 = __vmaddfp(w_re1, v_im_init, p_im1);
                    XMVECTOR r2 = __vmaddfp(w_im2, d_swap1, t2);
                    XMVECTOR new_im2 = __vmaddfp(w_re2, v_im_init, p_im2);

                    w_re1 = new_re1;
                    w_re2 = new_re2;
                    w_im1 = new_im1;
                    w_im2 = new_im2;

                    k += 1;

                    // Interleave results and store to temp
                    XMVECTOR out_lo = __vperm(r1, r2, pm_lo_v);
                    XMVECTOR out_hi = __vperm(r1, r2, pm_hi_v);

                    __stvx(out_lo, dst1, 0);
                    dst1 += 4;
                    __stvx(out_hi, dst2, 0);
                    dst2 += 4;
                    src_data += data_stride;
                } while (k < half_cols);
            }

            // Row FFT on temp buffer halves
            ret = FFTComplex(temp, cols, 1, scratch);
            if (ret != 0) goto cleanup;

            ret = FFTComplex((float *)((char *)temp + cols * 8), cols, 1, scratch);
            if (ret != 0) goto cleanup;

            // Deinterleave from temp back to data
            {
                // Same induction-variable merge as the forward transform's
                // twiddle loop -- see the NEGATIVE RESULT block in
                // fft_matrix_forward_columnwise.  The image walks three
                // pointers in (`mr r9, r28` / `mr r8, r27` / `mr r10, r30`,
                // FFT.s 0x890-0x8ac); we emit one `mr` and index the second
                // half off the first.  Both spellings tried there are inert.
                char *src1 = (char *)temp;
                char *src2 = (char *)temp2;
                char *out = (char *)data_ptr;
                k = 0;
                if (half_cols > 0) {
                    int stride = half_rows * 0x10;
                    do {
                        XMVECTOR a = __lvx(src1, 0);
                        XMVECTOR b = __lvx(src2, 0);
                        k += 1;
                        src1 += 0x10;
                        src2 += 0x10;
                        XMVECTOR hi = __vperm(a, b, *(XMVECTOR *)&perm_hi);
                        __stvx(__vperm(a, b, *(XMVECTOR *)&perm_lo), out, 0);
                        out += stride;
                        __stvx(hi, out, 0);
                        out += stride;
                    } while (k < half_cols);
                }
            }

            iter += 1;
            col_idx += 4;
            data_ptr += 4;
        } while (iter < half_rows);
    }

done_twiddle:
cleanup:
    free(temp);
    return ret;
}
#pragma float_control(pop)

// Real-input forward FFT (AltiVec), the large-transform sibling of
// fft_real_forward_scalar.  Runs a size/2-point complex FFT and then does the
// same conjugate-symmetric recombination, but four floats -- two complex bins
// -- at a time, and from BOTH ends of the spectrum at once:
//
//   out[k]        = 0.5 * ((X[k] + conj X[N/2-k]) - i*W^k*(X[k] - conj X[N/2-k]))
//
// The two halves need bins k, k+1 walking up and bins N/2-k-1, N/2-k walking
// down, and a descending 16-byte load never lands on the pair you want, so
// each end keeps the previous vector and __vsel's the straddling pair out of
// the two.  hiPrev starts as data[0..3] because bin N/2 aliases bin 0.
//
// The twiddles are carried in the same trig recurrence the scalar kernel uses
// (cc = 2 sin^2(theta/2), ss = sin theta, theta = 4*pi/N so each scalar pair
// advances two bins per iteration), in double precision, and re-vectorised
// through a stack XMVECTORF32 at the end of every iteration: cLo/sLo carry
// bins (2i, 2i+1) and cHi/sHi bins (2i+2, 2i+1), each value duplicated across
// the real and imaginary lane of its complex slot.
//
// The loop bound is `size / 8` written INLINE in the condition: lifted into a
// local it is a provable trip count and MSVC converts the loop to CTR
// (`mtctr` / `bdnz`), where the image keeps `addi r29, r29, 1` /
// `cmpw cr6, r29, r10` / `blt cr6` (0x82E50858-0x82E50904).  72.4 -> 79.1.
//
// RESIDUAL (w7-an, 79.1 canonical): the rest is VMX/FPR allocation and the
// schedule it drives.  (a) The image homes perm_d/perm_e on the stack and
// keeps sLo in a register for the whole loop; we keep both perm controls in
// registers and spill sLo instead, which costs three extra vector memory ops
// per iteration.  (b) The image walks the low half with two induction
// variables (`addi r9, r31, 0x10` in the preheader, then `addi r8/r9, .., 0x10`
// separately); MSVC folds ours into one, recomputing loRead as `loWrite + 16`
// and rotating with `mr`.  (c) The double trig recurrence is scheduled
// s-chain-first in our build and c-chain-first in the image.
// NEGATIVE RESULT (w7-an, 2026-09-14): reversing the four `sv.f[n]` stores is
// byte-for-byte inert; so is reversing the uc/us declarations, and so is
// swapping the perm_d/perm_e declarations.  Reversing the four
// `c1 = c1 - uc1` / `s1 = s1 - us1` updates buys +0.5pp but SHRINKS the frame
// by 0x10 and changes the callee-save helpers, so it is a step away from the
// image's prologue -- not kept.
int fft_real_forward_altivec(float* data, long size, float* context) {
    int ret = FFTComplex(data, size / 2, -1, context);
    if (ret == 0) {
        XMVECTORU32 sel_hi = { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 };
        XMVECTORU32 perm_a = { 0x00010203, 0x14151617, 0x08090A0B, 0x1C1D1E1F };
        XMVECTORU32 perm_b = { 0x04050607, 0x10111213, 0x0C0D0E0F, 0x18191A1B };
        XMVECTORU32 perm_c = { 0x10111213, 0x04050607, 0x18191A1B, 0x0C0D0E0F };
        XMVECTORU32 perm_d = { 0x04050607, 0x04050607, 0x14151617, 0x14151617 };
        XMVECTORU32 perm_e = { 0x00010203, 0x00010203, 0x1C1D1E1F, 0x1C1D1E1F };

        XMVECTOR v_zero = { 0.0f, 0.0f, 0.0f, 0.0f };
        XMVECTOR v_half = { 0.5f, 0.5f, 0.5f, 0.5f };
        XMVECTOR v_sign_lo = { 1.0f, -1.0f, 1.0f, -1.0f };
        XMVECTOR v_sign_hi = { -1.0f, 1.0f, -1.0f, 1.0f };

        float inv_n = 1.0f / (float)(long long)size;
        float angle1 = inv_n * (float)(2.0 * M_PI);
        float sin_a = (float)sin(angle1);
        float angle2 = inv_n * (float)(4.0 * M_PI);
        double cc = (double)sin_a * (double)sin_a;
        cc = cc * 2.0;
        double ss = (float)sin(angle2);

        XMVECTORF32 sv;
        sv.f[0] = 1.0f;
        sv.f[1] = 1.0f;
        double c1 = (float)cos(angle1);
        sv.f[2] = (float)c1;
        sv.f[3] = (float)c1;
        XMVECTOR cLo = sv.v;
        double c2 = (float)cos(angle2);
        sv.f[0] = (float)c2;
        sv.f[1] = (float)c2;
        XMVECTOR cHi = sv.v;

        sv.f[0] = 0.0f;
        sv.f[1] = 0.0f;
        double s1 = (float)sin(angle1);
        sv.f[2] = (float)s1;
        sv.f[3] = (float)s1;
        XMVECTOR sLo = sv.v;
        double s2 = (float)sin(angle2);
        sv.f[0] = (float)s2;
        sv.f[1] = (float)s2;
        XMVECTOR sHi = sv.v;

        XMVECTOR loCur = __lvx(data, 0);
        XMVECTOR hiPrev = loCur;
        float dc_re = data[0];
        float dc_im = data[1];

        float* loWrite = data;
        float* loRead = data + 4;
        float* hiRead = data + (size / 4) * 4 - 4;
        float* hiWrite = hiRead;

        for (long i = 0; i < size / 8; i++) {
            XMVECTOR hiNew = __lvx(hiRead, 0);
            XMVECTOR loNext = __lvx(loRead, 0);
            XMVECTOR hiPair = __vsel(hiNew, hiPrev, *(XMVECTOR*)&sel_hi);
            hiPrev = hiNew;
            XMVECTOR loPair = __vsel(loCur, loNext, *(XMVECTOR*)&sel_hi);

            XMVECTOR cLoSigned = __vmaddfp(cLo, v_sign_lo, v_zero);
            XMVECTOR sumLo = __vaddfp(loCur, hiPair);
            XMVECTOR diffLo = __vsubfp(loCur, hiPair);
            XMVECTOR cHiSigned = __vmaddfp(cHi, v_sign_hi, v_zero);
            XMVECTOR sumHi = __vaddfp(hiNew, loPair);
            XMVECTOR diffHi = __vsubfp(hiNew, loPair);
            loCur = loNext;

            double uc1 = s1 * ss + c1 * cc;
            double uc2 = s2 * ss + c2 * cc;
            double us1 = s1 * cc - c1 * ss;
            double us2 = s2 * cc - c2 * ss;

            XMVECTOR loAdd = __vperm(sumLo, diffLo, *(XMVECTOR*)&perm_a);
            XMVECTOR loMulC = __vperm(sumLo, diffLo, *(XMVECTOR*)&perm_b);
            XMVECTOR loMulS = __vperm(sumLo, diffLo, *(XMVECTOR*)&perm_c);
            XMVECTOR hiAdd = __vperm(sumHi, diffHi, *(XMVECTOR*)&perm_a);
            XMVECTOR hiMulC = __vperm(sumHi, diffHi, *(XMVECTOR*)&perm_b);
            XMVECTOR hiMulS = __vperm(sumHi, diffHi, *(XMVECTOR*)&perm_c);

            XMVECTOR outLo = __vmaddfp(cLoSigned, loMulC, loAdd);
            XMVECTOR outHi = __vmaddfp(cHiSigned, hiMulC, hiAdd);

            c1 = c1 - uc1;
            c2 = c2 - uc2;
            s1 = s1 - us1;
            s2 = s2 - us2;

            outLo = __vnmsubfp(sLo, loMulS, outLo);
            outHi = __vnmsubfp(sHi, hiMulS, outHi);

            sv.f[1] = (float)c1;
            sv.f[0] = (float)c2;
            sv.f[3] = (float)s1;
            sv.f[2] = (float)s2;
            XMVECTOR trig = sv.v;

            __stvx(__vmaddfp(outLo, v_half, v_zero), loWrite, 0);
            loWrite += 4;
            __stvx(__vmaddfp(v_half, outHi, v_zero), hiWrite, 0);
            hiWrite -= 4;

            cHi = __vmrghw(trig, trig);
            sLo = __vperm(sHi, trig, *(XMVECTOR*)&perm_e);
            cLo = __vperm(cHiSigned, trig, *(XMVECTOR*)&perm_d);
            sHi = __vmrglw(trig, trig);

            loRead += 4;
            hiRead -= 4;
        }

        data[1] = dc_re - dc_im;
    }
    return ret;
}

// Real-input forward FFT dispatcher: small transforms (< 32) use the scalar
// kernel, larger ones the AltiVec kernel.
int FFTRealForward(float* data, unsigned long size, float* context) {
    if (size < 0x20) {
        return fft_real_forward_scalar(data, size, context);
    }
    return fft_real_forward_altivec(data, (long)size, context);
}

// In-place transpose of a square complex matrix held as 2x2 blocks of
// XMVECTORs: `size` is the matrix dimension, one vector holds two adjacent
// complex samples, and one "row step" is therefore two matrix rows (size * 16
// bytes). The two permute controls split/merge the halves of a pair of vectors,
// which is what turns a 2x2 block swap into four vperms. The diagonal block of
// each row is transposed in place after the inner loop.
void SquareComplexTransposeVector(float* data, long size) {
    XMVECTORU32 perm_lo = { 0x00010203, 0x04050607, 0x10111213, 0x14151617 };
    XMVECTORU32 perm_hi = { 0x08090A0B, 0x0C0D0E0F, 0x18191A1B, 0x1C1D1E1F };

    long i = 0;
    long blocks = size / 2;
    if (blocks <= 0) {
        return;
    }

    long rowStep = size * 16;
    long halfStep = blocks * 16;
    XMVECTOR pm_lo = *(XMVECTOR*)&perm_lo;
    XMVECTOR pm_hi = *(XMVECTOR*)&perm_hi;

    char* row = (char*)data;
    char* col = (char*)data;
    do {
        char* rowLo = row;
        char* rowHi = row + halfStep;
        char* colLo = col;
        char* colHi = col + halfStep;
        for (long j = 0; j < i; j++) {
            XMVECTOR cHi = __lvx(colHi, 0);
            XMVECTOR cLo = __lvx(colLo, 0);
            XMVECTOR rLo = __lvx(rowLo, 0);
            XMVECTOR rHi = __lvx(rowHi, 0);
            XMVECTOR outRowLo = __vperm(cLo, cHi, pm_lo);
            XMVECTOR outRowHi = __vperm(cLo, cHi, pm_hi);
            XMVECTOR outColLo = __vperm(rLo, rHi, pm_lo);
            XMVECTOR outColHi = __vperm(rLo, rHi, pm_hi);
            __stvx(outRowLo, rowLo, 0);
            rowLo += 16;
            __stvx(outRowHi, rowHi, 0);
            rowHi += 16;
            __stvx(outColLo, colLo, 0);
            colLo += rowStep;
            __stvx(outColHi, colHi, 0);
            colHi += rowStep;
        }
        XMVECTOR dLo = __lvx(rowLo, 0);
        XMVECTOR dHi = __lvx(rowHi, 0);
        i += 1;
        row += rowStep;
        col += 16;
        __stvx(__vperm(dLo, dHi, pm_lo), rowLo, 0);
        __stvx(__vperm(dLo, dHi, pm_hi), rowHi, 0);
    } while (i < blocks);
}

// Square-matrix (four-step) complex FFT, used by FFTComplex when the transform
// is too big for the ping-pong path and log2(size) is even. Both directions run
// the column-wise pass and a square complex transpose; the forward direction
// transposes afterwards, the inverse before. `inverse == -1` selects the
// inverse. Returns 0x16 (EINVAL) when size is not a power of two, or when
// log2(size) is odd -- i.e. when the data is not a square matrix of complex
// pairs. `inverse == -1` is the FORWARD direction -- the argument is the FFT
// sign convention, not a boolean, and reading it as a boolean inverts the
// transform.
int fft_square_matrix(float* data, long size, long inverse, float* context) {
    long power;
    long bits = 1;
    if (size == 1) {
        power = 0;
    } else {
        long p2 = 2;
        if (size > 2) {
            do {
                p2 *= 2;
                bits += 1;
            } while (p2 < size);
        }
        power = bits;
    }

    if ((1L << power) != size) {
        return 0x16;
    }
    if (power & 1) {
        return 0x16;
    }

    long ret;
    if (inverse == -1) {
        ret = fft_matrix_forward_columnwise(data, size, context);
        if (ret == 0) {
            SquareComplexTransposeVector(data, 1L << (power / 2));
        }
    } else {
        SquareComplexTransposeVector(data, 1L << (power / 2));
        ret = fft_matrix_inverse_columnwise(data, size, context);
    }
    return ret;
}
