#include "WavReader.h"

#include "os\Memcard.h"

WavReader::WavReader(File *file, StandardStream *stream) {
    mInFile = file;
    mOutStream = stream;
    MILO_ASSERT(mInFile, 0x1a);
    mInFileStream = new FileStream(file, true);
    mInWaveFile = new WaveFile(*mInFileStream);
    MILO_ASSERT(mInWaveFile->SamplesPerSec() == 44100, 0x21);
    MILO_ASSERT(mInWaveFile->BitsPerSample() == 16, 0x22);
    MILO_ASSERT(mInWaveFile->NumChannels() <= 2, 0x23);
    mNumChannels = mInWaveFile->mNumChannels;
    mSampleRate = mInWaveFile->mSamplesPerSec;
    mSamplesLeft = mInWaveFile->mNumSamples;
    for (int i = 0; i < mInWaveFile->NumMarkers(); i++) {
        WaveFileMarker &wfm = mInWaveFile->Markers()[i];
        int frame = wfm.mFrame;
        float posMS = (float)frame * 1000.0f / (float)(int)mInWaveFile->mSamplesPerSec;
        Marker marker(wfm.mName);
        marker.position = frame;
        marker.posMS = posMS;
        stream->AddMarker(marker);
    }
    mInWaveFileData = new WaveFileData(*mInWaveFile);
    mInputBuffers[0] = new unsigned short[0x1000];
    mInputBuffers[1] = new unsigned short[0x1000];
    mRawInputBuffer = new unsigned short[0x2000];
    mTotalSamplesConsumed = 0;
    mBufNumSamples = 0;
    mBufOffset = 0;
    mEnableReads = true;
    mInitted = false;
}

WavReader::~WavReader() {
    delete mInWaveFileData;
    delete mInWaveFile;
    delete mInFileStream;
    delete[] mInputBuffers[0];
    delete[] mInputBuffers[1];
    delete mRawInputBuffer;
}

void WavReader::Seek(int samples) {
    mInWaveFileData->Seek(mNumChannels * samples * 2, BinStream::SeekType::kSeekBegin);
    mSamplesLeft = (mSamplesLeft - samples) + mBufNumSamples + mTotalSamplesConsumed;
    mTotalSamplesConsumed = samples;
    mBufOffset = 0;
    mBufNumSamples = 0;
}

void WavReader::Init() {
    MILO_ASSERT(mOutStream, 0xaa);
    mOutStream->InitInfo(mNumChannels, mSampleRate, false, mInWaveFile->NumSamples());
}

int WavReader::ConsumeData(void **data, int samples, int startSamp) {
    MILO_ASSERT(mOutStream, 0xb1);
    return mOutStream->ConsumeData(data, samples, startSamp);
}

void WavReader::Poll(float dt) {
    if (!mInitted) {
        mInitted = true;
        Init();
    }
    if (mBufNumSamples != 0) {
        void *bufs[2] = { mInputBuffers[0] + mBufOffset, mInputBuffers[1] + mBufOffset };
        int consumed = ConsumeData((void **)bufs, mBufNumSamples, mTotalSamplesConsumed);
        mTotalSamplesConsumed += consumed;
        mBufNumSamples -= consumed;
        mBufOffset += consumed;
        if (mBufNumSamples != 0) {
            return;
        }
    }
    if (mEnableReads) {
        while (mSamplesLeft != 0) {
            // A partial final frame drains the reader and ENDS the loop. The
            // previous source set mBufNumSamples = 0 here and fell through, which
            // left mSamplesLeft untouched -- an infinite loop for any
            // 0 < mSamplesLeft < mNumChannels. Retail stores 0 to mSamplesLeft
            // (0x20) and branches to the epilogue, not to mBufNumSamples (0x30).
            if (mSamplesLeft < mNumChannels) {
                mSamplesLeft = 0;
                break;
            }
            int numFrames = mSamplesLeft / mNumChannels;
            mBufNumSamples = numFrames > 0x1000 ? 0x1000 : numFrames;
            mInWaveFileData->Read(mRawInputBuffer, mNumChannels * mBufNumSamples * 2);
            mBufOffset = 0;
            mSamplesLeft -= mBufNumSamples;
            if (mNumChannels == 1) {
                for (int i = 0; i < mBufNumSamples; i++) {
                    unsigned short s = mRawInputBuffer[i];
                    mInputBuffers[0][i] = (s << 8) | (s >> 8);
                }
            } else {
                // w25-gj stop note: the image emits both stores below as sthx (index,
                // base); we emit (base, index), while the mono loop above is (base,
                // index) in both builds. c2's commutative-operand sort (C2RS-BRIDGE 8.7,
                // cqlo3) puts the &mInputBuffers[k] address add as M6[base V338 or V342]
                // key 0x18008 (sid 2 mod 4) ahead of the strength-reduced index temp V492
                // (0x17b00); the mono loop's index V475 (0x176c0) loses to the same V338.
                // One shared V338 cannot sit on both sides, so the image either has a
                // separate base temp here or index temp sids past 512 (21+ more temps).
                // Open: what splits the CSE of this+0x28 across the two loops. Inert:
                // pointer arithmetic, (*mInputBuffers)[i], named out pointers,
                // EndianSwap/SwapBytes.
                for (int i = 0; i < mBufNumSamples; i++) {
                    unsigned short s0 = mRawInputBuffer[i * 2];
                    mInputBuffers[0][i] = (s0 << 8) | (s0 >> 8);
                    unsigned short s1 = mRawInputBuffer[i * 2 + 1];
                    mInputBuffers[1][i] = (s1 << 8) | (s1 >> 8);
                }
            }
            if (mBufNumSamples != 0) {
                int consumed = ConsumeData((void **)mInputBuffers, mBufNumSamples, mTotalSamplesConsumed);
                mTotalSamplesConsumed += consumed;
                mBufNumSamples -= consumed;
                mBufOffset += consumed;
                if (mBufNumSamples != 0) {
                    return;
                }
            }
        }
    }
}
