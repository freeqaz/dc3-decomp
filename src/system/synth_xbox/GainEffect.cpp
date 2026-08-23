#include "GainEffect.h"

namespace ATG {

// Same story as HeadsetXferEffect: the shipped image emits
// ??__E?m_regProps@?$CSampleXAPOBase@VGainEffect@@... as a dynamic initializer
// (memset of FriendlyName's tail, memcpy of the shared 0x50-byte wide
// L"Copyright (C) 2008 Microsoft Corp..." literal into CopyrightInfo, memset of
// its tail, then seven scalar stores at +0x410). ALL FOURTEEN of these
// initializers are at 0% binary-wide, so this is a cross-cutting XDK class and
// not a GainEffect-specific gap -- solving it would pay 14x, and is deliberately
// left out of this lane. link_glue.cpp already /ALTERNATENAMEs the one we do not
// produce.
template <>
XAPO_REGISTRATION_PROPERTIES CSampleXAPOBase<GainEffect, GainEffectParams>::m_regProps = {};

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
