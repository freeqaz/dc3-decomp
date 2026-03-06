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
    if (msg.Job() == mRedeemTokenJob) {
        MILO_LOG("Token: server response: %s\n", mRedeemTokenJob->GetResponseString());
        String offerStr;
        static Symbol token_redemption_ready("token_redemption_ready");
        static Symbol token_redemption_error("token_redemption_error");
        static Symbol token_redemption_not_found("token_redemption_not_found");
        static Symbol token_redemption_other_player("token_redemption_other_player");
        static Symbol token_redemption_purchased("token_redemption_purchased");
        static Symbol token_redemption_too_early("token_redemption_too_early");
        static Symbol token_redemption_too_late("token_redemption_too_late");
        static Symbol leaderboard_no_net("leaderboard_no_net");

        int responseCode;
        mRedeemTokenJob->GetRedeemTokenData(responseCode, offerStr);

        Symbol errorSym = token_redemption_ready;
        bool isSuccess = false;

        if (responseCode <= 0xA0002) {
            if (responseCode == 0xA0002) {
                isSuccess = true;
                errorSym = token_redemption_ready;
            } else {
                unsigned int code = (unsigned int)responseCode - 0x800A0003;
                if (code == 0) errorSym = token_redemption_not_found;
                else {
                    switch (code) {
                    case 2: errorSym = token_redemption_other_player; break;
                    case 5: errorSym = token_redemption_too_late; break;
                    case 6: errorSym = token_redemption_too_early; break;
                    default:
                        if (!TheRockCentral.IsOnline()) errorSym = leaderboard_no_net;
                        else errorSym = token_redemption_error;
                        break;
                    }
                }
            }
        } else {
            unsigned int code = (unsigned int)responseCode - 0xA0005;
            if (code == 0) {
                isSuccess = true;
                errorSym = token_redemption_ready;
            } else if (code == 1) {
                isSuccess = true;
                errorSym = token_redemption_purchased;
            } else if (code == 2) {
                isSuccess = true;
                errorSym = token_redemption_ready;
            } else {
                if (!TheRockCentral.IsOnline()) errorSym = leaderboard_no_net;
                else errorSym = token_redemption_error;
            }
        }

        static TokenRedeemedMsg redeMsg(true, "", token_redemption_ready);
        redeMsg.SetSuccess(isSuccess);
        redeMsg.SetOfferString(offerStr);
        redeMsg.SetError(errorSym);

        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("store_redeem_token_panel", true);
        panel->Handle(redeMsg, true);

        mRedeemTokenJob = nullptr;
    } else if (msg.Job() == mGetWebLinkCodeJob) {
        String webCode;
        String displayStr;
        bool ok = mGetWebLinkCodeJob->GetWebLinkCodeData(webCode);

        static LinkingCodeRetrievedMsg retMsg(true, "");
        bool success = (ok && webCode != "N/A");

        static Symbol linking_code_desc("linking_code_desc");
        static Symbol linking_code_failure("linking_code_failure");

        if (success) {
            displayStr = Localize(linking_code_desc, nullptr, TheLocale);
            displayStr += "\n\n";
            displayStr += webCode;
        } else {
            displayStr = Localize(linking_code_failure, nullptr, TheLocale);
        }

        retMsg.SetSuccess(success);
        retMsg.SetLinkingCode(displayStr);

        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("options_panel", true);
        if (panel->GetState() == UIPanel::kUp) {
            panel->Handle(retMsg, true);
        }

        mGetWebLinkCodeJob = nullptr;
    } else {
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
