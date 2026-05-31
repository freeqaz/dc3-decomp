#include "moviebink/BinkMovieSys.h"
#include "moviebink/BinkMovieImpl.h"
#include "binkxenon/bink.h"
#include "movie/MovieSys.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/System.h"
#include "utl/MemMgr.h"

#ifdef HX_FFMPEG
#include "platform/FFmpegMovieImpl.h"
#elif defined(__EMSCRIPTEN__)
#include "platform/WebMovieImpl.h"
#endif

BinkMovieSys gBinkMovieSys;
BinkMovieSys &TheBinkMovieSys = gBinkMovieSys;

namespace {
    void *RadAlloc(unsigned int size) {
        return MemAlloc(size, __FILE__, 0x28, "Movie", 0x80);
    }
    void RadFree(void *ptr) { MemFree(ptr); }
}

BinkMovieSys::BinkMovieSys()
    : MovieSys(), mCriticalSection(0),
      mBinkCore0(-1), mBinkCore1(-1), mTrack(0) {
    mHasAsyncThread = true;
    mNumAsyncThreads = 1;
}

BinkMovieSys::~BinkMovieSys() {
    delete mCriticalSection;
    mCriticalSection = 0;
}

void BinkMovieSys::Init() {
    bool initial = IsInitialized();
    MovieSys::Init();
    MILO_ASSERT(IsInitialized(), 0x67);
    if (!mCriticalSection) {
        mCriticalSection = new CriticalSection();
    }
    CritSecTracker tracker(mCriticalSection);
    DataArray *cfg = SystemConfig("movie");
    cfg->FindData("bink_core0", mBinkCore0);
    cfg->FindData("bink_core1", mBinkCore1);
    if (!initial) {
#ifndef HX_FFMPEG
        BinkSetMemory(RadAlloc, RadFree);
        PlatformInit();
        if (mHasAsyncThread) {
            MILO_ASSERT_FMT(
                BinkStartAsyncThread(mBinkCore0, nullptr)
                    && BinkStartAsyncThread(mBinkCore1, nullptr),
                "Error starting bink async thread.\n"
            );
        }
#endif
    }
    DataRegisterFunc("set_bink_track", OnMovieSetTrack);
}

void BinkMovieSys::Terminate() {
    {
        CritSecTracker tracker(mCriticalSection);
        while (mMovies.size()) {
            mMovies.back()->Terminate();
        }
    }
    RELEASE(mCriticalSection);
    MovieSys::Terminate();
}

DataNode BinkMovieSys::OnMovieSetTrack(DataArray *a) {
    TheBinkMovieSys.mTrack = a->Int(1);
    return 0;
}

MovieImpl* BinkMovieSys::CreateMovieImpl() {
#ifdef HX_FFMPEG
    return new FFmpegMovieImpl();
#elif defined(__EMSCRIPTEN__)
    return new WebMovieImpl();
#else
    return new BinkMovieImpl();
#endif
}

// Native stub implementations (no Bink SDK on desktop native)
#if defined(HX_NATIVE) && !defined(__EMSCRIPTEN__)

struct BINKTRACK;

void BinkMovieSys::PlatformInit() {}

void BinkClose(BINK *) {}
void BinkCloseTrack(BINKTRACK *) {}
unsigned int BinkGetTrackData(BINKTRACK *, void *) { return 0; }
void BinkNextFrame(BINK *) {}
BINKTRACK *BinkOpenTrack(BINK *, unsigned char) { return nullptr; }

#endif // HX_NATIVE && !__EMSCRIPTEN__
