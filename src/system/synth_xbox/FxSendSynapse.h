#pragma once

namespace DSP {

struct SynapseBand {
    // bool, not char: SynapseAPO::OnSetParameters compares two of these with an
    // unsigned cmplw and passes the raw byte straight to
    // Synapse::SetVoiceEnabled(unsigned, bool). Declared char, MSVC sign-extends
    // both sides (extsb/extsb/cmpw) and then materialises a 0/1 with subic/subfe.
    bool enabled;       // 0x00
    char pad[3];        // 0x01-0x03
    float freq;         // 0x04 - center frequency
    float gain;         // 0x08 - gain in dB
    float q;            // 0x0c - quality factor
    float coeff0;       // 0x10 - biquad filter coefficient
    float coeff1;       // 0x14 - biquad filter coefficient
    float coeff2;       // 0x18 - biquad filter coefficient
};  // size = 0x1c

struct SynapseAPOParams {
    SynapseAPOParams() throw();

    SynapseBand bands[3];     // 0x00 - 0x53
    float lowCutoffFreq;      // 0x54 - low cutoff frequency (default 20 Hz)
    float highCutoffFreq;     // 0x58 - high cutoff frequency (default 40 Hz)
};

}  // namespace DSP
