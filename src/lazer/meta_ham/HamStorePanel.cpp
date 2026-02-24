#include "meta_ham/HamStorePanel.h"
#include "HamStoreOffer.h"
#include "meta/SongMgr.h"
#include "meta/StoreEnumeration.h"
#include "meta/StoreOffer.h"
#include "meta/StorePanel.h"
#include "meta_ham/HamProfile.h"
#include "meta_ham/HamStoreFilterProvider.h"
#include "meta_ham/HamStoreProvider.h"
#include "meta_ham/ProfileMgr.h"
#include "meta_ham/UIEventMgr.h"
#include "net_ham/HamStoreCartJobs.h"
#include "net_ham/RCJobDingo.h"
#include "net_ham/RockCentral.h"
#include "obj/Data.h"
#include "obj/Msg.h"
#include "obj/Object.h"
#include "os/ContentMgr.h"
#include "os/Debug.h"
#include "os/Platform.h"
#include "os/PlatformMgr.h"
#include "os/System.h"
#include "os/User.h"
#include "utl/Loader.h"
#include "utl/MakeString.h"
#include "utl/Std.h"
#include "utl/Symbol.h"

HamStorePanel::HamStorePanel()
    : unka0(), unka4(), mOfferProvider(), mMotd(), mAllowCancel(false), mLockData(), unk154(false),
      mCartEnabled(true), mCartLocked(false), mCartDataLoaded(false), mRemovingFromCart(false), mAddingToCart(false), unk184(),
      mXboxPurchaser() {
    for (int i = 0; i < 7; i++) {
        mJobs[i] = 0;
    }
    DataArray *sysConfig = SystemConfig("store");
    Symbol specialOffersSym("special_offers");
    DataArray *specialOfferArray = sysConfig->FindArray(specialOffersSym, false);
    if (specialOfferArray) {
        int numOffers = (specialOfferArray->Size() + 23) / 24;
        numOffers = (numOffers >= 7) ? 6 : numOffers;

        for (int i = 0; i < numOffers; i++) {
            DataNode offerNode = specialOfferArray->Node(i);
            if (offerNode.Type() == kDataArray) {
            }
        }
    }
    TheContentMgr.RegisterCallback(this, false);
}

HamStorePanel::~HamStorePanel() { TheContentMgr.UnregisterCallback(this, false); }

BEGIN_PROPSYNCS(HamStorePanel)
    SYNC_SUPERCLASS(StorePanel)
END_PROPSYNCS

void HamStorePanel::Load() {
    StorePanel::Load();
    MILO_ASSERT(!mOfferProvider, 0xd3);
    mOfferProvider = new HamStoreProvider(&mOffers, &mFilters, &mCartRows);
    mAllowCancel = false;
    mCancelTimer.Restart();
    RefreshSpecialOfferStatus();
}

bool HamStorePanel::Exiting() const {
    if (mXboxPurchaser != 0)
        return true;
    return StorePanel::Exiting();
}

bool HamStorePanel::IsSongInLibrary(const int &id) const {
    return TheSongMgr.HasSong(id);
}

Profile *HamStorePanel::StoreProfile() const {
    return TheProfileMgr.GetActiveProfile(true);
}

StoreOffer *HamStorePanel::MakeNewOffer(DataArray *d) {
    return new HamStoreOffer(d, &TheSongMgr);
}

void HamStorePanel::DisableCart() {
    if (mCartDataLoaded) {
        MILO_FAIL("Can\'t disable the cart after it is loaded");
    }
    mCartEnabled = false;
    mLockData = 0;
}

void HamStorePanel::RemoveDLCFromCart(int id) {
    mPendingRemoves.push_back(id);
    if (!mRemovingFromCart) {
        mRemovingFromCart = true;
        RemoveNextDLCFromCart();
    }
}

void HamStorePanel::AddDLCToCart(int id) {
    mPendingAdds.push_back(id);
    if (!mAddingToCart) {
        mAddingToCart = true;
        AddNextDLCToCart();
    }
}

void HamStorePanel::RemoveOfferFromCart(StoreOffer *offer) {
    mOfferProvider->UpdateOffersInCart(offer, 1);
    RemoveDLCFromCart(offer->GetSingleSongID());
}

bool HamStorePanel::IsCurrFilterCart(int id) {
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    return mFilters[id]->mFilterSym == store_filter_shopping_cart;
}

void HamStorePanel::GetCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x23d);
    mJobs[3] = new GetCartJob(this, profile);
    TheRockCentral.ManageJob(mJobs[3]);
}

void HamStorePanel::LockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x246);
    mCartLocked = true;
    mLockData = 0;
    mJobs[4] = new LockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[4]);
}

void HamStorePanel::UnlockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x252);
    mCartLocked = false;
    mJobs[5] = new UnlockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[5]);
}

void HamStorePanel::RelockCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x25d);
    mCartLocked = true;
    mJobs[6] = new LockCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[6]);
    mRelockTimer.Restart();
}

void HamStorePanel::EmptyCart() {
    mOfferProvider->UpdateOffersInCart(nullptr, 2);
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x26c);
    mJobs[2] = new EmptyCartJob(this, profile->GetOnlineID()->ToString());
    TheRockCentral.ManageJob(mJobs[2]);
}

void HamStorePanel::StoreUserProfileSwappedToUser(LocalUser *) {
    RefreshSpecialOfferStatus();
}

void HamStorePanel::ReadLockData() {
    ((LockCartJob *)mJobs[4])->GetLockData(mLockData);
    mJobs[4] = nullptr;
    mRelockTimer.Restart();
}

void HamStorePanel::ReadCartData() {
    GetCartJob *job = (GetCartJob *)mJobs[3];
    mCartRows.clear();
    job->GetRows(&mCartRows);
    mJobs[3] = nullptr;
    mCartDataLoaded = true;
}

StoreOffer *HamStorePanel::FindOffer(Symbol offerName) const {
    FOREACH (it, mOffers) {
        StoreOffer *offer = *it;
        Symbol s = offer->StoreOfferData()->Sym(0);
        if (s == offerName)
            return offer;
    }
    return nullptr;
}

void HamStorePanel::SetFilterToCart() {
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    for (int i = mFilters.size() - 1; i >= 0; i--) {
        if (mFilters[i]->mFilterSym == store_filter_shopping_cart) {
            mOfferProvider->SetFilter(mFilters[i]);
            return;
        }
    }
}

int HamStorePanel::SetFilterToSongs() {
    static Symbol songs("songs");
    for (int i = mFilters.size() - 1; i >= 0; i--) {
        if (mFilters[i]->mFilterSym == songs) {
            mOfferProvider->SetFilter(mFilters[i]);
            return i;
        }
    }
    return -1;
}

void HamStorePanel::AddNextDLCToCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x2da);
    mJobs[0] =
        new AddDLCToCartJob(this, profile->GetOnlineID()->ToString(), mPendingAdds.front());
    TheRockCentral.ManageJob(mJobs[0]);
}

void HamStorePanel::RemoveNextDLCFromCart() {
    HamProfile *profile = dynamic_cast<HamProfile *>(StoreProfile());
    MILO_ASSERT(profile, 0x2c0);
    mJobs[1] = new RemoveDLCFromCartJob(
        this, profile->GetOnlineID()->ToString(), mPendingRemoves.front()
    );
    TheRockCentral.ManageJob(mJobs[1]);
}

void HamStorePanel::AddOfferToCart(StoreOffer *offer) {
    mOfferProvider->UpdateOffersInCart(offer, 0);
    AddDLCToCart(offer->GetSingleSongID());
}

char const *HamStorePanel::GetIndexFile() const {
    const char *str = "store_index_%s_%s.dtz";
    Symbol platSym = PlatformSymbol(TheLoadMgr.GetPlatform());
    Symbol sysLang = SystemLanguage();
    return MakeString(str, sysLang, platSym);
}

void HamStorePanel::ExitStore(StoreError err) const {
    static Symbol store_load_failed("store_load_failed");
    if (TheUIEventMgr->CurrentEvent() != store_load_failed) {
        static Message init("init", -1);
        init[0] = err;
        TheUIEventMgr->TriggerEvent(store_load_failed, init);
    }
}

void HamStorePanel::CreateCartUIs() {
    static Symbol album_name("album_name");
    static Symbol art("art");
    auto _tmp0 = mFilters.begin();
    static Symbol artist("artist");
    static Symbol name("name");
    static Symbol store_filter_shopping_cart("store_filter_shopping_cart");
    static Symbol store_filter_song_import_offers("store_filter_song_import_offers");
    static Symbol fake("fake");
    static Symbol store_checkout("store_checkout");
    HamStoreFilter *filter1 = new HamStoreFilter(store_filter_shopping_cart);

    static Symbol description("description");
    static Symbol type("type");
    mFilters.insert(_tmp0, filter1);

    HamStoreFilter *filter2 = new HamStoreFilter(store_filter_song_import_offers);
    mFilters.push_back(filter2);
}

BEGIN_HANDLERS(HamStorePanel)
    HANDLE_EXPR(get_motd, mMotd)
    HANDLE_ACTION(set_filter, mOfferProvider->SetFilter(mFilters[_msg->Int(2)]))
    HANDLE_ACTION(
        set_filter_pack_singles, mOfferProvider->SetFilter(_msg->Obj<StoreOffer>(2))
    )
    HANDLE_EXPR(offer_provider, (Hmx::Object *)mOfferProvider)
    HANDLE_EXPR(filter_provider, (Hmx::Object *)mOfferProvider->GetFilterProvider())
    HANDLE_ACTION(reset_cancel_timer, (mAllowCancel = false, mCancelTimer.Restart()))
    HANDLE_EXPR(allow_cancel, mAllowCancel)
    HANDLE_EXPR(is_cart_enabled, mCartEnabled)
    HANDLE_ACTION(disable_cart, DisableCart())
    HANDLE_ACTION(get_cart, GetCart())
    HANDLE_ACTION(add_offer_to_cart, AddOfferToCart(_msg->Obj<StoreOffer>(2)))
    HANDLE_ACTION(remove_offer_from_cart, RemoveOfferFromCart(_msg->Obj<StoreOffer>(2)))
    HANDLE_ACTION(cart_checkout, MultipleItemsCheckout(mOfferProvider->GetCartOffers()))
    HANDLE_ACTION(lock_cart, LockCart())
    HANDLE_ACTION(unlock_cart, UnlockCart())
    HANDLE_EXPR(is_curr_filter_cart, IsCurrFilterCart(_msg->Int(2)))
    HANDLE_EXPR(is_cart_empty, mOfferProvider->NumOffersInCart() == 0)
    HANDLE_EXPR(is_cart_full, mOfferProvider->NumOffersInCart() == 6)
    HANDLE_ACTION(empty_cart, EmptyCart())
    HANDLE_ACTION(set_filter_to_cart, SetFilterToCart())
    HANDLE_ACTION(set_filter_to_songs, SetFilterToSongs())
    HANDLE_ACTION(refresh_special_offers, RefreshSpecialOfferStatus())
    HANDLE_EXPR(check_owned, IsSpecialOfferOwned(_msg->ForceSym(2)))
    HANDLE_EXPR(buy_special, BuySpecialOffer(_msg->ForceSym(2)))
    HANDLE_MESSAGE(RCJobCompleteMsg)
    HANDLE_ACTION(buy_dc1_import, BuySpecialOffer("dc1_import"))
    HANDLE_ACTION(buy_dc2_import, BuySpecialOffer("dc2_import"))
    HANDLE_ACTION(buy_dc2_pop, BuySpecialOffer("dc2_pop"))
    HANDLE_ACTION(buy_dc2_gond, BuySpecialOffer("dc2_gond"))
    HANDLE_SUPERCLASS(StorePanel)
END_HANDLERS
