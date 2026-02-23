#include "synth/BinkReader.h"
#include "os/Debug.h"
#include "os/Timer.h"
#include "utl/Symbol.h"
#include "utl/MakeString.h"

// External declarations - C functions from Bink SDK
extern "C" {
void BinkInit(void);
void BinkSetSoundTrack(int, int);
BINK *BinkOpen(File *, unsigned int);
void BinkSetVideoOnOff(BINK *, int);
const char *BinkGetError(void);
}

extern Timer *GetTimer(Symbol);
extern void BinkNextFrame(BINK *);
extern unsigned char BinkGetTrackData(int, int);
extern BINKTRACK *BinkOpenTrack(BINK *, unsigned char);
extern void BinkCloseTrack(BINKTRACK *);
extern void BinkClose(BINK *);
extern void *MemAlloc(int, const char *, int, const char *, int);
extern Debug TheDebug;

// Static heap reference
static int sHeap = 0;

// Static variables for timer initialization
static int sTimerInitialized = 0;
static Timer *sTimer = nullptr;

BinkReader::BinkReader(File *file, StandardStream *stream)
    : mFile(file), mStream(stream), mEnableReads(false),
      unkD4(0), unkD8(0), unkDC(0), mState(0), mHeapPtr(&sHeap) {
    // Initialize Bink library
    BinkInit();
    BinkSetSoundTrack(0, 0);

    // Open the Bink file
    mBink = BinkOpen(file, 0x2804400);

    if (mBink != nullptr) {
        mState = kInit;
        BinkSetVideoOnOff(mBink, 0);
    } else {
        const char *err = BinkGetError();
        TheDebug.Notify(MakeString("Error opening Bink audio file: %s", err));
        mState = kFail;
    }
}

BinkReader::~BinkReader() {
    if (mState > 1 && mBink->NumTracks > 0) {
        for (unsigned char i = 0; i < mBink->NumTracks; i++) {
            if (mTracks[i]) {
                BinkCloseTrack(mTracks[i]);
            }
            if (mPCMBuffers[i]) {
                MemFree(mPCMBuffers[i], "unknown", 0, "unknown");
            }
        }
        BinkClose(mBink);
    }
}

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
    case kFail: {
        // Error state - already failed
        TheDebug.Fail("BinkReader::Poll() failed", nullptr);
        break;
    }
    case kPlaying: {
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
    case kSetup: {
        // Setup complete, transition to playing
        mState = kPlaying;
        // Call Init function from vtable
        break;
    }
    case kInit: {
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
        mState = kSetup;
        break;
    }
    }

    // Check for error at end
    // if (mBink && mBink->hasError) {
    //     mState = kFail;
    // }
}

extern void BinkGoto(void *bink, unsigned int frame, int mode);

void BinkReader::Seek(int targetSample) {
    if (mBink != nullptr && mState != kFail) {
        double sampleRate;
        double samplesPerFrame;
        unsigned int targetFrame;
        unsigned int framesAfterSeek;
        unsigned int samplesAfterSeek;
        int deltaFrames;

        // Get audio sample frequency from first track
        // Note: mTracks[0] is a BINKTRACK*, but we're reading the first uint32 (Frequency field)
        sampleRate = (double)*((unsigned int *)mTracks[0]);

        // Calculate Bink video frames per second
        // The (double)(float)(double) casting pattern matches original compiler behavior for precision
        samplesPerFrame = (double)(float)((double)mBink->TimeBasis / (double)mBink->SampleRate);

        // Convert target audio sample number to Bink video frame index
        // The 0.75 offset adjusts for Bink's frame timing alignment
        targetFrame = (unsigned int)(((double)(float)((double)(long long)(int)targetSample / sampleRate) - 0.75) * samplesPerFrame + 1.0);

        // Validate that target frame is within video bounds
        if (mBink->FrameCount < targetFrame) {
            MILO_ASSERT(false, 0x102);
        }

        // Perform the seek operation
        BinkGoto(mBink, targetFrame, 1);

        // Calculate actual audio sample position after the seek
        // Work backwards: last frame -> time -> audio samples
        framesAfterSeek = (mBink->FrameCount) - 1;
        samplesAfterSeek = (unsigned int)(((double)(float)((double)framesAfterSeek * (double)(float)(1.0 / samplesPerFrame) + 0.75) * sampleRate));

        // Store the delta and update reader state
        deltaFrames = targetSample - samplesAfterSeek;
        mNumSamplesToConsume = deltaFrames;
        mSamplesRead = samplesAfterSeek;

        // Verify that the delta is within one frame's worth of audio samples
        if ((float)((double)(float)(1.0 / samplesPerFrame) * sampleRate) < (float)(deltaFrames & 0xFFFFFFFF)) {
            MILO_ASSERT(false, 0x10B);
        }

        mState = kPlaying;
    }
}

void BinkReader::Init() {
    MILO_ASSERT(mStream, 0x114);
    // Initialize stream with: num tracks, sample rate, float samples flag, channel count
    mStream->InitInfo(
        mBink->NumTracks,
        mTracks[0]->Frequency,
        false,
        -1
    );
}
