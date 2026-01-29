#pragma once

namespace soundtouch {
    class SoundTouch;
}

struct PitchShiftEffectParams {
};

template<typename T, typename P>
class CSampleXAPOBase {
public:
    CSampleXAPOBase();
    virtual ~CSampleXAPOBase();
};

class PitchShiftEffect : public CSampleXAPOBase<PitchShiftEffect, PitchShiftEffectParams> {
public:
    PitchShiftEffect();
    virtual ~PitchShiftEffect();

private:
    void* mSoundTouch;
    float mPitch;
    unsigned char mFlag;
    unsigned int mChannels;
};
