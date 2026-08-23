#include "HeadsetXferEffect.h"
#include "xdk\LIBCMT\string.h"

namespace ATG {

// The shipped image emits ??__E?m_regProps@?$CSampleXAPOBase@VHeadsetXferEffect@@...
// as a dynamic initializer; link_glue.cpp /ALTERNATENAMEs the one we do not
// produce. The values themselves were never recovered for this effect.
template <>
XAPO_REGISTRATION_PROPERTIES
    CSampleXAPOBase<HeadsetXferEffect, HeadsetXferEffectParams>::m_regProps = {};

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
