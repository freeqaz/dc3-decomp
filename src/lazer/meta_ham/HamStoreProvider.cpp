#include "meta_ham/HamStoreProvider.h"
#include "HamStoreFilterProvider.h"
#include "macros.h"
#include "meta/StoreOffer.h"
#include "meta_ham/AppLabel.h"
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

PackSongListProvider::~PackSongListProvider() {}

#pragma endregion PackSongListProvider
#pragma region HamStoreProvider

HamStoreProvider::HamStoreProvider(
    std::vector<StoreOffer *> *offers,
    std::vector<HamStoreFilter *> *filters,
    std::vector<CartRow> *rows
)
    : mAllOffers(offers), mFilters(filters), unk5c(0), mFilteredOffers(0), mCartRows(rows), unkb8(0) {
    mFilterProvider = new HamStoreFilterProvider(mFilters);
}

HamStoreProvider::~HamStoreProvider() {
    // Clean up dynamically allocated vectors in the map
    for (std::map<Symbol, std::vector<StoreOffer *> *>::iterator it = unk38.begin();
         it != unk38.end();
         ++it) {
        delete it->second;
        it->second = 0;
    }
    unk38.clear();
    mFilteredOffers = 0;
    RELEASE(mFilterProvider);
    mFilterProvider = 0;
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

void HamStoreProvider::Text(
    int, int data, UIListLabel *slot, UILabel *label
) const {
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
            auto sortCmp = SortCmp();
            std::sort(mFilteredOffers->begin(), mFilteredOffers->end(), sortCmp);
        }
    }
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

void HamStoreProvider::OnNextSort() {
    auto& sortIndex = mSortIndex;
    if (mSorts.size() > 1) {
        sortIndex = (sortIndex + 1) % (int)mSorts.size();
        Refresh();
    }
}

void HamStoreProvider::Refresh() {
    if (mFilteredOffers) {
        ApplySort();
    }
}

void HamStoreProvider::RefreshFilteredCartOffers() {
    // Repopulate cart offers section in filtered list
    PopulateOffersInCart();
}

void HamStoreProvider::PopulateOffersInCart() {
    // Nothing to do here without access to the cart row data
}

void HamStoreProvider::SetFilter(HamStoreFilter const *filter) {
    if (!filter) {
        mFilteredOffers = mAllOffers;
        mSorts.clear();
        mSortIndex = 0;
        Refresh();
        return;
    }
    // Find a new vector for the filtered offers
    auto it = unk38.find(filter->mFilterSym);
    if (it == unk38.end()) {
        std::vector<StoreOffer *> *offers = new std::vector<StoreOffer *>();
        unk38[filter->mFilterSym] = offers;
        mFilteredOffers = offers;
    } else {
        mFilteredOffers = it->second;
    }
    mFilteredOffers->clear();
    // Copy all offers that match the filter (for now, copy all)
    if (mAllOffers) {
        *mFilteredOffers = *mAllOffers;
    }
    // Apply sort types from filter
    mSorts = filter->mSortTypes;
    mSortIndex = 0;
    Refresh();
}

void HamStoreProvider::SetFilter(StoreOffer const *packOffer) {
    // Filter to show songs in a specific pack
    static Symbol songs("songs");
    Symbol packSym = packOffer ? packOffer->StoreOfferData()->Sym(0) : gNullStr;
    auto it = unk38.find(packSym);
    if (it == unk38.end()) {
        std::vector<StoreOffer *> *offers = new std::vector<StoreOffer *>();
        unk38[packSym] = offers;
        mFilteredOffers = offers;
    } else {
        mFilteredOffers = it->second;
    }
    mFilteredOffers->clear();
    if (packOffer && mAllOffers) {
        for (int i = 0; i < (int)mAllOffers->size(); i++) {
            StoreOffer *offer = (*mAllOffers)[i];
            if (packOffer->HasSong(offer)) {
                mFilteredOffers->push_back(offer);
            }
        }
    }
    mSorts.clear();
    mSortIndex = 0;
}

bool HamStoreProvider::ShowBrowserPurchased(StoreOffer const *offer) const {
        return offer && const_cast<StoreOffer *>(offer)->IsPurchased();
}

#pragma endregion HamStoreProvider
