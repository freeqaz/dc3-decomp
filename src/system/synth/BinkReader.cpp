#include "synth/BinkReader.h"
#include "os/Debug.h"
#include "os/Timer.h"
#include "utl/Symbol.h"

// External declarations - C functions from assembly
extern Timer *GetTimer(Symbol);
extern void BinkNextFrame(void *);
extern unsigned char BinkGetTrackData(int, int);
extern void *BinkOpenTrack(void *, unsigned char);
extern void *MemAlloc(int, const char *, int, const char *, int);
extern Debug TheDebug;

// Static variables for timer initialization
static int sTimerInitialized = 0;
static Timer *sTimer = nullptr;

BinkReader::BinkReader(File *file, StandardStream *stream)
    : mFile(file), mStream(stream), mBink(nullptr), mCurrentTrack(0),
      mNumSamplesToConsume(0), mSamplesRead(0), mSamplesPerFrame(0),
      mState(1), mEnableReads(true), mDone(false), mFail(false) {
    for (int i = 0; i < 16; i++) {
        mTracks[i] = nullptr;
    }
}

BinkReader::~BinkReader() {}

void BinkReader::Poll(float) {
    // Initialize timer if needed (static)
    if ((sTimerInitialized & 1) == 0) {
        sTimerInitialized |= 1;
        sTimer = GetTimer(Symbol("bink_audio"));
    }

    // Create AutoTimer (consumes CPU time tracking)
    AutoTimer timer(sTimer, 48.0f, nullptr, nullptr);

    // Get state from this object
    int state = mState;

    switch (state) {
    case 5: {
        // Error state - already failed
        TheDebug.Fail("BinkReader::Poll() failed", nullptr);
        break;
    }
    case 3: {
        // Playing state - process audio data
        // TheBlockMgr.Poll();  // Poll the block manager

        if (mNumSamplesToConsume > 0) {
            // Consume data from stream
            // int samplesConsumed = mStream->ConsumeData(&mTracks[0], 0x90, mNumSamplesToConsume);

            // Update counts
            // mSamplesRead += samplesConsumed;
            // mNumSamplesToConsume -= samplesConsumed;
        }

        // If we need more samples, get them from Bink
        if (mNumSamplesToConsume <= 0) {
            unsigned char localCurrentTrack = 0;
            int remainingBuffer = 0xB400;

            // Loop: get track data while buffer has space
            while (localCurrentTrack != 0) {
                // unsigned char trackBits = ...;
                // int trackBuffer = ...;
                // unsigned char samplesRead = BinkGetTrackData(trackBits, trackBuffer);
                // remainingBuffer -= samplesRead;
                // localCurrentTrack++;

                if (remainingBuffer <= 0) {
                    break;
                }
            }

            // Update state when frame is complete
            // if (localCurrentTrack == numTracks) {
            //     mSamplesPerFrame = 0;
            //     mNumSamplesToConsume = (samplesRead >> 1) - mSamplesPerFrame;
            //     mSamplesRead += mSamplesPerFrame;
            //
            //     // Check if buffers are equal
            //     int frameComplete = (mBink->readPos == mBink->writePos) ? 1 : 0;
            //     mState = frameComplete + 3;
            // }
        }
        break;
    }
    case 2: {
        // Setup complete, transition to playing
        mState = 3;
        // Call Init function from vtable
        break;
    }
    case 1: {
        // Initialization state - setup tracks
        // int numTracks = mBink->GetNumTracks();
        //
        // if (numTracks >= 0x10) {
        //     TheDebug.Fail("mBink->NumTracks() < BINK_AUDIO_CH", nullptr);
        // }
        //
        // if (numTracks == 0) {
        //     mState = 4;
        // } else {
        //     for (int i = 0; i < numTracks; i++) {
        //         void *track = BinkOpenTrack(mBink, i);
        //         mTracks[i] = track;
        //
        //         // Validate track properties
        //         // ...
        //     }
        // }
        mState = 2;
        break;
    }
    }

    // Check for error at end
    // if (mBink && mBink->hasError) {
    //     mState = 5;
    // }
}

void BinkReader::Seek(int) {
    // Seek implementation
}
