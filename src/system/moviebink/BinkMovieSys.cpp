#include "moviebink/BinkMovieSys.h"
#include "movie/MovieSys.h"
#include "moviebink/BinkMovieImpl.h"
#include "obj/Data.h"
#include "obj/DataFunc.h"
#include "os/CritSec.h"
#include "os/Debug.h"
#include "os/System.h"
#include "utl/MemMgr.h"
#include "utl/Symbol.h"

#ifdef HX_FFMPEG
#include "platform/FFmpegMovieImpl.h"
#endif

BinkMovieSys gBinkMovieSys;

extern void BinkSetMemory(void *(*)(int), void (*)(void *));
extern int BinkStartAsyncThread(int, int);
extern void *RadAlloc(int);

BinkMovieSys::BinkMovieSys()
    : MovieSys(), mCriticalSection(0),
      mBinkCore0(-1), mBinkCore1(-1), mMovieCount(0) {
    mHasAsyncThread = true;
    mNumAsyncThreads = 1;
}

BinkMovieSys::~BinkMovieSys() {
    delete mCriticalSection;
    mCriticalSection = 0;
}

DataNode BinkMovieSys::OnMovieSetTrack(DataArray *) {
    return DataNode();
}

void BinkMovieSys::Init() {
    bool wasInit = isInitalized;

    MovieSys::Init();

    if (!isInitalized) {
        TheDebug.Fail("IsInitialized", nullptr);
    }

    if (mCriticalSection == nullptr) {
#ifdef HX_NATIVE
        void *ptr = MemAlloc(sizeof(CriticalSection), __FILE__, __LINE__, "CriticalSection", 0);
#else
        void *ptr = MemAlloc(0x20, __FILE__, __LINE__, "CriticalSection", 0);
#endif
                if (ptr) {
            mCriticalSection = new (ptr) CriticalSection();
        } else {
            mCriticalSection = nullptr;
        }
    }

    CriticalSection *sec = mCriticalSection;
    if (sec != nullptr) {
        sec->Enter();
    }

    DataArray *cfg = SystemConfig(Symbol("movie"));
    cfg->FindData(Symbol("bink_core0"), mBinkCore0, true);
    cfg->FindData(Symbol("bink_core1"), mBinkCore1, true);

    if (!wasInit) {
#ifndef HX_FFMPEG
        BinkSetMemory(RadAlloc, operator delete);
        PlatformInit();

        if (mHasAsyncThread && (BinkStartAsyncThread(mBinkCore0, 0) == 0 || BinkStartAsyncThread(mBinkCore1, 0) == 0)) {
            TheDebug.Fail("Error starting bink async thread", nullptr);
        }
#endif
    }

    DataRegisterFunc(Symbol("set_bink_track"), OnMovieSetTrack);

    if (sec != nullptr) {
        sec->Exit();
    }
}

void BinkMovieSys::Terminate() {
    CriticalSection *cs = mCriticalSection;
    if (cs) {
        cs->Enter();
    }

    while (mMovies.size() > 0) {
        mMovies.back()->Terminate();
    }

    if (cs) {
        cs->Exit();
    }

    delete mCriticalSection;
    mCriticalSection = 0;

    MovieSys::Terminate();
}

MovieImpl* BinkMovieSys::CreateMovieImpl() {
#ifdef HX_FFMPEG
    return new FFmpegMovieImpl();
#else
    return new BinkMovieImpl();
#endif
}
