#include "WavReader.h"

#include "os/Memcard.h"

WavReader::WavReader(File *file, StandardStream *stream) {
    mInFile = file;
    mOutStream = stream;
    MILO_ASSERT(mInFile, 0x1a);
    mInFileStream = new FileStream(file, true);
    mInWaveFile = new WaveFile(*mInFileStream);
    MILO_ASSERT(mInWaveFile->SamplesPerSec() == 44100, 0x21);
    MILO_ASSERT(mInWaveFile->BitsPerSample() == 16, 0x22);
    MILO_ASSERT(mInWaveFile->NumChannels() <= 2, 0x23);
    mSamplesLeft = mInWaveFile->NumSamples();
    mSampleRate = mInWaveFile->SamplesPerSec();
    mNumChannels = mInWaveFile->NumChannels();
    for (int i = 0; i < mInWaveFile->NumMarkers(); i++) {
        WaveFileMarker &wfm = mInWaveFile->Markers()[i];
        int frame = wfm.mFrame;
        float posMS = (float)frame * 1000.0f / (float)mInWaveFile->mSamplesPerSec;
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
        mBufNumSamples -= consumed;
        mTotalSamplesConsumed += consumed;
        mBufOffset += consumed;
        if (mBufNumSamples != 0) {
            return;
        }
    }
    if (mEnableReads) {
        while (mSamplesLeft != 0) {
            int samplesPerRead = mSamplesLeft / mNumChannels;
            if (samplesPerRead > 0x1000) {
                samplesPerRead = 0x1000;
            }
            mBufNumSamples = samplesPerRead;
            mInWaveFileData->Read(mRawInputBuffer, mNumChannels * samplesPerRead * 2);
            mBufOffset = 0;
            mSamplesLeft -= samplesPerRead;
            if (mNumChannels == 1) {
                for (int i = 0; i < mBufNumSamples; i++) {
                    unsigned short s = mRawInputBuffer[i];
                    mInputBuffers[0][i] = (s << 8) | (s >> 8);
                }
            } else {
                for (int i = 0; i < mBufNumSamples; i++) {
                    unsigned short s0 = mRawInputBuffer[i * 2];
                    mInputBuffers[0][i] = (s0 << 8) | (s0 >> 8);
                    unsigned short s1 = mRawInputBuffer[i * 2 + 1];
                    mInputBuffers[1][i] = (s1 << 8) | (s1 >> 8);
                }
            }
            if (mBufNumSamples != 0) {
                void *bufs[2] = { mInputBuffers[0], mInputBuffers[1] };
                int consumed = ConsumeData((void **)bufs, mBufNumSamples, mTotalSamplesConsumed);
                mBufNumSamples -= consumed;
                mTotalSamplesConsumed += consumed;
                mBufOffset += consumed;
                if (mBufNumSamples != 0) {
                    return;
                }
            }
        }
    }
}
