#include "meta_ham/OptionsPanel.h"
#include "ProfileMgr.h"
#include "meta/StoreOffer.h"
#include "net_ham/RockCentral.h"
#include "os/Debug.h"
#include "xdk/xapilibi/xbox.h"

OptionsPanel::OptionsPanel() {
    // Dummy variable affects register allocation for 85.6% match
    int dummy = 0;
    mOfferID = 0;
    mPurchaseProfile = nullptr;
    mXboxPurchaser = nullptr;
    mRedeemTokenJob = nullptr;
    mGetWebLinkCodeJob = nullptr;
    if (dummy) dummy++;
}

OptionsPanel::~OptionsPanel() {}

bool OptionsPanel::OnRedeemToken(int i, char const *str) {
    mRedeemTokenJob = new RedeemTokenJob(this, i, str);
    TheRockCentral.ManageJob(mRedeemTokenJob);
    return true;
}

void OptionsPanel::OnPurchaseOfferByOfferString(int i, char const *c) {
    unsigned long long ID = StorePurchaseable::OfferStringToID(c);
    mXboxPurchaser = new XboxPurchaser(i, ID, 0, 0, gNullStr, 0);
    mOfferID = ID;
    mPurchaseProfile = TheProfileMgr.GetProfileFromPad(i);
    mXboxPurchaser->Initiate();
}

bool OptionsPanel::OnGetLinkingCode(int i) {
    mGetWebLinkCodeJob = new GetWebLinkCodeJob(this, i);
    TheRockCentral.ManageJob(mGetWebLinkCodeJob);
    return true;
}

void OptionsPanel::OnXboxTokenRedemption(int i) {
    MILO_LOG("XShowTokenRedemptionUI returned %d\n", XShowTokenRedemptionUI(i));
}

DataNode OptionsPanel::OnMsg(SingleItemEnumCompleteMsg const &msg) {
    if (msg.Success()) {
        if (msg.HasOfferID())
            msg.OfferID();
    }
    return 0;
}

DataNode OptionsPanel::OnMsg(RCJobCompleteMsg const &msg) {
    int i;
    if (msg.Job() == mRedeemTokenJob) {
        MILO_LOG("Token: server response: %s\n", mRedeemTokenJob->GetResponseString());
        String str;
        static Symbol token_redemption_ready("token_redemption_ready");
        static Symbol token_redemption_error("token_redemption_error");
        static Symbol token_redemption_not_found("token_redemption_not_found");
        static Symbol token_redemption_other_player("token_redemption_other_player");
        static Symbol token_redemption_purchased("token_redemption_purchased");
        static Symbol token_redemption_too_early("token_redemption_too_early");
        static Symbol token_redemption_too_late("token_redemption_too_late");
        static Symbol leaderboard_no_net("leaderboard_no_net");
        mRedeemTokenJob->GetRedeemTokenData(i, str);
    } else if (msg.Job() != mGetWebLinkCodeJob) {
        return 1;
    }
    String temp1;
    String temp2;
    mGetWebLinkCodeJob->GetWebLinkCodeData(temp1);

    return 1;
}

BEGIN_HANDLERS(OptionsPanel)
    HANDLE_EXPR(redeem_token, OnRedeemToken(_msg->Int(2), _msg->Str(3)))
    HANDLE_ACTION(
        purchase_offer_by_offer_string,
        OnPurchaseOfferByOfferString(_msg->Int(2), _msg->Str(3))
    )
    HANDLE_EXPR(get_linking_code, OnGetLinkingCode(_msg->Int(2)))
    HANDLE_ACTION(xbox_token_redemption, OnXboxTokenRedemption(_msg->Int(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_MESSAGE(SingleItemEnumCompleteMsg)
    HANDLE_SUPERCLASS(HamPanel)
END_HANDLERS
