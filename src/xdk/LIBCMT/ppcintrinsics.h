#pragma once
#include "vectorintrinsics.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned long long __mftb();
double __fsel(double fComparand, double fValGE, double fValLT);
float __frsqrte(float);
void __dcbst(int, void *);

#ifdef __cplusplus
}
#endif
