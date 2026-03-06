#include "BinkMovieImpl.h"
#include "obj/Dir.h"
#include "obj/Msg.h"
#include "os/Debug.h"
#include "os/OSFuncs.h"
#include "os/ThreadCall.h"
#include "synth/BinkReader.h"
#include "utl/MakeString.h"
#include <cstring>
#ifndef HX_NATIVE
#include <stl/_vector.h>
#include <stl/_algobase.h>
#endif
#include <algorithm>

std::vector<BinkMovieImpl *> BinkMovieImpl::sActiveMovies;

extern void *kNoHandle;
extern "C" int BinkWait(BINK *);
extern "C" int BinkShouldSkip(BINK *);

#ifndef HX_NATIVE
// Explicit template instantiation for vector<BINK*, StlNodeAlloc<BINK*>>
namespace stlpmtx_std {

template class vector<BINK*, StlNodeAlloc<BINK*>>;

} // namespace stlpmtx_std
#endif

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
    for (int j = 0; (unsigned int)j < 2; j++) {
        for (int i = 0; (unsigned int)i < 2; i++) {
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
      mWidth(0), mHeight(0), mReady(false), unk25(false), mKeepPlaying(false),
      mFrame(0), mNumFrames(0), mMsPerFrame(0), mPaused(false),
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
    mBinkVolume = 0x8000;
    mBufferOffset = 0;
    mThreadId = gMainThreadID;

    if (mThreadId != (unsigned long)GetCurrentThreadId()) {
        if (mThreadId == (unsigned long)-1 && MainThread()) {
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

bool BinkMovieImpl::Paused() const { return mPaused; }

bool BinkMovieImpl::SetPaused(bool paused) {
    mPaused = paused;
    return mPaused;
}

BinkMovieImpl::~BinkMovieImpl() {
    if (mThreadId != (unsigned long)GetCurrentThreadId()) {
        if (mThreadId == (unsigned long)-1 && MainThread()) {
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

bool BinkMovieImpl::Poll() {
    if (mThreadId != (unsigned long)GetCurrentThreadId()) {
        if (mThreadId == (unsigned long)-1 && MainThread()) {
            goto thread_ok;
        }
        TheDebug.Fail(
            MakeString(
                "%s called in the wrong thread (expected %d, cur thread is %d)",
                "BinkMovieImpl::Poll",
                mThreadId,
                GetCurrentThreadId()
            ),
            0
        );
    }
thread_ok:
    if (CheckOpen(true)) {
        return true;
    }

    if (!mBink) goto poll_done;
    if (!(unsigned int)mBufferOffset) goto poll_done;

    if (!(unsigned int)mWidth) {
        float splitMs = mPlayTimer.SplitMs();
        mPlayTimer.Restart();
        if (splitMs > 49.0f) {
            float binkMs = mLoadTimer.SplitMs();
            char *msg = (char *)MakeString(
                "GLITCH: %g ms (%g ms bink), %s\n", splitMs, binkMs, mFilename
            );

            static DataNode &notify_level = DataVariable("notify_level");
            if (notify_level.Int() == 0) {
                TheDebug << MakeString("%s\n", msg);
            } else {
                static Hmx::Object *cheat_display =
                    ObjectDir::Main()->Find<Hmx::Object>("cheat_display", true);
                static Message show("show", DataNode(0));
                show[0] = DataNode(msg);
                cheat_display->Handle(show, false);
            }
        }
    }

    DiscContentionCheck(0);
    mLoadTimer.Restart();

    if (BinkWait(mBink) == 0) {
        DoFrame();
        while (BinkShouldSkip(mBink)) {
            TheDebug << FormatString("skipped bink frame!\n").Str();
            DoFrame();
        }
    }

    mLoadTimer.Stop();

    if (mBink->BinkError != 0) goto poll_done;

    if (mKeepPlaying) {
        return true;
    }

    if (mBink->FrameNum != mBink->Frames) {
        return true;
    }

poll_done:
    return false;
}
