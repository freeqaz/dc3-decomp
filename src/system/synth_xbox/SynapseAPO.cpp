#include "SynapseAPO.h"
#include "Synapse_dsp.h"
#include <new>

namespace DSP {

SynapseAPOParams::SynapseAPOParams() {}

SynapseAPO::SynapseAPO() : CSampleXAPOBase<SynapseAPO, SynapseAPOParams>() {
  mSynapse = 0;
  new (&mParams) SynapseAPOParams();
  SetSamplingRate(48000.0f);
}

SynapseAPO::~SynapseAPO() {
    if (mSynapse != 0) {
        delete mSynapse;
    }
}

void SynapseAPO::SetSamplingRate(float rate) {
  Synapse::Synapse* prevSynapse = mSynapse;
  if (prevSynapse != 0) {
    delete prevSynapse;
  }
  mSynapse = new Synapse::Synapse(rate);
}

void SynapseAPO::OnSetParameters(const SynapseAPOParams& params) {}

void SynapseAPO::DoProcess(const SynapseAPOParams& params, int* arg1, float arg2, int arg3, int arg4) {}

}  // namespace DSP
