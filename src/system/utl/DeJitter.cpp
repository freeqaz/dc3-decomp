#include "utl/DeJitter.h"
#include "obj/Data.h"

float DeJitter::sTimeScale = 1;

DeJitter::DeJitter() { Reset(); }

void DeJitter::Reset() {
    unk80 = 0;
    unk84 = -2;
    unk88 = 0;
    unk8c = 0;
    for (int i = 0; i < 32; i++) {
        unk0[i] = 0;
    }
}

float DeJitter::NewMs(float f1, float &fref) {
    float filteredValue = 1.0000000150474662e+30;
    // Ring buffer indices (0-31 wrapping): prevPos is previous write position, historyPos is the
    // position unk84 steps back (for averaging interval)
    float sample = f1;
    static DataNode &n = DataVariable("dejitter_disable"); // FLT_MAX-like sentinel for uninitialized result
    int prevPos = (unk80 - 1) & 0x1F;
    int historyPos = (prevPos - unk84) & 0x1F;

    // Only apply jitter correction if enabled and have accumulated enough samples
    if (!n.Int()) {
        if (unk84 > 8) { // Need more than 8 samples in the history
            // Calculate average delta since unk84 steps ago
            float f0 = (unk0[prevPos] - unk0[historyPos]) / (float)unk84;
            // Smooth the average with exponential moving average (alpha=0.1)
            if (unk88 == 0.0f) {
                unk88 = f0;
            }
            f0 = (f0 - unk88) * 0.1f + unk88;
            filteredValue = f0;
            unk88 = f0;
            if (sTimeScale != 1.0f) {
                // With time scale, output is scaled delta
                f0 = f0 * sTimeScale;
                unk88 = f0;
                filteredValue = f0 + unk8c;
            } else {
                // Without time scale, clamp output to ±33ms from previous value
                float f12 = unk8c + f0;
                float f11 = sample - 33.0f;
                float f13 = sample + 33.0f;
                float f10 = ((f11 - f12) >= 0.0f) ? f11 : f12;
                filteredValue = ((f10 - f13) >= 0.0f) ? f13 : f10;
            }
            // Don't let result go below previous output value
            if (filteredValue < unk8c) {
                filteredValue = unk8c;
            }
        }
    }

    // Store new sample in ring buffer
    unk0[unk80] = sample;
    // Use computed jittered value if it was calculated, otherwise use raw input
    if (filteredValue != 1.0000000150474662e+30) {
        sample = filteredValue;
    }
    unk80 = (unk80 + 1) & 0x1F;

    // Output delta: on initialization (-2), use default frame time; otherwise use difference
    if (unk84 == -2) {
        fref = 16.666f; // Default 60 FPS frame time
    } else {
        fref = sample - unk8c;
    }

    // Count up to stabilization threshold
    if (unk84 < 30) {
        unk84 = unk84 + 1;
    }

    // Remember output for next iteration
    unk8c = sample;
    return sample;
}
