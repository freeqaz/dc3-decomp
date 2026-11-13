#include "meta_ham\ChooseProfilePanel.h"
#include "AppLabel.h"
#include "ProfileMgr.h"
#include "meta_ham\HamPanel.h"
#include "meta_ham\HamProfile.h"
#include "obj\Data.h"
#include "obj\Dir.h"
#include "obj/Object.h"
#include "os\Debug.h"
#include "os\PlatformMgr.h"
#include "ui\PanelDir.h"
#include "ui\UILabel.h"
#include "ui\UIListLabel.h"
#include "utl\Symbol.h"

void ChooseProfilePanel::Enter() {
    UpdateProfiles();
    HamPanel::Enter();
    ThePlatformMgr.AddSink(this);
}

void ChooseProfilePanel::Exit() { ThePlatformMgr.RemoveSink(this); }

HamProfile *ChooseProfilePanel::GetProfile(int index) {
    MILO_ASSERT(index >= 0, 0x3c);
    return TheProfileMgr.GetProfileFromPad(mPadNums[index]);
}

void ChooseProfilePanel::Text(
    int, int data, UIListLabel *uiListLabel, UILabel *uiLabel
) const {
    MILO_ASSERT_RANGE(data, 0, mPadNums.size(), 0x5d);
    static Symbol choose_save_data_sign_in("choose_save_data_sign_in");
    AppLabel *app_label = dynamic_cast<AppLabel *>(uiLabel);
    MILO_ASSERT(app_label, 0x61);
    int tag = mPadNums[data];
    if (uiListLabel->Matches("gamertag")) {
        if (tag == -1) {
            app_label->SetTextToken(choose_save_data_sign_in);
        } else {
            app_label->SetUserName(tag);
        }
    }
}

DataNode ChooseProfilePanel::OnMsg(const SigninChangedMsg &) {
    UpdateProfiles();
    return DATA_UNHANDLED;
}

void ChooseProfilePanel::UpdateProfiles() {
    mPadNums.clear();
    PanelDir *panDir = dynamic_cast<PanelDir *>(DataDir()); // goes unused
    static Symbol choose_save_data_sign_in("choose_save_data_sign_in");
    int signInMask = ThePlatformMgr.SignInMask();
    for (int i = 0; signInMask != 0; i++, signInMask >>= 1) {
        if (signInMask & 1) {
            if (!ThePlatformMgr.IsPadAGuest(i)) {
                mPadNums.push_back(i);
            }
        }
    }
#ifdef HX_NATIVE
    // PLAN G-22: the -1 row is "Sign Into a Gamer Profile", which opens the
    // Xbox guide's sign-in blade (SignInUsers -> XShowSigninUI) so another
    // profile can join.  A PC has no such UI -- native SignInUsers does
    // nothing -- so the row is offered only when no profile is signed in at
    // all, where it is the image's sole row.  With the local profile signed
    // in (native pad 0, xdk_shims XUserGetSigninState) the list is that
    // profile alone.
    if (mPadNums.empty())
#endif
    mPadNums.push_back(-1);
    static Message cRefreshMsg("refresh_ui");
    Handle(cRefreshMsg, true);
}

BEGIN_HANDLERS(ChooseProfilePanel)
    HANDLE_EXPR(profile_selected, ProfileSelected(_msg->Int(2)))
    HANDLE_EXPR(get_profile, GetProfile(_msg->Int(2)))
    HANDLE_ACTION(show_signin, ThePlatformMgr.SignInUsers(1, 0x1000000))
#ifdef HX_NATIVE
    HANDLE_EXPR(num_profiles, NumData() - (HasSignInRow() ? 1 : 0))
#else
    HANDLE_EXPR(num_profiles, NumData() - 1)
#endif
    HANDLE_MESSAGE(SigninChangedMsg)
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS

bool ChooseProfilePanel::ProfileSelected(int idx) const { return mPadNums[idx] != -1; }

#ifdef HX_NATIVE
bool ChooseProfilePanel::HasSignInRow() const {
    return !mPadNums.empty() && mPadNums.back() == -1;
}
#endif
