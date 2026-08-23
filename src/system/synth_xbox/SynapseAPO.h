#pragma once
#include "synth_xbox\FxSendSynapse.h"
#include "xdk\xaudio2\xapobase.h"

namespace DSP {

namespace Synapse {
class Synapse;
}

// Layout (sizeof == 0x1c8, cross-checked against the inlined `operator new(0x1c8)`
// in FxSendSynapse::CreateFx):
//   0x000  CXAPOParametersBase        (0x40)
//   0x040  CSampleXAPOBase::mParams[3] (3 * 0x5c == 0x114)
//   0x154  CSampleXAPOBase::mWav      (WAVEFORMATEX, 0x12 padded to 0x14)
//   0x168  mSynapse
//   0x16c  mCurrentParams             (0x5c)
class SynapseAPO : public ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams> {
public:
    SynapseAPO();
    virtual ~SynapseAPO();
    void SetSamplingRate(float rate);
    virtual void
    DoProcess(const SynapseAPOParams &, float *__restrict, unsigned int, unsigned int);

private:
    virtual void OnSetParameters(const SynapseAPOParams &params);

    Synapse::Synapse *mSynapse; // 0x168
    SynapseAPOParams mCurrentParams; // 0x16c
};

} // namespace DSP
