#include "synth/SampleData.h"
#include "synth/WavMgr.h"
#include "os/Debug.h"
#include "utl/BinStream.h"
#include "utl/ChunkStream.h"
#include "utl/WaveFile.h"

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
#ifdef HX_NATIVE
    if (!sFree) {
        mData = 0;
        mCRC.mCRC = 0;
        return;
    }
#endif
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

BinStream &operator>>(BinStream &bs, SampleMarker &m) {
    m.Load(bs);
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

int SampleData::SizeAs(Format fmt) const {
    if (fmt > 7U) {
        MILO_ASSERT(0, 0x136);
        return 0;
    }

    switch ((int)fmt) {
    case 1:
    case 0:
        return mNumChannels * mNumSamples * 2;
    case 2:
        return (((mNumSamples + 0x6F) / 0x70) + (mNumSamples + 0x6F >> 0x1F)) * mNumChannels * 0x40;
    case 3: {
        MILO_WARN("don't know size as XMA");
        return mNumSamples / 5;
    }
    case 4:
    case 5: {
        unsigned int tmp = mNumSamples + 0x3FF;
        return ((int)(tmp >> 10) + (tmp < 0 && (tmp & 0x3FF) != 0)) * mNumChannels * 0xc0;
    }
    case 6: {
        int tmp = mNumChannels * mNumSamples * 2;
        return 0x60 - (int)((float)tmp * -0.29411763f);
    }
    default:
        return 0;
    }
}

void SampleData::LoadWAV(BinStream &bs, const FilePath &fp, bool bigEndian) {
    Reset();
    WaveFile wav(bs);
    if (wav.NumChannels() != 1) {
        MILO_WARN("Wave file %s has multiple channels", fp);
        return;
    } else if (wav.BitsPerSample() != 0x10) {
        MILO_WARN("Wave file %s is not 16-bit", fp);
        return;
    } else if (wav.Format() != 1) {
        MILO_WARN("Wave file %s is compressed", fp);
        return;
    } else {
        mFormat = bigEndian ? kBigEndPCM : kPCM;
        mNumSamples = wav.NumSamples();
        mSampleRate = wav.SamplesPerSec();
        mSizeBytes = SizeAs(kPCM);
        mData = sAlloc(mSizeBytes, fp.c_str(), 0, "SampleData", 0);
        WaveFileData wavdata(wav);
        wavdata.Read(mData, mSizeBytes);
        for (int i = 0; i < wav.NumMarkers(); i++) {
            mMarkers.push_back(
                SampleMarker(wav.Markers()[i].GetName(), wav.Markers()[i].GetFrame())
            );
        }
    }
}

void SampleData::Load(BinStream &bs, const FilePath &fp) {
    Reset();
    LOAD_REVS(bs);
    MILO_ASSERT(d.rev <= 0x10, 0x8F);
    if (d.rev > 0xE) {
        int fmt;
        bs >> mCRC;
        bs >> fmt >> mNumSamples >> mSampleRate >> mSizeBytes;
        mFormat = (Format)fmt;
        bool hasData;
        bs >> hasData;
        if (hasData) {
            Hmx::CRC crc;
            crc.mCRC = mCRC.mCRC;
            if (crc.mCRC != 0) {
                mData = TheWavMgr->Get(crc);
            }
            if (mData) {
#ifdef HX_NATIVE
                // ChunkStream doesn't support seeking — read and discard
                void *tmp = sAlloc(mSizeBytes, fp.c_str(), 0, "SampleData", 0);
                ReadChunks(bs, tmp, mSizeBytes, 0x8000);
                sFree(tmp, fp.c_str(), 0, "SampleData");
#else
                bs.Seek(mSizeBytes, BinStream::kSeekCur);
#endif
            } else {
                mData = sAlloc(mSizeBytes, fp.c_str(), 0, "SampleData", 0);
                ReadChunks(bs, mData, mSizeBytes, 0x8000);
                if (crc.mCRC != 0) {
                    TheWavMgr->ReserveRes(crc, mData);
                }
            }
        }
        if (d.rev >= 0x10) {
            bs >> mMarkers;
            bs >> mNumChannels;
        } else if (d.rev >= 0xE) {
            bs >> mMarkers;
        }
    } else {
        int fmt;
        bs >> fmt >> mNumSamples >> mSampleRate >> mSizeBytes;
        bool hasData = true;
        mFormat = (Format)fmt;
        if (d.rev >= 0xB) {
            bs >> hasData;
        }
        if (hasData) {
            mData = sAlloc(mSizeBytes, fp.c_str(), 0, "SampleData", 0);
            ReadChunks(bs, mData, mSizeBytes, 0x8000);
        }
        if (d.rev >= 0xE) {
            bs >> mMarkers;
        }
    }
}
