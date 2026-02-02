#include "complex.h"
#include <math.h>

extern "C" {
    extern complex* lbl_8316EB70;
    extern complex* lbl_8316EBA8;
    extern complex* lbl_83172BB0;
    extern const double __real_0000000000000000;
    extern const double __real_3f50624dd2f1a9fc;
    extern const double __real_3fe0000000000000;
    extern const double __real_3ff0000000000000;
    extern const double __real_4000000000000000;
    extern const double __real_400921fb60000000;
    extern const double __real_401921fb60000000;
    extern const double __real_bff0000000000000;

    void expand(complex*, int, complex*, ...);
    complex expj(complex*, double);
    double exp(double);
}

void compute_z_mzt(void) {
    complex sp50;
    int loop_count = 0;
    int array_offset = 0;
    complex* src_base = lbl_8316EBA8;
    complex* dst_base = lbl_83172BB0;

    // Load header values from source
    int src_count1 = *(int*)((char*)src_base + 0x4000);
    int src_count2 = *(int*)((char*)src_base + 0x4004);

    // Copy headers to destination
    *(int*)((char*)dst_base + 0x4000) = src_count1;
    *(int*)((char*)dst_base + 0x4004) = src_count2;

    // Process first array
    if (src_count1 > 0) {
        do {
            complex* src = (complex*)((char*)src_base + array_offset);
            complex* dst = (complex*)((char*)dst_base + array_offset);

            expj(&sp50, src->x);
            loop_count++;
            array_offset += 0x10;
            dst->x = sp50.x;
            dst->y = sp50.y;

            src_count1 = *(int*)((char*)dst_base + 0x4000);
        } while (loop_count < src_count1);
    }

    src_count2 = *(int*)((char*)dst_base + 0x4004);
    loop_count = 0;

    // Process second array (offset by 0x2000)
    if (src_count2 > 0) {
        do {
            complex* src = (complex*)((char*)src_base + 0x2000 + array_offset);
            complex* dst = (complex*)((char*)dst_base + 0x2000 + array_offset);

            expj(&sp50, src->x);
            loop_count++;
            array_offset += 0x10;
            dst->x = sp50.x;
            dst->y = sp50.y;

            src_count2 = *(int*)((char*)dst_base + 0x4004);
        } while (loop_count < src_count2);
    }
}

void compute_bpres(double arg_sp10, double arg_sp18, double arg_sp20, double arg_sp28,
                   double arg_sp30, double arg_sp38, double arg_sp40, double arg_sp48,
                   double arg_sp50, double arg_sp58, double arg_sp60, double arg_sp68,
                   double arg_sp70,
                   double arg_sp78, complex* arg_sp80, complex* arg_sp90,
                   complex* arg_spA0, complex* arg_sp20B0) {
    double temp_f30 = lbl_8316EB70[6].x * __real_401921fb60000000;
    double temp_f28 = exp(-(temp_f30 / (lbl_8316EB70[0].x * __real_4000000000000000)));
    double var_f29 = temp_f30;
    unsigned char var_r30 = 0;
    double var_f27 = __real_0000000000000000;
    int var_r29 = 0;
    double var_f26 = __real_400921fb60000000;
    double temp_f24 = __real_3f50624dd2f1a9fc;
    double temp_f25 = __real_3fe0000000000000;

    while (1) {
        if (var_r30 != 0) break;

        complex* temp_r3 = &arg_sp80[0];
        expj(temp_r3, var_f29);

        arg_sp50 = temp_r3->x * temp_f28;
        double temp_f0 = arg_sp58 * temp_f28;
        arg_sp58 = temp_f0;

        arg_sp60 = arg_sp50;
        arg_sp68 = -temp_f0;

        expand(lbl_83172BB0, 2, arg_spA0);

        complex* temp_r3_2 = &arg_sp90[0];
        expj(temp_r3_2, temp_f30);

        double temp_f0_2 = arg_sp78 / arg_sp70;

        if ((float)temp_f0_2 > (float)__real_0000000000000000) {
            var_f26 = var_f29;
        } else {
            var_f27 = var_f29;
        }

        if (fabs(temp_f0_2) < (float)temp_f24) {
            var_r30 = 1;
        }

        var_r29++;
        var_f29 = (var_f26 + var_f27) * temp_f25;

        if (var_r29 >= 0x32) {
            break;
        }
    }
}
