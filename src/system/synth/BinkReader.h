#pragma once
#include "os/File.h"
#include "synth/StreamReader.h"
#include "synth/StandardStream.h"

class BinkReader : public StreamReader {
public:
    BinkReader(File *, StandardStream *);
    virtual ~BinkReader();
    virtual void Poll(float);
    virtual void Seek(int);
    virtual void EnableReads(bool enable) { mEnableReads = enable; }
    virtual bool Done() { return mDone; }
    virtual bool Fail() { return mFail; }
    virtual void Init() {}

private:
    File *mFile; // 0x00
    StandardStream *mStream; // 0x04
    void *mBink; // 0x08
    void *mTracks[16]; // 0x0C

    unsigned char mCurrentTrack; // 0xD0
    int mNumSamplesToConsume; // 0xD4
    int mSamplesRead; // 0xD8
    int mSamplesPerFrame; // 0xDC
    int mState; // 0xE0
    bool mEnableReads; // 0xE4
    bool mDone; // 0xE5
    bool mFail; // 0xE6
};
