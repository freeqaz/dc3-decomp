#include "FxSendMeterEffect.h"
#include "FxSend.h"
#include "macros.h"

FxSendMeterEffect360::FxSendMeterEffect360() : FxSend360(this), unkb0(0) {}

FxSendMeterEffect360::~FxSendMeterEffect360() { RELEASE(unkb0); }

void FxSendMeterEffect360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendMeterEffect360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendMeterEffect360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
