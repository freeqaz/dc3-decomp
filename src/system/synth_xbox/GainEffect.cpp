#include "GainEffect.h"
#include "HeadsetPlaybackEffect.h"
#include "xdk\LIBCMT\vectorintrinsics.h"

namespace ATG {

// m_regProps comes from the primary template in xdk/xaudio2/xapobase.h via
// __uuidof(GainEffect); see GainEffect.h for the uuid attribute.
template class CSampleXAPOBase<GainEffect, GainEffectParams>;

// The COMDAT the linker kept at 0x82E43FB0 -- inside the address range dtk
// attributes to GainEffect.obj -- carries the HeadsetPlaybackEffect name, not
// the GainEffect one (GainEffect.s:327).  Process's 33 instructions are
// identical for every Effect, so the instantiations fold and the surviving
// name need not be this TU's.  Emit the named one here or the row cannot pair.
template void CSampleXAPOBase<HeadsetPlaybackEffect, HeadsetPlaybackEffectParams>::Process(
    UINT, const XAPO_PROCESS_BUFFER_PARAMETERS *, UINT, XAPO_PROCESS_BUFFER_PARAMETERS *, INT
);

} // namespace ATG

// SetRemoteGain stores DbToRatio(...) here; the shipped .data holds 1.0f.
float GainEffect::sGain = 1.0f;

GainEffect::GainEffect() {
    GainEffectParams params;
    SetParameters(&params, sizeof(params));
}

// Scales the whole interleaved block by the current remote-talker gain, four
// __vector4s (16 floats) per iteration.
void GainEffect::DoProcess(
    const GainEffectParams &, float *__restrict buffer, unsigned int frameCount,
    unsigned int channelCount
) {
    __vector4 gain;
    gain.v[0] = sGain;
    // Splat sGain across the 4 lanes. This MUST be a loop, not straight-line
    // copies. Retail materialises TWO base registers (r1-0x10 = &u[0] and
    // r1-0xc = &u[1]) and strictly interleaves lwz/stw at +0/+4/+8 off each,
    // re-reading every word it just wrote. That is an unrolled 3-trip loop: the
    // two base regs are its induction pointers, and MSVC does not re-run
    // store-to-load forwarding after unrolling. Straight-line forms all let it
    // forward the stfs result and come out short. (Measured in rb3-xenon, which
    // ships the identical function: three 4-byte memcpys and two explicit
    // pointer vars both land at 84.7%, one 12-byte memcpy at 83.5%, this loop at
    // 100.0%.)
    for (int i = 0; i < 3; i++) {
        gain.u[i + 1] = gain.u[i];
    }
    float *end = buffer + frameCount * channelCount;
    for (float *p = buffer; p < end; p += 16) {
        __vector4 *v = (__vector4 *)p;
        v[0] = __vmulfp(v[0], gain);
        v[1] = __vmulfp(v[1], gain);
        v[2] = __vmulfp(v[2], gain);
        v[3] = __vmulfp(v[3], gain);
    }
}
