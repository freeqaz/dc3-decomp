#include "synth/SampleData.h"
#include "synth/WavMgr.h"
#include "utl/BinStream.h"
#include "utl/ChunkStream.h"

SampleDataAllocFunc SampleData::sAlloc = nullptr;
SampleDataFreeFunc SampleData::sFree = nullptr;

SampleData::SampleData() : mData(0), mMarkers() { Reset(); }
SampleData::~SampleData() { Dealloc(); }

void SampleData::SetAllocator(SampleDataAllocFunc a, SampleDataFreeFunc f) {
    sAlloc = a;
    sFree = f;
    TheWavMgr->SetAllocator((WavMgrAllocFunc)a, (WavMgrFreeFunc)f);
}

void SampleData::Dealloc() {
    Hmx::CRC crc;
    crc.mCRC = mCRC.mCRC;
    if (crc.mCRC == 0 || !TheWavMgr->ReleaseRes(crc)) {
        sFree(mData, "SampleData.cpp", 196, "SampleData");
    }
    mData = 0;
    mCRC.mCRC = 0;
}

void SampleData::Reset() {
    Dealloc();
    mFormat = kPCM;
    mSizeBytes = 0;
    mSampleRate = 0;
    mNumSamples = 0;
    mNumChannels = 1;
    mMarkers.clear();
}

int SampleData::NumMarkers() const { return mMarkers.size(); }

const SampleMarker &SampleData::GetMarker(int idx) const { return mMarkers[idx]; }

BinStream &operator<<(BinStream &bs, const SampleMarker &s) {
    s.Save(bs);
    return bs;
}

void SampleData::Save(BinStream &bs) const {
    SAVE_REVS(0x10, 0);
    bs << mCRC;
    bs << mFormat;
    bs << mNumSamples;
    bs << mSampleRate;
    bs << mSizeBytes;
    bool hasData = mData;
    bs << hasData;
    if (hasData) {
        WriteChunks(bs, mData, mSizeBytes, 0x8000);
    }
    bs << mMarkers;
    bs << mNumChannels;
}
