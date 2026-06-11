#include "FxSend.h"
#include "Synth.h"
#include "os/Debug.h"
#include "synth/FxSend.h"
#include "utl/Std.h"

FxSend360::FxSend360(FxSend *fx) : mOutputVoice(0), mThis(fx), unk30(true) {
    TheXboxSynth->AddFxSend(this);
    MILO_ASSERT(mThis, 0x19);
}

FxSend360::~FxSend360() {
    if (TheXboxSynth)
        TheXboxSynth->RemoveFxSend(this);
    CleanChain();
}

void FxSend360::AddOwnerVoice(Voice *v) { mOwnerVoices.push_back(v); }

void FxSend360::RemoveOwnerVoice(Voice *v) {
    std::vector<Voice *>::iterator itFind = mOwnerVoices.end();
    FOREACH (it, mOwnerVoices) {
        if (*it == v) {
            itFind = it;
        }
    }
    MILO_ASSERT(itFind != mOwnerVoices.end(), 0x265);
    mOwnerVoices.erase(itFind);
}

void FxSend360::Cleanup() {
    std::vector<Voice *> voices(mOwnerVoices);
    for (int i = 0; i < voices.size(); i++) {
        voices[i]->SetSend(nullptr);
    }
    if (mOutputVoice) {
        mOutputVoice->DestroyVoice();
        mOutputVoice = nullptr;
    }
    MILO_ASSERT(unk8.size() == unk20.size(), 0x2A);
    for (int i = 0; i != unk8.size(); i++) {
        unk8[i]->DestroyVoice();
        if (unk20[i]) {
            unk20[i]->Release();
            unk20[i] = nullptr;
        }
    }
    unk8.clear();
    unk20.clear();
}
