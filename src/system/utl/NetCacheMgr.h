#pragma once
#include "obj\Data.h"
#include "obj/Object.h"
#include "os/FileCache.h"
#include "utl\Cache.h"
#include "utl\NetCacheLoader.h"
#include "utl\NetLoader.h"
#include "utl\Str.h"
#include "utl\Symbol.h"
#include <list>

class NetCacheLoader;

enum NetCacheMgrFailType {
    kNCMFT_Unknown,
    kNCMFT_StoreServer,
    kNCMFT_ClientError,
    kNCMFT_NoEthernetCable,
    kNCMFT_Max
};

enum NetCacheMgrState {
    kNCMS_Load,
    kNCMS_Ready,
    kNCMS_UnloadWaitForWrite,
    kNCMS_UnloadUnmount,
    kNCMS_Failure,
    kNCMS_Max,
    kNCMS_Nil = -1
};

enum LoadState {
    kLS_None,
    kLS_Mount,
    kLS_Delete,
    kLS_ReMount,
    kLS_Resync
};

enum NetLoaderPos {
};

// w8-g 2026-09-15 -- BEHAVIOURAL LEAD, not closed.  The two NetCacheMgr EH
// funclets fn_827F1C80 and fn_827F1CF8 (40 B each, 99.80% normalized) each carry
// one charged row, and it names a destructor we never call:
//   image:  bl ??1?$pair@VString@@P6APAVLoader@@ABVFilePath@@W4LoaderPos@@@Z@stlpmtx_std@@QAA@XZ
//           i.e. ~pair<String, Loader *(*)(const FilePath &, LoaderPos)>,
//           at r31+0x80 (fn_827F1C80) and r31+0xa0 (fn_827F1CF8)
//   ours:   bl ??1NetLoaderRef@@QAA@XZ  at r31+0x90
// The parent frame also disagrees: 0x160 in the image, 0x150 in ours.  That is
// consistent with a container whose ELEMENT TYPE is wrong -- the image's is a
// std::pair of a String and a Loader-factory function pointer, ours is
// `std::list<NetLoaderRef> mNetLoaderRefs` (below).
//
// Both funclets are flagged UNVERIFIABLE_PAIRING (paired by byte signature, not
// by name), so the rows are not falsifiable in isolation, and the one question
// that IS falsifiable comes back NEGATIVE: our tree does emit that pair
// destructor -- `strings -a` finds it in build/373307D9/src/system/utl/Loader.obj
// (it is the Loader factory registry's map element), and the target has it in
// dozens of objects.  So this is most likely objdiff pairing our NetLoaderRef
// funclet against an unrelated Loader-registry funclet that happens to have the
// same masked bytes, NOT a missing symbol.  Left as a lead; do not spend a row
// budget on it without first matching the parent function.
struct NetLoaderRef {
    void Poll();
    bool NeedsToDownload();
    bool IsDownloading();
    bool IsLoadedOrFailed();
    bool IsSafeToDelete();
    void DeleteLoader();
    bool IsValid() const;
    NetLoaderRef &operator=(const NetLoaderRef &);

    String mName; // 0x0
    int mRefCount; // 0x8
    NetLoader *mNetLoader; // 0xc
    NetCacheLoader *mCacheLoader; // 0x10
};

class NetCacheMgr : public Hmx::Object {
public:
    // size 0x18
    struct ServerData {
        Symbol type; // 0x0
        bool local; // 0x4
        const char *server; // 0x8
        unsigned short port; // 0xc
        const char *root; // 0x10
        bool debug; // 0x14
        bool verifySSL; // 0x15
    };

    enum RefType {
    };

    enum CacheSize {
    };

    NetCacheMgr();
    // Hmx::Object
    virtual ~NetCacheMgr();
    virtual DataNode Handle(DataArray *, bool);
    virtual void Poll();

    unsigned int GetServiceId() const;
    NetCacheMgrFailType GetFailType() const;
    const char *GetXLSPFilter() const;
    bool IsUnloaded() const;
    bool IsReady() const;
    bool IsLocalFile(const char *) const;
    void DeleteNetLoader(NetLoader *);
    unsigned short GetPort() const;
    char const *GetServerRoot() const;
    bool IsServerLocal() const;
    bool IsDebug() const;
    Symbol CheatNextServer();
    void Unload();
    void Load(NetCacheMgr::CacheSize);
    void DeleteNetCacheLoader(NetCacheLoader *);
    NetLoader *AddNetLoader(const char *, NetLoaderPos);
    NetCacheLoader *AddNetCacheLoader(const char *, NetLoaderPos);

    bool GetHasFailed() const { return mHasFailed; }

private:
    void EnterLoadState();
    bool IsUnloadStateDone() const;
    void EnterUnloadState();
    NetCacheMgr::ServerData const &Server() const;

protected:
    virtual void LoadInit() {}
    virtual bool IsDoneLoading() const { return true; }
    virtual void ReadyInit() {}
    virtual void UnloadInit() {}
    virtual bool IsDoneUnloading() const { return true; }

    void SetFail(NetCacheMgrFailType);
    void SetState(NetCacheMgrState);
    void OnInit(DataArray *);
    void PollLoaders();
    void DebugClearCache();
    NetLoaderRef *AddLoaderRef(const char *, RefType, NetLoaderPos);

    int mState;
    bool mHasFailed;
    NetCacheMgrFailType mFailType; // 0x34
    String mXLSPFilter; // 0x38
    unsigned int mServiceId; // 0x40
    bool mServiceIDObtained;
    std::list<ServerData> mServers; // 0x48
    Symbol mServerType; // 0x50
    int mLoadCacheSize; // 0x54
    FileCache *mCache; // 0x58
    std::list<NetLoaderRef> mNetLoaderRefs; // 0x5c
    int mLoadCount; // 0x64
};

void NetCacheMgrTerminate();
void NetCacheMgrInit();

extern NetCacheMgr *TheNetCacheMgr;
