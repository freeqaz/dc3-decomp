#pragma once
#include "meta/StoreOffer.h"
#include "meta_ham/HamStoreFilterProvider.h"
#include "net_ham/HamStoreCartJobs.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "stl/_map.h"
#include "stl/_vector.h"
#include "ui/UIListProvider.h"
#include "utl/Str.h"
#include "utl/Symbol.h"
#include <list>

class PackSongListProvider : public UIListProvider, public Hmx::Object {
public:
    virtual ~PackSongListProvider();
    virtual void Text(int, int, UIListLabel *, UILabel *) const;
    virtual Symbol DataSymbol(int);
    virtual int NumData() const;

    PackSongListProvider();

    DataArray *mSongs; // 0x30
};

class HamStoreProvider : public UIListProvider, public Hmx::Object {
public:
    virtual ~HamStoreProvider();
    virtual DataNode Handle(DataArray *, bool);

    virtual void Text(int, int, UIListLabel *, UILabel *) const;
    virtual Symbol DataSymbol(int) const;
    virtual int NumData() const;

    HamStoreProvider(
        std::vector<StoreOffer *> *,
        std::vector<HamStoreFilter *> *,
        std::vector<CartRow> *
    );
    StoreOffer const *FindSong(int) const;
    StoreOffer const *FindPack(StoreOffer const *) const;
    bool ShowBrowserPurchased(StoreOffer const *) const;
    bool IsPartiallyPurchased(StoreOffer const *) const;
    void SetPackList(StoreOffer const *);
    Symbol CurrentSort() const;
    int NumOffersInCart();
    void UpdateOffersInCart(StoreOffer *, int);
    void SetFilter(StoreOffer const *);
    void SetFilter(HamStoreFilter const *);
    void OnNextSort();
    void Refresh();
    bool AllowSortToggle() { return mSorts.size() > 1; }
    bool IsOfferInCart(StoreOffer *);

    PackSongListProvider GetPackProvider() { return mPackProvider; }
    HamStoreFilterProvider *GetFilterProvider() { return mFilterProvider; }
    std::list<StoreOffer *> *GetCartOffers() { return &mCartOffers; }

protected:
    std::vector<StoreOffer *> *mAllOffers;
    std::vector<HamStoreFilter *> *mFilters;
    std::map<Symbol, std::vector<StoreOffer *> *> unk38;
    std::vector<StoreOffer *> unk54;
    const HamStoreFilter *mCurrentFilter; // 0x5c
    int mSortIndex; // 0x60
    std::vector<StoreOffer *> *mFilteredOffers; // 0x64
    std::vector<Symbol> mSorts; // 0x68
    HamStoreFilterProvider *mFilterProvider;
    PackSongListProvider mPackProvider;
    std::vector<CartRow> *mCartRows;
    std::list<StoreOffer *> mCartOffers;
    int unkb8;

private:
    StoreOffer *OnGetOffer(int);
    int OnGetOfferIndex(StoreOffer *);
    void RefreshFilteredCartOffers();
    void PopulateOffersInCart();
    void ApplySort();
};
