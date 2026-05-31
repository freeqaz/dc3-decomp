#pragma once

#include "movie/MovieSys.h"
#include "os/CritSec.h"
#include <list>

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

    void PlatformInit();
    void PlatformStoreCache(void *, unsigned int);

    bool GetUnkC() const { return mHasAsyncThread; }
    int GetUnk10() const { return mNumAsyncThreads; }
    int Core0() const { return mBinkCore0; }
    int Core1() const { return mBinkCore1; }
    int Track() const { return mTrack; }
    void AddMovie(BinkMovieImpl *movie) { mMovies.push_back(movie); }
    void RemoveMovie(BinkMovieImpl *movie) { mMovies.remove(movie); }

private:
    static DataNode OnMovieSetTrack(DataArray *);

    CriticalSection *mCriticalSection; // 0x8
    bool mHasAsyncThread; // 0xC
    int mNumAsyncThreads; // 0x10
    int mBinkCore0; // 0x14
    int mBinkCore1; // 0x18
    int mTrack; // 0x1C
    std::list<BinkMovieImpl*> mMovies; // 0x20
};

extern BinkMovieSys &TheBinkMovieSys;
