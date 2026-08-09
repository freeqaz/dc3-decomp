#pragma once
#include "Synth.h"
#include "stl\_vector.h"
#include "synth\FxSend.h"
#include "synth_xbox\Voice.h"
#include "xdk\xapilibi\xbase.h"
#include "xdk\xaudio2\xaudio2.h"

class FxSend360 {
public:
    virtual ~FxSend360();
    virtual void SyncEffectParams(IXAudio2SubmixVoice *) const = 0;
    virtual bool IsStandard() const { return true; }
    virtual void AddOwnerVoice(Voice *);
    virtual void RemoveOwnerVoice(Voice *);
    virtual IUnknown *CreateFx() = 0;
    // NOTE: CreateFx / SyncEffectParams(IXAudio2SubmixVoice*) are pure in the target
    // base vtable; derived effect classes provide the matching overrides.

    FxSend360(FxSend *);
    void SyncEffectParams();
    void UpdateVolumes();
    void Cleanup();
    void CleanChain();
    void Refresh(std::vector<FxSend *> &);

    bool HasVoices() const { return !mVoices.empty(); }
    IXAudio2Voice *GetOutputVoice() const { return mOutputVoice; }

protected:
    virtual void InitParams(IXAudio2SubmixVoice *, int) {}

    IXAudio2Voice *mOutputVoice; // 0x4
    std::vector<IXAudio2SubmixVoice *> mVoices; // 0x8
    std::vector<int> unk14; // 0x14
    std::vector<IUnknown *> mFx; // 0x20
    FxSend *mThis; // 0x2c
    bool unk30; // 0x30
    std::vector<Voice *> mOwnerVoices; // 0x34

private:
    IXAudio2Voice *OutputVoice();
    void UpdateVoiceMatrices();
    void CreateInputVoice();
    void Reconnect();
    void CreateVoice(int, int);
};
