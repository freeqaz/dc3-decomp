#include "utl\NetCacheLoader.h"
#include "NetCacheMgr.h"
#include "os\Debug.h"
#include "os/FileCache.h"
#include "utl\FilePath.h"
#include "utl/Loader.h"
#include "utl\NetCacheMgr.h"
#include "utl\Str.h"

extern void BinkFree(void *);

NetCacheLoader::NetCacheLoader(FileCache *f, const String &s)
    : mState(kS_Nil), mCache(f), mRemotePath(s), mFileLoader(0), mFileLoaderBuffer(0),
      mNetLoader(0), mNetLoaderBuffer(0), mFailType(0) {
    if (TheNetCacheMgr->IsLocalFile(mRemotePath.c_str())) {
        SetState((NetCacheLoader::State)0);
    } else {
        SetState((NetCacheLoader::State)1);
    }
    MILO_ASSERT(mState != kS_Nil, 0x28);
    MILO_ASSERT(mCache, 0x2b);
}

NetCacheLoader::~NetCacheLoader() { SetState(kS_Nil); }

bool NetCacheLoader::IsLoaded() const { return mState == 3; }
bool NetCacheLoader::HasFailed() const { return mState == 4; }

bool NetCacheLoader::IsSafeToDelete() const {
    if (mNetLoader) {
        return mNetLoader->IsSafeToDelete();
    } else
        return true;
}

int NetCacheLoader::GetSize() {
    if (mNetLoader) {
        return mNetLoader->GetSize();
    } else if (mFileLoader) {
        return mFileLoader->GetSize();
    } else
        return 0;
}

char *NetCacheLoader::GetBuffer() {
    if (mNetLoader) {
        return mNetLoaderBuffer;
    } else if (mFileLoader) {
        return mFileLoaderBuffer;
    } else
        return nullptr;
}

void NetCacheLoader::SetState(NetCacheLoader::State state) {
    // Residual 97.95% canonical / 97.23% raw, 32 rows / 16 B.
    //
    // 28 of the 32 rows are ONE callee-saved naming swap, r28 <-> r29.  Both
    // sides build the identical register file (bl __savegprlr_24, r24 = the
    // FilePath-constructed EH flag, r25 = 0, r26 = &kAssertStr, r27 = the
    // "NetCacheLoader.cpp" literal, r30 = the state arg then reused for
    // strPath, r31 = frame); the image puts `this` in r29 and the &TheDebug
    // anchor in r28 (reusing r28 for the new FileLoader pointer at the end),
    // we put `this` in r28 and the anchor in r29 with the same reuse.  Same
    // shape, different names.
    //
    // The other 4 rows are the address of the FilePath temp at the FileLoader
    // call: the image RE-FORMS it (`addi r4, r31, 0x68` immediately before the
    // ctor call) where we copy the FilePath ctor's return (`mr r4, r3` right
    // after it), which also shifts `stw r25, 0x54(r1)` by two slots.
    if (mState == state)
        return;
    switch (mState) {
    case 0:
        if (state == 3)
            goto lab;
        goto cleanup;
    case 2:
        if (state != 3) {
            MILO_ASSERT(!mNetLoader || mNetLoader->IsSafeToDelete(), 0xc1);
            RELEASE(mNetLoader);
        }
        goto lab;
    case 3:
        MILO_ASSERT(!mNetLoader || mNetLoader->IsSafeToDelete(), 0xc7);
        RELEASE(mNetLoader);
        mNetLoaderBuffer = nullptr;
        break;
    }
cleanup:
    RELEASE(mFileLoader);
    delete mFileLoaderBuffer;
    mFileLoaderBuffer = nullptr;
lab:
    mState = state;
    if (state != 0) {
        if (state == 2) {
            MILO_ASSERT(!mNetLoader, 0xef);
            mNetLoader = NetLoader::Create(mRemotePath);
        }
    } else {
        MILO_ASSERT(!mFileLoader, 0xe3);
        const char *strPath = mRemotePath.c_str();
        MILO_ASSERT(TheNetCacheMgr->IsLocalFile(strPath), 0xe6);
        // The FilePath argument MUST stay an unnamed temporary.  REFUTED:
        // spelling it `FilePath path(strPath);` and passing the named local
        // drops the function 97.95% -> 90.6% -- the frame SHRINKS by 0x10 and
        // the whole callee-saved assignment reshuffles, because the named local
        // no longer needs the EH-tracked temp slot the image keeps at 0x68(r31)
        // with its r24 "constructed" flag.
        mFileLoader = new FileLoader(FilePath(strPath), strPath, kLoadFront, 0, false, true, nullptr, nullptr);
    }
}

void NetCacheLoader::WriteToCache() {
    if (!TheNetCacheMgr->IsReady()) {
        mFailType = 0;
        SetState((NetCacheLoader::State)4);
    } else {
        MILO_ASSERT(!mNetLoaderBuffer, 0x103);
        mNetLoaderBuffer = mNetLoader->DetachBuffer();
        mCache->Add(mRemotePath.c_str(), mNetLoaderBuffer, mNetLoader->GetSize());
    }
}

void NetCacheLoader::Poll() {
    switch (mState) {
    case 0: {
        MILO_ASSERT(mFileLoader, 0x4E);
        if (mFileLoader->IsLoaded()) {
            MILO_ASSERT(!mFileLoaderBuffer, 0x52);
            mFileLoaderBuffer = mFileLoader->GetBuffer(nullptr);
            SetState((NetCacheLoader::State)3);
        }
        break;
    }
    case 1: {
        SetState((NetCacheLoader::State)2);
        break;
    }
    case 2: {
        mNetLoader->PollLoading();
        if (mNetLoader->IsLoaded()) {
            WriteToCache();
            SetState((NetCacheLoader::State)3);
        } else if (mNetLoader->HasFailed()) {
            if (mNetLoader->GetFailType() == 1) {
                MILO_LOG(
                    "Failed to find file on server: %s\n", mNetLoader->GetRemotePath()
                );
                mFailType = 2;
            } else {
                MILO_NOTIFY(
                    "NetCacheLoader failed for file: %s", mNetLoader->GetRemotePath()
                );
                mFailType = 1;
            }
            SetState((NetCacheLoader::State)4);
        }
        break;
    }
    default:
        break;
    }
}

const char *NetCacheLoader::GetRemotePath() const { return mRemotePath.c_str(); }

NetCacheMgrFailType NetCacheLoader::GetFailType() const {
    return (NetCacheMgrFailType)mFailType;
}
