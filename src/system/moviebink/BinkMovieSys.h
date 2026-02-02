#pragma once

#include "movie/MovieSys.h"

class CriticalSection;
class DataArray;
class DataNode;

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
    int mBinkCore0; // 0x14
    int mBinkCore1; // 0x18
};
