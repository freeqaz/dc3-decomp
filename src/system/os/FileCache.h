#pragma once
#include "obj/Object.h"
#include "os\File.h"
#include "stl\_vector.h"
#include "utl\FilePath.h"
#include "utl/Loader.h"
#include "utl\MemMgr.h"
#include "utl\PoolAlloc.h"

class FileCacheHelper {
public:
    virtual ~FileCacheHelper() {}
    // Pure: ??_7FileCacheHelper@@6B@ in build/373307D9/asm/system/rndobj/Utl.s
    // is exactly { ??_GFileCacheHelper, _purecall }, and
    // ?CacheFile@FileCacheHelper@@... appears nowhere in ham_xbox_r.map.
    // Both subclasses (ResourceFileCacheHelper, WavFileCacheHelper) override it.
    virtual const char *CacheFile(const char *) = 0;
};

class FileCacheEntry;

class FileCache {
public:
    FileCache(int, LoaderPos, bool, bool);
    ~FileCache();

    bool DoneCaching();
    bool FileCached(char const *);
    void StartSet(int);
    void Clear();
    void PollUntilLoaded();
    void Add(FilePath const &, int, FilePath const &);
    void Add(FilePath const &, char *, int);
    void EndSet();
    void SetSize(int);

    static void Init();
    static void Terminate();
    static void PollAll();
    static File *GetFileAll(char const *);
    static void RegisterResourceCacheHelper(class FileCacheHelper *);
    static void RegisterWavCacheHelper(class FileCacheHelper *);

    MEM_OVERLOAD(FileCache, 0x21);

protected:
    int mMaxSize; // 0x0
    bool mTryClear; // 0x4
    std::vector<FileCacheEntry *> mEntries; // 0x8
    LoaderPos mLoaderPos;
    bool unk18;
    bool unk19;

    static FileCacheHelper *sResourceCacheHelper;
    static FileCacheHelper *sWavCacheHelper;

    File *GetFile(char const *);
    int CurSize() const;
    void DumpOverSize(int);
    void Poll();
};
