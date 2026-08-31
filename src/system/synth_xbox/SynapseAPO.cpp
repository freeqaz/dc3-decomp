#include "SynapseAPO.h"
#include "Synapse_dsp.h"
#include "xdk\win_types.h"
#include <string.h>

namespace DSP {

SynapseAPO::SynapseAPO() : ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams>(), mSynapse(nullptr) {
    SetSamplingRate(48000.0f);
}

SynapseAPO::~SynapseAPO() {
    if (mSynapse) {
        delete mSynapse;
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
        if (mCurrentParams.bands[i].enabled != params.bands[i].enabled) {
            mSynapse->SetVoiceEnabled(i, params.bands[i].enabled);
        }
        if (mCurrentParams.bands[i].gain != params.bands[i].gain) {
            mSynapse->SetVoiceGain(i, params.bands[i].gain);
        }
        if (mCurrentParams.bands[i].freq != params.bands[i].freq) {
            mSynapse->SetVoiceTargetNote(i, params.bands[i].freq);
        }
        if (mCurrentParams.bands[i].q != params.bands[i].q) {
            mSynapse->SetVoiceTransposition(i, params.bands[i].q);
        }
        if (mCurrentParams.bands[i].coeff0 != params.bands[i].coeff0) {
            mSynapse->SetVoiceAmount(i, params.bands[i].coeff0);
        }
        if (mCurrentParams.bands[i].coeff1 != params.bands[i].coeff1) {
            mSynapse->SetVoiceProximityEffect(i, params.bands[i].coeff1);
        }
        if (mCurrentParams.bands[i].coeff2 != params.bands[i].coeff2) {
            mSynapse->SetVoiceProximityFocus(i, params.bands[i].coeff2);
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
