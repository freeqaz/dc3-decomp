#include "complex.h"
#include <cmath>

complex expj(double d1) {
    struct complex result;
    double dVar1 = sin(d1);
    double dVar2 = cos(d1);

    result.x = dVar2;
    result.y = dVar1;

    return result;
}

complex csqrt(complex cplx) {
    complex result;
    double h;
    double dy;
    double dx;

    h = hypot(cplx.y, cplx.x);
    dy = (h - cplx.x) * 0.5;
    result.y = (dy >= 0.0) ? sqrt(dy) : 0.0;

    dx = (cplx.y + h) * 0.5;
    result.x = (dx >= 0.0) ? sqrt(dx) : 0.0;

    if (cplx.y < 0.0)
        result.y = -result.y;

    return result;
}

complex cexp(complex cplx) {
    struct complex result;
    complex phase = expj(cplx.y);
    double magnitude = exp(cplx.x);
    result.x = magnitude * phase.x;
    result.y = magnitude * phase.y;
    return result;
}

complex operator/(complex cplx1, complex cplx2) {
    complex result;
    double dVar1 = 1.0 / (cplx2.x * cplx2.x + cplx2.y * cplx2.y);
    result.x = (cplx1.y * cplx2.y + cplx1.x * cplx2.x) * dVar1;
    result.y = (cplx1.y * cplx2.x - cplx1.x * cplx2.y) * dVar1;
    return result;
}

complex operator*(complex cplx1, complex cplx2) {
    complex result;
    result.x = cplx1.x * cplx2.x - cplx2.y * cplx1.y;
    result.y = cplx2.x * cplx1.y + cplx2.y * cplx1.x;
    return result;
}
