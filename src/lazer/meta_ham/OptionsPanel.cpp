#include "meta_ham\OptionsPanel.h"
#include "ProfileMgr.h"
#include "meta\StoreOffer.h"
#include "net_ham\RockCentral.h"
#include "obj\Dir.h"
#include "os\Debug.h"
#include "utl\Locale.h"
#include "xdk\xapilibi\xbox.h"

OptionsPanel::OptionsPanel() {
    mOfferID = 0;
    mPurchaseProfile = nullptr;
    mXboxPurchaser = nullptr;
    mRedeemTokenJob = nullptr;
    mGetWebLinkCodeJob = nullptr;
}

OptionsPanel::~OptionsPanel() {}

void OptionsPanel::Poll() {
    UIPanel::Poll();
    if (mXboxPurchaser) {
        mXboxPurchaser->Poll();
        if (!mXboxPurchaser->IsPurchasing()) {
            if (mXboxPurchaser->IsSuccess()) {
                if (!mXboxPurchaser->PurchaseMade()) {
                    if (mXboxPurchaser->NeedsEnum()) {
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
    unsigned int res = XShowTokenRedemptionUI(pad);
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
        int res;
        String offer;
        static Symbol token_redemption_ready("token_redemption_ready");
        static Symbol token_redemption_error("token_redemption_error");
        static Symbol token_redemption_not_found("token_redemption_not_found");
        static Symbol token_redemption_other_player("token_redemption_other_player");
        static Symbol token_redemption_purchased("token_redemption_purchased");
        static Symbol token_redemption_too_early("token_redemption_too_early");
        static Symbol token_redemption_too_late("token_redemption_too_late");
        static Symbol leaderboard_no_net("leaderboard_no_net");
        mRedeemTokenJob->GetRedeemTokenData(res, offer);
        bool success;
        Symbol error;
        switch (res) {
        case 0xA0002:
            error = token_redemption_ready;
            success = true;
            break;
        case 0xA0005:
            error = token_redemption_ready;
            success = true;
            break;
        case 0xA0006:
            error = token_redemption_purchased;
            success = true;
            break;
        case 0xA0007:
            error = token_redemption_ready;
            success = true;
            break;
        case (int)0x800A0003:
            error = token_redemption_not_found;
            success = false;
            break;
        case (int)0x800A0005:
            error = token_redemption_other_player;
            success = false;
            break;
        case (int)0x800A0008:
            error = token_redemption_too_late;
            success = false;
            break;
        case (int)0x800A0009:
            error = token_redemption_too_early;
            success = false;
            break;
        default:
            bool online = TheRockCentral.IsOnline();
            success = false;
            if (!online) {
                error = leaderboard_no_net;
            } else {
                error = token_redemption_error;
            }
            break;
        }
        static TokenRedeemedMsg msg(true, String(""), token_redemption_ready);
        msg.SetSuccess(success);
        msg.SetOfferString(offer);
        msg.SetError(error);
        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("store_redeem_token_panel");
        panel->HandleType(msg);
        mRedeemTokenJob = nullptr;
    } else if (msg.Job() == mGetWebLinkCodeJob) {
        String wlcData;
        String offer;
        bool webData = mGetWebLinkCodeJob->GetWebLinkCodeData(wlcData);
        static LinkingCodeRetrievedMsg msg(true, String(""));
        bool success = webData && wlcData != "N/A";
        static Symbol linking_code_desc("linking_code_desc");
        static Symbol linking_code_failure("linking_code_failure");
        if (success) {
            offer = Localize(linking_code_desc, false, TheLocale);
            offer += "\n\n";
            offer += wlcData;
        } else {
            offer = Localize(linking_code_failure, false, TheLocale);
        }
        msg.SetSuccess(success);
        msg.SetOfferString(offer);
        UIPanel *panel = ObjectDir::Main()->Find<UIPanel>("options_panel");
        if (panel->GetState() == UIPanel::kUp) {
            panel->HandleType(msg);
        }
        mGetWebLinkCodeJob = nullptr;
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
