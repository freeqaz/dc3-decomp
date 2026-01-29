#include "SynapseAPO.h"
#include <new>

namespace DSP {

SynapseAPOParams::SynapseAPOParams() {}

template <>
CSampleXAPOBase<SynapseAPO, SynapseAPOParams>::CSampleXAPOBase() {}

SynapseAPO::SynapseAPO() : CSampleXAPOBase<SynapseAPO, SynapseAPOParams>() {
  SetSamplingRate(48000.0f);
  unk168 = 0;
  new (&unk16C) SynapseAPOParams();
}

SynapseAPO::~SynapseAPO() {}

void SynapseAPO::SetSamplingRate(float rate) {}

void SynapseAPO::OnSetParameters(const SynapseAPOParams& params) {}

void SynapseAPO::DoProcess(const SynapseAPOParams& params, int* arg1, float arg2, int arg3, int arg4) {}

}  // namespace DSP
