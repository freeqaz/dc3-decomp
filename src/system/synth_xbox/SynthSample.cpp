#include "synth_xbox/SynthSample.h"
#include "obj/Object.h"
#include "synth/SampleData.h"
#include "synth_xbox/SampleInst360.h"
#include "utl/MemMgr.h"

void *SampleAlloc(int size, const char *, int, const char *, int) {
    return MemAlloc(size, __FILE__, __LINE__, "Sample Data", 0);
}

void SampleFree(void *mem, const char *, int, const char *) {
    if (mem)
        MemFree(mem, __FILE__, __LINE__, "");
}

SynthSample360::SynthSample360() {}

void SynthSample360::Init() {
    Register();
    SampleData::SetAllocator(SampleAlloc, SampleFree);
}

bool SynthSample360::IsXMA() const {
    return mSampleData.GetFormat() == SampleData::kXMA;
}

float SynthSample360::LengthMs() const {
    if (mSampleData.HasData()) {
        int numSamples = mSampleData.GetNumSamples();
        int sampleRate = GetSampleRate();
        return (float)numSamples * 1000.0f / (float)sampleRate;
    }
    return 0.0f;
}

SampleInst *SynthSample360::NewInst(bool b, int i1, int i2) {
    if (mSampleData.HasData()) {
        return new SampleInst360(this, b, i1, i2);
    }
    return nullptr;
}
