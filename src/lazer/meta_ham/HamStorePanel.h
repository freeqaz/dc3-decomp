#pragma once
#include "meta/Profile.h"
#include "meta/StoreEnumeration.h"
#include "meta/StoreOffer.h"
#include "meta/StorePanel.h"
#include "meta/StorePurchaser.h"
#include "meta_ham/HamStoreFilterProvider.h"
#include "meta_ham/HamStoreProvider.h"
#include "net_ham/HamStoreCartJobs.h"
#include "net_ham/RCJobDingo.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Timer.h"
#include "os/User.h"
#include "stl/_vector.h"
#include "types.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include <list>

struct HamSpecialOffer {
    Symbol unk0;
    int unk4;
    int unk8;
    int unkc;
    int unk10;
    int unk14;
};

class HamStorePanel : public StorePanel, public ContentMgr::Callback {
public:
    // Hmx::Object
    virtual ~HamStorePanel();
    OBJ_CLASSNAME(HamStorePanel)
    OBJ_SET_TYPE(HamStorePanel)
    virtual DataNode Handle(DataArray *, bool);
    virtual bool SyncProperty(DataNode &, DataArray *, int, PropOp);

    // UIPanel
    virtual void Load();
    virtual bool Exiting() const;
    virtual void Poll();
    virtual void Unload();
    virtual bool IsSongInLibrary(int const &) const;
    virtual void ExitStore(StoreError) const;
    virtual Profile *StoreProfile() const;
    virtual StoreOffer *MakeNewOffer(DataArray *);
    virtual StoreOffer *FindOffer(Symbol) const;
    virtual bool EnumerateSubsetOfOfferIDs() const { return 0; }
    virtual void GetOfferIDsToEnumerate(std::vector<u64> &, bool) const;

    // ContentMgr::Callback
    virtual bool ContentDiscovered(Symbol);
    virtual bool ContentTitleDiscovered(unsigned int, Symbol);
    virtual void ContentMounted(char const *, char const *);

    NEW_OBJ(HamStorePanel)

    HamStorePanel();
    void LockCart();
    void UnlockCart();
    void EmptyCart();
    bool IsCurrFilterCart(int);
    void SetFilterToCart();
    int SetFilterToSongs();
    void RemoveDLCFromCart(int);
    void RemoveOfferFromCart(StoreOffer *);
    void AddOfferToCart(StoreOffer *);

protected:
    virtual StoreError UpdateOffers(std::list<EnumProduct> const &, bool);
    virtual void StoreUserProfileSwappedToUser(LocalUser *);

    void ReadLockData();
    void DisableCart();
    char const *GetIndexFile() const;
    void RefreshSpecialOfferStatus();
    void GetCart();
    void RelockCart();
    bool IsSpecialOfferOwned(Symbol) const;
    bool BuySpecialOffer(Symbol);
    void FinishSpecialOfferEnum(std::vector<bool> const &, bool);
    void RemoveNextDLCFromCart();
    void AddNextDLCToCart();
    void AddDLCToCart(int);
    void CreateCartUIs();
    void ReadCartData();
    DataNode OnMsg(RCJobCompleteMsg const &);

    int unka0;
    int unka4;
    HamStoreProvider *mOfferProvider; // 0xa8
    std::vector<HamStoreFilter *> mFilters;
    String mMotd;
    bool mAllowCancel;
    Timer mCancelTimer;
    Timer mRelockTimer;
    int mLockData;
    std::vector<CartRow> mCartRows;
    RCJob *mJobs[7];
    bool unk154;
    bool mCartEnabled;
    bool mCartLocked;
    bool mCartDataLoaded;
    bool mRemovingFromCart;
    bool mAddingToCart;
    std::list<int> mPendingRemoves;
    std::list<int> mPendingAdds;
    std::vector<HamSpecialOffer> mSpecialOffers;
    std::vector<unsigned long long> mSpecialOfferIDs;
    int unk184;
    XboxPurchaser *mXboxPurchaser; // 0x188
};
