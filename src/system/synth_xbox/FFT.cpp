#include "FFT.h"
#define _USE_MATH_DEFINES
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdlib>

// External declarations
extern "C" {
    int FFTComplex(float* data, long size, int inverse, void* context);
    float* __savefpr_24();
    unsigned long __savevmx_120();
    void __restvmx_120(unsigned long);
}

// VMX constants
extern "C" {
    extern unsigned char __vmx_3f800000bf8000003f800000bf800000[];
    extern unsigned char __vmx_00000000000000000000000000000000[];
}

extern float __real_3f800000;  // 1.0f
extern double __real_4000000000000000;  // 2.0
extern float __real_40490fdb;  // PI

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

    void *temp = malloc(cols * 0x10);
    if (temp == 0) {
        return 0xC;
    }

    for (int i = cols - 1; i >= 0; i--) {
        ret = FFTComplex((float *)data + i * rows * 2, rows, 1, scratch);
        if (ret != 0) {
            free(temp);
            return ret;
        }
    }

    free(temp);
    return 0;
}
