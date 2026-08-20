#include "synth_xbox\SynthSample.h"
#include "Memory.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "synth\SampleData.h"
#include "synth360\SampleInst.h"
#include "utl\MemMgr.h"

















void *SampleAlloc(int size, const char *file, int line, const char *name, int) {
    void *ret = PhysicalAllocTracked(size, 4, file, line, "SampleData(phys)");
    MILO_ASSERT(ret, 0x19);
    return ret;
}

void SampleFree(void *mem, const char *, int, const char *) {
    if (mem)
        PhysicalFreeTracked(mem, __FILE__, __LINE__, "");
}

SynthSample360::SynthSample360() {}

void SynthSample360::Init() {
    Register();
    SampleData::SetAllocator(SampleAlloc, SampleFree);
}

bool SynthSample360::IsXMA() const {
    return mSampleData.GetFormat() == SampleData::kXMA;
}

int SynthSample360::GetNumSamples() const {
    return mSampleData.GetNumSamples();
}

int SynthSample360::GetNumBytes() const {
    return mSampleData.GetSizeBytes();
}

const void *SynthSample360::GetData() const {
#ifdef HX_NATIVE
    // DataAddr() truncates the pointer to 32 bits, which is a no-op on the
    // Xbox target and fatal on LP64.
    return mSampleData.DataPtr();
#else
    return (const void *)mSampleData.DataAddr();
#endif
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
