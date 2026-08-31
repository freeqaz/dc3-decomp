#include "synth_xbox\HeadsetPlaybackEffect.h"
#include "xdk\LIBCMT\string.h"

namespace ATG {

// m_regProps comes from the primary template in xdk/xaudio2/xapobase.h via
// __uuidof(HeadsetPlaybackEffect); see HeadsetPlaybackEffect.h for the uuid
// attribute.
template class CSampleXAPOBase<HeadsetPlaybackEffect, HeadsetPlaybackEffectParams>;

} // namespace ATG

HeadsetPlaybackEffect::HeadsetPlaybackEffect(HeadsetXferEffect **xfer) {
    mCounter = 0;
    for (int i = 0; i < 4; i++) {
        mXfer[i] = xfer[i];
    }

    HeadsetPlaybackEffectParams params;
    memset(&params, 0, sizeof(params));
    SetParameters(&params, sizeof(params));
}

// Concatenates one 0x400-byte half from each of the four transfer rings into the
// output block. HeadsetXferEffect::mBuffer sits at +0x64, i.e. 25 floats in, and
// holds two 256-frame halves -- so the read offset is 25 + (counter % 2) * 256
// floats, the half HeadsetXferEffect::DoProcess is not writing this pass.
void HeadsetPlaybackEffect::DoProcess(
    const HeadsetPlaybackEffectParams &, float *__restrict buffer, unsigned int,
    unsigned int
) {
    int index = mCounter;
    int offset = 25 + (index % 2) * 256;
    float *dest = buffer;
    for (int i = 0; i < 4; i++) {
        memcpy(dest, (float *)mXfer[i] + offset, 0x400);
        dest += 256;
    }
    mCounter = index + 1;
}
