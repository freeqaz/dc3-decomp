#include "os/PlatformMgr.h"
#include "os/OnlineID.h"
#include "system/utl/GlitchFinder.h"
#include "xdk/XAPILIB.h"
#include "xdk/XMP.h"
#include "xdk/XNET.h"
#include "xdk/NUI.h"
#include "xdk/xapilibi/winerror.h"
#include "xdk/xapilibi/xbox.h"

// Forward declarations for JSON writing functions
extern int XJSONBeginArray(void*);
extern int XJSONEndArray(void*);
extern int XJSONWriteNullValue(void*);
extern int XJSONWriteNumberValue(void*, double);
extern int XJSONWriteStringValue(void*, const unsigned char*, int);

// Forward declarations for merged functions
extern void* merged_DataArrayNode(void*, int);
extern void* merged_82610090(const void*, unsigned int*);

namespace {
    int mSigninSameGuest;
    XUID mXuidCache[4];
    int gNumSmartGlassClients;
    void *mFriendsEnum;
    void *mFriendsBuffer;
    Hmx::Object *mFriendsCallback;
    void *mFriendsAsync;
    void *mListener;
    unsigned int mPathLen;
    unsigned int mListSize;
    unsigned int mUserID;
    unsigned int mResult;
}

PlatformMgr::PlatformMgr() {
    mHasXSocialPhotoPost = false;
    mHasXSocialLinkPost = false;
    unk4c = 0;
    mSigninMask = 0;
    mSigninChangeMask = 0;
    mGuideShowing = true;
    mConfirmCancelSwapped = false;
    mConnected = false;
    mScreenSaver = false;
    mRegion = kRegionNone;
    mDiskError = kNoDiskError;

    mFriendsEnum = 0;
    mListSize = 0;
    mFriendsBuffer = 0;
    mFriendsCallback = 0;
    mPathLen = 0x200;
    mFriendsAsync = 0;
    mListener = 0;
    mUserID = -1;
    mResult = 0;
    mSigninSameGuest = 0;

    mJobMgr = new JobMgr(this);
}

bool PlatformMgr::IsEthernetCableConnected() { return XNetGetEthernetLinkStatus() != 0; }

void PlatformMgr::UpdateSigninState() {
    XUID oldCache[4] = { mXuidCache[0], mXuidCache[1], mXuidCache[2], mXuidCache[3] };
    int i;
    mSigninSameGuest = 0;
    mSigninMask = 0;
    for (i = 0; i < 4; i++) {
        if (XUserGetSigninState(i) != 0) {
            XUSER_SIGNIN_INFO info = {};
            mSigninMask |= (1 << i);
            XUserGetSigninInfo(i, 2, &info);
            XUserGetXUID(i, &info.xuid);
            mXuidCache[i] = info.xuid;
        } else {
            mXuidCache[i] = 0;
        }
        if (oldCache[i] != mXuidCache[i]) {
            mSigninChangeMask |= (1 << i);
            if (((oldCache[i] ^ mXuidCache[i]) & 0xff3fffffffffffff) == 0) {
                mSigninSameGuest |= (1 << i);
            }
        }
    }
}

bool PlatformMgr::HasCreatedContentPrivilege() const {
    bool ret = true;
    for (int i = 0; i < 4; i++) {
        int bptr = 0;
        if ((XUserCheckPrivilege(i, XPRIVILEGE_USER_CREATED_CONTENT, &bptr) == 0)
            && (XUserCheckPrivilege(i, XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY, &bptr) == 0)) {
            ret = ret && (bptr != 0);
        }
    }
    return ret;
}

bool PlatformMgr::HasKinectSharePrvilege() const {
    int bptr = 0;
    if (XUserCheckPrivilege(0xFF, XPRIVILEGE_SHARE_CONTENT_OUTSIDE_LIVE, &bptr) == 0) {
        return true;
    } else if (bptr == 0) {
        bool ret = bptr;
        return ret;
    }
}

bool PlatformMgr::IsSmartGlassConnected() { return gNumSmartGlassClients > 0; }

void PlatformMgr::SetPadContext(int padNum, int i2, int i3) const {
    if (padNum != -1 && ThePlatformMgr.IsSignedIn(padNum)) {
        XUserSetContext(padNum, i2, i3);
    }
}

void PlatformMgr::SetPadPresence(int padNum, int i2) const {
    if (padNum != -1 && ThePlatformMgr.IsSignedIn(padNum)) {
        XUserSetContext(padNum, 0x8001, i2);
    }
}

void PlatformMgr::ShowFriendsUI(int padNum) {
    unsigned long ul;

    if (IsSignedIn(padNum)) {
        if (sXShowCallback(ul)) {
            XShowNuiFriendsUI(ul, padNum);
        } else {
            XShowFriendsUI(padNum);
        }
    }
}

void PlatformMgr::SetBackgroundDownloadPriority(bool b1) {
    XBackgroundDownloadSetMode((XBACKGROUND_DOWNLOAD_MODE)(b1 + 1));
}

// int __cdecl ShowControllerRequiredUIThreaded(void)

bool PlatformMgr::ShowPartyUI(int padNum) {
    unsigned long ul;
    unsigned long ret = 1;

    if (IsSignedIn(padNum)) {
        if (sXShowCallback(ul)) {
            ret = XShowNuiPartyUI(ul, padNum);
        } else {
            ret = XShowPartyUI(padNum);
        }
    }

    return ret == 0;
}

bool PlatformMgr::ShowFitnessBodyProfileUI(int padNum) {
    unsigned long ul;
    unsigned long ret = 1;

    if (IsSignedIn(padNum)) {
        if (sXShowCallback(ul)) {
            ret = XShowNuiFitnessBodyProfileUI(ul, padNum);
        } else {
            ret = XShowFitnessBodyProfileUI(padNum);
        }
    }

    return ret == 0;
}

void PlatformMgr::EnableXMP() { XMPRestoreBackgroundMusic(); }
void PlatformMgr::DisableXMP() { XMPOverrideBackgroundMusic(); }

void PlatformMgr::SetScreenSaver(bool b1) {
    mScreenSaver = b1;
    XEnableScreenSaver(b1);
}

bool PlatformMgr::IsSignedIntoLive(int padNum) const {
    MILO_ASSERT(padNum >= 0, 0x671);

    if (!IsSignedIn(padNum)) {
        return false;
    } else {
        return (XUserGetSigninState(padNum) == eXUserSigninState_SignedInToLive);
    }
}

bool PlatformMgr::IsPadAGuest(int padNum) const {
    XUSER_SIGNIN_INFO signinInfo;

    DWORD ret = XUserGetSigninInfo(padNum, 0, &signinInfo);

    if (ret == ERROR_NO_SUCH_USER) {
        return IsSignedIn(padNum);
    } else {
        MILO_ASSERT(ret != ERROR_SUCCESS, 0x929);

        return signinInfo.dwInfoFlags >> 1 & 1;
    }
}

void PlatformMgr::ShowOfferUI(int padNum) {
    unsigned long ul;
    unsigned long ret;

    if (IsSignedIn(padNum)) {
        if (sXShowCallback(ul)) {
            ret = XShowNuiMarketplaceUI(
                ul, padNum, XSHOWMARKETPLACEUI_ENTRYPOINT_CONTENTLIST_BACKGROUND, 0, -1
            );
        } else {
            ret = XShowMarketplaceUI(
                padNum, XSHOWMARKETPLACEUI_ENTRYPOINT_CONTENTLIST_BACKGROUND, 0, -1
            );
        }

        if (ret != ERROR_SUCCESS) {
            MILO_NOTIFY("XShowMarketplaceUI failed (0x%x)", ret);
        }
    }
}

DWORD PlatformMgr::ShowDeviceSelectorUI(
    DWORD userIndex,
    DWORD contentType,
    DWORD contentFlags,
    ULARGE_INTEGER bytesRequested,
    DWORD *deviceID,
    XOVERLAPPED *overlapped
) {
    unsigned long ul;
    unsigned long ret;

    if (sXShowCallback(ul)) {
        ret = XShowNuiDeviceSelectorUI(
            ul, userIndex, contentType, contentFlags, bytesRequested, deviceID, overlapped
        );
    } else {
        ret = XShowDeviceSelectorUI(
            userIndex, contentType, contentFlags, bytesRequested, deviceID, overlapped
        );
    }

    return ret;
}

void PlatformMgr::RegionInit() {
    if (XGetGameRegion() != 0xFF) {
        SetRegion(kRegionEurope);
    } else {
        SetRegion(kRegionNA);
    }
}

namespace {
    void DtaToJsonHelper(void* writer, const DataArray* arr) {
        short count = *(short*)((char*)arr + 8);
        if (count != 0 && count > 0) {
            for (int i = 0; i < count; i++) {
                DataNode& nodeRef = arr->Node(i);
                DataNode* node = &nodeRef;
                unsigned int type = *(unsigned int*)((char*)node + 4);

                if (type >= 1) {
                    switch (type) {
                        case 18: {
                            const char* str = node->Str();
                            const char* start = str;
                            while (*str != 0) {
                                str++;
                            }
                            int len = str - start - 1;
                            const char* str2 = node->Str();
                            XJSONWriteStringValue(writer, (unsigned char*)str2, len);
                            break;
                        }
                        case 16: {
                            XJSONBeginArray(writer);
                            DataArray* subArr = node->Array();
                            DtaToJsonHelper(writer, subArr);
                            XJSONEndArray(writer);
                            break;
                        }
                        case 5: {
                            Symbol sym = node->Sym();
                            const char* symStart = sym.Str();
                            const char* symStr = symStart;
                            while (*symStr != 0) {
                                symStr++;
                            }
                            int len = symStr - symStart - 1;
                            Symbol sym2 = node->Sym();
                            XJSONWriteStringValue(writer, (unsigned char*)sym2.Str(), len);
                            break;
                        }
                        case 1: {
                            double val = node->Float();
                            XJSONWriteNumberValue(writer, val);
                            break;
                        }
                        default: {
                            unsigned int t = type;
                            const char* msg = "DtaToJson can't handle type %d r";
                            const char* formatted = (const char*)merged_82610090(&msg, &t);
                            TheDebug.Notify(formatted);
                            XJSONWriteNullValue(writer);
                            break;
                        }
                    }
                } else {
                    int intVal = node->Int();
                    double dblVal = (double)(long long)intVal;
                    XJSONWriteNumberValue(writer, dblVal);
                }
            }
        }
    }
}
