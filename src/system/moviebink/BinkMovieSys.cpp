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

extern void BinkSetMemory(void *(*)(int), void (*)(void *));
extern int BinkStartAsyncThread(int, int);
extern void *RadAlloc(int);

BinkMovieSys::BinkMovieSys()
    : MovieSys(), mCriticalSection(nullptr), mHasAsyncThread(false),
      mBinkCore0(0), mBinkCore1(0) {
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
        void *ptr = MemAlloc(0x20, __FILE__, __LINE__, "CriticalSection", 0);
        mCriticalSection = ptr ? new (ptr) CriticalSection() : nullptr;
    }

    CriticalSection *sec = mCriticalSection;
    if (sec != nullptr) {
        sec->Enter();
    }

    DataArray *cfg = SystemConfig(Symbol("movie"));
    cfg->FindData(Symbol("bink_core0"), mBinkCore0, true);
    cfg->FindData(Symbol("bink_core1"), mBinkCore1, true);

    if (!wasInit) {
        BinkSetMemory(RadAlloc, operator delete);
        PlatformInit();

        if (mHasAsyncThread && (BinkStartAsyncThread(mBinkCore0, 0) == 0 || BinkStartAsyncThread(mBinkCore1, 0) == 0)) {
            TheDebug.Fail("Error starting bink async thread", nullptr);
        }
    }

    DataRegisterFunc(Symbol("set_bink_track"), OnMovieSetTrack);

    if (sec != nullptr) {
        sec->Exit();
    }
}

void BinkMovieSys::Terminate() {
    MovieSys::Terminate();
}

MovieImpl* BinkMovieSys::CreateMovieImpl() {
    return new MovieImpl();
}

void BinkMovieSys::PlatformInit() {
    // Implemented in BinkMovieSys_Xbox.cpp
}
