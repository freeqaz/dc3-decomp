#include "synth/SampleData.h"
#include "synth/WavMgr.h"
#include "os/Debug.h"
#include "os/File.h"
#include "utl/BinStream.h"
#include "utl/ChunkStream.h"
#include "utl/WaveFile.h"

SampleDataAllocFunc SampleData::sAlloc = nullptr;
SampleDataFreeFunc SampleData::sFree = nullptr;
static const int gSampleDataMaxRev = 0x10;
static const int gSampleDataMaxAltRev = 0;

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
    if ((unsigned int)fmt <= 7U) {
        switch (fmt) {
        case 1:
            return mNumChannels * mNumSamples * 2;
        case 0:
            return mNumChannels * mNumSamples * 2;
        case 2:
            return ((mNumSamples + 0x6F) / 0x70) * mNumChannels * 0x40;
        case 3:
            MILO_WARN("don't know size as XMA");
            return mNumSamples / 5;
        case 4: {
            unsigned int uVar3 = mNumSamples + 0x3FF;
            return (((int)uVar3 >> 10) + ((int)uVar3 < 0 && (uVar3 & 0x3FF) != 0)) * mNumChannels * 0xC0;
        }
        case 5: {
            unsigned int uVar3 = mNumSamples + 0x3FF;
            return (((int)uVar3 >> 10) + ((int)uVar3 < 0 && (uVar3 & 0x3FF) != 0)) * mNumChannels * 0xC0;
        }
        case 6: {
            int iVar2 = mNumChannels * mNumSamples;
            return 0x60 - (int)((float)(long long)(iVar2 * 2) * -0.29411763f);
        }
        case 7: {
            int iVar2 = mNumChannels * mNumSamples;
            return 0x60 - (int)((float)(long long)(iVar2 * 2) * -0.29411763f);
        }
        }
    } else {
        MILO_FAIL(0, 0x136);
        return 0;
    }
    return 0;
}

void SampleData::LoadWAV(BinStream &bs, const FilePath &fp, bool bigEndian) {
    Reset();
    WaveFile wav(bs);
    if (wav.BitsPerSample() != 0x10) {
        MILO_WARN("Wave file %s is not 16-bit", fp);
        return;
    }
    if (wav.Format() != 1) {
        MILO_WARN("Wave file %s is compressed", fp);
        return;
    }
    if (!bigEndian) {
        mCRC = Hmx::CRC(FileRelativePath(FileExecRoot(), fp.c_str()));
    } else {
        mCRC.mCRC = 0;
    }
    mNumChannels = wav.NumChannels();
    mNumSamples = wav.NumSamples();
    mSampleRate = wav.SamplesPerSec();
    mFormat = kPCM;
    mSizeBytes = SizeAs(kPCM);
    if (mCRC.mCRC != 0) {
        if (!TheWavMgr->CreateSample(mCRC, mData, mSizeBytes)) {
            WaveFileData wavdata(wav);
            wavdata.Read(mData, mSizeBytes);
        }
    } else {
        mData = sAlloc(mSizeBytes, __FILE__, 0, "SampleData", 0);
        WaveFileData wavdata(wav);
        wavdata.Read(mData, mSizeBytes);
    }
    for (int i = 0; i < wav.NumMarkers(); i++) {
        mMarkers.push_back(
            SampleMarker(wav.Markers()[i].GetName(), wav.Markers()[i].GetFrame())
        );
    }
}

void SampleData::Load(BinStream &bs, const FilePath &fp) {
    Reset();
    LOAD_REVS(bs);
    if (d.rev > gSampleDataMaxRev) {
        MILO_FAIL("%s can't load new %s version %d > %d", fp, "SampleData", (int)d.rev, gSampleDataMaxRev);
    }
    if (d.altRev > gSampleDataMaxAltRev) {
        MILO_FAIL("%s can't load new %s alt version %d > %d", fp, "SampleData", (int)d.altRev, gSampleDataMaxAltRev);
    }
    if (d.rev > 0xE) {
        d >> mCRC;
    } else {
        const char *relPath = FileRelativePath(FileExecRoot(), fp.c_str());
        mCRC = Hmx::CRC(relPath);
    }
    int fmt;
    d >> fmt >> mNumSamples >> mSampleRate >> mSizeBytes;
    mFormat = (Format)fmt;
    bool hasData = true;
    if (d.rev >= 0xB) {
        d >> hasData;
    }
    if (hasData) {
        if (mCRC.mCRC != 0) {
            TheWavMgr->CreateSample(mCRC, mData, mSizeBytes);
        } else {
            mData = sAlloc(mSizeBytes, __FILE__, 0x6f, "SampleData", 0);
        }
        ReadChunks(bs, mData, mSizeBytes, 0x8000);
    }
    if (d.rev >= 0xE) {
        d >> mMarkers;
    }
    if (d.rev >= 0x10) {
        d >> mNumChannels;
    }
}
