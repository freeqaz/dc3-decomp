#include "synth_xbox\FxSendSynapse.h"
#include "synth_xbox\FxSendSynapse360.h"

// SynapseAPO.h cannot be included here: it ships a hand-rolled global
// IXAPOParameters that ODR-collides with the real xapo.h one pulled in via the
// FxSend/XAUDIO2 chain. Forward-declare just enough of DSP::SynapseAPO (object
// size 0x1c8, default ctor) to allocate + construct one — matching the target's
// inline `operator new(0x1c8)` + ??0SynapseAPO@DSP@@QAA@XZ.
namespace DSP {
class SynapseAPO {
public:
    SynapseAPO();

private:
    char mOpaque[0x1c8];
};
}  // namespace DSP

IUnknown *FxSendSynapse360::CreateFx() { return (IUnknown *)new DSP::SynapseAPO(); }

// w14-e (96.4 -> 99.5 canonical, 32 -> 7 rows): the map lists
// ??0SynapseAPOParams@DSP@@QAA@XZ as `f i` (synth_xbox:FxSendSynapse.obj),
// i.e. an inline/SELECT_ANY definition, and marking the definition below
// `inline` (docs/decomp/patterns/map-comdat-flag-gates-clobber-propagation.md)
// is enough: `this`/`voice` now live in r31/r30 across the ctor call with the
// image's 0xd0 frame, without moving the ctor to another object.  Two more
// spellings closed the rest of the body: band 0 stores coeff1/gain before
// freq (the image loads mProximityEffect ahead of mNote1Hz), and band 1's
// unison test is a full if/else (`beq` to the zeroing `fmr` over a `b`) where
// band 2's is a one-armed if (`bne` over it).  Left: 7 rows, the stfs order
// of 0x58/0x54/0x60/0x68/0xa4/0x7c/0xa8 -- moving the low/high cutoff stores
// above band 1, or to the end of the function, was inert or worse.
// The history below predates the `inline` lever.
// NOT YET 100% (96.4%, 32 rows of 89), and the residual is a *build-model*
// finding rather than a source one.  Every remaining row follows from register
// allocation: the target spends two callee-saved registers (r30 = voice,
// r31 = this) and a 0xd0 frame, while we keep `this` in the VOLATILE r8 across
// the `bl ??0SynapseAPOParams` and need only r31 and a 0xc0 frame.  That is
// only legal if the compiler knows the callee's clobber set -- MSVC's whole-TU
// register-usage propagation -- and it does, because the ctor is defined in
// this file.  Measured, one variable at a time:
//
//   ctor defined in this TU, at the bottom (as shipped here)   96.4%
//   ctor defined in this TU, between CreateFx and here          96.4%  (inert:
//       the analysis is whole-TU, not emission-order dependent)
//   ctor moved to SynapseAPO.cpp (a different TU)               98.8%
//
// So the original compiled SyncEffectParams without the ctor body in scope,
// i.e. in a separate object -- exactly how rb3-xenon splits the same engine
// code (synth_xbox/FxSendSynapse.cpp holds only the Params ctor;
// synth_xbox/FxSendSynapse360.cpp holds these methods).  Acting on that here
// needs config/373307D9/splits.txt to model two objects whose .text
// interleaves (CreateFx, ctor, SyncEffectParams by address), including a
// correct .rdata/.pdata division, so it is left for a lane that can validate
// the split.  Moving the ctor alone is a net loss: it costs the 80 bytes
// ??0SynapseAPOParams currently matches in this unit and SyncEffectParams
// still does not reach 100%.
void FxSendSynapse360::SyncEffectParams(IXAudio2SubmixVoice *voice) const {
    DSP::SynapseAPOParams params;

    // Band 0: the primary target note, always at full gain.
    params.bands[0].enabled = 1;
    params.bands[0].coeff1 = mProximityEffect;
    params.bands[0].gain = 1.0f;
    params.bands[0].freq = mNote1Hz;
    params.bands[0].coeff0 = mAmount;
    params.bands[0].coeff2 = mProximityFocus;

    // Band 1: target note 2 (or a detuned copy of note 1 when note 2 is unset).
    params.bands[1].enabled = 1;
    params.bands[1].coeff0 = mAmount;
    params.bands[1].coeff2 = mProximityFocus;
    params.lowCutoffFreq = mAttackSmoothing;
    params.highCutoffFreq = mReleaseSmoothing;
    float band1Gain = 1.0f;
    if (mNote2Hz == 0.0f) {
        if (mUnisonTrio)
            band1Gain = 1.0f;
        else
            band1Gain = 0.0f;
        params.bands[1].coeff1 = mProximityEffect;
        params.bands[1].freq = mNote1Hz * 0.9904912114143372f;
    } else {
        params.bands[1].coeff1 = 0.0f;
        params.bands[1].freq = mNote2Hz * 0.9960159659385681f;
    }
    params.bands[1].gain = band1Gain;

    // Band 2: target note 3 (or a detuned copy of note 1 when note 3 is unset).
    params.bands[2].enabled = 1;
    params.bands[2].coeff0 = mAmount;
    params.bands[2].coeff2 = mProximityFocus;
    float band2Gain = 1.0f;
    if (mNote3Hz == 0.0f) {
        if (!mUnisonTrio)
            band2Gain = 0.0f;
        params.bands[2].coeff1 = mProximityEffect;
        params.bands[2].freq = mNote1Hz * 1.009600043296814f;
    } else {
        params.bands[2].coeff1 = 0.0f;
        params.bands[2].freq = mNote3Hz * 1.003999948501587f;
    }
    params.bands[2].gain = band2Gain;

    voice->SetEffectParameters(0, &params, sizeof(DSP::SynapseAPOParams), 0);
}

namespace DSP {

inline SynapseAPOParams::SynapseAPOParams() throw() {
    for (int i = 0; i < 3; i++) {
        bands[i].freq = 220.0f;
        bands[i].gain = 0.0f;
        bands[i].enabled = 0;
        bands[i].q = 0.0f;
    }
    lowCutoffFreq = 20.0f;
    highCutoffFreq = 40.0f;
}

}  // namespace DSP
