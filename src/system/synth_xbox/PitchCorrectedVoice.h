#pragma once

namespace DSP {
namespace Synapse {

class PitchCorrectedVoice {
public:
    PitchCorrectedVoice();
    ~PitchCorrectedVoice();
    float GetCorrection();
    void SetAmount(float);
    void SetProximityEffect(float);
    void SetProximityFocus(float);
    void SetTransposition(float);
    void SetAttackSmoothing(float);
    void SetReleaseSmoothing(float);

    unsigned char _pad[0x38]; // sizeof = 0x38 (56 bytes)
};

} // namespace Synapse
} // namespace DSP
