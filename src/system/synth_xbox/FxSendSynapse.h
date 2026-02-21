#pragma once

namespace DSP {

struct SynapseBand {
    char enabled;       // 0x00
    char pad[3];        // 0x01-0x03
    float freq;         // 0x04
    float gain;         // 0x08
    float q;            // 0x0c
    float unk10;        // 0x10
    float unk14;        // 0x14
    float unk18;        // 0x18
};  // size = 0x1c

struct SynapseAPOParams {
    SynapseAPOParams();

    SynapseBand bands[3];  // 0x00 - 0x53
    float field_0x54;      // 0x54
    float field_0x58;      // 0x58
};

}  // namespace DSP
