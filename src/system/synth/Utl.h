#pragma once
#include "os/FileCache.h"
#include "utl/Cache.h"
#include "utl/Symbol.h"

class WavFileCacheHelper : public FileCacheHelper {
public:
    virtual const char *CacheFile(const char *);
};

void SynthUtlInit();
float CalcRateForTempoSync(Symbol, float);
float CalcSpeedFromTranspose(float);
float CalcTransposeFromSpeed(float);
const char *CacheWav(const char *, CacheResourceResult &);

extern WavFileCacheHelper gWavFileCacheHelper;