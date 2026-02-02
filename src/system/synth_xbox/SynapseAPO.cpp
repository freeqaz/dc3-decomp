#include "SynapseAPO.h"
#include <new>

namespace DSP {

SynapseAPOParams::SynapseAPOParams() {}

SynapseAPO::SynapseAPO() : CSampleXAPOBase<SynapseAPO, SynapseAPOParams>() {
  unk168 = 0;
  new (&unk16C) SynapseAPOParams();
  SetSamplingRate(48000.0f);
}

SynapseAPO::~SynapseAPO() {}

void SynapseAPO::SetSamplingRate(float rate) {}

void SynapseAPO::OnSetParameters(const SynapseAPOParams& params) {}

void SynapseAPO::DoProcess(const SynapseAPOParams& params, int* arg1, float arg2, int arg3, int arg4) {}

}  // namespace DSP
