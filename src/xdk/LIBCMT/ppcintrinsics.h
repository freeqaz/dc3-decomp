#pragma once
#include "vectorintrinsics.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HX_NATIVE
unsigned long long __mftb();
#endif
double __fsel(double fComparand, double fValGE, double fValLT);
float __frsqrte(float);
void __dcbst(int, void *);
void __emit(unsigned int);

// This compiler build has no __lwsync intrinsic -- c2.dll only knows the
// bare `sync`/`isync` mnemonics -- so spell it out. 0x7c2004ac is `sync 1`,
// which the PowerPC ISA calls lwsync.
#define __lwsync() __emit(0x7c2004ac)

#ifdef __cplusplus
}
#endif
