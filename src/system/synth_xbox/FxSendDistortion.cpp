#include "FxSendDistortion.h"
#include "FxSend.h"

FxSendDistortion360::FxSendDistortion360() : FxSend360(this) {}

FxSendDistortion360::~FxSendDistortion360() {}

void FxSendDistortion360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendDistortion360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendDistortion360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
