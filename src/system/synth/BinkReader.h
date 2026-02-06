#pragma once
#include "os/File.h"
#include "synth/StreamReader.h"
#include "synth/StandardStream.h"

// Forward declarations for Bink SDK structures
struct BINK {
    char padding[0x08];
    unsigned int FrameCount; // 0x08
    unsigned int FrameRate; // 0x0C
    unsigned int TimeBasis; // 0x10
    unsigned int SampleRate; // 0x14
    char padding2[0x20];
    int NumTracks; // 0x38
};

struct BINKTRACK {
    int Frequency; // 0x00
};

class BinkReader : public StreamReader {
public:
    BinkReader(File *, StandardStream *);
    virtual ~BinkReader();
    virtual void Poll(float);
    virtual void Seek(int);
    virtual void EnableReads(bool enable) { mEnableReads = enable; }
    virtual bool Done() { return mDone; }
    virtual bool Fail() { return mFail; }
    virtual void Init();

private:
    File *mFile;
    StandardStream *mStream;
    BINK *mBink;
    BINKTRACK *mTracks[16];
    void *mPCMBuffers[16];
    char pad[0x40]; // Padding or unknown members (64 bytes)
    unsigned char mCurrentTrack;
    int mNumSamplesToConsume;
    int mSamplesRead;
    int mSamplesPerFrame;
    int mState; // At offset 0xE0 based on objdiff
    bool mEnableReads;
    bool mDone;
    bool mFail;
};
