#pragma once

#include "movie/MovieSys.h"
#include <list>

class CriticalSection;
class DataArray;
class DataNode;
class BinkMovieImpl;

class BinkMovieSys : public MovieSys {
public:
    BinkMovieSys();
    virtual ~BinkMovieSys();

    virtual void Init();
    virtual void Terminate();
    virtual MovieImpl *CreateMovieImpl();

    virtual void PlatformInit();

    static DataNode OnMovieSetTrack(DataArray *);

private:
    CriticalSection *mCriticalSection; // 0x8
    bool mHasAsyncThread; // 0xC
    char _padD[7]; // 0xD-0x13
    int mBinkCore0; // 0x14
    int mBinkCore1; // 0x18
    char _pad1C[4]; // 0x1C-0x1F
    std::list<BinkMovieImpl*> mMovies; // 0x20
};
