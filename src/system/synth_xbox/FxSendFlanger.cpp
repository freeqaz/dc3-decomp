#include "synth_xbox/FxSendFlanger.h"
#include "FxSend.h"
#include "xdk/xaudio2/xaudio2.h"

FxSendFlanger360::FxSendFlanger360() : FxSend360(this) {}

FxSendFlanger360::~FxSendFlanger360() {}

void FxSendFlanger360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendFlanger360::UpdateMix() { FxSend360::UpdateVolumes(); }

void FxSendFlanger360::OnParametersChanged() { FxSend360::SyncEffectParams(); }
