#include "utl/ChunkStream.h"

#include "Compress.h"
#include "obj/Object.h"
#include "os/CritSec.h"
#include "os/Endian.h"
#include "os/File.h"
#include "os/SynchronizationEvent.h"
#include "os/System.h"
#include "utl/Std.h"

namespace {
    std::list<DecompressTask> gDecompressionQueue;
    CriticalSection gDecompressionCritSec;
    bool gDecompressionThread = false;
    SynchronizationEvent gDataProcessedEvt;
    SynchronizationEvent gDataReadyEvt;
    void *mThreadHandle;

    unsigned long DecompressionThread(void *v) {
        for (; gDecompressionThread != false;) {
            if (ChunkStream::PollDecompressionWorker()) {
                gDataProcessedEvt.Set();
            }
            else {
                gDataReadyEvt.Wait(-1);
            }
        }
        return false;
    }

    void StartDecompressionThread() {
        if (!gDecompressionThread) {
            gDecompressionThread = true;
            mThreadHandle = CreateThread(nullptr, 0, DecompressionThread, nullptr, 4, nullptr);
            MILO_ASSERT(mThreadHandle, 0x82);
            XSetThreadProcessor(mThreadHandle, 3);
            ResumeThread(mThreadHandle);
        } else {
            gDataReadyEvt.Set();
        }
    }
}

Hmx::Object *gActiveChunkObject;

void ChunkStream::SetPlatform(Platform plat) {
    if (plat == kPlatformNone) {
        plat = ConsolePlatform();
    }
    mLittleEndian = PlatformLittleEndian(plat);
    mPlatform = plat;
}

void ChunkStream::WriteImpl(const void *data, int bytes) {
    if (mCurBufOffset + bytes > mBufSize) {
        while (mCurBufOffset + bytes > mBufSize)
            mBufSize += mBufSize;
        void *a = _MemAllocTemp(mBufSize, __FILE__, 0x1E4, "ChunkStreamBuf", 0);
        memcpy(a, mBuffers[0], mCurBufOffset);
        MemFree(mBuffers[0]);
        mBuffers[0] = (char *)a;
        MemFree(mBuffers[1]);
        mBuffers[1] =
            (char *)_MemAllocTemp(mBufSize, __FILE__, 0x1EA, "ChunkStreamBuf", 0);
    }
    memcpy(mBuffers[0] + mCurBufOffset, data, bytes);
    mCurBufOffset += bytes;
}

void ChunkStream::ReadChunkAsync() {
    int bufIdx = 1;
    int idx;
    for (; bufIdx < 4; bufIdx++) {
        idx = (mCurBufferIdx + bufIdx) % 3;
        if (mBuffersState[idx] == kInvalid)
            break;
    }
    if (mBuffersState[idx] == kInvalid) {
        int *thechunk = &mCurChunk[bufIdx];
        if (thechunk != mChunkEnd) {
            int thechunkval = *thechunk;
            int sizemask = thechunkval & kChunkSizeMask;
            if (mChunkInfo.mID != 0xCABEDEAF && !(thechunkval & 0x01000000)) {
                mFile->ReadAsync(mBuffers[idx] + mBufSize - sizemask, sizemask);
            } else
                mFile->ReadAsync(mBuffers[idx], sizemask);
            mBuffersOffset[idx] = &mCurChunk[bufIdx];
            mBuffersState[idx] = kReading;
        }
    }
}

void SetActiveChunkObject(Hmx::Object *obj) { gActiveChunkObject = obj; }

BinStream &ReadChunks(BinStream &bs, void *data, int total_len, int max_chunk_size) {
    int curr_size = 0;
    while (curr_size != total_len) {
        int len_left = Min(total_len - curr_size, max_chunk_size);
        char *dataAsChars = (char *)data;
        bs.Read(&dataAsChars[curr_size], len_left);
        curr_size += len_left;
        while (bs.Eof() == TempEof)
            Timer::Sleep(0);
    }
    return bs;
}

ChunkStream::ChunkStream(
    const char *file,
    FileType type,
    int chunkSize,
    bool compress,
    Platform plat,
    bool cached
)
    : BinStream(false), mFile(nullptr), mFilename(file), mFail(false), mType(type),
      mChunkInfo(compress), mIsCached(cached), mBufSize(-1), mCurReadBuffer(nullptr),
      mRecommendedChunkSize(chunkSize), mLastWriteMarker(0), mCurBufferIdx(-1),
      mCurBufOffset(0), mChunkInfoPending(false), mCurChunk(nullptr), mChunkEnd(nullptr),
      mTell(0) {
    SetPlatform(plat);
    for (int bufCnt = 0; bufCnt < 3; bufCnt++) {
        mBuffersState[bufCnt] = kInvalid;
        mBuffersOffset[bufCnt] = 0;
        mBuffers[bufCnt] = 0;
    }
    mFile = NewFile(file, type == kRead ? 2 : 0x301);
    mFail = !mFile;
    if (!mFail) {
        if (type == kWrite) {
            mFile->Write(&mChunkInfo, 0x810);
            mBufSize = mRecommendedChunkSize * 2;
            mBuffers[0] =
                (char *)_MemAllocTemp(mBufSize, __FILE__, 0x144, "ChunkStreamBuf", 0);
            mBuffers[1] =
                (char *)_MemAllocTemp(mBufSize, __FILE__, 0x145, "ChunkStreamBuf", 0);
            mCurBufferIdx = 0;
        } else {
            mChunkInfoPending = true;
            mFile->ReadAsync(&mChunkInfo, 0x810);
        }
    }
}

ChunkStream::~ChunkStream() {
    if (mFail == false && mType == kWrite) {
        MaybeWriteChunk(true);
        if (mChunkInfo.mNumChunks == 512) {
            MILO_FAIL("%s is %d compressed bytes too large", mFilename, sizeof(mChunkInfo.mChunks));
        }
        //memset()
        for (int i = 0; i < sizeof(mChunkInfo.mChunks); i++) {
            int maxChunk = mChunkInfo.mMaxChunkSize;
        }
    }
}

bool ChunkStream::Cached() const { return mIsCached; }
Platform ChunkStream::GetPlatform() const { return mPlatform; }

void ChunkStream::ReadImpl(void *data, int bytes) {
    MILO_ASSERT(mCurBufferIdx != -1, 0x1D3);
    MILO_ASSERT(mBuffersState[mCurBufferIdx] == kReady, 0x1D4);
    MILO_ASSERT(mBuffersOffset[mCurBufferIdx] == mCurChunk, 0x1D5);
    MILO_ASSERT(mCurBufOffset + bytes <= (*mCurChunk & kChunkSizeMask), 0x1D6);
    memcpy(data, (void *)(mCurReadBuffer + mCurBufOffset), bytes);
    mCurBufOffset += bytes;
    mTell += bytes;
}

void ChunkStream::SeekImpl(int, SeekType) { MILO_FAIL("Can't seek on chunkstream"); }

int ChunkStream::Tell() {
    if (mType == kRead) {
        return mTell;
    } else {
        MILO_FAIL("Can't tell on chunkstream");
        return 0;
    }
}

int ChunkStream::WriteChunk() {
    MILO_ASSERT(mCurBufOffset < kChunkSizeMask, 778);
    int size = mCurBufOffset;
    int flags = 0;
    int *firstbuf = (int *)mBuffers[0];
    if (mChunkInfo.mID == 0xCDBEDEAF) {
        int l38 = mBufSize - 4;
        unsigned int *secondbuf = (unsigned int *)mBuffers[1];
        *secondbuf = size;
        EndianSwapEq(*secondbuf);
        CompressMem(mBuffers[0], size, secondbuf + 1, l38, 0);
        if (((float)mCurBufOffset / (float)l38) > 1.1f && mChunkInfo.mNumChunks != 0) {
            size = l38 + 4;
            firstbuf = (int *)secondbuf;
        } else
            flags |= 0x1000000;
    }
    if (mFile->Write(firstbuf, size) != size) {
        mFail = true;
    }
    MILO_ASSERT((size & ~kChunkSizeMask) == 0, 820);
    MILO_ASSERT((flags & (kChunkSizeMask|kChunkUnusedMask)) == 0, 822);
    int result = size | flags;
    MILO_ASSERT((result & kChunkUnusedMask) == 0, 827);
    return result;
}

BinStream &MarkChunk(BinStream &bs) {
    ChunkStream *cs = dynamic_cast<ChunkStream *>(&bs);
    if (cs)
        cs->PotentiallyWriteChunk(false);
    return bs;
}

void DecompressMemHelper(const void *compressedMem, int size, void *dst, int &dstLen, const char *c) {
    unsigned int rawSize = *(unsigned int *)compressedMem;
    DecompressMem((const char *)compressedMem + 4, size - 4, dst, dstLen, c);
    int expectedDstLen = EndianSwap(rawSize);
    MILO_ASSERT(dstLen == expectedDstLen, 0x3bb);
}

void ChunkStream::DecompressChunk(DecompressTask &task) {
    MILO_ASSERT(*task.mState == kDecompressing, 0x3c1);
    int data = *task.mChunk;
    int dataMsk = data & kChunkSizeMask;
    MILO_ASSERT((data & ~kChunkSizeMask) == 0, 0x3c5);
    int out_len = task.mDecompressedSize;
    int id = task.mID;
    if (id == 0xCDBEDEAF) {
        char *dataOffset = (char *)task.mBuffer + (out_len - dataMsk);
        DecompressMemHelper(dataOffset, dataMsk, task.mBuffer, out_len, task.mTempBuf);
    } else if (id == 0xCCBEDEAF) {
        char *dataOffset = (char *)task.mBuffer + (out_len - dataMsk) + 10;
        DecompressMem(dataOffset, dataMsk - 0x12, task.mBuffer, out_len, task.mTempBuf);
    } else {
        MILO_ASSERT(task.mID == CHUNKSTREAM_Z_ID, 0x3d7);
        char *dataOffset = (char *)task.mBuffer + (task.mDecompressedSize - dataMsk);
        DecompressMem(dataOffset, dataMsk, task.mBuffer, out_len, task.mTempBuf);
    }
    *task.mChunk = out_len;
    *task.mState = kReady;
}

void ChunkStream::DecompressChunkAsync() {
    int bufIdx = 1;
    int idx;
    for (; bufIdx <= 2; bufIdx++) {
        idx = (mCurBufferIdx + bufIdx) % 3;
        if (mBuffersState[idx] == kReading)
            break;
    }

    if (mBuffersState[idx] == kReading) {
        bool maskexists = (mCurChunk[bufIdx] >> 24) & 1;
        if (mChunkInfo.mID != 0xCABEDEAF && !maskexists) {
            mBuffersState[idx] = kDecompressing;
            DecompressTask dtask;
            dtask.mChunk = &mCurChunk[bufIdx];
            dtask.mBuffer = mBuffers[idx];
            dtask.mState = &mBuffersState[idx];
            dtask.mDecompressedSize = mBufSize;
            dtask.mID = mChunkInfo.mID;
            dtask.mTempBuf = (char *)mFilename.c_str();
            gDecompressionQueue.push_back(dtask);
        } else {
            mBuffersState[idx] = kReady;
        }
    }
}

bool ChunkStream::PollDecompressionWorker() {
    gDecompressionCritSec.Enter();
    unsigned int counter = 0;
    for (std::list<DecompressTask>::iterator it = gDecompressionQueue.begin(); it != gDecompressionQueue.end(); it++) {
        counter++;
    }
    if (counter != 0) {
        DecompressTask task;
        memcpy(&task, &gDecompressionQueue.front(), sizeof(DecompressTask));
        gDecompressionQueue.pop_front();
        gDecompressionCritSec.Exit();
        DecompressChunk(task);
        return true;
    } else {
        gDecompressionCritSec.Exit();
        return false;
    }
}

BinStream &WriteChunks(BinStream &bs, const void *v, int i1, int i2) {
    for (int i = 0; i != i1;) {
        int temp = i1 - i;
        if (i2 < temp) {
            temp = i2;
        }
        bs.Write((void *)(i + (int)v), temp);
        i += temp;
        if (bs.GetPlatform() == kPlatformWii) {
            MarkChunk(bs);
        }
    }
    return bs;
}

void ChunkStream::MaybeWriteChunk(bool b) {
    if (mChunkInfo.mNumChunks < 2 && 0x2000 <= mCurBufOffset) {
        b = true;
    }
    if (mCurBufOffset >= mRecommendedChunkSize || b) {
        bool maxchunks = mChunkInfo.mNumChunks == 511;
        if (!b && maxchunks) {
            return;
        }
        if ((mCurBufOffset >= mRecommendedChunkSize + 0x2000)
            && (0x2000 <= mLastWriteMarker) && !maxchunks) {
            int __n = mCurBufOffset - mLastWriteMarker;
            void *__dest = _MemAllocTemp(__n, __FILE__, 0x2e6, "ChunkStreamBuf", 0);
            memcpy(__dest, mBuffers[0] + mLastWriteMarker, __n);
            mCurBufOffset = mLastWriteMarker;
            mLastWriteMarker = 0;
            MaybeWriteChunk(true);
            mCurBufOffset = __n;
            memcpy(mBuffers[0], __dest, __n);
            MemFree(__dest);
            if (!b) {
                return;
            }
        }
        MILO_ASSERT_FMT(
            mChunkInfo.mNumChunks < 512,
            "%s has %d chunks, max is %d",
            mFilename,
            mChunkInfo.mNumChunks,
            512
        );
        unsigned int wrote = WriteChunk();
        int mask = wrote & kChunkSizeMask;
        mChunkInfo.mChunks[mChunkInfo.mNumChunks++] = wrote;
        mChunkInfo.mMaxChunkSize =
            Max<int>(mask, mCurBufOffset, mChunkInfo.mMaxChunkSize);
        mCurBufOffset = 0;
    }
    mLastWriteMarker = mCurBufOffset;
}