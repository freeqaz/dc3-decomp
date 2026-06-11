#include "FxSendCompress.h"
#include "FxSend.h"
#include "xdk/xapilibi/xbase.h"

FxSendCompress360::FxSendCompress360() : FxSend360(this) {}

FxSendCompress360::~FxSendCompress360() {}

void FxSendCompress360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendCompress360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendCompress360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
