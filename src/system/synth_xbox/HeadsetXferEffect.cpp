#include "HeadsetXferEffect.h"
#include "xdk\LIBCMT\string.h"

namespace ATG {

// m_regProps comes from the primary template in xdk/xaudio2/xapobase.h via
// __uuidof(HeadsetXferEffect); see HeadsetXferEffect.h for the uuid attribute.
template class CSampleXAPOBase<HeadsetXferEffect, HeadsetXferEffectParams>;

} // namespace ATG

HeadsetXferEffect::HeadsetXferEffect() {
    mBufferIndex = 0;
    memset(mBuffer, 0, sizeof(mBuffer));

    HeadsetXferEffectParams params;
    params.effect = this;
    SetParameters(&params, sizeof(params));
}

// Copies one 0x400-byte block into alternating halves of mBuffer, advancing a
// free-running index.
void HeadsetXferEffect::DoProcess(
    const HeadsetXferEffectParams &, float *__restrict buffer, unsigned int, unsigned int
) {
    memcpy(&mBuffer[(mBufferIndex % 2) * 0x400], buffer, 0x400);
    mBufferIndex++;
}
