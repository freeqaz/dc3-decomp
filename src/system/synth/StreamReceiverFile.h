#pragma once
#include "synth\StreamReceiver.h"
#include "utl\MemMgr.h"

#define kStreamBufSize 0x4000

class StreamReceiverFile : public StreamReceiver {
public:
    StreamReceiverFile(int, bool);
    virtual ~StreamReceiverFile() {}
    virtual void SetVolume(float) {}
    virtual void SetPan(float) {}
    virtual void SetSpeed(float) {}
    virtual void SetSlipOffset(float) {}
    virtual void SlipStop() {}
    virtual void SetSlipSpeed(float) {}
    virtual float GetSlipOffset();

    static int sPlayCursor;

protected:
    virtual int GetPlayCursor();
    virtual void PauseImpl(bool) {}
    virtual void PlayImpl() {}
    virtual void StartSendImpl(unsigned char *, int, int);
    virtual bool SendDoneImpl() { return true; }

    unsigned char *mTargetBuffer; // 0x802c
    int mBufSize; // 0x8030
    // The target's `new StreamReceiverFile` passes 0x8038 to
    // StreamReceiver::operator new, but the constructor only initialises
    // 0x802c and 0x8030 and no StreamReceiverFile method reads 0x8034.  So the
    // class carries one more 4-byte member here that this build never touches;
    // it still has to be present or every allocation is 4 bytes short.
    int unk8034; // 0x8034
};
