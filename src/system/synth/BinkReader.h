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
    virtual bool Done() { return mState == 4; } // State 4 = playback complete
    virtual bool Fail() { return mState == 5; } // State 5 = error/failure
    virtual void Init();

private:
    // BinkReader uses a state machine instead of separate boolean flags
    enum State {
        kInit = 1,    // Initializing tracks
        kSetup = 2,   // Setup complete, ready to play
        kPlaying = 3, // Actively playing
        kDone = 4,    // Playback complete
        kFail = 5     // Error occurred
    };

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
    int mState; // Current state (see State enum)
    bool mEnableReads;
};
