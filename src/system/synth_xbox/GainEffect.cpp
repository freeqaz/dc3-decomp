#include "GainEffect.h"

namespace ATG {

// m_regProps comes from the primary template in xdk/xaudio2/xapobase.h via
// __uuidof(GainEffect); see GainEffect.h for the uuid attribute.
template class CSampleXAPOBase<GainEffect, GainEffectParams>;

} // namespace ATG

// SetRemoteGain stores DbToRatio(...) here; the shipped .data holds 1.0f.
float GainEffect::sGain = 1.0f;

GainEffect::GainEffect() {
    GainEffectParams params;
    SetParameters(&params, sizeof(params));
}

// Scales the whole interleaved block by the current remote-talker gain. The
// target is VMX128-vectorised (splatted sGain in v63, four lvx128/vmulfp128/
// stvx128 per iteration) off a plain pointer loop -- note the unsigned pointer
// compare and early `bgelr`, which is the vectoriser's trip-count guard.
void GainEffect::DoProcess(
    const GainEffectParams &, float *__restrict buffer, unsigned int frameCount,
    unsigned int channelCount
) {
    float *end = buffer + frameCount * channelCount;
    for (float *p = buffer; p < end; p++) {
        *p *= sGain;
    }
}
