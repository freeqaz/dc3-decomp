#include "synth\Mic.h"
#include "math\Utl.h"
#include "os\Debug.h"
#include "obj\Data.h"
#include "utl\MemMgr.h"
#include <algorithm>
#include <cstring>

void Mic::Set(const DataArray *data) {
    MILO_ASSERT(data, 0x12);
    SetGain(data->FindFloat("gain"));
    SetDMA(data->FindInt("dma"));
    DataArray *compressorArr = data->FindArray("compressor");
    SetCompressor(compressorArr->Int(1));
    SetCompressorParam(compressorArr->Float(2));
}

RingBuffer::~RingBuffer() {
    if (mBuffer) {
        MemFree(mBuffer, __FILE__, 0x23);
        mBuffer = nullptr;
    }
}

void RingBuffer::Reset() {
    memset(mBuffer, 0, mSize);
    mWriteIx = 0;
    mReadIx = 0;
    mTotal = 0;
}

void RingBuffer::Init(int size) {
    mSize = size;
    if (mBuffer) {
        MemFree(mBuffer, __FILE__, 0x2B);
        mBuffer = nullptr;
    }
    mBuffer = MemAlloc(size, __FILE__, 0x2C, "VirtualMic RingBuffer", 0x80);
    MILO_ASSERT(mBuffer, 0x2D);
    Reset();
}

int RingBuffer::Peek(void *data, int len) {
    MILO_ASSERT(len <= mSize, 0x62);
    // Start `len` bytes behind the write head, wrapping.
    int startIx = ((mWriteIx - len) + mSize) % mSize;
    // NOTE: std::min, not Min(). Retail binds both operands by const reference:
    // it homes the `len` parameter to its caller stack slot, spills `chunk1` to
    // a frame slot, selects between the two ADDRESSES and reloads. Milo's
    // Min(T, T) is by value and compiles to a register select instead.
    int chunk1 = std::min(mSize - startIx, len);
    memcpy(data, (char *)mBuffer + startIx, chunk1);
    if (chunk1 != len) {
        memcpy((char *)data + chunk1, mBuffer, len - chunk1);
    }
    return len;
}

int RingBuffer::Write(void *data, int len) {
    // More than the buffer holds: keep only the last mSize bytes. Retail reuses
    // the `len` parameter's own home slot here rather than a separate local.
    if (len > mSize) {
        data = (char *)data + len - mSize;
        len = mSize;
    }
    char *src = (char *)data;

    int available = mSize - mWriteIx;
    int returnVal = (mTotal - mSize) + len;
    // std::min, not Min(): retail binds by const reference and selects between
    // the two operands' addresses. See the note in Peek().
    int chunk1 = std::min(available, len);

    memcpy((char *)mBuffer + mWriteIx, src, chunk1);

    if (chunk1 != len) {
        memcpy(mBuffer, src + chunk1, len - chunk1);
    }

    mWriteIx = (mWriteIx + len) % mSize;
    mTotal = std::min(mSize, mTotal + len);

    if (mTotal == mSize) {
        mReadIx = mWriteIx;
    }

    return returnVal;
}

int RingBuffer::Read(void *data, int len) {
    int readLen;
    if (mTotal >= len) {
        readLen = len;
    } else {
        readLen = mTotal;
    }

    if (readLen == 0) {
        return 0;
    }

    int available = mSize - mReadIx;
    // std::min, not Min(): see the note in Peek().
    int chunk1 = std::min(available, readLen);

    memcpy(data, mReadIx + (char *)mBuffer, chunk1);

    if (chunk1 != readLen) {
        memcpy((char *)data + chunk1, mBuffer, readLen - chunk1);
    }

    mTotal -= readLen;
    mReadIx = (mReadIx + readLen) % mSize;

    return readLen;
}
