// DC3 Native Port - PlatformMgr Implementation
// Replaces PlatformMgr_Xbox.cpp - platform-specific methods only
// Non-platform methods are in PlatformMgr.cpp (shared)

#include "os/PlatformMgr.h"
#include "obj/Dir.h"
#include "os/Debug.h"
#include "utl/JobMgr.h"
#include "xdk/XAPILIB.h"

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
    mJobMgr = new JobMgr(this);
    memset(&mOverlapped, 0, sizeof(mOverlapped));
    // The image takes this snapshot in Init() (below).  Native takes it here
    // as well because, on main, native SystemInit does not yet call
    // ThePlatformMgr.Init() (lane w23-g14 restores the image's init list);
    // UpdateSigninState is a pure function of the XUserGetSigninState shim,
    // and nothing reads the mask before SystemInit, so the two are equivalent.
    UpdateSigninState();
}

PlatformMgr::~PlatformMgr() {
    delete mJobMgr;
}

// Image (PlatformMgr_Xbox.cpp): SetName("platform_mgr"), WinSockSocket::Init,
// XOnlineStartup, XNotifyCreateListener, UpdateSigninState, SmartGlassInit and
// the service-id retry timer.  PLATFORM: native has no Xbox Live, XNotify
// listener or SmartGlass, so only the name and the sign-in snapshot run.  The
// name makes DTA's {platform_mgr ...} reach this object's real handlers (the
// image's), not an App-side stand-in.
void PlatformMgr::Init() {
    SetName("platform_mgr", ObjectDir::Main());
    UpdateSigninState();
}

// Platform-specific methods (Xbox stubs)
void PlatformMgr::PreInit() {}
void PlatformMgr::RegionInit() { SetRegion(kRegionNA); }
void PlatformMgr::Poll() {}
bool PlatformMgr::IsSignedIntoLive(int) const { return false; }
bool PlatformMgr::HasOnlinePrivilege(int) const { return false; }
bool PlatformMgr::IsPadAGuest(int) const { return false; }
void PlatformMgr::ShowFriendsUI(int) {}
void PlatformMgr::ShowOfferUI(int) {}
bool PlatformMgr::ShowPartyUI(int) { return false; }
void PlatformMgr::InviteParty(int) {}
int PlatformMgr::GetOwnerOfGuest(int pad) { return pad; }
bool PlatformMgr::IsEthernetCableConnected() { return true; }
const char *PlatformMgr::GetName(int) const { return "Player"; }
bool PlatformMgr::HasCreatedContentPrivilege() const { return true; }
bool PlatformMgr::HasKinectSharePrvilege() const { return true; }
void PlatformMgr::ShowControllerRequiredUI(Hmx::Object *) {}
bool PlatformMgr::IsInParty() { return false; }
bool PlatformMgr::IsInPartyWithOthers() { return false; }
bool PlatformMgr::ShowFitnessBodyProfileUI(int) { return false; }
void PlatformMgr::SetBackgroundDownloadPriority(bool) {}
void PlatformMgr::DisableXMP() {}
void PlatformMgr::EnableXMP() {}
void PlatformMgr::SetScreenSaver(bool b) { mScreenSaver = b; }
void PlatformMgr::CheckMailbox() {}
void PlatformMgr::RunNetStartUtility() {}
void PlatformMgr::SetNotifyUILocation(NotifyLocation) {}
bool PlatformMgr::PollXSocialCapabilities() { return true; }
bool PlatformMgr::QueryXSocialCapabilities() { return false; }
void PlatformMgr::SmartGlassSend(unsigned long, const DataArray *) {}
bool PlatformMgr::IsSmartGlassConnected() { return false; }
// The image's mask derivation (PlatformMgr_Xbox.cpp:UpdateSigninState): one bit
// per user index whose XUserGetSigninState is not NotSignedIn.  Who is signed
// in is decided by the XUserGetSigninState shim (native/src/xdk_shims.cpp) --
// the single native sign-in stand-in.  The image's XUID cache only feeds
// mSigninChangeMask / mSigninSameGuest for XN_SYS_SIGNINCHANGED, which native
// never receives (no XNotify), so both stay 0 as the image leaves them when
// nothing changed.
void PlatformMgr::UpdateSigninState() {
    mSigninChangeMask = 0;
    mSigninMask = 0;
    for (int i = 0; i < 4; i++) {
        if (XUserGetSigninState(i) != eXUserSigninState_NotSignedIn) {
            mSigninMask |= (1 << i);
        }
    }
}
void PlatformMgr::SetPadContext(int, int, int) const {}
void PlatformMgr::SetPadPresence(int, int) const {}
void PlatformMgr::SetPadProperty(int, int, unsigned short const *) const {}
void PlatformMgr::EnumerateFriends(int, std::vector<Friend *> &, Hmx::Object *) {}
DWORD PlatformMgr::ShowDeviceSelectorUI(DWORD, DWORD, DWORD, ULARGE_INTEGER, DWORD *, XOVERLAPPED *) { return 0; }
bool PlatformMgr::GetServiceID(const String &, unsigned int &) { return false; }
void PlatformMgr::SignInUsers(int, unsigned long) {}
ShowGamercardResult PlatformMgr::ShowGamercardForPadNum(int, const OnlineID *) { return kShowGamercardResult_Failed; }
