#include "meta_ham/OptionsPanel.h"
#include "ProfileMgr.h"
#include "meta/StoreOffer.h"
#include "net_ham/RockCentral.h"
#include "obj/Dir.h"
#include "os/Debug.h"
#include "utl/Locale.h"
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

void OptionsPanel::Poll() {
    UIPanel::Poll();
    if (mXboxPurchaser) {
        mXboxPurchaser->PollUpdate();
        if (!mXboxPurchaser->IsPurchasing()) {
            if (mXboxPurchaser->IsSuccess()) {
                if (!mXboxPurchaser->PurchaseMade()) {
                    if (mXboxPurchaser->IsReady()) {
                        if (mPurchaseProfile) {
                            PostPurchaseEnumJob *job = new PostPurchaseEnumJob(
                                this, mXboxPurchaser->mUserIndex, mOfferID,
                                mXboxPurchaser->mSource, mXboxPurchaser->mUserIndex
                            );
                            ThePlatformMgr.QueueEnumJob(job);
                        }
                    }
                }
            }
            delete mXboxPurchaser;
            mXboxPurchaser = nullptr;
        }
    }
}

bool OptionsPanel::OnRedeemToken(int pad, char const *token) {
    mRedeemTokenJob = new RedeemTokenJob(this, pad, token);
    TheRockCentral.ManageJob(mRedeemTokenJob);
    return true;
}

void OptionsPanel::OnPurchaseOfferByOfferString(int pad, char const *offer) {
    unsigned long long id = StorePurchaseable::OfferStringToID(offer);
    mXboxPurchaser = new XboxPurchaser(pad, id, 0, 0, gNullStr, 0);
    mOfferID = id;
    mPurchaseProfile = TheProfileMgr.GetProfileFromPad(pad);
    mXboxPurchaser->Initiate();
}

bool OptionsPanel::OnGetLinkingCode(int pad) {
    mGetWebLinkCodeJob = new GetWebLinkCodeJob(this, pad);
    TheRockCentral.ManageJob(mGetWebLinkCodeJob);
    return true;
}

void OptionsPanel::OnXboxTokenRedemption(int pad) {
    int res = XShowTokenRedemptionUI(pad);
    MILO_LOG("XShowTokenRedemptionUI returned %d\n", res);
}

DataNode OptionsPanel::OnMsg(SingleItemEnumCompleteMsg const &msg) {
    if (msg.Success()) {
        if (msg.HasOfferID()) {
            msg.OfferID();
        }
    }
    return DataNode(0);
}

DataNode OptionsPanel::OnMsg(RCJobCompleteMsg const &msg) {
    int i = 0;
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
        Symbol errorSym;
        bool success = false;
        switch ((unsigned int)i) {
        case 0x800a0003:
            errorSym = token_redemption_too_early;
            break;
        case 0x800a0005:
            errorSym = token_redemption_too_late;
            break;
        case 0x800a0008:
            errorSym = token_redemption_other_player;
            break;
        case 0x800a0009:
            errorSym = token_redemption_not_found;
            break;
        case 0xa0002:
        case 0xa0005:
        case 0xa0007:
            errorSym = token_redemption_ready;
            success = true;
            break;
        case 0xa0006:
            errorSym = token_redemption_purchased;
            break;
        default:
            if (!TheRockCentral.IsOnline()) {
                errorSym = leaderboard_no_net;
            } else {
                errorSym = token_redemption_error;
            }
            break;
        }
        static TokenRedeemedMsg tokenMsg(true, String(""), token_redemption_ready);
        tokenMsg.SetSuccess(success);
        tokenMsg.SetOfferString(str);
        tokenMsg.SetError(errorSym);
        UIPanel *panel =
            ObjectDir::Main()->Find<UIPanel>("store_redeem_token_panel", true);
        panel->HandleType(tokenMsg);
        mRedeemTokenJob = 0;
        return 1;
    } else if (msg.Job() == mGetWebLinkCodeJob) {
        String temp1;
        String temp2;
        bool gotData = mGetWebLinkCodeJob->GetWebLinkCodeData(temp1);
        static LinkingCodeRetrievedMsg linkMsg(true, String(""));
        bool success = false;
        if (gotData) {
            success = (temp1 != "N/A");
        }
        static Symbol linking_code_desc("linking_code_desc");
        static Symbol linking_code_failure("linking_code_failure");
        if (success) {
            temp2 = Localize(linking_code_desc, 0, TheLocale);
            temp2 += "\n\n";
            temp2 += temp1;
        } else {
            temp2 = Localize(linking_code_failure, 0, TheLocale);
        }
        linkMsg.SetSuccess(success);
        linkMsg.SetLinkingCode(temp2);
        UIPanel *panel =
            ObjectDir::Main()->Find<UIPanel>("options_panel", true);
        if (panel->GetState() == UIPanel::kUp) {
            panel->HandleType(linkMsg);
        }
        mGetWebLinkCodeJob = 0;
        return 1;
    }
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
