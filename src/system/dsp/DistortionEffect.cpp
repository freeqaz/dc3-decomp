#include "synth\DistortionEffect.h"
#include "os\Debug.h"
#include "xdk\xaudio2\xaudio2.h"
#include <cmath>

#line 6 "dsp\\DistortionEffect.cpp"
DistortionEffect::DistortionEffect(IXAudioBatchAllocator *) : mDrive(0) {}

// Waveshaper distortion: f(x) = x * (amount+1) / (|x| * amount + 1)
void DistortionEffect::Process(float *f, int numSamples, int numChans) {
    MILO_ASSERT(numChans <= 2, 0x1b);

    float drive = mDrive;
    float headroomCopy = 1.0f - drive;
    float minHeadroom = 0.01f;
    float headroom = headroomCopy;
    float *divisor;

    if (headroom < 0.01f) {
        divisor = &minHeadroom;
    } else {
        divisor = &headroomCopy;
    }

    float amount = (drive / *divisor) * 2.0f;

    // w19-d: fuzzy 99.85 -> 100 (byte-identical): plain indexed loop (MSVC
    // makes the r9/r10 walkers itself); `gain` is loop-scoped -- hoisted above
    // the loop it is computed before the numSamples guard and recolours f12/f13.
    for (int i = 0; i < numSamples; i++) {
        float gain = amount + 1.0f;
        float sampleL = f[i * numChans];
        f[i * numChans] = (sampleL * gain) / ((fabsf(sampleL) * amount) + 1.0f);
        if (numChans == 2) {
            float sampleR = f[i * numChans + 1];
            f[i * numChans + 1] = (sampleR * gain) / ((fabsf(sampleR) * amount) + 1.0f);
        }
    }
}

void DistortionEffect::SetParameters(DistortionEffect::Params const &params) {
    mDrive = params.unk4 * 0.01f;
}

void DistortionEffect::Reset() {}
