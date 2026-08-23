#pragma once
#include "utl\NetCacheMgr.h"
#include "net\XLSPConnection.h"

class NetCacheMgrXbox : public NetCacheMgr {
public:
    NetCacheMgrXbox();
    virtual ~NetCacheMgrXbox();
    /* No Handle override: ??_7NetCacheMgrXbox@@6B@ slot 0x14 points straight at
     * NetCacheMgr::Handle in the target, so the forwarding wrapper that used to
     * live here was a function the original binary does not contain. */
    virtual void Poll();

    unsigned int GetIP();

protected:
    virtual void LoadInit();
    virtual bool IsDoneLoading() const { return mDoneLoading; }
    virtual void UnloadInit();
    virtual bool IsDoneUnloading() const;

    bool mDoneLoading; // 0x68
    int unk6c;
    XLSPConnection mConnection; // 0x70
};
