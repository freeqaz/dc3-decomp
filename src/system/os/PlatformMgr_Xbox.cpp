#include "os/PlatformMgr.h"
#include "game/PartyModeMgr.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "os/OnlineID.h"
#include "utl/GlitchFinder.h"
#include "xdk/XAPILIB.h"
#include "xdk/XBC.h"
#include "xdk/XMP.h"
#include "xdk/XNET.h"
#include "xdk/NUI.h"
#include "xdk/xapilibi/winerror.h"
#include "xdk/xapilibi/xbox.h"

// Forward declarations for merged functions
extern void* merged_DataArrayNode(void*, int);
extern void* merged_82610090(const void*, unsigned int*);

struct XSTORAGE_ENUMERATE_RESULTS;
enum ServiceIdState {};

namespace {
    int mSigninSameGuest;
    int gNumSmartGlassClients;
    unsigned long gSmartGlassClientIDs[XBC_MAX_CLIENTS];
    int gNumSmartGlassSendsInProgress;
    void *mFriendsEnum;
    void *mFriendsBuffer;
    Hmx::Object *mFriendsCallback;
    void *mFriendsAsync;
    std::vector<Friend *> *mFriendsList;
    void *mListener;
    XOVERLAPPED *mServiceIDOverlapped;
    XOVERLAPPED *mServiceIDOverlapped2;
    XUID mXuidCache[4];
    XSTORAGE_ENUMERATE_RESULTS *mStorageList;
    unsigned long mPathLen;
    ServiceIdState mServiceIdState;
    unsigned long mListSize;
    unsigned long mUserID;
    unsigned long mResult;
}

PlatformMgr::PlatformMgr() {
    mSigninMask = 0;
    mScreenSaver = true;
    mSigninChangeMask = 0;
    mGuideShowing = false;
    mConfirmCancelSwapped = false;
    mConnected = false;
    mRegion = kRegionNone;
    mDiskError = kNoDiskError;
    unk69 = false;

    mSigninSameGuest = 0;
    mFriendsEnum = 0;
    mFriendsBuffer = 0;
    mFriendsCallback = 0;
    mFriendsAsync = 0;
    mFriendsList = 0;
    mListener = 0;

    mJobMgr = new JobMgr(this);

    mServiceIDOverlapped = 0;
    mXuidCache[0] = 0;
    mServiceIDOverlapped2 = 0;
    mStorageList = 0;
    mPathLen = 0x200;
    mXuidCache[1] = 0;
    mXuidCache[2] = 0;
    mXuidCache[3] = 0;
    mServiceIdState = (ServiceIdState)0;
    mListSize = 0;
    mUserID = -1;
    mResult = 0;
    mOverlapped.hEvent = 0;
}

bool PlatformMgr::IsEthernetCableConnected() { return XNetGetEthernetLinkStatus() != 0; }

void PlatformMgr::UpdateSigninState() {
    XUID oldCache[4] = { mXuidCache[0], mXuidCache[1], mXuidCache[2], mXuidCache[3] };
    int i;
    mSigninMask = 0;
    mSigninSameGuest = 0;
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
    bool allUsersRestricted = true;
    for (int userIndex = 0; userIndex < 4; ++userIndex) {
        int privilegeResult = 0;
        bool createdContentBlocked = XUserCheckPrivilege(userIndex, XPRIVILEGE_USER_CREATED_CONTENT, &privilegeResult) != 0
            || privilegeResult != 0;
        bool friendsOnlyContentBlocked =
            XUserCheckPrivilege(userIndex, XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY, &privilegeResult) != 0
            || privilegeResult != 0;
        bool userIsRestricted = !(createdContentBlocked && friendsOnlyContentBlocked);
        allUsersRestricted = allUsersRestricted & userIsRestricted;
    }

    return allUsersRestricted;
}

bool PlatformMgr::HasKinectSharePrvilege() const {
    int bptr = 0;
    return XUserCheckPrivilege(0xFF, XPRIVILEGE_SHARE_CONTENT_OUTSIDE_LIVE, &bptr) == 0 && bptr != 0;
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

void PlatformMgr::SetBackgroundDownloadPriority(bool highPriority) {
    XBackgroundDownloadSetMode(highPriority ?
        XBACKGROUND_DOWNLOAD_MODE_ALWAYS_ALLOW :
        XBACKGROUND_DOWNLOAD_MODE_AUTO);
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

void PlatformMgr::PreInit() { XMPOverrideBackgroundMusic(); }
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
        MILO_ASSERT(ret == ERROR_SUCCESS, 0x929);

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
    void DtaToJsonHelper(HJSONWRITER__ *writer, const DataArray *arr);
    HJSONWRITER__ *DtaToJson(const DataArray *arr);
    void XbcSendMsg(unsigned long clientID, const DataArray *arr);
    void SmartGlassPoll();
    DataArrayPtr JsonToDta(HJSONREADER__ *reader, bool topLevel);
    void XbcRecieveMsg(unsigned long clientID, HJSONREADER__ *reader);
    void XbcCallback(long error, _XBC_EVENT_PARAMS *params, void *state);
    void SmartGlassInit();

    void DtaToJsonHelper(HJSONWRITER__ *writer, const DataArray *arr) {
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
                            XJSONWriteStringValue(writer, str2, len);
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
                            XJSONWriteStringValue(writer, sym2.Str(), len);
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

    HJSONWRITER__ *DtaToJson(const DataArray *arr) {
        HJSONWRITER__ *writer = XJSONCreateWriter();
        XJSONBeginArray(writer);
        DtaToJsonHelper(writer, arr);
        XJSONEndArray(writer);
        return writer;
    }

    void XbcSendMsg(unsigned long clientID, const DataArray *arr) {
        HJSONWRITER__ *writer = DtaToJson(arr);
        if (clientID == 0) {
            for (int i = 0; i < XBC_MAX_CLIENTS; i++) {
                if (gSmartGlassClientIDs[i] != 0) {
                    XbcSendJSON(XBC_DELIVERY_DEFAULT, gSmartGlassClientIDs[i], writer, 0);
                    gNumSmartGlassSendsInProgress++;
                }
            }
        } else {
            XbcSendJSON(XBC_DELIVERY_DEFAULT, clientID, writer, 0);
            gNumSmartGlassSendsInProgress++;
        }
        XJSONCloseWriter(writer);
    }

    void SmartGlassPoll() {
        long result = XbcDoWork();
        if (result != 0) {
            MILO_NOTIFY("SmartGlass: error: %d\n", result);
        }
    }

    DataArrayPtr JsonToDta(HJSONREADER__ *reader, bool topLevel) {
        DataArrayPtr container;
        DataArray *fieldName = 0;
        _JSONTokenType tokenType;
        unsigned long param1, param2;

        while (XJSONReadToken(reader, &tokenType, &param1, &param2) == 0) {
            DataNode node(0);
            char charBuf[256];
            charBuf[0] = '\0';

            if ((int)tokenType >= 5 && ((int)tokenType <= 6 || tokenType == 10)) {
                wchar_t wcharBuf[128];
                XJSONGetTokenValue(reader, wcharBuf, 0x80);
                wcstombs(charBuf, wcharBuf, 0x100);
            }

            switch (tokenType) {
            case kJSONTokenBeginArray:
                node = DataNode(JsonToDta(reader, false));
                break;
            case kJSONTokenEndArray:
            case kJSONTokenEndMap:
                return container;
            case kJSONTokenBeginMap:
                node = DataNode(JsonToDta(reader, false));
                break;
            case kJSONTokenString:
                node = DataNode(charBuf);
                break;
            case kJSONTokenNumber:
                if (strchr(charBuf, '.')) {
                    node = DataNode((float)atof(charBuf));
                } else {
                    node = DataNode(atoi(charBuf));
                }
                break;
            case kJSONTokenTrue:
                node = DataNode(1);
                break;
            case kJSONTokenFalse:
                node = DataNode(0);
                break;
            case kJSONTokenNull:
                node = DataNode(0);
                break;
            case kJSONTokenFieldName:
                fieldName = new DataArray(2);
                fieldName->Node(0) = DataNode(Symbol(charBuf));
                continue;
            case kJSONTokenEnd:
            case kJSONTokenComment:
            case kJSONTokenError:
            default:
                continue;
            }

            if (fieldName) {
                fieldName->Node(1) = node;
                node = DataNode(fieldName, kDataArray);
                fieldName = 0;
            }

            if (topLevel && node.Type() == kDataArray) {
                container = node.Array();
                topLevel = false;
            } else {
                ((DataArray *)container)->Insert(((DataArray *)container)->Size(), node);
            }
        }

        return container;
    }

    void XbcRecieveMsg(unsigned long clientID, HJSONREADER__ *reader) {
        DataArrayPtr dta = JsonToDta(reader, false);
        SmartGlassMsg msg(clientID, (DataArray *)dta);
        ThePlatformMgr.Handle(msg.Data(), true);
    }

    void XbcCallback(long error, _XBC_EVENT_PARAMS *params, void *state) {
        if (error != 0) {
            MILO_NOTIFY("SmartGlass: Error in cb: 0x%08x", error);
            return;
        }
        unsigned int userIdx = params->userIndex;
        if (userIdx >= XBC_MAX_CLIENTS) {
            MILO_NOTIFY("SmartGlass: Error in cb: user index %d (event: %d)", userIdx, params->eventType);
            return;
        }
        switch (params->eventType) {
        case XBC_EVENT_CLIENT_CONNECTED:
            gNumSmartGlassClients++;
            gSmartGlassClientIDs[userIdx] = params->clientID;
            MILO_ASSERT(gNumSmartGlassClients <= XBC_MAX_CLIENTS, 0x20C);
            break;
        case XBC_EVENT_CLIENT_DISCONNECTED:
            gSmartGlassClientIDs[userIdx] = 0;
            gNumSmartGlassClients--;
            MILO_ASSERT(gNumSmartGlassClients >= 0, 0x214);
            break;
        case XBC_EVENT_SEND_COMPLETE:
            gNumSmartGlassSendsInProgress--;
            break;
        case XBC_EVENT_DATA_RECEIVED:
            XbcRecieveMsg(params->clientID, params->jsonReader);
            break;
        default:
            break;
        }
    }

    void SmartGlassInit() {
        gSmartGlassClientIDs[0] = 0;
        gSmartGlassClientIDs[1] = 0;
        gSmartGlassClientIDs[2] = 0;
        gSmartGlassClientIDs[3] = 0;
        long result = XbcInitialize(XbcCallback, 0);
        if (result < 0) {
            MILO_FAIL("Failed to initialize Xbox SmartGlass library.\n");
        }
    }
}

void PlatformMgr::SmartGlassSend(unsigned long clientID, const DataArray *arr) {
    XbcSendMsg(clientID, arr);
}
