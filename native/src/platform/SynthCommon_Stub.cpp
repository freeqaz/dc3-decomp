// DC3 Native Port - Synth Common Xbox Stub
// Replaces system/synth/Common_Xbox.cpp

// DSP functions that were Xbox-specific
void DspClearBuffer(float *&buf, int sizeSamps) {
    if (buf) {
        for (int i = 0; i < sizeSamps; i++) buf[i] = 0.0f;
    }
}
void DspFree(float *&f) {
    delete[] f;
    f = nullptr;
}
// The third parameter must be IXAudioBatchAllocator*, not void*. Every caller
// (dsp/DelayEffect.cpp, dsp/FlangerEffect.cpp) includes synth/Common_Xbox.h and
// therefore references _Z11DspAllocateRPfiP21IXAudioBatchAllocator, while a
// void* parameter defined _Z11DspAllocateRPfiPv -- a different symbol. The
// mismatch left the real reference unresolved, the weak asm-label stub
// _stub_fn_8 in engine_stubs_generated.cpp satisfied it and returned 0 without
// touching `buf`, and DelayEffect::mBuffer / FlangerEffect::mDelayBuffers[] are
// not in their constructors' init lists -- so the delay and flanger effects ran
// on an indeterminate pointer and ~DelayEffect handed it to delete[].
class IXAudioBatchAllocator;
void DspAllocate(float *&buf, int sizeSamps, IXAudioBatchAllocator *) {
    buf = new float[sizeSamps];
}
