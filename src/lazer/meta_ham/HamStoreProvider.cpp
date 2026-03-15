#include "meta_ham/HamStoreProvider.h"
#include "HamStoreFilterProvider.h"
#include "macros.h"
#include "meta/StoreOffer.h"
#include "meta_ham/AppLabel.h"
#include "meta_ham/HamStorePanel.h"
#include "meta_ham/HamUI.h"
#include "obj/Data.h"
#include "obj/Object.h"
#include "os/Debug.h"
#include "stl/_algo.h"
#include "ui/UILabel.h"
#include "ui/UIListLabel.h"
#include "ui/UIListProvider.h"
#include "utl/NetCacheMgr.h"
#include "utl/Std.h"
#include "utl/Symbol.h"
#include "utl/trie.h"

#pragma region PackSongListProvider

PackSongListProvider::PackSongListProvider() : mSongs() {}

void PackSongListProvider::Text(
    int, int data, UIListLabel *uiListLabel, UILabel *uiLabel
) const {
    MILO_ASSERT(mSongs, 0x23);
    MILO_ASSERT_RANGE(data, 0, mSongs->Size(), 0x24);
    if (uiListLabel->Matches("song")) {
        static Symbol name("name");
        DataArray *array = mSongs->Node(data).Array(mSongs);
        static_cast<AppLabel *>(uiLabel)->SetPackSongName(array);
    } else {
        uiLabel->SetTextToken(gNullStr);
    }
}

int PackSongListProvider::NumData() const {
    if (mSongs)
        return mSongs->Size();
    return 0;
}

Symbol PackSongListProvider::DataSymbol(int idx) {
    MILO_ASSERT(mSongs, 0x3b);
    MILO_ASSERT_RANGE(idx, 0, mSongs->Size(), 0x3c);
    return mSongs->Node(idx).Array(mSongs)->Sym(0);
}

#pragma endregion PackSongListProvider
#pragma region HamStoreProvider

HamStoreProvider::HamStoreProvider(
    std::vector<StoreOffer *> *offers,
    std::vector<HamStoreFilter *> *filters,
    std::vector<CartRow> *rows
)
    : mAllOffers(offers), mFilters(filters), mCurrentFilter(0), mFilteredOffers(0),
      mCartRows(rows), mCartCheckout(0) {
    mFilterProvider = new HamStoreFilterProvider(mFilters);
}

HamStoreProvider::~HamStoreProvider() {
    FOREACH (it, unk38) {
        RELEASE(it->second);
    }
    unk38.clear();
    mFilteredOffers = nullptr;
    RELEASE(mFilterProvider);
}

int HamStoreProvider::NumOffersInCart() {
    int offers = 0;
    FOREACH (it, mCartOffers) {
        offers++;
    }
    return offers;
}

bool HamStoreProvider::IsOfferInCart(StoreOffer *offer) {
    FOREACH (it, mCartOffers) {
        if (*it == offer)
            return true;
    }
    return false;
}

int HamStoreProvider::NumData() const { return mFilteredOffers->size(); }

Symbol HamStoreProvider::DataSymbol(int idx) const {
    MILO_ASSERT_RANGE(idx, 0, mFilteredOffers->size(), 0x166);
    return (*mFilteredOffers)[idx]->StoreOfferData()->Sym(0);
}

void HamStoreProvider::Text(int, int data, UIListLabel *slot, UILabel *label) const {
    MILO_ASSERT_RANGE(data, 0, mFilteredOffers->size(), 0x118);
    StoreOffer *offer = (*mFilteredOffers)[data];
    if (!offer) {
        label->SetTextToken(gNullStr);
        return;
    }
    static Symbol store_checkout("store_checkout");
    Symbol offerSym = offer->StoreOfferData()->Sym(0);
    if (offerSym == store_checkout) {
        if (slot->Matches("song")) {
            label->SetTextToken(store_checkout);
        } else {
            label->SetTextToken(gNullStr);
        }
        return;
    }
    if (slot->Matches("song")) {
        static Symbol by_artist("by_artist");
        static Symbol song("song");
        if (CurrentSort() == by_artist && offer->OfferType() == song) {
            static_cast<AppLabel *>(label)->SetStoreOfferArtist(offer);
        } else {
            static_cast<AppLabel *>(label)->SetStoreOfferName(offer);
        }
        return;
    }
    if (slot->Matches("purchased")) {
        if (ShowBrowserPurchased(offer)) {
            label->SetTextToken(Symbol("store_purchased"));
            return;
        }
        if (offer->InLibrary()) {
            label->SetTextToken(Symbol("store_in_library"));
            return;
        }
        if (!offer->IsAvailable()) {
            label->SetTextToken(Symbol("store_unavailable"));
            return;
        }
    }
    if (slot->Matches("cost")) {
        String temp;
        if (!ShowBrowserPurchased(offer) && !offer->InLibrary() && offer->IsAvailable()) {
            static_cast<AppLabel *>(label)->SetStoreOfferCost(offer);
        }
        return;
    }
    if (slot->Matches("new")) {
        if (offer->IsNewRelease()) {
            static Symbol new_content("new_content");
            label->SetTextToken(new_content);
        } else {
            label->SetTextToken(gNullStr);
        }
        return;
    }
    label->SetTextToken(gNullStr);
}

int HamStoreProvider::OnGetOfferIndex(StoreOffer *offer) {
    if (offer) {
        for (int i = 0; i < mFilteredOffers->size(); i++) {
            if ((*mFilteredOffers)[i] == offer)
                return i;
        }
    }
    return -1;
}

StoreOffer *HamStoreProvider::OnGetOffer(int idx) {
    MILO_ASSERT_RANGE(idx, 0, mFilteredOffers->size(), 0x1da);
    return (*mFilteredOffers)[idx];
}

StoreOffer const *HamStoreProvider::FindPack(StoreOffer const *song) const {
    MILO_ASSERT(song->OfferType() == "song", 0x18e);
    static Symbol pack("pack");
    FOREACH_PTR (it, mAllOffers) {
        if ((*it)->OfferType() == pack && (*it)->HasSong(song))
            return *it;
    }
    return nullptr;
}

StoreOffer const *HamStoreProvider::FindSong(int id) const {
    static Symbol song("song");
    FOREACH_PTR (it, mAllOffers) {
        StoreOffer *so = *it;
        if (so->OfferType() == song && so->GetSingleSongID() == id)
            return so;
    }
    return nullptr;
}

Symbol HamStoreProvider::CurrentSort() const {
    if (mSorts.size() > 1) {
        return mSorts[mSortIndex];
    }
    return gNullStr;
}

// action: 0 = add to cart, 1 = remove from cart, 2 = clear cart
void HamStoreProvider::UpdateOffersInCart(StoreOffer *offer, int i) {
    switch (i) {
    case 0:
        mCartOffers.push_back(offer);
        break;
    case 1:
        mCartOffers.remove(offer);
        break;
    case 2:
        mCartOffers.clear();
        break;
    }
    RefreshFilteredCartOffers();
}

void HamStoreProvider::SetPackList(StoreOffer const *offer) {
    static Symbol pack("pack");
    if (offer->OfferType() == pack) {
        static Symbol songs("songs");
        mPackProvider.mSongs = offer->GetData(DataArrayPtr(songs), false).Array(0);
    } else {
        mPackProvider.mSongs = 0;
    }
}

bool HamStoreProvider::IsPartiallyPurchased(StoreOffer const *offer) const {
    static Symbol song("song");
    static Symbol pack("pack");
    if (ShowBrowserPurchased(offer)) {
        return true;
    } else {
        if (offer->OfferType() == pack) {
            for (int i = 0; i < offer->NumSongs(); i++) {
                const StoreOffer *song = FindSong(offer->Song(i));
                if (song && const_cast<StoreOffer *>(song)->IsPurchased()) {
                    return true;
                }
            }
        }
    }
    return false;
}

void HamStoreProvider::ApplySort() {
    if (!mSorts.empty()) {
        MILO_ASSERT_RANGE(mSortIndex, 0, mSorts.size(), 0xf1);
        if (mSorts[mSortIndex].Str() != gNullStr) {
            SortCmp cmp(mSorts[mSortIndex]);
            std::sort(mFilteredOffers->begin(), mFilteredOffers->end(), cmp);
        }
    }
}

bool HamStoreProvider::ShowBrowserPurchased(const StoreOffer *offer) const {
    static Symbol song("song");
    static Symbol pack("pack");
    if (offer->IsPurchased()) {
        return true;
    } else {
        if (offer->OfferType() == song) {
            const StoreOffer *currOffer = FindPack(offer);
            if (currOffer && currOffer->HasSong(offer) && currOffer->IsPurchased()) {
                return true;
            }
        } else if (offer->OfferType() == pack) {
            for (int i = 0; i < offer->NumSongs(); i++) {
                const StoreOffer *currOffer = FindSong(offer->Song(i));
                if (!currOffer || !currOffer->IsPurchased()) {
                    return false;
                }
            }
            return offer->NumSongs() != 0;
        }
    }
    return false;
}

void HamStoreProvider::SetFilter(StoreOffer const *pack) {
    MILO_ASSERT(pack->OfferType()=="pack", 0xb0);
    unk54.clear();
    unk54.push_back((StoreOffer *)pack);
    for (int i = 0; i < pack->NumSongs(); i++) {
        const StoreOffer *offer = FindSong(pack->Song(i));
        if (offer && (offer->IsAvailable() || TheNetCacheMgr->IsDebug())) {
            unk54.push_back((StoreOffer *)offer);
        }
    }
    mFilteredOffers = &unk54;
    mSorts.clear();
    mSortIndex = 0;
}

void HamStoreProvider::PopulateOffersInCart() {
    HamStorePanel *storePanel = dynamic_cast<HamStorePanel *>(TheHamUI.FocusPanel());
    MILO_ASSERT(storePanel, 0x206);
    mCartOffers.clear();
    FOREACH_PTR (it_cartRow, mCartRows) {
        CartRow &row = *it_cartRow;
        FOREACH_PTR (it_storeOffer, mAllOffers) {
            StoreOffer *offer = *it_storeOffer;
            if (offer->IsAvailable() || TheNetCacheMgr->IsDebug()) {
                static Symbol song("song");
                if (offer->OfferType() == song
                    && offer->GetSingleSongID() == row.mSongID) {
                    if (offer->IsPurchased()) {
                        storePanel->RemoveDLCFromCart(offer->GetSingleSongID());
                    } else {
                        mCartOffers.push_back(offer);
                    }
                    break;
                }
            }
        }
    }
    RefreshFilteredCartOffers();
}

void HamStoreProvider::OnNextSort() {
    MILO_ASSERT(AllowSortToggle(), 0xe8);
    mSortIndex = (mSortIndex + 1) % mSorts.size();
    ApplySort();
}

void HamStoreProvider::SetFilter(HamStoreFilter const *filter) {
    mCurrentFilter = (HamStoreFilter *)filter;
    unk54.clear();
    std::map<Symbol, std::vector<StoreOffer *> *>::iterator it;
    if (mCurrentFilter
        && (it = unk38.find(mCurrentFilter->mFilterSym), it != unk38.end())) {
        mFilteredOffers = it->second;
        mSorts = mCurrentFilter->mSortTypes;
    } else {
        mCurrentFilter = nullptr;
        mFilteredOffers = mAllOffers;
        mSorts.clear();
    }
    mSortIndex = 0;
    ApplySort();
}

void HamStoreProvider::Refresh() {
    // Delete all vectors in unk38 map, then clear it
    for (std::map<Symbol, std::vector<StoreOffer *> *>::iterator it = unk38.begin();
         it != unk38.end();
         ++it) {
        if (it->second) {
            delete it->second;
        }
        it->second = 0;
    }
    unk38.clear();
    mFilteredOffers = 0;

    // Iterate all offers, categorize by filter symbols
    std::vector<StoreOffer *> *allOffers = mAllOffers;
    for (StoreOffer **it = allOffers->begin(); it != allOffers->end(); ++it) {
        StoreOffer *offer = *it;
        if (offer->isAvailable || TheNetCacheMgr->IsDebug()) {
            DataArray *filters =
                offer->GetData(DataArrayPtr(Symbol("filters")), true).Array(0);
            for (int i = 1; i < filters->Size(); i++) {
                Symbol filterSym = filters->Sym(i);
                std::map<Symbol, std::vector<StoreOffer *> *>::iterator mapIt =
                    unk38.find(filterSym);
                if (mapIt == unk38.end()) {
                    std::vector<StoreOffer *> *vec = new std::vector<StoreOffer *>();
                    vec->push_back(offer);
                    unk38.insert(
                        std::pair<Symbol, std::vector<StoreOffer *> *>(filterSym, vec)
                    );
                } else {
                    mapIt->second->push_back(offer);
                }
            }
        }
    }

    PopulateOffersInCart();

    // Remove empty filters (but keep shopping_cart and song_import_offers)
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    static Symbol store_filter_song_import_offers("store_filter_song_import_offers");

    std::vector<HamStoreFilter *> *filters = mFilters;
    std::vector<HamStoreFilter *>::iterator filterIt = filters->begin();
    while (filterIt != filters->end()) {
        HamStoreFilter *filter = *filterIt;
        std::map<Symbol, std::vector<StoreOffer *> *>::iterator mapIt =
            unk38.find(filter->mFilterSym);
        if ((mapIt == unk38.end() || mapIt->second->size() == 0)
            && filter->mFilterSym != store_filter_shopping_cart
            && filter->mFilterSym != store_filter_song_import_offers) {
            filterIt = filters->erase(filterIt);
        } else {
            ++filterIt;
        }
    }

    SetFilter(mCurrentFilter);
}

BEGIN_HANDLERS(HamStoreProvider)
    HANDLE_ACTION(refresh, Refresh())
    HANDLE_EXPR(get_offer, OnGetOffer(_msg->Int(2)))
    HANDLE_ACTION(set_pack, SetPackList(_msg->Obj<StoreOffer>(2)))
    HANDLE_EXPR(get_pack_provider, (Hmx::Object *)&mPackProvider)
    HANDLE_EXPR(find_pack, (Hmx::Object *)FindPack(_msg->Obj<StoreOffer>(2)))
    HANDLE_EXPR(show_browser_purchased, ShowBrowserPurchased(_msg->Obj<StoreOffer>(2)))
    HANDLE_EXPR(show_unavailable, TheNetCacheMgr->IsDebug())
    HANDLE_EXPR(is_partially_purchased, IsPartiallyPurchased(_msg->Obj<StoreOffer>(2)))
    HANDLE_EXPR(allow_sort_toggle, mSorts.size() > 1)
    HANDLE_EXPR(get_current_sort_name, CurrentSort())
    HANDLE_ACTION(next_sort, OnNextSort())
    HANDLE_EXPR(is_offer_in_cart, IsOfferInCart(_msg->Obj<StoreOffer>(2)))
    HANDLE_EXPR(find_song, (Hmx::Object *)FindSong(_msg->Int(2)))
    HANDLE_EXPR(get_offer_index, OnGetOfferIndex(_msg->Obj<StoreOffer>(2)))
    HANDLE_SUPERCLASS(UIListProvider)
    HANDLE_SUPERCLASS(Hmx::Object)
END_HANDLERS

void HamStoreProvider::RefreshFilteredCartOffers() {
    // Remove existing "store_filter_shopping_cart" entry from map
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    std::map<Symbol, std::vector<StoreOffer *> *>::iterator it =
        unk38.find(store_filter_shopping_cart);
    if (it != unk38.end()) {
        if (it->second) {
            delete it->second;
        }
        it->second = 0;
        unk38.erase(it);
    }

    // Count cart offers
    unsigned int count = 0;
    for (std::list<StoreOffer *>::iterator cit = mCartOffers.begin();
         cit != mCartOffers.end();
         ++cit) {
        count++;
    }

    if (count != 0) {
        // Find the store_checkout offer if mCartCheckout is not set
        if (!mCartCheckout) {
            static Symbol store_checkout("store_checkout");
            StoreOffer **end = mAllOffers->end();
            while (end != mAllOffers->begin()) {
                --end;
                if ((*end)->StoreOfferData()->Sym(0) == store_checkout) {
                    mCartCheckout = *end;
                    break;
                }
            }
        }
        MILO_ASSERT(mCartCheckout, 0x242);

        // Build new vector with checkout offer + all cart offers
        std::vector<StoreOffer *> *vec = new std::vector<StoreOffer *>();
        vec->push_back(mCartCheckout);
        for (std::list<StoreOffer *>::iterator cit = mCartOffers.begin();
             cit != mCartOffers.end();
             ++cit) {
            vec->push_back(*cit);
        }
        unk38.insert(
            std::pair<Symbol, std::vector<StoreOffer *> *>(store_filter_shopping_cart, vec)
        );
    }
}

#pragma endregion HamStoreProvider
