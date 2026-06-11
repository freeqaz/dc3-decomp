#include "FxSendBitCrush.h"
#include "FxSend.h"

FxSendBitCrush360::FxSendBitCrush360() : FxSend360(this) {}

FxSendBitCrush360::~FxSendBitCrush360() {}

void FxSendBitCrush360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendBitCrush360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendBitCrush360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
