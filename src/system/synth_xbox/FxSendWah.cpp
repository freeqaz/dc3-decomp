#include "FxSendWah.h"
#include "FxSend.h"
#include "dsp/StandardEffect.h"
#include "synth/WahEffect.h"
#include "xdk/xaudio2/xaudio2.h"

FxSendWah360::FxSendWah360() : FxSend360(this) {}

void FxSendWah360::OnParametersChanged() { FxSend360::SyncEffectParams(); }

void FxSendWah360::Recreate(std::vector<FxSend *> &sends) { FxSend360::Refresh(sends); }

void FxSendWah360::UpdateMix() { FxSend360::UpdateVolumes(); }

// WahEffect::Params layout conflict: our WahEffect::Params (backs ?SetParameters@WahEffect
// at 100%) uses a filter-band layout incompatible with the FxSendWah member set the target
// SyncEffectParams marshals. Leaving SyncEffectParams unmatched to avoid regressing the
// 100% SetParameters body. (wave-9 blocker)
void FxSendWah360::SyncEffectParams(IXAudio2SubmixVoice *) const {}

IUnknown *FxSendWah360::CreateFx() {
    return static_cast<CXAPOBase *>(new StandardEffect<WahEffect>());
}
