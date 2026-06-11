#include "FxSendChorus.h"
#include "FxSend.h"

FxSendChorus360::FxSendChorus360() : FxSend360(this) {}

FxSendChorus360::~FxSendChorus360() {}

void FxSendChorus360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendChorus360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendChorus360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
