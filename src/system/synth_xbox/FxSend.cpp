#include "FxSend.h"
#include "Synth.h"
#include "math\Decibels.h"
#include "math\Utl.h"
#include "os\Debug.h"
#include "os\Timer.h"
#include "synth\FxSend.h"
#include "synth\Synth.h"
#include "utl\Std.h"

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

void FxSend360::SyncEffectParams() {
    START_AUTO_TIMER("voice_cs");
    if (!mThis->UpdatesEnabled()) {
        unk30 = true;
        return;
    } else {
        if (unk30 || mThis->UpdatesEnabled()) {
            for (int i = 0; i != mVoices.size(); i++) {
                SyncEffectParams(mVoices[i]);
            }
        }
        unk30 = false;
    }
}

void FxSend360::Refresh(std::vector<FxSend *> &sends) {
    if (TheXboxSynth) {
        for (int i = sends.size() - 1; i >= 0; i--) {
            FxSend360 *send360 = dynamic_cast<FxSend360 *>(sends[i]);
            send360->Cleanup();
        }
        for (int i = 0; i < sends.size(); i++) {
            FxSend360 *send360 = dynamic_cast<FxSend360 *>(sends[i]);
            send360->Reconnect();
        }
    }
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
    MILO_ASSERT(mVoices.size() == mFx.size(), 0x2A);
    for (int i = 0; i != mVoices.size(); i++) {
        mVoices[i]->DestroyVoice();
        if (mFx[i]) {
            mFx[i]->Release();
            mFx[i] = nullptr;
        }
    }
    mVoices.clear();
    mFx.clear();
}

void FxSend360::CleanChain() {
    std::vector<FxSend *> sends;
    mThis->BuildChainVector(sends);
    for (int i = sends.size() - 1; i >= 0; i--) {
        FxSend360 *send360 = dynamic_cast<FxSend360 *>(sends[i]);
        send360->Cleanup();
    }
}

IXAudio2Voice *FxSend360::OutputVoice() {
    if (mThis->NextSend()) {
        FxSend360 *send = dynamic_cast<FxSend360 *>(mThis->NextSend());
        MILO_ASSERT(send, 0x225);
        return send->mOutputVoice;
    } else {
        Synth360 *synth = dynamic_cast<Synth360 *>(TheSynth);
        return synth->OutputVoice();
    }
}

void FxSend360::CreateInputVoice() {
    MILO_ASSERT(OutputVoice(), 0x177);

    XAUDIO2_SEND_DESCRIPTOR sendsAll[4];
    sendsAll[0].Flags = 0;
    sendsAll[0].pOutputVoice = mVoices[0];
    sendsAll[1].Flags = 0;
    sendsAll[1].pOutputVoice = (mVoices.size() >= 2) ? mVoices[1] : nullptr;
    sendsAll[2].Flags = 0;
    sendsAll[2].pOutputVoice = (mVoices.size() >= 3) ? mVoices[2] : nullptr;
    sendsAll[3].Flags = 0;
    sendsAll[3].pOutputVoice = OutputVoice();

    XAUDIO2_SEND_DESCRIPTOR sendsCenter[2];
    sendsCenter[0].Flags = 0;
    sendsCenter[0].pOutputVoice = mVoices[0];
    sendsCenter[1].Flags = 0;
    sendsCenter[1].pOutputVoice = OutputVoice();

    XAUDIO2_SEND_DESCRIPTOR sendsStereo[2];
    sendsStereo[0].Flags = 0;
    sendsStereo[0].pOutputVoice = mVoices[0];
    sendsStereo[1].Flags = 0;
    sendsStereo[1].pOutputVoice = OutputVoice();

    int stage = mThis->mStage * 2;
    XAUDIO2_VOICE_SENDS voiceSends;
    switch (mThis->GetChannels()) {
    case kSendAll:
    case kSendAllXMix:
        voiceSends.SendCount = 4;
        voiceSends.pSends = sendsAll;
        break;
    case kSendCenter:
        voiceSends.SendCount = 2;
        voiceSends.pSends = sendsCenter;
        break;
    case kSendStereo:
        voiceSends.SendCount = 2;
        voiceSends.pSends = sendsStereo;
        break;
    default:
        MILO_FAIL("FxSend: Unknown Channels");
        break;
    }

    int *pEngine = (int *)TheXboxSynth->unkec;
    HRESULT hr = ((HRESULT(*)(
        int *,
        IXAudio2Voice **,
        int,
        int,
        int,
        int,
        XAUDIO2_VOICE_SENDS *,
        int
    ))(*(int *)(*(int *)pEngine + 0x24)))(
        pEngine, &mOutputVoice, 6, 48000, 0, stage, &voiceSends, 0
    );
    MILO_ASSERT(SUCCEEDED(hr), 0x1a6);
}

void FxSend360::CreateVoice(int channel1, int channel2) {
    XAUDIO2_SEND_DESCRIPTOR desc;
    desc.Flags = 0;
    int numChannels = (channel2 == -1) ? 1 : 2;
    int stage = mThis->mStage * 2 + 1;
    desc.pOutputVoice = OutputVoice();
    std::vector<XAUDIO2_SEND_DESCRIPTOR> sends;
    if (desc.pOutputVoice) {
        sends.push_back(desc);
    }
    if (mThis->mReverbEnable) {
        desc.Flags = 0;
        desc.pOutputVoice = TheXboxSynth->UnkF8();
        sends.push_back(desc);
    }
    XAUDIO2_VOICE_SENDS voiceSends;
    XAUDIO2_VOICE_SENDS *pSends = nullptr;
    voiceSends.pSends = &sends[0];
    voiceSends.SendCount = sends.size();
    if (voiceSends.SendCount != 0) {
        pSends = &voiceSends;
    }

    mFx.push_back(CreateFx());
    MILO_ASSERT(mFx.back(), 0x1ce);

    XAUDIO2_EFFECT_DESCRIPTOR effectDesc;
    effectDesc.pEffect = mFx.back();
    effectDesc.InitialState = 1;
    effectDesc.OutputChannels = numChannels;
    XAUDIO2_EFFECT_CHAIN effectChain;
    effectChain.EffectCount = 1;
    effectChain.pEffectDescriptors = &effectDesc;

    mVoices.resize(mVoices.size() + 1);
    unk14.resize(mVoices.size());

    int *pEngine = (int *)TheXboxSynth->unkec;
    HRESULT hr = ((HRESULT(*)(
        int *,
        IXAudio2SubmixVoice **,
        int,
        int,
        int,
        int,
        XAUDIO2_VOICE_SENDS *,
        XAUDIO2_EFFECT_CHAIN *
    ))(*(int *)(*(int *)pEngine + 0x24)))(
        pEngine,
        &mVoices.back(),
        numChannels,
        48000,
        0,
        stage,
        pSends,
        &effectChain
    );
    MILO_ASSERT(SUCCEEDED(hr), 0x1d8);
    unk14.back() = numChannels;
    InitParams(mVoices.back(), numChannels);
}

void FxSend360::UpdateVolumes() {
    if (!mOutputVoice)
        return;
    float wet = DbToRatio(mThis->mWetGain);
    float dry = DbToRatio(mThis->mDryGain);
    if (mThis->mBypass) {
        wet = 0.0f;
        dry = 1.0f;
    }
    float clampedDry = Max(dry, 1.0e-10f);
    wet = Max(wet, 1.0e-10f);
    float matrix[6][6];
    matrix[0][0] = clampedDry;
    matrix[0][1] = 0.0f;
    matrix[0][2] = 0.0f;
    matrix[0][3] = 0.0f;
    matrix[0][4] = 0.0f;
    matrix[0][5] = 0.0f;
    matrix[1][0] = 0.0f;
    matrix[1][1] = clampedDry;
    matrix[1][2] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[1][4] = 0.0f;
    matrix[1][5] = 0.0f;
    matrix[2][0] = 0.0f;
    matrix[2][1] = 0.0f;
    matrix[2][2] = clampedDry;
    matrix[2][3] = 0.0f;
    matrix[2][4] = 0.0f;
    matrix[2][5] = 0.0f;
    matrix[3][0] = 0.0f;
    matrix[3][1] = 0.0f;
    matrix[3][2] = 0.0f;
    matrix[3][3] = clampedDry;
    matrix[3][4] = 0.0f;
    matrix[3][5] = 0.0f;
    matrix[4][0] = 0.0f;
    matrix[4][1] = 0.0f;
    matrix[4][2] = 0.0f;
    matrix[4][3] = 0.0f;
    matrix[4][4] = clampedDry;
    matrix[4][5] = 0.0f;
    matrix[5][0] = 0.0f;
    matrix[5][1] = 0.0f;
    matrix[5][2] = 0.0f;
    matrix[5][3] = 0.0f;
    matrix[5][4] = 0.0f;
    matrix[5][5] = clampedDry;
    HRESULT hr = mOutputVoice->SetOutputMatrix(OutputVoice(), 6, 6, &matrix[0][0], 0);
    MILO_ASSERT(SUCCEEDED(hr), 0x203);
    UpdateVoiceMatrices();
    for (int i = 0; i < mVoices.size(); i++) {
        hr = mVoices[i]->SetVolume(wet, 0);
        MILO_ASSERT(SUCCEEDED(hr), 0x212);
    }
    if (IsStandard()) {
        for (int i = 0; i != mVoices.size(); i++) {
            SyncEffectParams(mVoices[i]);
        }
    }
}

void FxSend360::UpdateVoiceMatrices() {
    float inputGain = DbToRatio(mThis->mInputGain);
    if (mThis->mBypass) {
        inputGain = 0.0f;
    }
    float reverbGain = DbToRatio(mThis->mReverbMixDb);
    inputGain = Max(inputGain, 1.0e-10f);
    reverbGain = Max(reverbGain, 1.0e-10f);

    switch (mThis->GetChannels()) {
    case kSendAll: {
        // The three submix voices take L/R, centre and the surround pair
        // straight off the 5.1 input.
        float toStereo[2][6];
        toStereo[0][0] = inputGain;
        toStereo[0][1] = 0.0f;
        toStereo[0][2] = 0.0f;
        toStereo[0][3] = 0.0f;
        toStereo[0][4] = 0.0f;
        toStereo[0][5] = 0.0f;
        toStereo[1][0] = 0.0f;
        toStereo[1][1] = inputGain;
        toStereo[1][2] = 0.0f;
        toStereo[1][3] = 0.0f;
        toStereo[1][4] = 0.0f;
        toStereo[1][5] = 0.0f;
        mOutputVoice->SetOutputMatrix(mVoices[0], 6, 2, &toStereo[0][0], 0);

        float toCenter[1][6];
        toCenter[0][0] = 0.0f;
        toCenter[0][1] = 0.0f;
        toCenter[0][2] = inputGain;
        toCenter[0][3] = 0.0f;
        toCenter[0][4] = 0.0f;
        toCenter[0][5] = 0.0f;
        mOutputVoice->SetOutputMatrix(mVoices[1], 6, 1, &toCenter[0][0], 0);

        float toSurround[2][6];
        toSurround[0][0] = 0.0f;
        toSurround[0][1] = 0.0f;
        toSurround[0][2] = 0.0f;
        toSurround[0][3] = 0.0f;
        toSurround[0][4] = inputGain;
        toSurround[0][5] = 0.0f;
        toSurround[1][0] = 0.0f;
        toSurround[1][1] = 0.0f;
        toSurround[1][2] = 0.0f;
        toSurround[1][3] = 0.0f;
        toSurround[1][4] = 0.0f;
        toSurround[1][5] = inputGain;
        mOutputVoice->SetOutputMatrix(mVoices[2], 6, 2, &toSurround[0][0], 0);

        // Route each submix voice on to this send's output.
        float outStereo[6][2];
        outStereo[0][0] = 1.0f;
        outStereo[0][1] = 0.0f;
        outStereo[1][0] = 0.0f;
        outStereo[1][1] = 1.0f;
        outStereo[2][0] = 0.0f;
        outStereo[2][1] = 0.0f;
        outStereo[3][0] = 0.0f;
        outStereo[3][1] = 0.0f;
        outStereo[4][0] = 0.0f;
        outStereo[4][1] = 0.0f;
        outStereo[5][0] = 0.0f;
        outStereo[5][1] = 0.0f;
        if (mVoices[0]) {
            mVoices[0]->SetOutputMatrix(OutputVoice(), 2, 6, &outStereo[0][0], 0);
        }

        float outCenter[6][1];
        outCenter[0][0] = 0.0f;
        outCenter[1][0] = 0.0f;
        outCenter[2][0] = 1.0f;
        outCenter[3][0] = 0.0f;
        outCenter[4][0] = 0.0f;
        outCenter[5][0] = 0.0f;
        if (mVoices[1]) {
            mVoices[1]->SetOutputMatrix(OutputVoice(), 1, 6, &outCenter[0][0], 0);
        }

        float outSurround[6][2];
        outSurround[0][0] = 0.0f;
        outSurround[0][1] = 0.0f;
        outSurround[1][0] = 0.0f;
        outSurround[1][1] = 0.0f;
        outSurround[2][0] = 0.0f;
        outSurround[2][1] = 0.0f;
        outSurround[3][0] = 0.0f;
        outSurround[3][1] = 0.0f;
        outSurround[4][0] = 1.0f;
        outSurround[4][1] = 0.0f;
        outSurround[5][0] = 0.0f;
        outSurround[5][1] = 1.0f;
        if (mVoices[2]) {
            mVoices[2]->SetOutputMatrix(OutputVoice(), 2, 6, &outSurround[0][0], 0);
        }

        if (mThis->mReverbEnable) {
            float revStereo[6][2];
            revStereo[0][0] = reverbGain;
            revStereo[0][1] = 0.0f;
            revStereo[1][0] = 0.0f;
            revStereo[1][1] = reverbGain;
            revStereo[2][0] = 0.0f;
            revStereo[2][1] = 0.0f;
            revStereo[3][0] = 0.0f;
            revStereo[3][1] = 0.0f;
            revStereo[4][0] = 0.0f;
            revStereo[4][1] = 0.0f;
            revStereo[5][0] = 0.0f;
            revStereo[5][1] = 0.0f;
            mVoices[0]->SetOutputMatrix(TheXboxSynth->UnkF8(), 2, 6, &revStereo[0][0], 0);

            float revCenter[6][1];
            revCenter[0][0] = 0.0f;
            revCenter[1][0] = 0.0f;
            revCenter[2][0] = reverbGain;
            revCenter[3][0] = 0.0f;
            revCenter[4][0] = 0.0f;
            revCenter[5][0] = 0.0f;
            mVoices[1]->SetOutputMatrix(TheXboxSynth->UnkF8(), 1, 6, &revCenter[0][0], 0);

            float revSurround[6][2];
            revSurround[0][0] = 0.0f;
            revSurround[0][1] = 0.0f;
            revSurround[1][0] = 0.0f;
            revSurround[1][1] = 0.0f;
            revSurround[2][0] = 0.0f;
            revSurround[2][1] = 0.0f;
            revSurround[3][0] = 0.0f;
            revSurround[3][1] = 0.0f;
            revSurround[4][0] = reverbGain;
            revSurround[4][1] = 0.0f;
            revSurround[5][0] = 0.0f;
            revSurround[5][1] = reverbGain;
            mVoices[2]->SetOutputMatrix(TheXboxSynth->UnkF8(), 2, 6, &revSurround[0][0], 0);
        }
        break;
    }
    case kSendCenter: {
        float toCenter[1][6];
        toCenter[0][0] = inputGain * 0.5f;
        toCenter[0][1] = inputGain * 0.5f;
        toCenter[0][2] = inputGain;
        toCenter[0][3] = 0.0f;
        toCenter[0][4] = inputGain * 0.25f;
        toCenter[0][5] = inputGain * 0.25f;
        mOutputVoice->SetOutputMatrix(mVoices[0], 6, 1, &toCenter[0][0], 0);

        if (mThis->mReverbEnable) {
            float toReverb[6][1];
            toReverb[0][0] = 0.0f;
            toReverb[1][0] = 0.0f;
            toReverb[2][0] = reverbGain;
            toReverb[3][0] = 0.0f;
            toReverb[4][0] = 0.0f;
            toReverb[5][0] = 0.0f;
            mVoices[0]->SetOutputMatrix(TheXboxSynth->UnkF8(), 1, 6, &toReverb[0][0], 0);
        }
        break;
    }
    case kSendStereo: {
        float toStereo[2][6];
        toStereo[0][0] = inputGain;
        toStereo[0][1] = 0.0f;
        toStereo[0][2] = inputGain * 0.7f;
        toStereo[0][3] = 0.0f;
        toStereo[0][4] = inputGain * 0.3f;
        toStereo[0][5] = 0.0f;
        toStereo[1][0] = 0.0f;
        toStereo[1][1] = inputGain;
        toStereo[1][2] = inputGain * 0.7f;
        toStereo[1][3] = 0.0f;
        toStereo[1][4] = 0.0f;
        toStereo[1][5] = inputGain * 0.3f;
        mOutputVoice->SetOutputMatrix(mVoices[0], 6, 2, &toStereo[0][0], 0);

        if (mThis->mReverbEnable) {
            float toReverb[6][2];
            toReverb[0][0] = reverbGain;
            toReverb[0][1] = 0.0f;
            toReverb[1][0] = 0.0f;
            toReverb[1][1] = reverbGain;
            toReverb[2][0] = 0.0f;
            toReverb[2][1] = 0.0f;
            toReverb[3][0] = 0.0f;
            toReverb[3][1] = 0.0f;
            toReverb[4][0] = 0.0f;
            toReverb[4][1] = 0.0f;
            toReverb[5][0] = 0.0f;
            toReverb[5][1] = 0.0f;
            mVoices[0]->SetOutputMatrix(TheXboxSynth->UnkF8(), 2, 6, &toReverb[0][0], 0);
        }
        break;
    }
    case kSendAllXMix: {
        // Same split as kSendAll, but the centre channel is cross-mixed into
        // the stereo and surround pairs as well.
        float xmixGain = inputGain * 0.25f;

        float toStereo[2][6];
        toStereo[0][0] = inputGain;
        toStereo[0][1] = 0.0f;
        toStereo[0][2] = xmixGain;
        toStereo[0][3] = 0.0f;
        toStereo[0][4] = 0.0f;
        toStereo[0][5] = 0.0f;
        toStereo[1][0] = 0.0f;
        toStereo[1][1] = inputGain;
        toStereo[1][2] = xmixGain;
        toStereo[1][3] = 0.0f;
        toStereo[1][4] = 0.0f;
        toStereo[1][5] = 0.0f;
        mOutputVoice->SetOutputMatrix(mVoices[0], 6, 2, &toStereo[0][0], 0);

        float toCenter[1][6];
        toCenter[0][0] = 0.0f;
        toCenter[0][1] = 0.0f;
        toCenter[0][2] = inputGain * 0.1f;
        toCenter[0][3] = 0.0f;
        toCenter[0][4] = 0.0f;
        toCenter[0][5] = 0.0f;
        mOutputVoice->SetOutputMatrix(mVoices[1], 6, 1, &toCenter[0][0], 0);

        float toSurround[2][6];
        toSurround[0][0] = 0.0f;
        toSurround[0][1] = 0.0f;
        toSurround[0][2] = xmixGain;
        toSurround[0][3] = 0.0f;
        toSurround[0][4] = inputGain;
        toSurround[0][5] = 0.0f;
        toSurround[1][0] = 0.0f;
        toSurround[1][1] = 0.0f;
        toSurround[1][2] = xmixGain;
        toSurround[1][3] = 0.0f;
        toSurround[1][4] = 0.0f;
        toSurround[1][5] = inputGain;
        mOutputVoice->SetOutputMatrix(mVoices[2], 6, 2, &toSurround[0][0], 0);

        // Route each submix voice on to this send's output.
        float outStereo[6][2];
        outStereo[0][0] = 1.0f;
        outStereo[0][1] = 0.0f;
        outStereo[1][0] = 0.0f;
        outStereo[1][1] = 1.0f;
        outStereo[2][0] = 0.0f;
        outStereo[2][1] = 0.0f;
        outStereo[3][0] = 0.0f;
        outStereo[3][1] = 0.0f;
        outStereo[4][0] = 0.0f;
        outStereo[4][1] = 0.0f;
        outStereo[5][0] = 0.0f;
        outStereo[5][1] = 0.0f;
        if (mVoices[0]) {
            mVoices[0]->SetOutputMatrix(OutputVoice(), 2, 6, &outStereo[0][0], 0);
        }

        float outCenter[6][1];
        outCenter[0][0] = 0.0f;
        outCenter[1][0] = 0.0f;
        outCenter[2][0] = 1.0f;
        outCenter[3][0] = 0.0f;
        outCenter[4][0] = 0.0f;
        outCenter[5][0] = 0.0f;
        if (mVoices[1]) {
            mVoices[1]->SetOutputMatrix(OutputVoice(), 1, 6, &outCenter[0][0], 0);
        }

        float outSurround[6][2];
        outSurround[0][0] = 0.0f;
        outSurround[0][1] = 0.0f;
        outSurround[1][0] = 0.0f;
        outSurround[1][1] = 0.0f;
        outSurround[2][0] = 0.0f;
        outSurround[2][1] = 0.0f;
        outSurround[3][0] = 0.0f;
        outSurround[3][1] = 0.0f;
        outSurround[4][0] = 1.0f;
        outSurround[4][1] = 0.0f;
        outSurround[5][0] = 0.0f;
        outSurround[5][1] = 1.0f;
        if (mVoices[2]) {
            mVoices[2]->SetOutputMatrix(OutputVoice(), 2, 6, &outSurround[0][0], 0);
        }

        if (mThis->mReverbEnable) {
            float revStereo[6][2];
            revStereo[0][0] = reverbGain;
            revStereo[0][1] = 0.0f;
            revStereo[1][0] = 0.0f;
            revStereo[1][1] = reverbGain;
            revStereo[2][0] = 0.0f;
            revStereo[2][1] = 0.0f;
            revStereo[3][0] = 0.0f;
            revStereo[3][1] = 0.0f;
            revStereo[4][0] = 0.0f;
            revStereo[4][1] = 0.0f;
            revStereo[5][0] = 0.0f;
            revStereo[5][1] = 0.0f;
            mVoices[0]->SetOutputMatrix(TheXboxSynth->UnkF8(), 2, 6, &revStereo[0][0], 0);

            float revCenter[6][1];
            revCenter[0][0] = 0.0f;
            revCenter[1][0] = 0.0f;
            revCenter[2][0] = reverbGain;
            revCenter[3][0] = 0.0f;
            revCenter[4][0] = 0.0f;
            revCenter[5][0] = 0.0f;
            mVoices[1]->SetOutputMatrix(TheXboxSynth->UnkF8(), 1, 6, &revCenter[0][0], 0);

            float revSurround[6][2];
            revSurround[0][0] = 0.0f;
            revSurround[0][1] = 0.0f;
            revSurround[1][0] = 0.0f;
            revSurround[1][1] = 0.0f;
            revSurround[2][0] = 0.0f;
            revSurround[2][1] = 0.0f;
            revSurround[3][0] = 0.0f;
            revSurround[3][1] = 0.0f;
            revSurround[4][0] = reverbGain;
            revSurround[4][1] = 0.0f;
            revSurround[5][0] = 0.0f;
            revSurround[5][1] = reverbGain;
            mVoices[2]->SetOutputMatrix(TheXboxSynth->UnkF8(), 2, 6, &revSurround[0][0], 0);
        }
        break;
    }
    default:
        MILO_ASSERT(0, 0x136);
        break;
    }
}

void FxSend360::Reconnect() {
    if (OutputVoice()) {
        switch (mThis->GetChannels()) {
        case kSendAll:
        case kSendAllXMix:
            CreateVoice(0, 1);
            CreateVoice(2, -1);
            CreateVoice(4, 5);
            break;
        case kSendCenter:
            CreateVoice(2, -1);
            break;
        case kSendStereo:
            CreateVoice(0, 1);
            break;
        default:
            MILO_ASSERT(0, 0x150);
            break;
        }
        CreateInputVoice();
        SyncEffectParams();
        UpdateVolumes();
    }
}
