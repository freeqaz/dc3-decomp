#include "os\PlatformMgr.h"
#include "game\PartyModeMgr.h"
#include "stl\_map.h"
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "os\OnlineID.h"
#include "os\NetworkSocket_Win.h"
#include "os\Timer.h"
#include "obj\Dir.h"
#include "stl\_list.h"
#include "utl\DataPointMgr.h"
#include "utl\Locale.h"
#include "os\ThreadCall.h"
#include "utl\GlitchFinder.h"
#include "xdk\XAPILIB.h"
#include "xdk\xparty\xparty.h"
#include "xdk\XBC.h"
#include "xdk\XMP.h"
#include "xdk\XNET.h"
#include "xdk\NUI.h"
#include "xdk\xapilibi\winerror.h"
#include "xdk\xapilibi\xbox.h"

enum ServiceIdState {};

namespace {
    DWORD gSmartGlassClientIDs[4];
    XUID mXuidCache[4];
    unsigned long mResult;
    unsigned long mUserID;
    unsigned long mPathLen;
    unsigned long mListSize;
    XSTORAGE_ENUMERATE_RESULTS *mStorageList;
    XOVERLAPPED *mServiceIDOverlapped;
    XOVERLAPPED *mServiceIDOverlapped2;
    ServiceIdState mServiceIdState;
    float mRetryTime;
    Hmx::Object *mFriendsCallback;
    void *mFriendsAsync;
    void *mFriendsBuffer;
    void *mFriendsEnum;
    void *mListener;
    int mSigninSameGuest;
    int gNumSmartGlassClients;
    int gNumSmartGlassSendsInProgress;
    std::vector<Friend *> *mFriendsList;

    // One outstanding PlatformMgr::EnumerateFriends request.  Allocated with
    // MEM_OVERLOAD so the allocation is attributed to this file.
    class FriendEnumRequest {
    public:
        FriendEnumRequest(int padNum, std::vector<Friend *> *list, Hmx::Object *callback)
            : mPadNum(padNum), mList(list), mCallback(callback) {}

        MEM_OVERLOAD(FriendEnumRequest, 0x3E);

        int mPadNum; // 0x0
        std::vector<Friend *> *mList; // 0x4
        Hmx::Object *mCallback; // 0x8
    };

    std::list<FriendEnumRequest *> mFriendEnumRequests;
    Timer mTime;
    std::map<String, unsigned int> mServiceIdMap;

    int GetPadNumFromXuid(unsigned __int64 xuid);
    void SmartGlassInit();
}

Hmx::Object *PlatformMgr::spShowControllerObject;
DWORD PlatformMgr::sdwShowControllerTrackingID;
int PlatformMgr::snShowControllerPadNum;

PlatformMgr::PlatformMgr() : mSigninMask(0) {
    mScreenSaver = true;
    mSigninChangeMask = 0;
    mGuideShowing = false;
    mConfirmCancelSwapped = false;
    mConnected = false;
    mRegion = kRegionNone;
    mDiskError = kNoDiskError;
    unk69 = false;
    mSigninSameGuest = 0;
    mFriendsEnum = nullptr;
    mFriendsBuffer = nullptr;
    mFriendsCallback = nullptr;
    mFriendsAsync = nullptr;
    mFriendsList = nullptr;
    mListener = nullptr;
    mJobMgr = new JobMgr(this);
    for (int i = 0; i < 4; i++) {
        mXuidCache[i] = 0;
    }
    mServiceIDOverlapped = nullptr;
    mServiceIDOverlapped2 = nullptr;
    mStorageList = nullptr;
    mPathLen = 0x200;
    mServiceIdState = (ServiceIdState)0;
    mListSize = 0;
    mUserID = -1;
    mResult = 0;
    mOverlapped.hEvent = nullptr;
}

PlatformMgr::~PlatformMgr() {
    DWORD ret = CloseHandle(mListener);
    MILO_ASSERT(ret == ERROR_SUCCESS, 0x3E7);
    ret = XOnlineCleanup();
    MILO_ASSERT(ret == ERROR_SUCCESS, 0x3EA);
}

void PlatformMgr::Init() {
    SetName("platform_mgr", ObjectDir::Main());
    WinSockSocket::Init();
    DWORD ret = XOnlineStartup();
    MILO_ASSERT(ret == ERROR_SUCCESS, 0x419);
    mListener = XNotifyCreateListener(0xA7);
    MILO_ASSERT(mListener, 0x41C);
    UpdateSigninState();
    SmartGlassInit();
    mTime.Start();
    mRetryTime = mTime.Ms() + 300000.0f;
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

bool PlatformMgr::HasOnlinePrivilege(int padNum) const {
    static GlitchAverager glAvg;
    AutoGlitchPoker poker("PlatformMgr::HasOnlinePrivilege", 1.0f, 0.0f, &glAvg);
    MILO_ASSERT(padNum >= 0, 0x693);
    if (!IsSignedIntoLive(padNum)) {
        return false;
    }
    BOOL result;
    XUserCheckPrivilege(padNum, XPRIVILEGE_MULTIPLAYER_SESSIONS, &result);
    return result != 0;
}

bool PlatformMgr::HasCreatedContentPrivilege() const {
    bool allUsersRestricted = true;
    for (int userIndex = 0; userIndex < 4; ++userIndex) {
        int privilegeResult = 0;
        bool createdContentOK = XUserCheckPrivilege(userIndex, XPRIVILEGE_USER_CREATED_CONTENT, &privilegeResult) == 0
            && privilegeResult == 0;
        bool friendsOnlyContentOK =
            XUserCheckPrivilege(userIndex, XPRIVILEGE_USER_CREATED_CONTENT_FRIENDS_ONLY, &privilegeResult) == 0
            && privilegeResult == 0;
        bool userIsRestricted = !(createdContentOK && friendsOnlyContentOK);
        allUsersRestricted = allUsersRestricted & userIsRestricted;
    }

    return allUsersRestricted;
}

bool PlatformMgr::HasKinectSharePrvilege() const {
    int bptr = 0;
    return XUserCheckPrivilege(0xFF, XPRIVILEGE_SHARE_CONTENT_OUTSIDE_LIVE, &bptr) == 0 && bptr != 0;
}

bool PlatformMgr::IsSmartGlassConnected() { return gNumSmartGlassClients > 0; }

bool PlatformMgr::IsInParty() {
    HRESULT noPartyResult = 0x807D0003;
    HRESULT result = noPartyResult;
    if (IsSignedIntoLive(0) || IsSignedIntoLive(1) || IsSignedIntoLive(2) || IsSignedIntoLive(3)) {
        XPARTY_USER_LIST userList;
        result = XPartyGetUserList(&userList);
    }
    return result != noPartyResult;
}

bool PlatformMgr::IsInPartyWithOthers() {
    XPARTY_USER_LIST userList;
    bool result = IsInParty() && (XPartyGetUserList(&userList), (int)userList.dwUserCount > 1);
    return result;
}

void PlatformMgr::SetPadContext(int padNum, int i2, int i3) const {
    if (padNum != -1 && ThePlatformMgr.IsSignedIn(padNum)) {
        XUserSetContext(padNum, i2, i3);
    }
}

void PlatformMgr::SetPadProperty(int padNum, int propertyId, unsigned short const *value) const {
    if (padNum != -1 && ThePlatformMgr.IsSignedIn(padNum)) {
        int byteLength = wcslen((const wchar_t *)value) * 2;
        if (byteLength > 0x7E) {
            byteLength = 0x7E;
        }
        XUserSetPropertyEx(padNum, propertyId, byteLength, value, 0);
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

int ShowControllerRequiredUIThreaded() {
    return XShowNuiControllerRequiredUI(
        PlatformMgr::sdwShowControllerTrackingID, PlatformMgr::snShowControllerPadNum
    );
}

void ShowControllerRequiredUIThreadedCB(int result) {
    static ControllerReqOpCompleteMsg msg(true);
    msg.SetSuccess(result == 0);
    if (PlatformMgr::spShowControllerObject) {
        PlatformMgr::spShowControllerObject->Handle(msg, false);
    }
}

void PlatformMgr::ShowControllerRequiredUI(Hmx::Object *obj) {
    sdwShowControllerTrackingID = 0;
    snShowControllerPadNum = 0;
    spShowControllerObject = 0;
    unsigned long trackingID;
    if (sXShowCallback(trackingID)) {
        sdwShowControllerTrackingID = trackingID;
        snShowControllerPadNum = 0xFF;
        spShowControllerObject = obj;
        ThreadCall(ShowControllerRequiredUIThreaded, ShowControllerRequiredUIThreadedCB);
    } else {
        static ControllerReqOpCompleteMsg msg(true);
        msg.SetSuccess(true);
        if (obj) {
            obj->Handle(msg, false);
        }
    }
}

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

void PlatformMgr::SetNotifyUILocation(NotifyLocation location) {
    DWORD position;
    switch (location) {
    case kNotify0:
        position = 9;
        break;
    case kNotify1:
        position = 2;
        break;
    default:
        MILO_FAIL("Unknown NotifyLocation %d", location);
        return;
    }
    XNotifyPositionUI(position);
}

void PlatformMgr::InviteParty(int padNum) {
    MILO_ASSERT(IsInParty(), 0x87B);
    if (IsSignedIn(padNum)) {
        XPartySendGameInvites(padNum, 0);
    }
}

void PlatformMgr::PreInit() { XMPOverrideBackgroundMusic(); }
void PlatformMgr::EnableXMP() { XMPRestoreBackgroundMusic(); }
void PlatformMgr::DisableXMP() { XMPOverrideBackgroundMusic(); }
void PlatformMgr::CheckMailbox() {}
void PlatformMgr::RunNetStartUtility() {}

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

int PlatformMgr::GetOwnerOfGuest(int padNum) {
    MILO_ASSERT(padNum != -1, 0x8F9);

    XUSER_SIGNIN_INFO signinInfo;
    DWORD ret = XUserGetSigninInfo(padNum, 0, &signinInfo);
    int result = -1;
    if (ret == ERROR_NO_SUCH_USER) {
        XUID xuid;
        if (XUserGetXUID(padNum, &xuid) == 0) {
            result = GetPadNumFromXuid(xuid & 0xff3fffffffffffff);
        }
    } else {
        MILO_ASSERT(ret == ERROR_SUCCESS, 0x911);
        MILO_ASSERT(signinInfo.dwInfoFlags & XUSER_INFO_FLAG_GUEST, 0x912);
        result = signinInfo.dwSponsorUserIndex;
    }
    return result;
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
    int GetPadNumFromXuid(unsigned __int64 xuid) {
        XUSER_SIGNIN_INFO info;
        for (int pad = 0; pad < 4; pad++) {
            memset(&info, 0, sizeof(info));
            XUserGetSigninInfo(pad, 1, &info);
            if (xuid == info.xuid) {
                return pad;
            }
            memset(&info, 0, sizeof(info));
            XUserGetSigninInfo(pad, 2, &info);
            if (xuid == info.xuid) {
                return pad;
            }
            memset(&info, 0, sizeof(info));
            XUserGetXUID(pad, &info.xuid);
            if (xuid == info.xuid) {
                return pad;
            }
        }
        return -1;
    }

    bool XPrivilegeCheck(_XPRIVILEGE_TYPE priv1, _XPRIVILEGE_TYPE priv2, unsigned __int64 xuid) {
        BOOL result = 0;
        XUserCheckPrivilege(0xFF, priv1, &result);
        if (result == 0) {
            XUserCheckPrivilege(0xFF, priv2, &result);
            if (result == 0) {
                return false;
            }
            for (int i = 0; i < 4; i++) {
                if (XUserCheckPrivilege(i, priv1, &result) == 0 && result == 0
                    && XUserAreUsersFriends(i, &xuid, 1, &result, 0) == 0 && result == 0) {
                    return false;
                }
            }
        }
        return true;
    }

    void DtaToJsonHelper(HJSONWRITER *writer, const DataArray *a) {
        int aSize = a->Size();
        if (aSize != 0) {
            for (int i = 0; i < aSize; i++) {
                const DataNode &n = a->Node(i);
                switch (n.Type()) {
                case kDataInt:
                    XJSONWriteNumberValue(writer, n.Int());
                    break;
                case kDataFloat:
                    XJSONWriteNumberValue(writer, n.Float());
                    break;
                case kDataSymbol:
                    XJSONWriteStringValue(writer, n.Sym().Str(), strlen(n.Sym().Str()));
                    break;
                case kDataArray:
                    XJSONBeginArray(writer);
                    DtaToJsonHelper(writer, n.Array());
                    XJSONEndArray(writer);
                    break;
                case kDataString:
                    XJSONWriteStringValue(writer, n.Str(), strlen(n.Str()));
                    break;
                default:
                    MILO_NOTIFY("DtaToJson can't handle type %d right now", n.Type());
                    XJSONWriteNullValue(writer);
                    break;
                }
            }
        }
    }

    HJSONWRITER *DtaToJson(const DataArray *a) {
        HJSONWRITER *writer = XJSONCreateWriter();
        XJSONBeginArray(writer);
        DtaToJsonHelper(writer, a);
        XJSONEndArray(writer);
        return writer;
    }

    void XbcSendMsg(DWORD id, const DataArray *a) {
        HJSONWRITER *writer = DtaToJson(a);
        if (id == 0) {
            // i hate this
            for (int i = 0; i < 4; i++) {
                if (gSmartGlassClientIDs[i] != 0) {
                    XbcSendJSON(XBC_DELIVERY_RELIABLE, gSmartGlassClientIDs[i], writer, 0);
                    gNumSmartGlassSendsInProgress++;
                }
            }
        } else {
            XbcSendJSON(XBC_DELIVERY_RELIABLE, id, writer, 0);
            gNumSmartGlassSendsInProgress++;
        }
        XJSONCloseWriter(writer);
    }

    void SmartGlassPoll() {
        HRESULT res = XbcDoWork();
        if (res != 0) {
            MILO_NOTIFY("SmartGlass: error: %d\n", res);
        }
    }

    DataArrayPtr JsonToDta(HJSONREADER *reader, bool topLevel) {
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

            // Case-body layout order is source order, and the target lays
            // kJSONTokenFieldName out FIRST.
            switch (tokenType) {
            case kJSONTokenFieldName:
                fieldName = new DataArray(2);
                fieldName->Node(0) = DataNode(Symbol(charBuf));
                continue;
            case kJSONTokenBeginArray:
                node = DataNode(JsonToDta(reader, false));
                break;
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
            // Written out one label at a time: the target's byte jump table
            // covers 0..0xd with three separate `b` thunks for these, which a
            // fall-through group onto `default` collapses (turning the bounds
            // check into subi/cmplwi 9).
            case kJSONTokenEnd:
                continue;
            case kJSONTokenComment:
                continue;
            case kJSONTokenError:
                continue;
            default:
                continue;
            case kJSONTokenEndArray:
                return container;
            case kJSONTokenEndMap:
                return container;
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

    void XbcRecieveMsg(DWORD id, HJSONREADER *reader) {
        DataArrayPtr dta = JsonToDta(reader, true);
        SmartGlassMsg msg(id, dta);
        ThePlatformMgr.Handle(msg, true);
    }

    void XbcCallback(HRESULT err, XBC_EVENT_PARAMS *params, void *) {
        if (err != 0) {
            MILO_NOTIFY("SmartGlass: Error in cb: 0x%08x", err);
        } else if (params->nUserIndex >= 4) {
            MILO_NOTIFY(
                "SmartGlass: Error in cb: user index %d (event: %d)",
                params->nUserIndex,
                params->Type
            );
        } else {
            switch (params->Type) {
            case XBC_EVENT_CLIENT_CONNECTED: {
                gSmartGlassClientIDs[params->nUserIndex] = params->nClientId;
                gNumSmartGlassClients++;
                MILO_ASSERT(gNumSmartGlassClients <= XBC_MAX_CLIENTS, 0x20C);
                break;
            }
            case XBC_EVENT_CLIENT_DISCONNECTED: {
                gSmartGlassClientIDs[params->nUserIndex] = 0;
                gNumSmartGlassClients--;
                MILO_ASSERT(gNumSmartGlassClients >= 0, 0x214);
                break;
            }
            case XBC_EVENT_JSON_SEND_COMPLETE: {
                gNumSmartGlassSendsInProgress--;
                break;
            }
            case XBC_EVENT_JSON_RECEIVE_COMPLETE: {
                XbcRecieveMsg(params->nClientId, params->hReader);
                break;
            }
            default:
                break;
            }
        }
    }

    void SmartGlassInit() {
        for (int i = 0; i < 4; i++) {
            gSmartGlassClientIDs[i] = 0;
        }
        if (XbcInitialize(XbcCallback, nullptr) < 0) {
            MILO_FAIL("Failed to initialize Xbox SmartGlass library.\n");
        }
    }
}

void PlatformMgr::SignInUsers(int count, unsigned long controllerMask) {
    MILO_ASSERT(count == 1 || count == 2 || count == 4, 0x64E);
    unsigned long trackingID;
    if (sXShowCallback(trackingID)) {
        XShowNuiSigninUI(trackingID, controllerMask);
    } else {
        XShowSigninUI(count, controllerMask);
    }
}

ShowGamercardResult PlatformMgr::ShowGamercardForPadNum(int padNum, const OnlineID *onlineID) {
    static GlitchAverager glAvg;
    AutoGlitchPoker poker("PlatformMgr::ShowGamercard", 1.0f, 0.0f, &glAvg);
    MILO_ASSERT(onlineID, 0x7C6);

    unsigned long trackingID;
    if (!onlineID->GetIsValid()) {
        return kShowGamercardResult_Failed;
    }
    if (!IsSignedIntoLive(padNum)) {
        return kShowGamercardResult_NotSignedIn;
    }
    XUID xuid = onlineID->GetXUID();
    if (!XPrivilegeCheck(XPRIVILEGE_PROFILE_VIEWING, XPRIVILEGE_PROFILE_VIEWING_FRIENDS_ONLY, xuid)) {
        return kShowGamercardResult_PrivilegeFailed;
    }
    DWORD ret;
    if (sXShowCallback(trackingID)) {
        ret = XShowNuiGamerCardUI(trackingID, padNum, xuid);
    } else {
        ret = XShowGamerCardUI(padNum, xuid);
    }
    if (ret != 0) {
        return kShowGamercardResult_Failed;
    }
    return kShowGamercardResult_Success;
}

bool PlatformMgr::QueryXSocialCapabilities() {
    mSocialCapabilities = 0;
    mOverlapped.InternalContext = 0;
    mOverlapped.InternalHigh = 0;
    mOverlapped.InternalLow = 0;
    mOverlapped.hEvent = CreateEventA(0, 1, 0, "QueryXSocialCapabilities");
    if (!mOverlapped.hEvent) {
        TheDebug << MakeString("mOverlapped.hEvent is null");
        return false;
    } else {
        mOverlapped.pCompletionRoutine = 0;
        mOverlapped.dwCompletionContext = 0;
        mOverlapped.dwExtendedError = 0;
        DWORD result = XSocialGetCapabilities((DWORD *)&mSocialCapabilities, &mOverlapped);
        if (result == 0) {
            TheDebug << MakeString("XSocialGetCapabilities() returns success - %x\n", mSocialCapabilities);
            BOOL privResult = 0;
            mHasXSocialPhotoPost = (unsigned char)(unsigned int)mSocialCapabilities & 1;
            mHasXSocialLinkPost = ((unsigned char)(unsigned int)mSocialCapabilities >> 1) & 1;
            if (XUserCheckPrivilege(0xFF, XPRIVILEGE_SOCIAL_NETWORK_SHARING, &privResult) != 0 || privResult == 0) {
                mHasXSocialPhotoPost = false;
                mHasXSocialLinkPost = false;
            }
            return true;
        } else if (result == 0x3e5) {
            TheDebug << MakeString("XSocialGetCapabilities() returns ERROR_IO_PENDING\n");
            return true;
        } else {
            if (mOverlapped.hEvent != 0) {
                CloseHandle(mOverlapped.hEvent);
                mOverlapped.hEvent = 0;
            }
            return false;
        }
    }
}

bool PlatformMgr::PollXSocialCapabilities() {
    if (mOverlapped.hEvent == 0 || mOverlapped.InternalLow == 0x3e5) {
        return false;
    }
    CloseHandle(mOverlapped.hEvent);
    int result = 0;
    mOverlapped.hEvent = 0;
    mHasXSocialPhotoPost = (unsigned int)mSocialCapabilities & 1;
    mHasXSocialLinkPost = ((unsigned int)mSocialCapabilities >> 1) & 1;
    if (XUserCheckPrivilege(0xFF, XPRIVILEGE_SOCIAL_NETWORK_SHARING, &result) != 0 || result == 0) {
        mHasXSocialPhotoPost = false;
        mHasXSocialLinkPost = false;
    }
    const char *linkStr = mHasXSocialLinkPost ? "YES" : "NO";
    const char *photoStr = mHasXSocialPhotoPost ? "YES" : "NO";
    TheDebug
        << MakeString("PollXSocialCapabilities() - can post Photo:%s can post Link:%s\n", photoStr, linkStr);
    return true;
}

DataNode PlatformMgr::OnSignInUsers(const DataArray *msg) {
    unsigned long flags = 0;
    if (msg->Size() > 3) {
        if (msg->Int(3) != 0) {
            flags = 2;
        }
    }
    SignInUsers(msg->Int(2), flags);
    return DataNode(0);
}

bool PlatformMgr::GetServiceID(const String &name, unsigned int &serviceId) {
    bool found = false;
    serviceId = 0;
    std::map<String, unsigned int>::iterator it = mServiceIdMap.find(name);
    if (it != mServiceIdMap.end()) {
        serviceId = it->second;
        found = true;
    }
    return found;
}

void PlatformMgr::SmartGlassSend(unsigned long clientID, const DataArray *arr) {
    XbcSendMsg(clientID, arr);
}

const char *PlatformMgr::GetName(int padNum) const {
    if (IsSignedIn(padNum)) {
        char name[16];
        int ret = XUserGetName(padNum, name, 16);
        if (ret == 0) {
            return MakeString(name);
        }
    }
    static Symbol player("player");
    return MakeString("%s %i", Localize(player, 0, TheLocale), padNum + 1);
}

void PlatformMgr::EnumerateFriends(
    int padNum, std::vector<Friend *> &friends, Hmx::Object *callback
) {
    mFriendEnumRequests.push_back(new FriendEnumRequest(padNum, &friends, callback));
}

#include "utl/JobMgr.h"
#include "meta\StorePanel.h"
#include "lazer\meta_ham\OptionsPanel.h"

void MultipleItemsEnumJob::Cancel(Hmx::Object *) {
    MILO_FAIL("MultipleItemsEnumJob::Cancel called");
}

SingleItemEnumJob::SingleItemEnumJob(Hmx::Object *callback, int pad, u64 itemID)
    : mObject(callback), mUserIndex(pad), mItemID(itemID), mStatus(0), mSuccess(false),
      mEnumBuffer(0), mEnumHandle(0) {}

SingleItemEnumJob::~SingleItemEnumJob() {
    if (mStatus == 1 && mOverlapped.InternalLow == 0x3e5) {
        unsigned int result = XCancelOverlapped(&mOverlapped);
        if (result != 0) {
            TheDebug.Fail(MakeString("Error cancelling enum %d", result), 0);
        }
    }
    if (mEnumHandle != 0) {
        CloseHandle(mEnumHandle);
        mEnumHandle = 0;
    }
    ::operator delete(mEnumBuffer);
    mEnumBuffer = 0;
}

void SingleItemEnumJob::Start() {
    mStatus = 1;
    DWORD bufSize = 0;
    unsigned int result = XMarketplaceCreateOfferEnumeratorByOffering(
        mUserIndex, 1, &mItemID, 1, &bufSize, &mEnumHandle
    );
    if (result != 0) {
        if (mEnumHandle != 0) {
            CloseHandle(mEnumHandle);
            mEnumHandle = 0;
        }
        TheDebug.Notify(MakeString("Error creating enumerator after purchase: %d", result));
        mStatus = 3;
        return;
    }
    mEnumBuffer = new char[bufSize];
    memset(mEnumBuffer, 0, bufSize);
    memset(&mOverlapped, 0, sizeof(mOverlapped));
    result = XEnumerate(mEnumHandle, mEnumBuffer, bufSize, 0, &mOverlapped);
    if (result == 0x3e5) {
        return;
    }
    if (mEnumHandle != 0) {
        CloseHandle(mEnumHandle);
        mEnumHandle = 0;
    }
    ::operator delete(mEnumBuffer);
    mEnumBuffer = 0;
    TheDebug.Notify(MakeString("Error enumerating after purchase: %d", result));
    mStatus = 3;
}

void SingleItemEnumJob::Poll() {
    if (mStatus == 1 && mOverlapped.InternalLow != 0x3e5) {
        DWORD numEnumerated;
        unsigned int result = XGetOverlappedResult(&mOverlapped, &numEnumerated, 0);
        if (numEnumerated == 1 && result == 0) {
            mStatus = 2;
            mSuccess = *(int *)((u64 *)mEnumBuffer + 9) != 0;
        } else {
            mStatus = 3;
            TheDebug.Notify(MakeString("Error enumerating after purchase: %d", result));
        }
        if (mEnumHandle != 0) {
            CloseHandle(mEnumHandle);
            mEnumHandle = 0;
        }
        ::operator delete(mEnumBuffer);
        mEnumBuffer = 0;
    }
}

bool SingleItemEnumJob::IsFinished() {
    if (mStatus == 1) {
        Poll();
    }
    return mStatus != 1;
}

void SingleItemEnumJob::Cancel(Hmx::Object *) {
    MILO_FAIL("SingleItemEnumJob::Cancel called");
}

void SingleItemEnumJob::OnCompletion(Hmx::Object *) {
    if (mObject) {
        static SingleItemEnumCompleteMsg msg(false, false, gNullStr);
        msg.SetSuccess(mStatus == 2);
        msg.SetPurchaseMade(mSuccess);
        String itemStr(MakeString("%016llX", mItemID));
        msg.SetOfferID(itemStr);
        mObject->Handle(msg, true);
    }
}

PostPurchaseEnumJob::PostPurchaseEnumJob(Hmx::Object *obj, int userIndex, u64 itemID, Symbol offerSym, unsigned int purchaserID)
    : SingleItemEnumJob(obj, userIndex, itemID), mOfferSymbol(offerSym), mPurchaserID(purchaserID) {
}

PostPurchaseEnumJob::~PostPurchaseEnumJob() {}

void PostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    if ((mStatus == 2) && (mSuccess != 0)) {
        static Symbol sSourceSymbol("source");
        static Symbol sOfferSymbol("offer");
        static Symbol sPurchaserSymbol("purchaser");

        String dataStr(MakeString("%016llX", mItemID));
        SendDataPoint("store/purchase", sSourceSymbol, mOfferSymbol, sOfferSymbol, dataStr.c_str(), sPurchaserSymbol, mPurchaserID);
    }
    SingleItemEnumJob::OnCompletion(obj);
}

unsigned long long SingleItemEnumCompleteMsg::OfferID() const {
    return _strtoui64(mData->Str(4), 0, 16);
}

MultipleItemsEnumJob::MultipleItemsEnumJob(Hmx::Object *callback, int pad, std::vector<u64> &ids)
    : mObject(callback), mUserIndex(pad), mItemIDs(ids), mStatus(0), mSuccess(false),
      mEnumBuffer(0), mEnumHandle(0) {}

MultipleItemsEnumJob::~MultipleItemsEnumJob() {
    if (mStatus == 1 && mOverlapped.InternalLow == 0x3e5) {
        DWORD result = XCancelOverlapped(&mOverlapped);
        if (result != 0) {
            TheDebug.Fail(MakeString("Error cancelling enum %d", result), 0);
        }
    }
    if (mEnumHandle != 0) {
        CloseHandle(mEnumHandle);
        mEnumHandle = 0;
    }
    ::operator delete(mEnumBuffer);
    mEnumBuffer = 0;
}

void MultipleItemsEnumJob::Poll() {
    if (mStatus == 1 && mOverlapped.InternalLow != 0x3e5) {
        DWORD resultVal;
        void *result = (void *)XGetOverlappedResult(&mOverlapped, &resultVal, 0);
        if (result == 0) {
            mStatus = 2;
            u64 *enumEntry = (u64 *)mEnumBuffer;
            u64 *itemIt = &mItemIDs[0];
            unsigned int i = 0;
            auto purchasedIt = mPurchased.begin();
            unsigned int bitOffset = purchasedIt._M_offset;
            unsigned int *bitChunk = purchasedIt._M_p;
            if (mItemIDs.size() > 0) {
                do {
                    if (*enumEntry == *itemIt) {
                        unsigned int mask = 1 << bitOffset;
                        int purchased = *(int *)(enumEntry + 9);
                        if (purchased != 0) {
                            *bitChunk |= mask;
                        } else {
                            *bitChunk &= ~mask;
                        }
                        bool success = mSuccess || ((*bitChunk & mask) != 0);
                        enumEntry += 0xd;
                        mSuccess = success;
                    } else {
                        TheDebug.Notify(MakeString("Could not enumerate offerId %016llX", *itemIt));
                        *bitChunk &= ~(1 << bitOffset);
                    }
                    if (bitOffset++ == 31) {
                        bitOffset = 0;
                        bitChunk++;
                    }
                    i++;
                    itemIt++;
                } while (i < (unsigned int)mItemIDs.size());
            }
        } else {
            mStatus = 3;
            TheDebug.Notify(MakeString("Error enumerating after purchase: %d", result));
        }
        if (mEnumHandle != 0) {
            CloseHandle(mEnumHandle);
            mEnumHandle = 0;
        }
        ::operator delete(mEnumBuffer);
        mEnumBuffer = 0;
    }
}

bool MultipleItemsEnumJob::IsFinished() {
    if (mStatus == 1) {
        Poll();
    }
    return mStatus != 1;
}

void MultipleItemsEnumJob::Start() {
    mStatus = 1;
    int count = (int)mItemIDs.size();
    mPurchased.resize(count, false);
    fill(mPurchased.begin(), mPurchased.end(), false);

    DWORD bufSize = 0;
    DWORD result = XMarketplaceCreateOfferEnumeratorByOffering(
        mUserIndex, (int)mItemIDs.size(), &mItemIDs[0], (WORD)mItemIDs.size(), &bufSize, &mEnumHandle
    );
    if (result != 0) {
        if (mEnumHandle != 0) {
            CloseHandle(mEnumHandle);
            mEnumHandle = 0;
        }
        TheDebug.Notify(MakeString("Error creating enumerator after purchase: %d", result));
        mStatus = 3;
        return;
    }
    mEnumBuffer = new char[bufSize];
    memset(mEnumBuffer, 0, bufSize);
    memset(&mOverlapped, 0, sizeof(mOverlapped));
    result = XEnumerate(mEnumHandle, mEnumBuffer, bufSize, 0, &mOverlapped);
    if (result == 0x3e5) {
        return;
    }
    if (mEnumHandle != 0) {
        CloseHandle(mEnumHandle);
        mEnumHandle = 0;
    }
    ::operator delete(mEnumBuffer);
    mEnumBuffer = 0;
    TheDebug.Notify(MakeString("Error enumerating after purchase: %d", result));
    mStatus = 3;
}

void MultipleItemsEnumJob::OnCompletion(Hmx::Object *) {
    if (mObject) {
        static MultipleItemsEnumCompleteMsg msg(false, false, mItemIDs.size(), gNullStr);
        msg.SetSuccess(mStatus == 2);
        msg.SetPurchaseMade(mSuccess);
        int numIDs = mItemIDs.size();
        msg.SetNumOfferIDs(numIDs);
        for (int i = 0; i < numIDs; i++) {
            String curID = MakeString("%016llX", mItemIDs[i]);
            msg.SetOfferID(i, curID);
            msg.SetPurchased(i, mPurchased[i]);
        }
        mObject->Handle(msg, true);
    }
}

MultipleItemsPostPurchaseEnumJob::MultipleItemsPostPurchaseEnumJob(
    Hmx::Object *obj, int userIndex, std::vector<u64> &itemIDs, Symbol offerSym, unsigned int purchaserID)
    : MultipleItemsEnumJob(obj, userIndex, itemIDs), mOfferSymbol(offerSym), mPurchaserID(purchaserID) {
}

MultipleItemsPostPurchaseEnumJob::~MultipleItemsPostPurchaseEnumJob() {}

void MultipleItemsPostPurchaseEnumJob::OnCompletion(Hmx::Object *obj) {
    if (mStatus == 2 && mSuccess != 0) {
        static Symbol sSourceSymbol("source");
        static Symbol sOfferSymbol("offer");
        static Symbol sPurchaserSymbol("purchaser");

        for (unsigned int i = 0; i < mItemIDs.size(); i++) {
            String itemStr(MakeString("%016llX", mItemIDs[i]));
            SendDataPoint("store/purchase", sSourceSymbol, mOfferSymbol, sOfferSymbol, itemStr.c_str(), sPurchaserSymbol, mPurchaserID);
        }
    }
    MultipleItemsEnumJob::OnCompletion(obj);
}

void MultipleItemsEnumCompleteMsg::SetNumOfferIDs(int count) {
    mData->Node(4) = count;
    mData->Node(5).Array(mData)->Resize(count);
    mData->Node(6).Array(mData)->Resize(count);
}

void MultipleItemsEnumCompleteMsg::SetOfferID(int index, const String &s) {
    DataNode dn(s);
    mData->Node(5).Array(mData)->Node(index) = dn;
}

unsigned long long MultipleItemsEnumCompleteMsg::OfferID(int index) const {
    DataArray *arr = mData->Node(5).Array(mData);
    const char *str = arr->Node(index).Str(arr);
    return _strtoui64(str, nullptr, 0x10);
}

void MultipleItemsEnumCompleteMsg::SetPurchased(int index, bool b) {
    mData->Node(6).Array(mData)->Node(index) = b;
}
