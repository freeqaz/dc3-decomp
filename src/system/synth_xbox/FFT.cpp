#include "FFT.h"
#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdlib>
#include "xdk/LIBCMT/vectorintrinsics.h"

// External declarations
int FFTComplex(float* data, long size, long inverse, float* context);

// VMX constants
extern "C" {
    extern unsigned char __vmx_3f800000bf8000003f800000bf800000[];
    extern unsigned char __vmx_bf8000003f800000bf8000003f800000[];
    extern unsigned char __vmx_00000000000000000000000000000000[];
}

int fft_matrix_forward_columnwise(float* data, long size, float* context) {
    if (data == nullptr || size <= 0 || (((unsigned long)data) & 0xF) != 0) {
        return 0x16;  // Invalid pointer alignment
    }

    // Calculate power of 2 for size
    int power = 0;
    int p2 = 1;

    if (size == 1) {
        power = 0;
    } else if (size == 2) {
        power = 1;
    } else {
        p2 = 2;
        power = 1;
        while (p2 < size) {
            p2 *= 2;
            power++;
        }
    }

    // Check if size is power of 2
    if ((1 << power) != size) {
        return 0x16;
    }

    // Check data alignment (must be 16-byte aligned)
    if ((((unsigned long)data) & 0xF) != 0) {
        return 0x16;
    }

    // Calculate temp buffer size
    int temp_power = power / 2;
    int rows = 1 << temp_power;
    int cols_power = temp_power;
    if (power & 1) {
        cols_power++;
    }
    int cols = 1 << cols_power;

    // Allocate temp buffer
    float* temp = (float*)malloc(rows * 16);
    if (temp == nullptr) {
        return 0xC;  // Out of memory
    }

    // Setup VMX permutation masks and constants
    int half_cols = cols / 2;
    int half_rows = rows / 2;

    // Main processing loop
    for (int col = 0; col < half_cols; col++) {
        // Calculate twiddle factors
        float angle1 = (float)(col * 2.0 * M_PI / (float)cols);
        float angle2 = (float)((col + 2) * 2.0 * M_PI / (float)cols);

        float sin_val1 = sinf(angle1);
        float sin2_val1 = sin_val1 * sin_val1 * 2.0f;
        float sin_angle1 = sinf(angle1 * 2.0f);

        float sin_val2 = sinf(angle2);
        float sin2_val2 = sin_val2 * sin_val2 * 2.0f;
        float sin_angle2 = sinf(angle2 * 2.0f);

        float cos_angle1 = cosf(angle1);
        float cos_angle2 = cosf(angle2);

        // Process rows
        for (int row = 0; row < half_rows; row++) {
            // Perform column-wise FFT
            // ... vector operations using VMX
        }

        // Perform FFT on temp buffer
        int err = FFTComplex(temp, rows, -1, context);
        if (err != 0) {
            free(temp);
            return err;
        }

        // Process second half of columns
        err = FFTComplex((float*)((unsigned long)temp + rows * 8), rows, -1, context);
        if (err != 0) {
            free(temp);
            return err;
        }
    }

    // Process remaining columns if size is power of 2 > 2
    if (rows > 1) {
        for (int row = rows - 1; row >= 0; row--) {
            float* col_ptr = (float*)((unsigned long)data + row * cols * 8);
            int err = FFTComplex(col_ptr, cols, -1, context);
            if (err != 0) {
                free(temp);
                return err;
            }
        }
    }

    free(temp);
    return 0;
}

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
            // vmrglw gives {sin_2a1, sin_2a1, sin_2a2, sin_2a2}
            XMVECTOR v_sin2a = __vmrglw(sv.v, sv.v);
            // vmrghw gives {sin2_1, sin2_1, sin2_2, sin2_2} (same source vector)
            XMVECTOR v_sin2 = __vmrghw(sv.v, sv.v);

            // Start overwriting sv for cos vector
            sv.f[0] = one_f;

            // vor128 v126, v124, v124 (copy sign to v126)
            XMVECTOR v_im_init = v_sign;
            // vmaddcfp128 v126, v0, v125 (v126 = v0 * v126 + zero)
            v_im_init = __vmaddfp(v_sin2a, v_im_init, v_zero);

            sv.f[2] = (float)cos(angle1);
            sv.f[3] = (float)cos(angle2);

            XMVECTOR v_cos_vec = __lvx(&sv, 0);
            XMVECTOR v_cos_splat = __vspltw(v_cos_vec, 0);

            // Phase 3: Overwrite with sin values, load it
            sv.f[2] = (float)s1d;
            sv.f[3] = (float)s2d;

            XMVECTOR v_sin_vec = __lvx(&sv, 0);
            XMVECTOR v_sin_merged = __vmrglw(v_sin_vec, v_sin_vec);

            // Initialize running twiddle factors
            XMVECTOR v_cos_merged = __vmrglw(v_cos_vec, v_cos_vec);
            XMVECTOR w_re1 = v_cos_splat;
            XMVECTOR w_im1 = v_zero;
            XMVECTOR w_re2 = v_cos_merged;
            // vmaddcfp128 v11, v124, v125 => v11 = v124 * v11 + v125
            XMVECTOR w_im2 = __vmaddfp(v_sign, v_sin_merged, v_zero);

            float *dst1 = temp;
            float *dst2 = temp2;
            char *src_data = (char *)data_ptr;
            int k = 0;

            if (half_cols > 0) {
                int data_stride = half_rows * 0x10;

                do {
                    // Load first data element (row 0)
                    XMVECTOR d0 = __lvx(src_data, 0);
                    src_data += data_stride;

                    // Copy sin² values for this iteration
                    XMVECTOR sp_sin2 = v_sin2;
                    XMVECTOR sp_sin2_2 = v_sin2;
                    XMVECTOR sp_sin2_3 = v_sin2;

                    // Load swap mask, begin twiddle recurrence
                    XMVECTOR pm_swap_v = *(XMVECTOR *)&perm_swap;
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
                    XMVECTOR new_im1 = __vmaddfp(w_re1, v_im_init, p_im1);
                    XMVECTOR r2 = __vmaddfp(w_im2, d_swap1, t2);
                    new_re2 = __vnmsubfp(w_im2, v_im_init, new_re2);
                    XMVECTOR new_im2 = __vmaddfp(w_re2, v_im_init, p_im2);

                    w_re1 = new_re1;
                    w_re2 = new_re2;
                    w_im1 = new_im1;
                    w_im2 = new_im2;

                    k += 1;

                    // Interleave results and store to temp
                    XMVECTOR pm_lo_v = *(XMVECTOR *)&perm_lo;
                    XMVECTOR out_lo = __vperm(r1, r2, pm_lo_v);
                    XMVECTOR out_hi = __vperm(r1, r2, *(XMVECTOR *)&perm_hi);

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
