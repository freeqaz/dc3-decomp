#pragma once
#include "synth/StreamReceiver.h"
#include "synth_xbox/Voice.h"

class StreamReceiver360 : public StreamReceiver {
public:
    StreamReceiver360(int, int, bool);
    virtual ~StreamReceiver360();
    virtual void SetVolume(float);
    virtual void SetPan(float);
    virtual void SetSpeed(float);
    virtual void Poll();
    virtual void SetSlipOffset(float);
    virtual void SlipStop();
    virtual void SetSlipSpeed(float);
    virtual float GetSlipOffset();
    virtual int GetPlayCursor();
    virtual void PauseImpl(bool);
    virtual void PlayImpl();
    virtual void StartSendImpl(unsigned char *, int, int);
    virtual bool SendDoneImpl();

protected:
    unsigned char *mStreamBuffer; // 0x802C
    Voice *mVoice; // 0x8030
    int unk8034; // 0x8034
    int unk8038; // 0x8038
    int unk803C; // 0x803C
};
