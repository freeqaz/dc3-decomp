#include "FxSendEQ.h"
#include "FxSend.h"
#include "xdk/xaudio2/xaudio2.h"

FxSendEQ360::FxSendEQ360() : FxSend360(this) {}

FxSendEQ360::~FxSendEQ360() {}

void FxSendEQ360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

void FxSendEQ360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendEQ360::UpdateMix() { FxSend360::UpdateVolumes(); }

// EQ Params layout conflict: our EQEffect::Params is a 15-field band layout (backs
// ?SetParameters@EQEffect at 100%) which is incompatible with the StandardEffect<T>
// template's bypass(unk0) assumption. Leaving CreateFx/SyncEffectParams unmatched here
// rather than risk regressing the 100% SetParameters body. (wave-9 blocker)
void FxSendEQ360::SyncEffectParams(IXAudio2SubmixVoice *) const {}

IUnknown *FxSendEQ360::CreateFx() { return nullptr; }
