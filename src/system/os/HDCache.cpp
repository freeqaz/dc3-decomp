#include "os/HDCache.h"
#include "math/SHA1.h"
#include "math/Utl.h"
#include "os/Archive.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/File.h"
#include "os/OSFuncs.h"
#include "os/System.h"
#include "utl/BinStream.h"
#include "utl/FileStream.h"
#include "utl/HxGuid.h"
#include "utl/MemStream.h"
#include "utl/Option.h"

HDCache TheHDCache;

HDCache::HDCache()
    : mBlockState(0), mWriteFileIdx(0), mWriteBlock(-1), mWritingHeader(false),
      mReadFileIdx(0), mDirtyCache(0), mLastHdrWriteMs(-1), mLastCacheWriteMs(-1),
      mLockId(-1), unk3c(0), mCritSec(nullptr), mHdrIdx(0), mHdrBuf(nullptr),
      unk64(false) {}

HDCache::~HDCache() {}

bool HDCache::LockCache() {
    CritSecTracker cst(mCritSec);
    if (mLockId == -1 || mLockId == GetCurrentThreadId()) {
        mLockId = GetCurrentThreadId();
        unk3c++;
        return true;
    } else {
        return false;
    }
}

void HDCache::UnlockCache() {
    CritSecTracker cst(mCritSec);
    MILO_ASSERT(mLockId == CurrentThreadId(), 0xfa);
    unk3c--;
    if (unk3c == 0)
        mLockId = -1;
}

int HDCache::HdrSize() {
    int i5 = 32;
    int numArkfiles = TheArchive->NumArkFiles();
    for (int i = 0; i < numArkfiles; i++) {
        if (TheArchive->GetArkfileCachePriority(i) >= 0) {
            int numBlocks = TheArchive->GetArkfileNumBlocks(i) + 0x1F;
            i5 += (numBlocks / 32 + 1) * 4;
        }
    }
    int i2 = i5 + 0x100;
    if (i5 != 0) {
        i2 = (i2 - i5) + 0x1000;
    }
    return i2;
}

bool HDCache::ReadFail() {
    File *file = mReadArkFiles[mReadFileIdx];
    if (file && file->Fail()) {
        MILO_LOG("HDCache Read %d failed\n", mReadFileIdx);
        return true;
    } else
        return false;
}

bool HDCache::ReadDone() {
    File *file = mReadArkFiles[mReadFileIdx];
    if (!file) {
        return true;
    }
    return file->ReadDone();
}

bool HDCache::WriteDone() {
    if (mWriteBlock >= 0) {
        if (mWriteArkFiles[mWriteFileIdx]->WriteDone()) {
            MILO_ASSERT(mReadArkFiles[mWriteFileIdx]->Size() == mWriteArkFiles[mWriteFileIdx]->Size(), 499);
            UnlockCache();
            if (mWriteArkFiles[mWriteFileIdx]->Fail()) {
                MILO_LOG("HDCache Write %d.%d failed\n", mWriteFileIdx, mWriteBlock);
            } else {
                if (++mDirtyCache == 1) {
                    mLastHdrWriteMs = SystemMs();
                }
                int idx = (mWriteBlock / 32) * 4;
                mBlockState[mWriteFileIdx][idx] |= 1 << mWriteBlock;
            }
            mWriteBlock = -1;
        }
    }
    return mWriteBlock == -1;
}

// void HDCache::Flush() {}

void HDCache::Poll() {
    if (mWritingHeader) {
        if (mHdr[mHdrIdx]->WriteDone()) {
            UnlockCache();
            if (mHdr[mHdrIdx]->Fail()) {
                MILO_LOG("HDCache Write Header Failed\n");
            }
            Flush();
            mWritingHeader = false;
        }
    }
    if (mDirtyCache && !mWritingHeader
        && (mDirtyCache > 0x400 || SystemMs() - mLastHdrWriteMs > 60000)) {
        WriteHdr();
    }
}

bool HDCache::ReadAsync(int arkfileNum, int blockNum, void *ptr) {
    MILO_ASSERT(ReadDone(), 0x191);
    if (mBlockState[arkfileNum]) {
        MILO_ASSERT(blockNum < TheArchive->GetArkfileNumBlocks(arkfileNum), 0x196);
        // Check if block is in bitmap
        if ((mBlockState[arkfileNum][(blockNum / 32)] & (1 << (blockNum % 32)))) {
            File *readFile = mReadArkFiles[arkfileNum];
            MILO_ASSERT(readFile->Size() >= ((blockNum + 1) * kArkBlockSize), 0x19D);
            mReadFileIdx = arkfileNum;
            readFile->Seek(blockNum * kArkBlockSize, 0);
            return readFile->ReadAsync(ptr, kArkBlockSize);
        }
    }
    return false;
}

// bool HDCache::WriteAsync(int, int, void const *) { return false; }

// void HDCache::Init() {}

FileStream *HDCache::OpenHeader() {
    if (mHdrFmt.empty())
        return nullptr;
    else {
        const char *str;
        int i;
        for (i = 0; i < 2; i++) {
            str = MakeString(mHdrFmt.c_str(), 0);
            if (FileExists(str, 0x10000, nullptr))
                break;
        }
        if (i != 2) {
            return new FileStream(str, FileStream::kReadNoArk, true);
        } else {
            return nullptr;
        }
    }
}

void HDCache::WriteHdr() {
    if (!mHdr[mHdrIdx]->Fail() && LockCache()) {
        MILO_ASSERT(mHdr[mHdrIdx]->WriteDone(), 0x144);
        CSHA1 sha;
        mHdrBuf->Seek(0, BinStream::kSeekBegin);
        mHdrBuf->EnableWriteEncryption();
        *mHdrBuf << 2;
        HxGuid guid;
        TheArchive->GetGuid(guid);
        *mHdrBuf << guid;
        int numArkfiles = TheArchive->NumArkFiles();
        *mHdrBuf << numArkfiles;
        for (int i = 0; i < numArkfiles; i++) {
            int blockSize = 0;
            if (mBlockState[i]) {
                int numBlocks = TheArchive->GetArkfileNumBlocks(i) + 0x1F;
                blockSize = (numBlocks / 32) * 4;
            }
            *mHdrBuf << blockSize;
            if (blockSize > 0) {
                mHdrBuf->Write(mBlockState[i], blockSize);
                sha.Update((const unsigned char *)mBlockState[i], blockSize);
            }
        }
        char buf[256];
        buf[0] = 0;
        memset(&buf[1], 0, 255);
        sha.Final().ReportHash(buf, 0);
        mHdrBuf->Write(buf, 256);
        mHdrBuf->DisableEncryption();
        mDirtyCache = 0;
        int finalSize = HdrSize();
        MILO_ASSERT(mHdrBuf->Size() <= finalSize, 0x176);
        char buf1e0[0x80];
        memset(buf1e0, 0, 0x80);
        while (mHdrBuf->Size() < finalSize) {
            int size = finalSize - mHdrBuf->Size();
            if (size > 0x80U)
                size = 0x80;
            mHdrBuf->Write(buf1e0, size);
        }
        MILO_ASSERT(mHdrBuf->Size() == finalSize, 0x183);
        int oldSize = mHdr[mHdrIdx]->Size();
        int newSize = mHdrBuf->Size();
        MILO_ASSERT(oldSize == newSize, 0x186);
        mWritingHeader = true;
        mHdr[mHdrIdx]->Seek(0, 0);
        mHdr[mHdrIdx]->WriteAsync(mHdrBuf->Buffer(), mHdrBuf->Size());
    }
}

void HDCache::OpenFiles(int numCachedArkfiles) {
    if (mFileFmt.empty())
        return;
    int numArkfiles = TheArchive->NumArkFiles();
    MILO_ASSERT(numCachedArkfiles <= numArkfiles, 0x22F);
    FileMkDir(FileGetPath(mFileFmt.c_str()));
    std::vector<int> pendingArkfiles;
    for (int i = 0; i < numArkfiles; i++) {
        const char *fileFmt = MakeString(mFileFmt.c_str(), i);
        bool exists = FileExists(fileFmt, 0x10000, nullptr);
        int prio = TheArchive->GetArkfileCachePriority(i);
        if (exists && i > numCachedArkfiles) {
            FileDelete(fileFmt);
        }
        if (prio >= 0) {
            pendingArkfiles.push_back(i);
        }
    }
    const char *hdrFmt = MakeString(mHdrFmt.c_str(), 0);
    mHdr[0] = NewFile(hdrFmt, 0x50101);
    bool i11 = mHdr[0] && !mHdr[0]->Fail();
    if (i11) {
        int hdrSize = HdrSize();
        mHdr[0]->Truncate(hdrSize);
        RELEASE(mHdr[0]);
        mHdr[0] = NewFile(hdrFmt, 0x50001);
        i11 = (hdrSize - mHdr[0]->Size()) == 0;
    }
    if (!i11) {
        RELEASE(mHdr[0]);
    } else {
        while (pendingArkfiles.size() != 0) {
            int i5 = -1;
            auto max = pendingArkfiles.begin();
            MILO_ASSERT(max != pendingArkfiles.end(), 0x26F);
            // there's a pendingArkfiles iteration somewhere here
            const char *fileFmt = MakeString(mFileFmt.c_str(), i5);
            File *file = NewFile(fileFmt, 0x50101);
            pendingArkfiles.erase(max);
        }
        for (int i = 0; i < numArkfiles; i++) {
            const char *fileFmt = MakeString(mFileFmt.c_str(), i);
            File *write = NewFile(fileFmt, 0x50002);
            File *read = NewFile(fileFmt, 0x50001);
            if (write && read && !write->Fail() && !read->Fail()) {
                mReadArkFiles[i] = read;
                mWriteArkFiles[i] = write;
            }
        }
    }
}

void HDCache::Init() {
    mCritSec = new CriticalSection();
    if (TheArchive) {
        if (OptionBool("no_hdcache", true)) {
            Flush();
        }
        int numArkfiles = TheArchive->NumArkFiles();
        mReadArkFiles.resize(numArkfiles);
        mWriteArkFiles.resize(numArkfiles);
        FileStream *header = OpenHeader();
        bool next = header && header->Size() == HdrSize();
        if (next) {
            header->EnableReadEncryption();
            int version;
            *header >> version;
            next = version == 2;
        }
        if (next) {
            HxGuid guid1, guid2;
            *header >> guid1;
            TheArchive->GetGuid(guid2);
            next = guid1 == guid2;
        }
        int numFilesToOpen = 0;
        if (next) {
            *header >> numFilesToOpen;
            if (numFilesToOpen < 0 || numFilesToOpen > numArkfiles) {
                numFilesToOpen = 0;
                next = false;
            }
        }
        OpenFiles(numFilesToOpen);
        mBlockState = new int *[numArkfiles];
        CSHA1 sha;
        // more
    }
    mHdrFmt = "";
    mFileFmt = "";
    mHdrBuf = new MemStream(true);
}
