#include "utl\MemStream.h"

// Native only (ported from rb3-xenon 2117aa92e, W16-TW): `&mBuffer[mTell]` with
// mTell == size() is operator[] one past the end. The 360's STLport vector indexes
// a raw pointer, so a 0-byte access at the end (an empty Symbol or String body,
// or a read clamped to 0 at EOF) is harmless there; libstdc++'s checked
// operator[] (_GLIBCXX_ASSERTIONS, which the host libstdc++ turns on for any
// unoptimized build, e.g. a -O0 debug or ASan native build) aborts on
// it. The native spelling forms the same address through data().
#ifdef HX_NATIVE
#define MEMSTREAM_AT(i) (mBuffer.data() + (i))
#else
#define MEMSTREAM_AT(i) (&mBuffer[i])
#endif

void MemStream::Flush() {}

bool MemStream::Fail() { return mFail; }

void MemStream::ReadImpl(void *data, int bytes) {
    if (mTell + bytes > mBuffer.size()) {
        bytes = mBuffer.size() - mTell;
        mFail = true;
    }
    memcpy(data, MEMSTREAM_AT(mTell), bytes);
    mTell += bytes;
}

void MemStream::SeekImpl(int offset, SeekType t) {
    int pos;

    switch (t) {
    case kSeekBegin:
        pos = offset;
        break;
    case kSeekCur:
        pos = mTell + offset;
        break;
    case kSeekEnd:
        pos = mBuffer.size() + offset;
        break;
    default:
        return;
    }

    if (pos < 0 || pos > mBuffer.size()) {
        mFail = true;
    } else {
        mTell = pos;
    }

    // case 0: validate offset, mFail = true or mTell = offset
    // case 1: offset += mTell, then case 0's logic
    // case 2: offset += mSize, then case 0's logic
}

void MemStream::Compact() {
    mBuffer.erase(mBuffer.begin(), mBuffer.begin() + mTell);
    mTell = 0;
}

MemStream::MemStream(bool b) : BinStream(b) {
    mBuffer.reserve(0x1000);

    // Initializer list wasn't used here for some reason
    mFail = false;
    mTell = 0;
}

void MemStream::WriteImpl(const void *data, int bytes) {
    int toReserve = mBuffer.capacity();
    while (mTell + bytes > toReserve)
        toReserve += toReserve;
    mBuffer.reserve(toReserve);
    if (mTell + bytes > mBuffer.size()) {
        mBuffer.resize(mTell + bytes);
    }
    memcpy(MEMSTREAM_AT(mTell), data, bytes);
    mTell += bytes;
}

void MemStream::WriteStream(BinStream &bs, int bytes) {
    int toReserve = mBuffer.capacity();
    while (mTell + bytes > toReserve)
        toReserve += toReserve;
    mBuffer.reserve(toReserve);
    if (mTell + bytes > mBuffer.size()) {
        mBuffer.resize(mTell + bytes);
    }
    bs.Read(MEMSTREAM_AT(mTell), bytes);
    mTell += bytes;
}
