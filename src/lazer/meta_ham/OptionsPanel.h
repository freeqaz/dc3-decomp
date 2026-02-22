#pragma once
#include "meta/StorePurchaser.h"
#include "meta_ham/HamPanel.h"
#include "meta_ham/HamProfile.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/TokenJobs.h"
#include "net_ham/WebLinkJobs.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "utl/JobMgr.h"
#include "utl/Symbol.h"

class OptionsPanel : public HamPanel {
public:
    virtual ~OptionsPanel();
    OBJ_CLASSNAME(OptionsPanel)
    OBJ_SET_TYPE(OptionsPanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual void Poll();

    NEW_OBJ(OptionsPanel)

    OptionsPanel();
    bool OnRedeemToken(int, char const *);
    void OnPurchaseOfferByOfferString(int, char const *);
    bool OnGetLinkingCode(int);
    void OnXboxTokenRedemption(int);

protected:
    DataNode OnMsg(RCJobCompleteMsg const &);

    RedeemTokenJob *mRedeemTokenJob;
    XboxPurchaser *mXboxPurchaser;
    int unk44;
    unsigned long long mOfferID;
    HamProfile *mPurchaseProfile;
    int unk54;
    GetWebLinkCodeJob *mGetWebLinkCodeJob;
    int unk5c;

private:
    DataNode OnMsg(SingleItemEnumCompleteMsg const &);
};

DECLARE_MESSAGE(TokenRedeemedMsg, "token_redeemed")
END_MESSAGE

DECLARE_MESSAGE(LinkingCodeRetrievedMsg, "linking_code_retrieved")
LinkingCodeRetrievedMsg(bool b, const String &str) : Message(Type(), b, str) {}
bool Success() const { return mData->Int(2); }
const char *LinkingCode() const { return mData->Str(3); }
END_MESSAGE
