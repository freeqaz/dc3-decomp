#include "utl/NetCacheMgr_Xbox.h"
#include "os/Debug.h"
#include "os/PlatformMgr.h"
#include "utl/NetCacheMgr.h"

NetCacheMgrXbox::NetCacheMgrXbox() : mDoneLoading(false) {}
NetCacheMgrXbox::~NetCacheMgrXbox() {}

void NetCacheMgrXbox::Poll() {
    NetCacheMgr::Poll();
    mConnection.Poll();
    if (unk2c < 2U) {
        if (IsServerLocal()) {
            mDoneLoading = true;
        } else {
            if (!mDoneLoading && mConnection.GetState() == 3) {
                mDoneLoading = true;
            }
            if (!unk30 && mConnection.GetState() == 4) {
                u32 r3 = (u32)ThePlatformMgr.IsEthernetCableConnected();
                u32 r11 = -(s32)r3;
                SetFail((NetCacheMgrFailType)(3 + (r11 >> 31)));
            }
        }
    }
}

void NetCacheMgrXbox::LoadInit() {
    mDoneLoading = false;
    if (!IsServerLocal()) {
        mConnection.Connect(
            TheNetCacheMgr->GetXLSPFilter(), TheNetCacheMgr->GetServiceId()
        );
    }
}

void NetCacheMgrXbox::UnloadInit() {
    if (!IsServerLocal()) {
        mConnection.Disconnect();
    }
}

unsigned int NetCacheMgrXbox::GetIP() {
    MILO_ASSERT(!IsServerLocal(), 0x48);
    if (mConnection.GetState() == 4) {
        MILO_NOTIFY("NetCacheMgr Error: XLSPConnection Failed");
        return 0;
    } else {
        return mConnection.GetServiceIP();
    }
}
