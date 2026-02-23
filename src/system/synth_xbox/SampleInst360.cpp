#include "synth_xbox/SampleInst360.h"

SampleInst360::SampleInst360(SynthSample360 *sample, bool b, int i1, int i2)
    : SampleInst(sample), unk_a8(0), unk_ac(0) {
    // TODO: implement Xbox 360 specific initialization
}

SampleInst360::~SampleInst360() {}

bool SampleInst360::IsPlaying() const { return false; }

void SampleInst360::SetFXCore(FXCore core) {}

void SampleInst360::StartImpl() {}

void SampleInst360::StopImpl(bool b) {}

void SampleInst360::SetVolumeImpl(float vol) {}

void SampleInst360::SetPanImpl(float pan) {}

void SampleInst360::SetSpeedImpl(float speed) {}

void SampleInst360::Pause(bool b) {}

void SampleInst360::SetADSR(const ADSRImpl &adsr) {}
