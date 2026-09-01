#include "SynapseAPO.h"
#include "Synapse_dsp.h"
#include "xdk\win_types.h"
#include <string.h>

namespace DSP {

// The two-row residual in this constructor is a scheduling floor: the target
// interleaves the implicit `this` computation for the mCurrentParams
// constructor between the two implicit vtable-pointer stores, and we emit both
// stores first. Neither row has a source-level expression -- both the second
// vtable store (IXAPOParameters at +0x20) and the `addi r3, this, 0x16c` are
// compiler-generated. Controls: adding an explicit `mCurrentParams()` to the
// initialiser list is byte-inert, and rb3-xenon's independently-written
// SynapseAPO.cpp -- a different class spelling with padding members instead of
// real ones -- lands on the identical 93.3333% with the same two rows.
SynapseAPO::SynapseAPO() : ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams>(), mSynapse(nullptr) {
    SetSamplingRate(48000.0f);
}

SynapseAPO::~SynapseAPO() {
    // Read the member into a local before deleting, exactly as SetSamplingRate
    // does. Deleting the member expression directly makes MSVC emit an
    // out-of-line ??3@YAXPAX@Z call plus a spill; through a local it inlines
    // operator delete to the direct RadFree the target has.
    Synapse::Synapse *synapse = mSynapse;
    if (synapse) {
        delete synapse;
    }
}

void SynapseAPO::SetSamplingRate(float rate) {
    Synapse::Synapse* prevSynapse = mSynapse;
    if (prevSynapse) {
        delete prevSynapse;
    }
    mSynapse = new Synapse::Synapse(rate);
}

void SynapseAPO::OnSetParameters(const SynapseAPOParams& params) {
    for (unsigned int i = 0; i < 3; i++) {
        SynapseBand *cur = &mCurrentParams.bands[i];
        const SynapseBand *neu = &params.bands[i];
        if (cur->enabled != neu->enabled) {
            mSynapse->SetVoiceEnabled(i, neu->enabled);
        }
        if (cur->gain != neu->gain) {
            mSynapse->SetVoiceGain(i, neu->gain);
        }
        if (cur->freq != neu->freq) {
            mSynapse->SetVoiceTargetNote(i, neu->freq);
        }
        if (cur->q != neu->q) {
            mSynapse->SetVoiceTransposition(i, neu->q);
        }
        if (cur->coeff0 != neu->coeff0) {
            mSynapse->SetVoiceAmount(i, neu->coeff0);
        }
        if (cur->coeff1 != neu->coeff1) {
            mSynapse->SetVoiceProximityEffect(i, neu->coeff1);
        }
        if (cur->coeff2 != neu->coeff2) {
            mSynapse->SetVoiceProximityFocus(i, neu->coeff2);
        }
    }
    if (mCurrentParams.lowCutoffFreq != params.lowCutoffFreq) {
        mSynapse->SetAttackSmoothing(params.lowCutoffFreq);
    }
    if (mCurrentParams.highCutoffFreq != params.highCutoffFreq) {
        mSynapse->SetReleaseSmoothing(params.highCutoffFreq);
    }
    memcpy(&mCurrentParams, &params, sizeof(SynapseAPOParams));
}

void SynapseAPO::DoProcess(
    const SynapseAPOParams &, float *__restrict buffer, unsigned int frameCount, unsigned int
) {
    if (mSynapse) {
        mSynapse->ProcessInPlace(frameCount, buffer);
    }
}

}  // namespace DSP

namespace ATG {

// m_regProps itself now lives on the primary template in xdk/xaudio2/xapobase.h,
// keyed off __uuidof(SynapseAPO); the uuid attribute is on the class in
// SynapseAPO.h. That spelling is what makes cl.exe emit the shipped
// ??__E?m_regProps dynamic initializer instead of folding the whole block.
template class CSampleXAPOBase<DSP::SynapseAPO, DSP::SynapseAPOParams>;

} // namespace ATG
