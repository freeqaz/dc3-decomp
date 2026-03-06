#include "net/XLSPConnection.h"
#include "utl/MemMgr.h"
#include "xdk/XAPILIB.h"
#include "xdk/XNET.h"
#include "xdk/XONLINE.h"
#include <utility>

const int XLSPConnection::kTitleServerEnumMaxCount = 8;
std::map<unsigned long, int> XLSPConnection::mXLSPRefCountMap;

XLSPConnection::XLSPConnection()
    : mState((State)-1), mConnectionRequest(0), mServiceId(0), mEnumHandle(INVALID_HANDLE_VALUE), mEnumBuffer(0),
      mEnumBufferSize(0) {
    memset(&mXOverlapped, 0, sizeof(XOVERLAPPED));
    unk44 = 0;
    mReconnectTimer.Reset();
    SetState((State)0);
}

XLSPConnection::~XLSPConnection() { SetState((State)-1); }

int XLSPConnection::ThreadStart() {
    if (mState != 5) {
        MILO_FAIL("Unhandled state %d in ThreadStart", mState);
    } else {
        XCancelOverlapped(&mXOverlapped);
    }
    return 0;
}

void XLSPConnection::ThreadDone(int i1) {
    if (mState != 5) {
        MILO_FAIL("Unhandled state %d in ThreadStart", mState);
    } else {
        memset(&mXOverlapped, 0, sizeof(XOVERLAPPED));
        CloseHandle(mEnumHandle);
        mEnumBufferSize = 0;
        mEnumHandle = INVALID_HANDLE_VALUE;
        if (mEnumBuffer) {
            MemFree(mEnumBuffer, __FILE__, 299);
            mEnumBuffer = nullptr;
        }
        SetState((State)0);
    }
}

void XLSPConnection::Connect(const char *cc, unsigned int ui) {
    mServerInfo = cc;
    mServiceId = ui;
    if (mConnectionRequest != 3) {
        mConnectionRequest = 3;
    }
    if (mState == 0) {
        SetState((State)1);
    }
}

unsigned int XLSPConnection::GetServiceIP() { return unk44; }

void XLSPConnection::Disconnect() {
    if (mConnectionRequest != 0) {
        mConnectionRequest = 0;
    }
    if (mState > 0 && mState <= 4) {
        SetState((State)5);
    }
}

void XLSPConnection::StartEnumeration() {
    DWORD res = XTitleServerCreateEnumerator(mServerInfo.c_str(), 8, &mEnumBufferSize, &mEnumHandle);
    if (res != ERROR_SUCCESS) {
        MILO_NOTIFY("XTitleServerCreateEnumerator failed with error %d", res);
        SetState((State)4);
    } else {
        mEnumBuffer = _MemAllocTemp(mEnumBufferSize, __FILE__, 0x1CB, "XLSPConnection", 0);
        res = XEnumerate(mEnumHandle, mEnumBuffer, mEnumBufferSize, nullptr, &mXOverlapped);
        if (res != ERROR_IO_PENDING) {
            MILO_NOTIFY("XEnumerate failed with error %d", res);
            SetState((State)4);
        }
    }
}

bool XLSPConnection::SecureDisconnect(in_addr a) {
    bool ret = true;
#ifdef HX_NATIVE
    auto it = mXLSPRefCountMap.find(a.s_addr);
#else
    auto it = mXLSPRefCountMap.find(a.s_un.s_addr);
#endif
    if (it != mXLSPRefCountMap.end()) {
        it->second--;
        if (it->second == 0) {
            mXLSPRefCountMap.erase(it);
            if (XNetGetConnectStatus(a) != 3) {
                XNetUnregisterInAddr(a);
            }
        }
    } else {
        ret = false;
        MILO_NOTIFY("XLSPConnection::SecureDisconnect() - connection not found!");
    }
    return ret;
}

int XLSPConnection::StartGatewayConnection(in_addr a) {
    int ret;
#ifdef HX_NATIVE
    auto it = mXLSPRefCountMap.find(a.s_addr);
#else
    auto it = mXLSPRefCountMap.find(a.s_un.s_addr);
#endif
    if (it != mXLSPRefCountMap.end()) {
        ret = 0;
        it->second++;
    } else {
        ret = XNetConnect(a);
        if (ret == 0) {
#ifdef HX_NATIVE
            mXLSPRefCountMap.insert(std::make_pair(a.s_addr, 1));
#else
            mXLSPRefCountMap.insert(std::make_pair(a.s_un.s_addr, 1));
#endif
        } else {
#ifdef HX_NATIVE
            unsigned char *bytes = (unsigned char *)&a.s_addr;
            MILO_NOTIFY(
                "XNetConnect(%d.%d.%d.%d) failed with %d",
                bytes[0],
                bytes[1],
                bytes[2],
                bytes[3],
                ret
            );
#else
            MILO_NOTIFY(
                "XNetConnect(%d.%d.%d.%d) failed with %d",
                a.s_un.s_un_b.s_b1,
                a.s_un.s_un_b.s_b2,
                a.s_un.s_un_b.s_b3,
                a.s_un.s_un_b.s_b4,
                ret
            );
#endif
        }
    }
    return ret;
}
