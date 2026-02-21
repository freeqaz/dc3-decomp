#include "BinkMovieImpl.h"
#include "os/Debug.h"
#include "os/OSFuncs.h"
#include "os/ThreadCall.h"
#include "utl/MakeString.h"
#include <cstring>
#include <stl/_vector.h>
#include <stl/_algobase.h>
#include <algorithm>

extern void *kNoHandle;

// Explicit template instantiation for vector<BINK*, StlNodeAlloc<BINK*>>
namespace stlpmtx_std {

template class vector<BINK*, StlNodeAlloc<BINK*>>;

} // namespace stlpmtx_std

MovieInternalBuffers::MovieInternalBuffers() {
    // Zero out padding region (0x44-0xBC)
    memset(reinterpret_cast<char*>(this) + 0x44, 0, 0x78);

    // Zero out all pointer fields
    for (int i = 0; i < 17; i++) {
        mBinks[i] = nullptr;
    }
    mUnknown = nullptr;
}

MovieInternalBuffers::~MovieInternalBuffers() {
    delete mBinks[16];
    mBinks[16] = nullptr;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 2; i++) {
            int base = j * 2 + i;
            delete mBinks[base];
            mBinks[base] = nullptr;
            delete mBinks[base + 4];
            mBinks[base + 4] = nullptr;
            delete mBinks[base + 8];
            mBinks[base + 8] = nullptr;
            delete mBinks[base + 12];
            mBinks[base + 12] = nullptr;
        }
    }
}

BinkMovieImpl::BinkMovieImpl()
    : mLoader(0), mLoader2(0), mFilename(), mBink(0), mLoop(false),
      mWidth(0), mHeight(0), mPaused(false),
      mFrame(0), mNumFrames(0), mMsPerFrame(0), mReady(false),
      mPlayTimer(), mLoadTimer(),
      mVolume(0), mVolumeTarget(0), mHandle(kNoHandle),
      mOpen(false)
{
    mTreeCount = 0;
    mTreeColor = 0;
    mTreeParent = 0;
    mTreeLeft = &mTreeColor;
    mTreeRight = &mTreeColor;
    mEndianSwapped = false;
    mHasAudio = false;
    mMaxBuffer = 0x8000;
    mBufferOffset = 0;
    mThreadId = gMainThreadID;

    if (mThreadId != (unsigned int)GetCurrentThreadId()) {
        if (mThreadId == (unsigned int)-1 && MainThread()) {
            return;
        }
        TheDebug.Fail(
            MakeString(
                "%s called in the wrong thread (expected %d, cur thread is %d)",
                "BinkMovieImpl::BinkMovieImpl",
                mThreadId,
                GetCurrentThreadId()
            ),
            0
        );
    }
}

BinkMovieImpl::~BinkMovieImpl() {
    if (mThreadId != (unsigned int)GetCurrentThreadId()) {
        if (mThreadId == (unsigned int)-1 && MainThread()) {
            goto done;
        }
        TheDebug.Fail(
            MakeString(
                "%s called in the wrong thread (expected %d, cur thread is %d)",
                "BinkMovieImpl::~BinkMovieImpl",
                mThreadId,
                GetCurrentThreadId()
            ),
            0
        );
    }
done:
    End();
}
