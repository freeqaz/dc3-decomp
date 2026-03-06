#include "meta_ham/OptionsPanel.h"
#include "ProfileMgr.h"
#include "meta/StoreOffer.h"
#include "net_ham/RockCentral.h"
#include "obj/Dir.h"
#include "os/Debug.h"
#include "utl/Locale.h"
#include "xdk/xapilibi/xbox.h"

OptionsPanel::OptionsPanel() {
    int dummy = 0;
    mOfferID = 0;
    mPurchaseProfile = nullptr;
    mXboxPurchaser = nullptr;
    mRedeemTokenJob = nullptr;
    mGetWebLinkCodeJob = nullptr;
    if (dummy) {
        dummy++;
    }
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
                                    this, mPurchaseProfile->GetPadNum(), mOfferID,
                                    mXboxPurchaser->mSource,
                                    static_cast<StorePurchaser *>(mXboxPurchaser)->mUserIndex
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
        char *responseString = (char *)mRedeemTokenJob->GetResponseString();
        MILO_LOG("Token: server response: %s\n", responseString);
        int response = 0;
        String offer;
        static Symbol token_redemption_ready("token_redemption_ready");
        static Symbol token_redemption_error("token_redemption_error");
        static Symbol token_redemption_not_found("token_redemption_not_found");
        static Symbol token_redemption_other_player("token_redemption_other_player");
        static Symbol token_redemption_purchased("token_redemption_purchased");
        static Symbol token_redemption_too_early("token_redemption_too_early");
        static Symbol token_redemption_too_late("token_redemption_too_late");
        static Symbol leaderboard_no_net("leaderboard_no_net");
        mRedeemTokenJob->GetRedeemTokenData(response, offer);
        Symbol error = token_redemption_ready;
        bool success;
        if (response <= 0xA0002) {
            if (response == 0xA0002) {
                success = true;
            } else {
                unsigned int code = response - 0x800A0003;
                if (code == 0) {
                    error = token_redemption_not_found;
                } else if (code == 2) {
                    error = token_redemption_other_player;
                } else if (code == 5) {
                    error = token_redemption_too_late;
                } else if (code == 6) {
                    error = token_redemption_too_early;
                } else {
                    if (!TheRockCentral.IsOnline()) {
                        error = leaderboard_no_net;
                    } else {
                        error = token_redemption_error;
                    }
                }
                success = false;
            }
        } else {
            unsigned int code = response - 0xA0005;
            if (code == 0) {
                success = true;
            } else if (code == 1) {
                error = token_redemption_purchased;
                success = true;
            } else if (code == 2) {
                success = true;
            } else {
                if (!TheRockCentral.IsOnline()) {
                    error = leaderboard_no_net;
                } else {
                    error = token_redemption_error;
                }
                success = false;
            }
        }
        static TokenRedeemedMsg tokenMsg(true, String(""), token_redemption_ready);
        tokenMsg.SetSuccess(success);
        tokenMsg.SetOfferString(offer);
        tokenMsg.SetError(error);
        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("store_redeem_token_panel", true);
        panel->HandleType(tokenMsg);
        mRedeemTokenJob = nullptr;
        return 1;
    } else if (msg.Job() == mGetWebLinkCodeJob) {
        String code;
        String text;
        bool gotData = mGetWebLinkCodeJob->GetWebLinkCodeData(code);
        static LinkingCodeRetrievedMsg linkMsg(true, String(""));
        bool success;
        if (gotData) {
            bool hasCode = (code != "N/A");
            success = true;
            if (!hasCode) {
                success = false;
            }
        } else {
            success = false;
        }
        static Symbol linking_code_desc("linking_code_desc");
        static Symbol linking_code_failure("linking_code_failure");
        if (success) {
            text = Localize(linking_code_desc, 0, TheLocale);
            text += "\n\n";
            text += code;
        } else {
            text = Localize(linking_code_failure, 0, TheLocale);
        }
        linkMsg.SetSuccess(success);
        linkMsg.SetLinkingCode(text);
        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("options_panel", true);
        if (panel->GetState() == UIPanel::kUp) {
            panel->HandleType(linkMsg);
        }
        mGetWebLinkCodeJob = nullptr;
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
