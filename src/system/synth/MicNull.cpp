#include "synth/MicNull.h"
#include "math/Rand.h"

Rand sRand(0x1bca7);

short *MicNull::GetRecentBuf(int &size) {
    int sz = 0x600;
    size = sz;
    short *src = mBuf;
    short *dst = mRecentBuf;
    memcpy(dst, src, 0xc00);
    return dst;
}

int MicNull::GetSampleRate() const { return 48000; }

MicNull::MicNull() {
    for (int i = 0; i < 10000U; i++) {
        mBuf[i] = sRand.Int(-32000, 32000);
    }
}
