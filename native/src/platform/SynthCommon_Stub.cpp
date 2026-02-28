// DC3 Native Port - Synth Common Xbox Stub
// Replaces system/synth/Common_Xbox.cpp

// DSP functions that were Xbox-specific
void DspClearBuffer(float *&, int) {}
void DspFree(float *&f) { f = nullptr; }
void DspAllocate(float *&buf, int sizeSamps, void *) {
    buf = new float[sizeSamps];
}
