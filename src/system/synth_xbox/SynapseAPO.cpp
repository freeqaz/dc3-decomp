#include "SynapseAPO.h"
#include "Synapse_dsp.h"
#include <new>

namespace ATG {

template <typename Derived, typename Params>
CSampleXAPOBase<Derived, Params>::CSampleXAPOBase() : CXAPOBase() {}

template class CSampleXAPOBase<DSP::SynapseAPO, DSP::SynapseAPOParams>;

} // namespace ATG

namespace DSP {

SynapseAPO::SynapseAPO() : ATG::CSampleXAPOBase<SynapseAPO, SynapseAPOParams>() {
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

void SynapseAPO::DoProcess(const SynapseAPOParams& params, unsigned int* arg1, float& arg2, unsigned int arg3, unsigned int arg4) {}

}  // namespace DSP
