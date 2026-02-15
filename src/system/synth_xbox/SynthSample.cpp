#include "synth_xbox/SynthSample.h"
#include "obj/Object.h"
#include "synth/SampleData.h"
#include "utl/MemMgr.h"

void *SampleAlloc(int size, const char *file, int line, const char *name, int) {
    return MemAlloc(size, file, line, name, 0x20);
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
    int rate = mSampleData.GetSampleRate();
    if (rate == 0)
        return 0;
    int numChannels = mSampleData.NumChannels();
    if (numChannels == 0)
        return 0;
    int size = mSampleData.SizeAs(SampleData::kPCM);
    return (float)(size / (numChannels * 2)) / (float)rate * 1000.0f;
}

SampleInst *SynthSample360::NewInst(bool b, int i1, int i2) {
    return nullptr; // TODO: needs SampleInst360 implementation
}
