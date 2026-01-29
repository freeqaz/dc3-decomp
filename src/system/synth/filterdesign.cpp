#include "complex.h"
#include <math.h>

extern "C" {
    extern complex* lbl_8316EB70;
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
